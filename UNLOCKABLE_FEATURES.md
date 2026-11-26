# Lumix DC-S5 (`MC801`, FW 2.9) — Unlockable Features Reference

This document catalogs every video and camera feature investigated via the **volatile RTOS RAM** workflow over Panasonic temporary Service Mode USB (`04DA:2370`, `op 0x9B` read / `op 0x9C` write).

> **Safety & Persistence Model:**
> - **Volatile RAM only:** The v1.5 installer writes its patches to volatile RAM (no flash, EEPROM, or bootloader writes).
> - **Survives Power-Switch OFF/ON (suspend-to-RAM)** via expanded tables in unused DRAM (`0x870DD200`–`0x870DD510`) and redirected pointers.
> - **Cold boot clears patches:** Removing the battery forces a cold boot that clears those patches. Experimental recording modes can fail, so test them before relying on them for a shoot.
> - **Strict target:** Original Lumix DC-S5 (Mark I, `MC801`), stock **Firmware 2.9** only. Evidence for the DC-S5D is preliminary and limited to read-only checks.

---

## 1. Live-Verified & Shipped in S5 Unlocker v1.5

| Feature | Mode / Address | Specs & Verified Behavior |
| :--- | :--- | :--- |
| **6K 3:2 Open Gate 23.98p MOV** | `qid 0x00ED` | `5952×3968` (full 3:2 sensor), 23.98p, HEVC Main 10 4:2:0 10-bit, 200 Mbps, 48 kHz stereo PCM audio, timecode. Appears at the **top** of **Rec Quality (MOV)**, updates live-view `6K` badge, plays back natively in-camera, no 30-min cap. |
| **5.9K 16:9 29.97p MOV** | `qid 0x00F4` | `5888×3312` (full-frame 16:9), 29.97p, HEVC Main 10 4:2:0 10-bit, 200 Mbps. Appears at the **top** of NTSC MOV Rec Quality, no 30-min cap. |
| **5.9K 16:9 23.98p MOV** | `qid 0x00F1` | `5888×3312` (full-frame 16:9), 23.98p, HEVC Main 10 4:2:0 10-bit, 200 Mbps. Appears at the **top** of NTSC MOV Rec Quality, no 30-min cap. |
| **5.9K 16:9 25.00p MOV (PAL)** | `qid 0x00F3` | `5888×3312` (full-frame 16:9), 25.00p (50.00 Hz PAL), HEVC Main 10 4:2:0 10-bit, 200 Mbps. Appears at the **top** of PAL MOV Rec Quality, no 30-min cap. |
| **Top-of-Menu & Zero Stock Modes Removed** | `0x870DD200`–`0x870DD510` | Expanded NTSC (20 modes) and PAL (12 modes) MOV tables with `6K`/`5.9K` prepended at the top and all 17 NTSC + 11 PAL stock MOV modes preserved. Redirects 4 pointers (`0x86462C34`, `0x86462C40`, `0x866931B8`, `0x866931D4`). |
| **Live HDMI Output During 6K / 5.9K Recording** | `0x866ACC9C`, `0x8664425C` → `0xBF00` (`nop`) | Removes the two hardcoded `Is6KMode && IsRecording` HDMI-blanking branches so external monitors stay live during 6K and 5.9K recording. |
| **Fixed Recording Timer Removal (All Timed Modes)** | `0x868261B6: 0x4628 → 0x2000` | Removes the fixed 30-minute (`0x708`) and 15-minute (`0x384`) recording timers across 4K, C4K, 4K-A 10-bit, 60p, and VFR modes. Card capacity and hardware thermal protection remain intact. |
| **Power-Switch Persistence (Suspend-to-RAM)** | Redirected pointers + DRAM tables | Keeps all unlocked modes, menu ordering, live HDMI, and fixed recording timer removal active across Power-Switch OFF/ON cycles until battery removal. |

---

## 2. Tested & Closed (Do Not Ship)

Every item below was investigated in the RTOS dumps and/or live-tested on the DC-S5 and confirmed not viable on stock FW 2.9:

| Feature | QID / Address | Finding |
| :--- | :--- | :--- |
| **PAL 6K 3:2 Open Gate 25p** | N/A | **Not available.** Stock FW 2.9 has no 6K 25p Open Gate encoder/sensor mode (even the S1H limits 6K 3:2 to 23.98p/24p). PAL users get **5.9K 16:9 25p (`0x00F3`)** in v1.5 instead. |
| **5.4K 3:2 Open Gate 25p / 29.97p** | `0x00EF` / `0x00F0` | **Excluded from v1.5.** Recording stalled during testing. |
| **True 24.00p Cinema (6K / 5.9K)** | `0x00EE` / `0x00F2` | **Excluded from v1.5.** Recording stalled during testing. |
| **4K 400 Mbps 4:2:2 10-bit ALL-I** | `0x0098` | **Refuses to record.** Throws *"cannot record due to incompatible format"* and locks the Rec Quality menu until `Menu → Setup → Reset → Reset Rec Settings`. |
| **6K Bitrate Bump (`200 → 300 Mbps`)** | `0x08AA65AC` `w5` | **Metadata-only.** Live-tested `.MOV` still encoded at ~190 Mbps; real rate control lives inside the VCL encoder DSP. |
| **Internal Live-View RAW Capture** | `sMonRawInfo` | **Monitor proxy only.** Live-tested custom ARM code writing 12-bit Bayer frames to SD, but `sMonRawInfo` is a `1982×560` vertically line-skipped monitor buffer; full 5.9K HDMI RAW streams straight from ISP to HDMI TX without buffering in DRAM. |
| **HDMI Down Convert (`1080p` in 6K)** | N/A | The original DC-S5 does not have a built-in HDMI Down Convert menu option; HDMI resolution is always negotiated via monitor EDID handshake, and S5 HDMI latency is a hardware pipeline limit. |

---

## 3. Research Findings (Not Shipped in S5 Unlocker)

- **S1H Hardware Waveform Monitor & Vectorscope (`AVIO_SetWfm` @ `0x8678DA82` / `WfmHwEnable` @ `0x8658BDC4` / `VscHwEnable` @ `0x8658BE06`):** Live-verified hardware Waveform on the S5 LCD with automatic menu/playback hiding (`AVIO_WfmForceMute` @ `0x8678DB1C`). Intentionally kept out of `S5 Unlocker` so the unlocker stays simple and focused.

---

## 4. Hardware-Limited (Cannot Be Unlocked by Software)

- **Full-frame uncropped 4K 60p or 6K 60p:** Limited by the IMX410 24 MP sensor's physical readout speed (60p requires APS-C crop; 4K 60p 10-bit APS-C is already stock).
- **Internal ProRes / Internal RAW (BRAW):** No ProRes or RAW hardware encoder on the S5's Venus Engine.
- **Phase-Detect Autofocus (PDAF):** Physical sensor limitation (S5 uses DFD contrast AF only).
- **Permanent Flash Persistence:** `.UPD` enforces ECDSA P-256 (`0x868C08C6`); `ROM BACKUP` (`0x8785CE42`) is EEPROM-only; no SD-card boot autoloader exists. Volatile RAM + suspend-to-RAM survival avoids flash writes and clears on cold boot.
