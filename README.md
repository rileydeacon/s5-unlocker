# S5 Unlocker v1.6

Adds **6K Open Gate** and **5.9K recording** to the original **Lumix DC-S5 on stock firmware 2.9**. This is an unofficial tool, not affiliated with Panasonic. The S5D and later S5 models are unsupported.

**[Open in Chrome (v1.6)](https://rileydeacon.github.io/s5-unlocker/)** · **[Download the Android app (v1.5 RAM-only)](https://github.com/rileydeacon/s5-unlocker/releases/tag/v1.5)**

## Quick start

1. Open the Chrome installer (v1.6) or install the Android app (v1.5 RAM-only) on a phone with USB OTG support.
2. With the camera off, set the **drive mode dial to Single Shot**. Hold **PLAYBACK + AF ON** while turning the camera on, then connect it with a USB-C data cable. In Chrome, click **Connect camera**.
3. Choose your install method:
   - **Temporary unlock (Recommended · Default):** Runs 100% in volatile RAM with zero writes to internal camera memory. Survives the power switch; removing the battery resets the camera to stock.
   - **Stay after battery swap (Advanced · Opt-in):** Writes a 2 KiB loader to internal camera memory so the unlock survives battery swaps. After verification finishes, unplug USB, **remove the battery for 5 seconds**, power on normally, and open **Menu → Video → Rec Quality** once. Plug back in anytime to restore factory stock.
4. Set **Rec File Format** to **MOV**. The unlocked modes appear at the top of **Video → Rec Quality**.

Chrome also offers **Just check if my camera is compatible** to verify your camera with zero writes.

> **Important warning for the opt-in permanent option:** Installing or uninstalling **Stay after battery swap** writes 2 KiB to internal camera memory. Use only on the original Lumix DC-S5 with firmware 2.9, keep your battery **above 50%**, and do not unplug USB until verification completes. If power cuts out or the write is interrupted, the camera may stop booting and require service. If you want zero flash risk, stick to the default **Temporary unlock**.

## Internal recording modes

| System frequency | Mode | Frame rate |
| --- | --- | --- |
| 59.94 Hz | 6K Open Gate · 3:2 · 5952×3968 | 23.98p |
| 59.94 Hz | 5.9K · 16:9 · 5888×3312 | 23.98p, 29.97p |
| 50 Hz (PAL) | 5.9K · 16:9 · 5888×3312 | 25p |

**HDMI monitoring.** The monitor feed stays active while the camera records 6K or 5.9K internally. 6K Open Gate is internal recording only; an external recorder receives the camera's standard downscaled HDMI monitoring feed.

**Recording limits.** The unlock removes fixed recording timers. It does not intentionally modify thermal protection or card-capacity limits. Test your setup before relying on the unlocked modes for a shoot.

## Battery swaps and recovery (Temporary mode)

If you use the default **Temporary unlock**, it survives a normal power-switch cycle and clears when you pull the battery. **Switch to a factory recording mode before swapping batteries** because the camera can remember the last selected mode.

### If the MOV Rec Quality Menu Is Blank or Stuck

If you left an unlocked mode selected on Temporary mode, try these steps in order:

1. Press **Q** and select a factory recording quality.
2. Switch **MOV → MP4 → MOV**.
3. Open **Setup → Reset → Reset Rec Settings**.

[Feature notes](UNLOCKABLE_FEATURES.md) · [Battery guide](BATTERY_PERSISTENCE_AND_DONGLE.md) · [macOS CLI](mac-cli/) · [Report an issue](https://github.com/rileydeacon/s5-unlocker/issues)

Provided as is under the [MIT License](LICENSE). Use at your own risk.
