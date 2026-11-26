# Battery Swaps, Persistence & the \$5 USB-C Pocket Dongle

The #1 question about **S5 Unlocker** is: *"Can you make it permanent so it survives a battery swap?"*

Here is how v1.5 behaves across power cycles and battery swaps.

---

## Why S5 Unlocker Does Not Write to Flash
1. **Volatile RAM only.** The v1.5 installer writes its patches to volatile RAM.
2. **Firmware Signing:** Panasonic `.UPD` firmware updates are protected by **ECDSA P-256** cryptographic signatures, and a full 22.5 MiB scan of the DC-S5 RTOS confirmed there is **no Canon `BOOTDISK`-style SD-card code autoloader**.
3. **Cold boot clears the patches.** Select a stock recording mode before removing the battery. The camera may remember the selected mode.

---

## Power Options for Longer Shoots

### 1. Normal Power-Switch OFF/ON (Already Built-In!)
You do **not** need to re-unlock between takes or when turning the camera off. Flipping the power switch **OFF and back ON** keeps DRAM self-refreshed — **6K Open Gate, 5.9K, and fixed recording timer removal all survive** as long as the battery door stays closed.

### 2. All-Day Power Without Opening the Battery Door
- **DMW-BGS5 Battery Grip (Best for Handheld):** Keep a charged battery in the camera body and hot-swap batteries in the grip all day — the body battery keeps volatile RAM alive continuously!
- **USB-C PD Power Bank / V-Mount / Dummy Battery:** Top up the S5 over USB-C PD between takes (or run off a V-Mount/dummy battery on a rig) without ever opening the battery door.

### 3. Build a \$5–\$10 USB-C Keychain Auto-Arm Dongle (ESP32-S3 / RP2040)
If you swap internal batteries frequently and don't want to pull out an Android phone or laptop, you can build a thumb-drive-sized **USB-C Host auto-arm dongle** that lives in your battery pouch:
1. Swap your battery and boot the S5 in Service Mode (**PLAYBACK + AF/AE LOCK + Power ON**).
2. Plug the tiny dongle into the S5's USB-C port for **~1 second** until its LED turns green.
3. Unplug the dongle once it reports that the RAM patches are applied.

#### Recommended Off-the-Shelf Hardware (\$5–\$12, Zero Soldering)
Any microcontroller board with **native USB Host (OTG)** support and a USB-C port (or USB-A + tiny C-to-C adapter):
- **Waveshare ESP32-S3-USB-OTG** or **Seeed Studio XIAO ESP32-S3** (ESP-IDF `usb_host` stack)
- **Raspberry Pi Pico / RP2040-Zero** (TinyUSB `tuh_*` Host stack)

#### Exact USB Host Protocol (100% Open Source)
Your microcontroller firmware simply mirrors the exact same 3 steps as [`docs/index.html`](docs/index.html) and [`android/src/com/s5mod/unlocker/MainActivity.java`](android/src/com/s5mod/unlocker/MainActivity.java):
1. **Enumerate USB Device:** VID `0x04DA`, PID `0x2370` (Vendor Class `0xFF/0xA0/0xB0`), open Bulk OUT (`epOut`) and Bulk IN (`epIn`).
2. **Verify All 19 Static Signature Anchors (`op 0x9B` 16-bit read):**
   - Send 16-byte packet: `00 00 00 00 9B 00 00 00 <addr_u32_le> 02 00 00 00`
   - Read response: 16-bit value is at `rx[8..9]` (little-endian).
   - **Abort (blink red LED)** if any of the 19 anchors in `docs/index.html` / `MainActivity.java` don't match!
3. **Write & Read-Back-Verify the 25 Volatile Halfwords (`op 0x9C` 16-bit write):**
   - Send 16-byte packet: `00 00 00 00 9C 00 00 00 <addr_u32_le> 02 00 <val_u16_le>`
   - Read ack (`rx[3] == 0x01 || 0x00`), read back with `op 0x9B` to confirm match.
   - **Turn LED solid green** — total time ~300 ms!

*(Community PRs with pre-built ESP32-S3 / RP2040 `.uf2` / `.bin` firmware files for this dongle are very welcome!)*
