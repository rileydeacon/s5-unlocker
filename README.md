# S5 Unlocker (`v1.5`)

Unlock native-menu **6K Open Gate**, **5.9K (NTSC and PAL)**, **live HDMI output during 6K/5.9K recording**, and **removal of the fixed recording timer** on the original **Panasonic Lumix DC-S5 (Mark I, Firmware 2.9)**.

> [!CAUTION]
> **Original Lumix DC-S5 (Mark I, `MC801`) running stock firmware 2.9 only.** S5 Unlocker is an unofficial community project unaffiliated with Panasonic. The Lumix DC-S5D is unsupported, even if it passes a read-only check.
>
> v1.5 applies temporary patches only to RAM. The patches can survive a normal power-switch cycle. Removing and reinserting the battery clears them, but the camera may remember the last recording quality. Select a factory recording mode before a battery swap. Test the unlocked modes with your own setup before relying on them for a shoot.

The Chrome and Android installers check **25 firmware values** and stop before applying patches if any check fails. Chrome’s **Read-only compatibility check** checks those values without applying the unlock. The macOS command-line tool performs its own compatibility check.

---

## What It Unlocks

| Feature | Details |
| :--- | :--- |
| **6K 3:2 Open Gate (NTSC)** | `5952×3968` · 23.98p · HEVC 10-bit 4:2:0 · 200 Mbps (`0x00ED`) |
| **5.9K 16:9 Full-Frame (NTSC)** | `5888×3312` · 23.98p (`0x00F1`) and 29.97p (`0x00F4`) · HEVC 10-bit 4:2:0 · 200 Mbps |
| **5.9K 16:9 Full-Frame (PAL)** | `5888×3312` · 25.00p (`0x00F3`) · HEVC 10-bit 4:2:0 · 200 Mbps |
| **Top-of-Menu Placement** | Unlocked modes appear at the **top** of **Menu → Video → Rec Quality (MOV)** |
| **Factory MOV Modes** | All 17 NTSC and 11 PAL factory MOV options remain available in Rec Quality |
| **Live HDMI in 6K / 5.9K** | Fixes the stock behavior where external HDMI output blanks out during 6K/5.9K recording |
| **Fixed Recording Timer Removal** | Removes the fixed 30-minute and 15-minute recording timers across 4K, C4K, 4K-A 10-bit, 60p, and VFR modes (thermal and card limits still apply) |
| **Normal Power-Switch Cycles** | The RAM patches can stay active across a normal OFF/ON cycle while the battery remains in place |

At **50.00 Hz (PAL)**, v1.5 adds **5.9K 16:9 at 25p**. The v1.5 Open Gate option is **6K 3:2 at 23.98p** in the **59.94 Hz (NTSC)** system.

You can run the unlock with MP4 or MOV selected. Switch to **MOV** to see the unlocked options for your current system frequency.

---

## Quick Start

Keep USB connected while the installer applies the patches. Wait for **Armed** before unplugging. If the process is interrupted or the camera behaves unexpectedly, wait for any card activity to finish, power the camera off, and remove and reinsert the battery to clear the patches. If **Rec Quality** is blank or stuck afterward, follow the [recovery steps](#if-the-mov-rec-quality-menu-is-blank-or-stuck).

### Option 1: Offline Android App (`S5Unlocker.apk`)

Requires a USB-C Android phone with USB OTG. The app works offline and can open automatically when you connect the camera.

1. Download `S5Unlocker.apk` from the [v1.5 release](https://github.com/rileydeacon/s5-unlocker/releases/tag/v1.5) (or build it from [`android/`](android/)) and install it on your phone.
2. On the DC-S5, hold **PLAYBACK + AF/AE LOCK** and flip Power **ON** to enter temporary service mode (`04DA:2370`).
3. Connect the phone and camera with a USB-C data cable. Open **S5 Unlocker** if it doesn’t launch automatically.
4. Tap **Unlock camera**, wait for **Armed**, then unplug the cable.
5. Set **Rec File Format → MOV** and open **Menu → Video → Rec Quality**. Your 6K and 5.9K modes will be at the top of the list.

### Option 2: Chrome WebUSB (Mac, Windows, or Android)

Open the hosted web app at **https://rileydeacon.github.io/s5-unlocker/** (served from [`docs/index.html`](docs/index.html)) in **Google Chrome**.

1. Boot the DC-S5 holding **PLAYBACK + AF/AE LOCK** and flip Power **ON**.
2. Connect the camera to your computer or Android phone with a USB-C data cable.
3. Click **Connect USB**, then click **Unlock camera**. Wait for **Armed**, then unplug the cable.
4. Set **Rec File Format → MOV** and open **Menu → Video → Rec Quality**. The unlocked modes appear at the top.

To check the firmware values without applying the unlock, choose **Read-only compatibility check**. A pass means the expected values matched.

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
- A normal **OFF/ON** cycle can keep the unlock active while the camera retains power to RAM.
- Removing and reinserting the battery clears the RAM patches. The camera may still remember the selected recording quality.
- For options that avoid re-arming during shoots (in-body USB-C charging, the DMW-BGS5 battery grip, or a \$5 USB-C keychain auto-arm dongle), see [`BATTERY_PERSISTENCE_AND_DONGLE.md`](BATTERY_PERSISTENCE_AND_DONGLE.md).

### If the MOV Rec Quality Menu Is Blank or Stuck
If you pull the battery while `6K` or `5.9K` is still selected, the RAM patch clears on cold boot while the camera still remembers the unlocked mode ID. This can make the MOV Rec Quality menu appear blank or stuck until reset.

Try these recovery steps in order.
- Press the **Q** button and change **Rec Quality** back to a normal stock mode.
- Switch **Rec File Format: MOV → MP4 → MOV**, or re-apply the unlock and select a stock mode.
- Run **Menu → Setup (Wrench) → Reset → Reset Rec Settings**. This resets recording settings and requires no USB connection.

### Thermal Protection
S5 Unlocker removes the fixed recording timers. It does not intentionally modify thermal protection or card-capacity limits. Test long takes with your card and shooting conditions before relying on them.

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
| Recording Limit Getter | `0x868261B6` | `0x2000` (`movs r0, #0`) → `0x4628` | Removes fixed 30-minute and 15-minute recording timers |
| 6K/5.9K HDMI Blanking Checks | `0x866ACC9C`, `0x8664425C` | `0xBF00` (`nop`) → `0xD1E7`, `0xD17B` | Keeps external HDMI feed live during 6K/5.9K recording |

5.4K Open Gate, 6K Open Gate at 25p, and true 24.00p are outside the v1.5 feature set.

---

## Scope, Issues & Feature Requests

Please use **GitHub Issues** or **Discussions** for bug reports on the supported DC-S5 and for feature requests.

- **In scope:** Original **Lumix DC-S5** (FW 2.9), volatile RAM patches only (cleared on cold boot).
- **Out of scope:** Permanent flash or ROM writes, hardware-limited requests (uncropped 4K60, PDAF, internal RAW/ProRes), and other Lumix bodies I do not own (S5II/X, S9, GH7, S1II, G9). This project is MIT-licensed, so owners of other bodies are welcome to fork it and start with read-only checks.

---

## Disclaimer & License

Provided as-is with no warranty. Use at your own risk.

MIT License — see [`LICENSE`](LICENSE). This release does not bundle a Panasonic firmware image.
