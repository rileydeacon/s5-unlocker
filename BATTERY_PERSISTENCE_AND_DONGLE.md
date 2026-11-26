# Battery Swaps, Persistence & the \$5 USB-C Pocket Dongle

The most common question about **S5 Unlocker** is: *"Can you make it permanent so it survives a battery swap?"*

Here is the honest technical answer, followed by three practical, volatile-RAM ways to shoot all day without a phone or laptop.

---

## Why S5 Unlocker Does Not Write to Flash

1. **Volatile RAM Only:** The v1.5 installer writes its patches to volatile RAM rather than internal NAND flash or the bootloader. A cold boot clears those patches. Experimental recording modes can fail, so test them before relying on them for a shoot.
2. **Cryptographic Signing:** Panasonic `.UPD` firmware updates are encrypted and protected by **ECDSA P-256** signatures, and a full scan of the DC-S5 RTOS confirmed there is **no Canon `BOOTDISK`-style SD-card autoloader**.
3. **Cold-Boot Reset:** Staying in volatile DRAM means removing the battery forces a cold boot that clears those patches.

---

## Three Volatile-RAM Ways to Survive All-Day Shoots

### 1. Normal Power-Switch OFF/ON (Built-In)
You do **not** need to re-unlock between takes or when turning the camera off. Flipping the power switch **OFF and back ON** keeps DRAM self-refreshed — **6K Open Gate, 5.9K, live HDMI in 6K/5.9K, and fixed recording timer removal all survive** as long as the battery door stays closed.

### 2. All-Day Power Without Opening the Battery Door
- **DMW-BGS5 Battery Grip (Best for Handheld):** Keep a charged battery in the camera body and hot-swap batteries in the grip all day. The body battery keeps volatile RAM alive continuously.
- **USB-C PD Power Bank / V-Mount / Dummy Battery:** Top up the S5 over USB-C PD between takes, or power the camera continuously on a rig without opening the battery door.

### 3. Build a \$5–\$10 USB-C Keychain Auto-Arm Dongle (ESP32-S3 / RP2040)
If you swap internal batteries frequently and do not want to pull out an Android phone or laptop, you can build a thumb-drive-sized **USB-C Host auto-arm dongle** that lives in your battery pouch:

1. Swap your battery and boot the S5 in Service Mode (**PLAYBACK + AF/AE LOCK + Power ON**).
2. Plug the dongle into the S5's USB-C port for ~2 seconds until its LED turns green.
3. Unplug and shoot — no phone, no app, no laptop, and no flash writes.

#### Recommended Off-the-Shelf Hardware (\$5–\$12, Zero Soldering)
Any microcontroller board with **native USB Host (OTG)** support and a USB-C port (or USB-A + compact C-to-C adapter):
- **Waveshare ESP32-S3-USB-OTG** or **Seeed Studio XIAO ESP32-S3** (ESP-IDF `usb_host` stack)
- **Raspberry Pi Pico / RP2040-Zero** (TinyUSB `tuh_*` Host stack)

#### Exact USB Host Protocol (v1.5, 100% Open Source)
Your microcontroller firmware mirrors [`docs/index.html`](docs/index.html) and [`android/src/com/s5mod/unlocker/MainActivity.java`](android/src/com/s5mod/unlocker/MainActivity.java):

1. **Enumerate USB Device:** VID `0x04DA`, PID `0x2370` (Vendor Class `0xFF/0xA0/0xB0`), claim interface `0`, open Bulk OUT (`epOut`) and Bulk IN (`epIn`).
2. **Packet Framing (`prepare` + 16-byte framed bulk transfer):**
   - Before each Bulk OUT, send vendor control transfer: `bmRequestType=0x40, bRequest=0, wValue=0, wIndex=0`.
   - Before each Bulk IN, send vendor control transfer: `bmRequestType=0x40, bRequest=0, wValue=0, wIndex=1`.
   - **16-bit Read (`op 0x9B`):** TX `[0x08, 0x00, 0xFF, 0x9B, addr_u32_le, 0,0,0,0,0,0, sum(tx[1..13]), 0x00]`. Read 1-byte type (`0x08`), then 16-byte reply (`rx[3]==0x36`, value at `rx[4..5]` LE).
   - **16-bit Write (`op 0x9C`):** TX `[0x08, 0x00, 0xFF, 0x9C, addr_u32_le, val_u16_le, 0,0,0,0, sum(tx[1..13]), 0x00]`. Read 1-byte type (`0x08` or `0x01`), then ack frame (`rx[3]==0x00` or `0x01`).
3. **Verify All 25 Static Signature Anchors:** Read all 25 anchors from `SIG_CHECKS` in `docs/index.html` / `MainActivity.java`. Abort (blink red LED) if any anchor does not match.
4. **Write Expanded v1.5 Tables & Control Words:**
   - Write the expanded NTSC/PAL MOV UI and whitelist tables to `0x870DD200`–`0x870DD510`.
   - Write and read-back-verify the 11 control halfwords (4 table pointers + `0x868261B6` fixed recording timer removal + `0x866ACC9C`/`0x8664425C` live HDMI in 6K/5.9K).
   - Refresh `0x08A6836C` if NTSC or PAL MOV is currently loaded, then turn LED solid green.

*(Community PRs with pre-built ESP32-S3 or RP2040 `.uf2` / `.bin` firmware files for this dongle are welcome.)*
