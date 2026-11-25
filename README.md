# S5 Unlocker (`v1.5`)

Unlock native-menu **6K Open Gate**, **5.9K (NTSC and PAL)**, **live HDMI output during 6K/5.9K recording**, and **unlimited recording** on the original **Panasonic Lumix DC-S5 (Mark I, Firmware 2.9)**.

All changes are written strictly to volatile RAM over USB. There are no flash writes, no soldering, no removed stock modes, and zero brick risk. Pulling the battery restores 100% factory stock firmware.

> [!CAUTION]
> **Supported hardware and firmware only:** Original **Lumix DC-S5 (Mark I, `MC801`)** running stock **Firmware 2.9**.
>
> Early read-only checks indicate the **Lumix DC-S5D** shares the same memory map; use the **Read-only compatibility check** button first. Both the Android app and WebUSB page verify **25 static firmware signature anchors** before writing a single byte and will safely abort on any other camera or firmware version. No warranty; use at your own risk.

---

## What It Unlocks

| Feature | Details |
| :--- | :--- |
| **6K 3:2 Open Gate (NTSC)** | `5952×3968` · 23.98p · HEVC 10-bit 4:2:0 · 200 Mbps (`0x00ED`) |
| **5.9K 16:9 Full-Frame (NTSC)** | `5888×3312` · 23.98p (`0x00F1`) and 29.97p (`0x00F4`) · HEVC 10-bit 4:2:0 · 200 Mbps |
| **5.9K 16:9 Full-Frame (PAL)** | `5888×3312` · 25.00p (`0x00F3`) · HEVC 10-bit 4:2:0 · 200 Mbps |
| **Top-of-Menu Placement** | Unlocked modes appear at the **top** of **Menu → Video → Rec Quality (MOV)** |
| **Zero Stock Modes Removed** | All 17 NTSC and 11 PAL stock MOV modes remain intact |
| **Live HDMI in 6K / 5.9K** | Fixes the stock behavior where external HDMI output blanks out during 6K/5.9K recording |
| **Unlimited Recording** | Removes the 30-minute and 15-minute caps across all 4K, C4K, 4K-A 10-bit, 60p, and VFR modes |
| **Power-Switch Persistence** | Stays active across normal Power OFF/ON cycles until the battery is removed |

Works regardless of whether your camera is set to MP4, MOV, PAL, or NTSC when you run the unlock.

---

## Quick Start

Choose whichever method fits your workflow.

### Option 1: Offline Android App (`S5Unlocker.apk`) — Recommended for Field Use

Works on any USB-C Android phone with USB OTG. Requires no Wi-Fi or browser and opens automatically when you plug in the camera.

1. Download `S5Unlocker.apk` (v1.5) from **Releases** (or build it from [`android/`](android/)) and install it on your phone.
2. On the DC-S5, hold **PLAYBACK + AF/AE LOCK** and flip Power **ON** to enter temporary service mode (`04DA:2370`).
   - *S5D users: set the left drive dial to **Single Shot** first.*
3. Connect the phone and camera with a USB-C cable. **S5 Unlocker** will open automatically.
4. Tap **Unlock camera**, wait ~2 seconds for **Armed**, and unplug the cable.
5. Set **Rec File Format → MOV** and open **Menu → Video → Rec Quality**. Your 6K and 5.9K modes will be at the top of the list.

### Option 2: Chrome WebUSB (Mac, Windows, or Android)

Open the hosted web app at **https://rileydeacon.github.io/s5-unlocker/** (served from [`docs/index.html`](docs/index.html)) in **Google Chrome**.

1. Boot the DC-S5 holding **PLAYBACK + AF/AE LOCK** and flip Power **ON** *(S5D: left drive dial on **Single Shot**)*.
2. Connect the camera to your computer or Android phone via USB-C.
3. Click **Connect USB**, then click **Unlock camera**. Once it shows **Armed**, unplug and shoot.
   - To verify compatibility without writing anything, click **Read-only compatibility check** to inspect all 25 anchors.

*Note: iOS and iPadOS Safari do not support WebUSB.*

### Option 3: macOS Command-Line Tool

Build and run [`mac-cli/s5_v15_test.c`](mac-cli/s5_v15_test.c):

```bash
clang -O2 -framework IOKit -framework CoreFoundation mac-cli/s5_v15_test.c -o s5_unlocker
./s5_unlocker check
./s5_unlocker enable --confirm-write
./s5_unlocker restore --confirm-write
```

---

## Important Notes & Troubleshooting

