#!/usr/bin/env python3
import sys
import struct
import hashlib

MARKER = b"PICODASH_INTEG"

def stamp(bin_path, uf2_path):
    # 1. Read raw binary
    with open(bin_path, "rb") as f:
        bin_data = bytearray(f.read())

    offset = bin_data.find(MARKER)
    if offset == -1:
        print(f"[Stamp Error] Marker not found in {bin_path}!")
        return

    code_size = len(bin_data)

    # 2. Zero-out the hash field in memory before computing the digest
    # Struct layout:
    # 0..15:  marker (16 bytes)
    # 16..19: is_stamped (4 bytes)
    # 20..23: code_size (4 bytes)
    # 24..55: sha256_hash (32 bytes)
    hash_input = bytearray(bin_data)
    hash_input[offset + 16 : offset + 56] = b"\x00" * 40

    digest = hashlib.sha256(hash_input).digest()

    # 3. Pack the stamped payload: marker, is_stamped=1, code_size, sha256
    stamped_bytes = struct.pack("<16sII32s", MARKER, 1, code_size, digest)

    # 4. Stamp the .bin file
    bin_data[offset : offset + 56] = stamped_bytes
    with open(bin_path, "wb") as f:
        f.write(bin_data)

    # 5. Stamp the .uf2 file directly
    with open(uf2_path, "rb") as f:
        uf2_data = bytearray(f.read())

    uf2_offset = uf2_data.find(MARKER)
    if uf2_offset == -1:
        print(f"[Stamp Error] Marker not found in {uf2_path}!")
        return

    uf2_data[uf2_offset : uf2_offset + 56] = stamped_bytes
    with open(uf2_path, "wb") as f:
        f.write(uf2_data)

    print(f"[Stamp] Successfully signed BIN and UF2!")
    print(f"[Stamp] Total Firmware Size: {code_size} bytes")
    print(f"[Stamp] SHA-256 Digest: {digest.hex()}")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: stamp_firmware.py <path_to.bin> <path_to.uf2>")
        sys.exit(1)
    stamp(sys.argv[1], sys.argv[2])