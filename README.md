# S5 Unlocker v1.5

Adds **6K Open Gate** and **5.9K recording** to the original **Lumix DC-S5 on stock firmware 2.9**. This is an unofficial project unaffiliated with Panasonic. The S5D and later S5 models are unsupported.

**[Open in Chrome](https://rileydeacon.github.io/s5-unlocker/)** · **[Download the Android app](https://github.com/rileydeacon/s5-unlocker/releases/tag/v1.5)**

## Quick start

1. Open the Chrome installer or install the Android app on a phone with USB OTG support.
2. With the camera off, set the **drive mode dial to Single Shot**. Hold **PLAYBACK + AF/AE LOCK** while turning the camera on, then connect it with a USB-C data cable. In Chrome, click **Connect USB**.
3. Select **Unlock camera**. Wait for **Armed** before unplugging.
4. Set **Rec File Format** to **MOV**. The unlocked modes appear at the top of **Video → Rec Quality**.

Chrome also offers a **Read-only compatibility check** that checks the camera without applying the unlock.

## Internal recording modes

| System frequency | Mode | Frame rate |
| --- | --- | --- |
| 59.94 Hz | 6K Open Gate · 3:2 · 5952×3968 | 23.98p |
| 59.94 Hz | 5.9K · 16:9 · 5888×3312 | 23.98p, 29.97p |
| 50 Hz (PAL) | 5.9K · 16:9 · 5888×3312 | 25p |

**HDMI monitoring.** The monitor feed stays active while the camera records 6K or 5.9K internally. An external recorder receives the camera’s selected HDMI output format.

**Recording limits.** The unlock removes fixed recording timers. It does not intentionally modify thermal protection or card-capacity limits. Test your setup before relying on the unlocked modes for a shoot.

## Battery swaps and recovery

The unlock runs in RAM. It can survive a normal power-switch cycle, and a battery pull clears it. **We recommend switching to a factory recording mode before swapping batteries** because the camera can remember the last selected mode.

### If the MOV Rec Quality Menu Is Blank or Stuck

If you left an unlocked mode selected, try these steps in order:

1. Press **Q** and select a factory recording quality.
2. Switch **MOV → MP4 → MOV**.
3. Open **Setup → Reset → Reset Rec Settings**.

[Feature notes](UNLOCKABLE_FEATURES.md) · [Battery guide](BATTERY_PERSISTENCE_AND_DONGLE.md) · [macOS CLI](mac-cli/) · [Report an issue](https://github.com/rileydeacon/s5-unlocker/issues)

Provided as is under the [MIT License](LICENSE). Use at your own risk.
