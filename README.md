# Lumix DC-S5 (`MC801`, FW 2.9) — Native 6K Open Gate, 5.9K (NTSC + PAL) & Unlimited Recording

**v1.5** — Unlock **native-menu internal 6K 3:2 Open Gate (`5952×3968` 23.98p)**, **5.9K 16:9 (`5888×3312` 23.98p / 29.97p NTSC & 25p PAL)**, **live HDMI monitor output during 6K/5.9K recording**, and **unlimited recording in all limited modes** on the original **Panasonic Lumix DC-S5 (Mark I)** — with **zero flash writes, zero stock modes removed, and 0% brick risk**.

- **Top-of-Menu Placement & Zero Stock Modes Removed (NEW in v1.5):** `6K Open Gate` and `5.9K` now appear right at the **very top** of **Menu → Video → Rec Quality (MOV)** instead of mixed down with 1080p, and **100% of stock recording modes are preserved** (all 17 NTSC + 11 PAL stock MOV modes stay intact!).
- **Live HDMI Output During 6K / 5.9K Recording (v1.4):** Fixes the stock firmware behavior where HDMI output blanks out when starting/stopping 6K Open Gate or 5.9K recording (`0x866ACC9C` & `0x8664425C` → `NOP`).
- **Native S5 UI (NTSC & PAL):**
  - **NTSC (59.94Hz) MOV:** `6K 3:2 Open Gate 23.98p` (`0x00ED`), `5.9K 16:9 29.97p` (`0x00F4`), and `5.9K 16:9 23.98p` (`0x00F1`) appear at the **top** of **Menu → Video → Rec Quality (MOV)**.
  - **PAL (50.00Hz) MOV:** `5.9K 16:9 25.00p` (`0x00F3`) appears at the **top** of **Menu → Video → Rec Quality (MOV)**.
  - Works regardless of whether your camera is currently in MP4, MOV, PAL, or NTSC when you click Unlock.
- **Unlimited Recording:** Removes both the 30-minute and 15-minute artificial caps across all 4K, C4K, 4K-A 10-bit, 60p, and VFR modes (card capacity and hardware thermal protection stay intact).
- **Survives Power-Switch OFF/ON (Resets on Battery Pull):** Stays active across normal power-switch cycles via DRAM self-refresh until you open the battery door or pull the battery. Pulling the battery instantly restores 100% factory stock.
- **100% Reversible (0% Brick Risk):** Verifies **25 static firmware signature anchors** before writing to volatile RAM.

> [!CAUTION]
> **Supported hardware & firmware ONLY:** Original **Lumix DC-S5 (Mark I, `MC801`) running stock Firmware 2.9** *(early read-only checks indicate the **Lumix DC-S5D** shares the same memory map — use the **Read-only compatibility check** button first!)*.
> Both the Android app and WebUSB page verify **25 firmware signature anchors** before writing a single byte and safely abort on any other camera or firmware version. No warranty; use at your own risk.

---

## Quick Start (Choose One)

### Option 1 — Offline Android App (`S5Unlocker.apk`, Recommended for Field Use)
Works on any USB-C Android phone with USB OTG (**zero Wi-Fi, zero browser**, auto-launches on plug-in):
1. Download `S5Unlocker.apk` (v1.5) from **Releases** (or build from [`android/`](android/)) and install it on your Android phone.
2. On the DC-S5, hold **PLAYBACK + AF/AE LOCK** and flip Power **ON** (temporary service mode `04DA:2370`). *(On the **S5D**, set the left drive dial to **Single Shot** first.)*
3. Plug a USB-C cable between the phone and S5 — **S5 Unlocker** pops up automatically.
4. Tap **Unlock camera**, wait ~2 seconds for **Armed**, and unplug the cable.
5. Set **Rec File Format → MOV**, open **Menu → Video → Rec Quality**, and your **6K / 5.9K** modes are right at the **top**!

### Option 2 — 1-Click Chrome WebUSB (Mac / Windows / Android)
Open the live hosted page at **https://rileydeacon.github.io/s5-unlocker/** (from [`docs/index.html`](docs/index.html)) in **Google Chrome**:
1. Boot the DC-S5 holding **PLAYBACK + AF/AE LOCK** + Power **ON** *(S5D: left drive dial on **Single Shot**)*.
2. Plug USB-C into your Mac, PC, or Android phone.
3. Click **Connect USB** → **Unlock camera** → unplug and shoot — or click **Read-only compatibility check** to inspect all 25 anchors with zero writes.
*(Note: iOS / iPadOS Safari does not support WebUSB.)*

