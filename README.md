# Lumix DC-S5 (`MC801`, FW 2.9) — Native 6K Open Gate, 5.9K (NTSC + PAL) & Unlimited Recording

**v1.3** — Unlock **native-menu internal 6K 3:2 Open Gate (`5952×3968` 23.98p)**, **5.9K 16:9 (`5888×3312` 23.98p / 29.97p NTSC & 25p PAL)**, and **unlimited recording in all limited modes** on the original **Panasonic Lumix DC-S5 (Mark I)** — with **zero flash writes, zero soldering, and 0% brick risk**.

- **Native S5 UI (NTSC & PAL):**
  - **NTSC (59.94Hz) MOV:** `6K 3:2 Open Gate 23.98p` (`0x00ED`), `5.9K 16:9 23.98p` (`0x00F1`), and `5.9K 16:9 29.97p` (`0x00F4`) appear directly at the bottom of **Menu → Video → Rec Quality (MOV)**.
  - **PAL (50.00Hz) MOV:** `5.9K 16:9 25.00p` (`0x00F3`) appears directly at the bottom of **Menu → Video → Rec Quality (MOV)**.
  - Works regardless of whether your camera is currently in MP4, MOV, PAL, or NTSC when you click Unlock.
- **Unlimited Recording:** Removes both the 30-minute and 15-minute artificial caps across all 4K, C4K, 4K-A 10-bit, 60p, and VFR modes (card capacity and hardware thermal protection stay intact).
- **Survives Power-Switch OFF/ON (Resets on Battery Pull):** Stays active across normal power-switch cycles via DRAM self-refresh until you open the battery door or pull the battery. Pulling the battery instantly restores 100% factory stock.
- **100% Reversible (0% Brick Risk):** Verifies **19 static firmware signature anchors** before writing 25 volatile RAM halfwords.

> [!CAUTION]
> **Supported hardware & firmware ONLY:** Original **Lumix DC-S5 (Mark I, `MC801`) running stock Firmware 2.9** *(early read-only checks indicate the **Lumix DC-S5D** shares the same memory map — use the **Read-only compatibility check** button first!)*.
> Both the Android app and WebUSB page verify **19 firmware signature anchors** before writing a single byte and safely abort on any other camera or firmware version. No warranty; use at your own risk.

---

## Quick Start (Choose One)

### Option 1 — Offline Android App (`S5Unlocker.apk`, Recommended for Field Use)
Works on any USB-C Android phone with USB OTG (**zero Wi-Fi, zero browser**, auto-launches on plug-in):
1. Download `S5Unlocker.apk` (v1.3) from **Releases** (or build from [`android/`](android/)) and install it on your Android phone.
2. On the DC-S5, hold **PLAYBACK + AF/AE LOCK** and flip Power **ON** (temporary service mode `04DA:2370`). *(On the **S5D**, set the left drive dial to **Single Shot** first.)*
3. Plug a USB-C cable between the phone and S5 — **S5 Unlocker** pops up automatically.
4. Tap **Unlock camera**, wait ~1 second for **Armed**, and unplug the cable.
5. Set **Rec File Format → MOV**, open **Menu → Video → Rec Quality**, and scroll to the bottom!

### Option 2 — 1-Click Chrome WebUSB (Mac / Windows / Android)
Open the live hosted page at **https://rileydeacon.github.io/s5-unlocker/** (from [`docs/index.html`](docs/index.html)) in **Google Chrome**:
1. Boot the DC-S5 holding **PLAYBACK + AF/AE LOCK** + Power **ON** *(S5D: left drive dial on **Single Shot**)*.
2. Plug USB-C into your Mac, PC, or Android phone.
3. Click **Connect USB** → **Unlock camera** → unplug and shoot — or click **Read-only compatibility check** to inspect all 19 anchors with zero writes.
*(Note: iOS / iPadOS Safari does not support WebUSB.)*

### Option 3 — macOS Command-Line Tool
Build and run [`mac-cli/s5_59k_branch.c`](mac-cli/s5_59k_branch.c) with `clang -O2 -framework IOKit -framework CoreFoundation mac-cli/s5_59k_branch.c -o s5_59k_branch` (`v13_check`, `v13_enable --confirm-write`, `v13_restore --confirm-write`).

---

## How It Works (MC801 FW 2.9 Volatile RAM Map)

The DC-S5's Venus Engine already contains the 2-core HEVC tile-split encoder (`3072+2880` for `5952×3968`; `2944+2944` for `5888×3312`), the live movie-quality table (`0x08AA45E0`), and the built-in `6K` / `5.9K` UI glyphs & strings. v1.3 swaps low-priority FHD MOV slots across both NTSC and PAL tables and bypasses the recording-limit getter in `0x86826102`:

| Structure | Address | Unlock → Stock | Purpose |
| :--- | :--- | :--- | :--- |
| NTSC Validator Whitelist | `0x86F15E8C`–`96` | `00F4/80F4/00F1/80F1/00ED/80ED` → `00CE..80D2` | Allows 5.9K30, 5.9K24, 6K24 in `0x864629B2` |
| NTSC Master ROM-in-RAM Template | `0x87089604`–`2C` | `00F4/80F4/00F1/80F1/00ED/80ED` → `00CE..80D2` | Survives soft Power-Switch OFF/ON wake-up |
| NTSC Active MOV Template | `0x08A68C4C`–`74` | `00F4/80F4/00F1/80F1/00ED/80ED` → `00CE..80D2` | Copied into live UI buffer by `0x866930B6` |
| PAL Validator Whitelist | `0x86F15ED6`–`D8` | `00F3/80F3` → `00CD/80CD` | Allows 5.9K 25p in PAL (50.00Hz) |
| PAL Master ROM-in-RAM Template | `0x870896CC`–`D4` | `00F3/80F3` → `00CD/80CD` | Survives soft Power-Switch OFF/ON wake-up |
| PAL Active MOV Template | `0x08A68D14`–`1C` | `00F3/80F3` → `00CD/80CD` | Copied into live UI buffer in PAL MOV |
| Recording Limit Getter | `0x868261B6` | `0x2000` (`movs r0, #0`) → `0x4628` | Removes 30-min & 15-min caps across all modes |

*(Note: 5.4K Open Gate `0x00EF`/`0x00F0` is intentionally excluded because its `5376×3584` pipeline stalls on record on the S5.)*

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
