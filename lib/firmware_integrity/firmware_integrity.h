#ifndef FIRMWARE_INTEGRITY_H
#define FIRMWARE_INTEGRITY_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Verifies the flash memory integrity against the trailing SHA-256 stamp
bool verify_firmware_integrity(void);

#endif