### Power Switch vs. Battery Pull
- Flipping the power switch **OFF and ON** keeps the unlock active via DRAM self-refresh.
- Opening the battery door or removing the battery forces a cold boot back to 100% stock firmware.
- For options that avoid re-arming during shoots (in-body USB-C charging, the DMW-BGS5 battery grip, or a \$5 USB-C keychain auto-arm dongle), see [`BATTERY_PERSISTENCE_AND_DONGLE.md`](BATTERY_PERSISTENCE_AND_DONGLE.md).

### Always Switch to a Stock Mode Before Pulling the Battery
If you pull the battery while `6K` or `5.9K` is still selected, the RAM patch clears on cold boot while the camera still remembers the unlocked mode ID. This can make the MOV Rec Quality menu appear blank or stuck until reset.

If that happens, recover in seconds with any of these methods:
- Press the **Q** button and change **Rec Quality** back to a normal stock mode (fastest).
- Switch **Rec File Format: MOV → MP4 → MOV**, or re-apply the unlock and select a stock mode.
- Run **Menu → Setup (Wrench) → Reset → Reset Rec Settings** (guaranteed recovery, no USB required).

### Thermal Protection
Hardware thermal protection and card-capacity limits remain completely intact. Long 6K or 4K takes in warm environments will still display normal thermal warnings if the body overheats.

---

## Technical Details (MC801 FW 2.9 Volatile RAM Map)

The DC-S5's Venus Engine already includes the 2-core HEVC tile-split encoder (`3072+2880` for `5952×3968`, `2944+2944` for `5888×3312`), the live movie-quality table (`0x08AA45E0`), and the built-in `6K` and `5.9K` UI glyphs and strings.

v1.5 writes expanded NTSC and PAL MOV menu/whitelist tables into unused volatile RAM (`0x870DD200`–`0x870DD510`) with `6K`/`5.9K` prepended at the top and all stock modes preserved, redirects the four table pointers, and patches the recording-limit and 6K-HDMI checks:

| Structure | Address | Unlock → Stock | Purpose |
| :--- | :--- | :--- | :--- |
| Expanded MOV UI & Whitelist Tables | `0x870DD200`–`0x870DD510` | 20 NTSC + 12 PAL MOV entries | Places `6K`/`5.9K` at top while preserving 100% of stock modes |
| NTSC Whitelist Pointer | `0x86462C34` | `0x870DD440` → `0x06F15E30` | Allows `6K24`, `5.9K30`, `5.9K24` plus all stock NTSC modes |
| PAL Whitelist Pointer | `0x86462C40` | `0x870DD4C0` → `0x06F15E9A` | Allows `5.9K25` plus all stock PAL modes |
| NTSC MOV Template Pointer | `0x866931B8` | `0x870DD200` → `0x08A68B6C` | Populates NTSC MOV Rec Quality list (survives Power OFF/ON) |
| PAL MOV Template Pointer | `0x866931D4` | `0x870DD360` → `0x08A68C84` | Populates PAL MOV Rec Quality list (survives Power OFF/ON) |
| Recording Limit Getter | `0x868261B6` | `0x2000` (`movs r0, #0`) → `0x4628` | Removes 30-min and 15-min caps across all modes |
| 6K/5.9K HDMI Blanking Checks | `0x866ACC9C`, `0x8664425C` | `0xBF00` (`nop`) → `0xD1E7`, `0xD17B` | Keeps external HDMI feed live during 6K/5.9K recording |

*Excluded modes:* `5.4K Open Gate` (`0x00EF`/`0x00F0`) and `True 24.00p` (`0x00EE`) are intentionally omitted because Panasonic compiled out their sensor timing and readout tables on the S5.

---

## Scope, Issues & Feature Requests

Please use **GitHub Issues** or **Discussions** for bug reports, S5/S5D compatibility reports, and feature requests.

- **In scope:** Original **Lumix DC-S5 / S5D** (FW 2.9), strictly volatile RAM (zero flash writes, zero brick risk).
- **Out of scope:** Permanent flash or ROM writes (brick risk), hardware-limited requests (uncropped 4K60, PDAF, internal RAW/ProRes), and other Lumix bodies I do not own (S5II/X, S9, GH7, S1II, G9). This project is MIT-licensed, so owners of other bodies are welcome to fork it and start with read-only checks.

---

## Disclaimer & License

**Unofficial community project — not affiliated with, endorsed by, or sponsored by Panasonic.** Provided as-is with no warranty; use at your own risk.

MIT License — see [`LICENSE`](LICENSE). Contains 100% original host code and memory offsets only; no Panasonic firmware binaries or copyrighted assets are included.
