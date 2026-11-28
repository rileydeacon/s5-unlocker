# S5 Unlocker for macOS (Apple Silicon)

Native macOS terminal port of the reference web installer (`docs/index.html`
v1.7). Same patches, same bytes, same USB order — with a colored interactive
menu. No dependencies beyond macOS system frameworks.

## Build

```sh
make -C mac-cli
# or: cc -O2 -Wall -Wextra -arch arm64 -o mac-cli/s5-unlocker \
#       mac-cli/s5_unlocker.c -framework IOKit -framework CoreFoundation
```

Produces `mac-cli/s5-unlocker` (arm64 only). Verify with:

```sh
file mac-cli/s5-unlocker
./mac-cli/s5-unlocker --self-test
```

## Use

```sh
./mac-cli/s5-unlocker            # interactive colored menu (default)
./mac-cli/s5-unlocker check     # read-only compatibility check
./mac-cli/s5-unlocker unlock [--cinema]
./mac-cli/s5-unlocker restore
./mac-cli/s5-unlocker install [--cinema]   # flash, two-step with prompts
./mac-cli/s5-unlocker uninstall            # flash, two-step with prompts
```

Camera setup is the same as the web installer: camera OFF, drive dial to
Single Shot, hold PLAYBACK + AF ON while powering on, connect USB-C.

Flash actions print a warning, require typing `WRITE`, stage the image, then
ask you to open **Menu → Video → Rec Quality** once on the camera before
verifying the 2048-byte read-back.

## Byte-perfect parity

Every patch table, the 88-byte stub, the hook words, the 2048-byte postboot2
image (`optb_image.h`, extracted verbatim from `OPTB_B64`), and the order of
all writes and interleaved read-backs match `docs/index.html` exactly. To
audit without a camera:

```sh
./mac-cli/s5-unlocker --dump-plan ram_unlock_cinema | head
./mac-cli/s5-unlocker --dump-plan stage_cinema | wc -l
```

`--dump-plan` prints the full ordered traffic plan (`W`/`R`/`B` lines) for
each operation, including camera-dependent branches as labeled sections.

Note: `s5_v15_test.c` (v1.5 prototype) writes the two HDMI words before the
unlimited-timer word; the canonical v1.7/Android order is unlimited first
with per-halfword read-back. This tool follows the canonical order.
