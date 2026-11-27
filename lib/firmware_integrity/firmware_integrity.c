#include "firmware_integrity.h"
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "mbedtls/sha256.h"

#define MARKER_STRING "PICODASH_INTEG"

typedef struct {
    char marker[16];
    uint32_t is_stamped;
    uint32_t code_size;
    uint8_t  expected_hash[32];
} __attribute__((packed)) firmware_integrity_t;

// Placed in flash with a known marker pattern
__attribute__((used))
const volatile firmware_integrity_t g_integrity = {
    .marker = MARKER_STRING,
    .is_stamped = 0, // Set to 1 post-build by Python
    .code_size = 0,
    .expected_hash = {0}
};

extern char __flash_binary_start;

bool verify_firmware_integrity(void) {
    if (g_integrity.is_stamped != 1) {
        printf("[Integrity] WARNING: Firmware not stamped with signature! (Dev build?)\n");
        return true; // Allow dev testing
    }

    uintptr_t flash_start = (uintptr_t)&__flash_binary_start;
    size_t total_size = g_integrity.code_size;
    uintptr_t stamp_addr = (uintptr_t)&g_integrity;

    if (stamp_addr < flash_start || (stamp_addr + sizeof(firmware_integrity_t)) > (flash_start + total_size)) {
        printf("[Integrity] CRITICAL: Invalid stamp bounds!\n");
        return false;
    }

    // Copy expected hash to stack before hashing
    uint8_t expected[32];
    memcpy(expected, g_integrity.expected_hash, 32);

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);

    // 1. Hash flash from start up to the marker + 16 bytes
    size_t part1_len = (stamp_addr + 16) - flash_start;
    mbedtls_sha256_update(&ctx, (const unsigned char *)flash_start, part1_len);

    // 2. Hash 40 bytes of zeros (matches how Python prepared the buffer)
    uint8_t zeros[40] = {0};
    mbedtls_sha256_update(&ctx, zeros, sizeof(zeros));

    // 3. Hash the rest of the binary to the end
    uintptr_t part3_start = stamp_addr + sizeof(firmware_integrity_t);
    uintptr_t flash_end = flash_start + total_size;
    if (flash_end > part3_start) {
        size_t part3_len = flash_end - part3_start;
        mbedtls_sha256_update(&ctx, (const unsigned char *)part3_start, part3_len);
    }

    uint8_t calculated[32];
    mbedtls_sha256_finish(&ctx, calculated);
    mbedtls_sha256_free(&ctx);

    if (memcmp(calculated, expected, 32) == 0) {
        printf("[Integrity] SUCCESS: Cryptographic hash verified! (%u bytes)\n", (unsigned int)total_size);
        return true;
    } else {
        printf("[Integrity] CRITICAL: HASH MISMATCH! Firmware tampered or corrupted!\n");
        return false;
    }
}