### Option 3 — macOS Command-Line Tool
Build and run [`mac-cli/s5_v15_test.c`](mac-cli/s5_v15_test.c) with `clang -O2 -framework IOKit -framework CoreFoundation mac-cli/s5_v15_test.c -o s5_unlocker` (`check`, `enable --confirm-write`, `restore --confirm-write`).

---

## How It Works (MC801 FW 2.9 Volatile RAM Map)

The DC-S5's Venus Engine already contains the 2-core HEVC tile-split encoder (`3072+2880` for `5952×3968`; `2944+2944` for `5888×3312`), the live movie-quality table (`0x08AA45E0`), and the built-in `6K` / `5.9K` UI glyphs & strings. v1.5 writes expanded NTSC/PAL MOV UI & whitelist tables into unused volatile RAM (`0x870DD200`–`0x870DD510`) with `6K`/`5.9K` prepended at the top and all stock modes preserved, redirects the 4 table pointers, and patches the recording-limit & 6K-HDMI checks:

| Structure | Address | Unlock → Stock | Purpose |
| :--- | :--- | :--- | :--- |
| Expanded MOV UI & Whitelist Tables | `0x870DD200`–`0x870DD510` | 20 NTSC + 12 PAL MOV entries | `6K`/`5.9K` at top + 100% of stock modes preserved |
| NTSC Whitelist Pointer | `0x86462C34` | `0x870DD440` → `0x06F15E30` | Allows `6K24`, `5.9K30`, `5.9K24` + all stock NTSC modes |
| PAL Whitelist Pointer | `0x86462C40` | `0x870DD4C0` → `0x06F15E9A` | Allows `5.9K25` + all stock PAL modes |
| NTSC MOV Template Pointer | `0x866931B8` | `0x870DD200` → `0x08A68B6C` | Populates MOV Rec Quality list (survives soft Power OFF/ON) |
| PAL MOV Template Pointer | `0x866931D4` | `0x870DD360` → `0x08A68C84` | Populates PAL MOV Rec Quality list (survives soft Power OFF/ON) |
| Recording Limit Getter | `0x868261B6` | `0x2000` (`movs r0, #0`) → `0x4628` | Removes 30-min & 15-min caps across all modes |
| 6K/5.9K HDMI Blanking Checks | `0x866ACC9C`, `0x8664425C` | `0xBF00` (`nop`) → `0xD1E7`, `0xD17B` | Keeps external HDMI feed live during 6K/5.9K recording |

*(Note: 5.4K Open Gate `0x00EF`/`0x00F0` and True 24.00p `0x00EE` are intentionally excluded because Panasonic compiled out their sensor timing/readout tables on the S5.)*

---

## Scope, Roadmap & Feature Requests

Please use **GitHub Issues / Discussions** for bug reports, S5/S5D compatibility reports, and feature requests!
- **In scope:** Original **Lumix DC-S5 / S5D** (FW 2.9), **100% volatile RAM** (0% brick risk, zero flash writes).
- **Out of scope for this repo:** Permanent flash/ROM writes (bricking risk), hardware-limited requests (uncropped 4K60, PDAF), and other Lumix bodies I don't own (S5II/X, S9, GH7, S1II, G9) — this repo is **MIT-licensed**, so owners of other bodies are encouraged to fork it and start with read-only RAM checks!

## Thermal, Battery & Troubleshooting Notes
- **Hardware thermal protection remains active.** Long 6K/4K takes in hot environments will still display normal thermal warnings if the body overheats.
- **Power-switch vs. battery pull:** Flipping the power switch OFF/ON keeps the unlock active. Charging in-body over USB-C (without opening the battery door), using a **DMW-BGS5 battery grip**, or building a **\$5 USB-C keychain auto-arm dongle** solves battery swaps with 0% brick risk — see **[`BATTERY_PERSISTENCE_AND_DONGLE.md`](BATTERY_PERSISTENCE_AND_DONGLE.md)** for full details! Opening the battery door or pulling the battery forces a cold boot back to 100% stock.
- **After pulling the battery while in 6K/5.9K:** If you pull the battery while 6K or 5.9K is still selected, the RAM patch clears on cold boot while the camera still remembers the unlocked mode ID — which can make the MOV Rec Quality menu appear blank or stuck until reset. **Always switch back to a stock mode before pulling the battery.** If it does happen, switch **Rec File Format: MOV → MP4 → MOV**, re-apply the patch and pick a stock mode, or use **Menu → Setup (Wrench) → Reset → Reset Rec Settings** (guaranteed 10-second recovery with no USB needed).

## Disclaimer & License
**Unofficial community project — not affiliated with, endorsed by, or sponsored by Panasonic.** Provided as-is with no warranty; use at your own risk.

MIT License — see [`LICENSE`](LICENSE). Contains 100% original host code and memory offsets only; no Panasonic firmware binaries or copyrighted assets are included.
