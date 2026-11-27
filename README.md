# Pico 2 W Homelab Physical Status Monitor & Clock

An embedded, cybersecurity-hardened physical monitoring appliance and desktop clock powered by the **Raspberry Pi Pico 2 W (RP2350)** running **FreeRTOS** across dual Cortex-M33 cores.

The device bridges local environmental sensors, physical displays, and homelab telemetry (RPi 5, ThinkCentre micro-servers) via **MQTT**, **Zabbix**, and **Homepage**, secured end-to-end with **TLS 1.2 HTTPS**, **Bearer Token Authentication**, and a **Software Secure Boot** verification pipeline.

---

## Hardware Architecture

| Component | Interface | Purpose |
| :--- | :--- | :--- |
| **Raspberry Pi Pico 2 W** | RP2350 (ARM Cortex-M33) | Dual-core processing, Wi-Fi 4 (CYW43439), crypto engine |
| **16x2 Character LCD** | I2C (`i2c0`, `0x27`) | Server telemetry, temperatures, RAM usage, diagnostics |
| **4-Digit 7-Segment Display** | Direct GPIO Multiplexing | Real-time synchronized digital clock |
| **RGB Status LED** | Hardware PWM | Visual health indicator (Normal, Warning, Critical, Outage) |
| **DS18B20 Sensor** | 1-Wire Digital GPIO | Ambient room temperature monitoring |
| **Photoresistor (LDR)** | ADC (`ADC0`) | Ambient light tracking & automatic LCD backlight control |
| **Multifunction Tactile Button** | GPIO (Pull-up) | Context-aware physical hardware controls |

---

## FreeRTOS Dual-Core Task Scheduling

The system leverages FreeRTOS Symmetric Multiprocessing (SMP) with strict core affinity to eliminate screen flicker and avoid race conditions:

* **Core 1 (Real-Time Hardware & UI):**
  * `vTimeDisplayTask`: Multiplexed 7-segment clock refresh.
  * `vLCDDisplayTask`: Atomic LCD screen paging & diagnostic rendering.
  * `vStatusLEDTask`: Hardware PWM RGB health status animator.
  * `vButtonTask`: Physical button hold-time debouncer and event dispatcher.
* **Core 0 (Networking, Crypto & Background Services):**
  * `vNetworkTask`: Wi-Fi STA connection management & HTTPS server bootstrapper.
  * `vUpdateFromMQTTTask`: Subscribes to broker topics (`lab/pi/status`).
  * `vTempReadingTask`: 1-Wire DS18B20 polling (750 ms non-blocking yield).
  * `vLightReadingTask`: ADC photoresistor polling with hysteresis backlight trigger.
  * `vCPUWatchdog`: Hardware watchdog feeder (8-second trip threshold).
  * `HTTPS_Server`: Mbed TLS socket daemon serving authenticated JSON telemetry.

---

## Cybersecurity Hardening Features (v2.0.10)

### 1. Transport Encryption (TLS 1.2 HTTPS)
* Replaced cleartext HTTP (port 80) with **HTTPS on port 443** using **Mbed TLS**.
* Configured with **ECDSA NIST P-256 (`secp256r1`)** and AES-GCM cipher suites for optimal Cortex-M33 hardware execution.
* **Zero-RAM Certificate Embedding:** Server certificate (`server.crt`) and private key (`server.key`) are compiled directly into read-only Flash (`.rodata`) via assembly (`certs.S` `.incbin`), avoiding RAM allocations during boot.

### 2. Zero-Trust Access Control
* **Bearer Token Enforcement:** Every incoming GET or POST request must supply an authorized `Authorization: Bearer <TOKEN>` header.
* **Timing-Attack Resistance:** Token evaluation utilizes constant-time memory comparison (`mbedtls_ct_memcmp`) to mitigate side-channel timing leaks.

### 3. Software Secure Boot & Firmware Self-Integrity Check
* **Automated Build Stamping:** A post-build Python script (`tools/stamp_firmware.py`) computes the SHA-256 digest of the raw ~504 KB firmware and stamps it in-place into both `.bin` and `.uf2` binaries.
* **Pre-Boot Flash Hashing:** During boot in `main()`, *before* Wi-Fi, FreeRTOS, or sensors initialize, the CPU hashes its own flash memory using Mbed TLS.
* **Tamper Lockdown:** If even a single bit in flash is modified or corrupted, the hash calculation fails: the Pico halts execution, locks out all network interfaces, and pulses a rapid red warning LED.
* **Zero-Brick Risk:** Unlike one-way silicon OTP fuse burning, this verification remains 100% reversible by holding the physical BOOTSEL button to reflash valid firmware.

### 4. Field Provisioning & Network Mobility
* **Physical-Presence Setup:** Holding the physical button for 4 seconds switches the board into Serial Config Mode.
* **Task Quarantine:** Automatically freezes all sensor, LCD, and network tasks during provisioning, preventing serial debug clutter and display overwrites.
* **Non-Volatile Flash Persistence:** Wi-Fi credentials and target MQTT broker IPs are written directly to a reserved 4KB flash sector (`0x103FF000`) with magic-byte validation (`0x50494332`), surviving complete power loss.

### 5. Defensive Software Architecture
* **Race Condition Mitigation:** All shared variables (`LIGHT_THRESHOLD`, display messages, ADC readings) are guarded by FreeRTOS Mutexes (`xSemaphoreCreateMutex`).
* **HTTP Stream Buffering:** Implemented a stateful receive buffer to prevent malformed requests caused by TCP packet fragmentation.
* **Strict Input Sanitization:** Rejects out-of-bounds ADC threshold inputs (`0 <= val <= 4095`) with `400 Bad Request`.
* **Defensive HTTP Headers:** Emits `Cache-Control: no-store` and `X-Content-Type-Options: nosniff`.

---

## Physical Button Controls

| Hold Duration | Physical Feedback | Triggered Action |
| :--- | :--- | :--- |
| **< 1 Second** | Quick LED flash | Quick Tap (Display status acknowledge) |
| **2 Seconds** | Magenta LED pulse | **LCD Soft Reset** (Re-initializes I2C HD44780 controller) |
| **3 Seconds** | White LED pulse | **System Reboot** (Triggers hardware watchdog reset) |
| **4 Seconds** | Teal LED hold | **Serial Provisioning Console** (Freezes tasks, opens USB CLI) |

---

## Building from Source

### Prerequisites
* Raspberry Pi Pico SDK (v2.x or later)
* Arm GNU Embedded Toolchain (`arm-none-eabi-gcc`)
* Python 3 & CMake 3.13+

### 1. Environment & Secrets Setup
Copy the template credentials file and populate your local parameters:
```bash
cp src/secrets.h.example src/secrets.h
```

### 2. Generate Self-Signed ECC Certificates

Generate an Elliptic Curve certificate and private key in the project root:
```
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 \
  -keyout server.key -out server.crt -days 3650 -nodes \
  -subj "/CN=pico2w.local"
  ```

### 3. Build & Cryptographically Stamp the Firmware

```
mkdir build && cd build
cmake -DPICO_SDK_PATH=/path/to/pico-sdk \
      -DFREERTOS_KERNEL_PATH=/path/to/FreeRTOS-Kernel \
      -DPICO_PLATFORM=rp2350 \
      -DPICO_BOARD=pico2_w ..

make -j$(nproc)
```
The build pipeline will automatically compile the binary, generate the extra outputs, and stamp pico_dashboard_v*VERSION*.uf2 with its SHA-256 digest.

### 4. Flashing
Hold the BOOTSEL button on the Pico 2 W, connect it via USB, and drag-and-drop the generated .uf2 file onto the mounted RPI-RP2 drive.

## License
MIT License. Developed for homelab automation, telemetry aggregation, and embedded security demonstration.