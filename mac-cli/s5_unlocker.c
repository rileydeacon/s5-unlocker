/*
 * s5_unlocker.c — S5 Unlocker v1.7 for macOS (Apple Silicon) terminal.
 *
 * Native macOS port of the reference web installer (docs/index.html v1.7).
 * Talk to a Lumix DC-S5 (FW 2.9) in Service Mode over USB (04DA:2370)
 * using IOKit, with a colored interactive terminal menu.
 *
 * BYTE-PERFECT CONTRACT: every patch byte and every write's position in the
 * USB sequence is identical to docs/index.html, function for function:
 *   verifySignatures / verifyCinemaAnchors / writeUiList / applyRamUnlock /
 *   prepareHookRegion / stageAndArm / disarmHookAndScratch / verifyAndFinish.
 * Table contents, staging bytes, stub words, hook words, and the 2048-byte
 * postboot2 image (mac-cli/optb_image.h, extracted verbatim from OPTB_B64)
 * are identical. Use `--dump-plan <op>` to print the exact ordered
 * (address,value) write plan and diff it against the reference.
 *
 * NOTE on mac-cli/s5_v15_test.c: that v1.5 prototype writes the HDMI words
 * before the unlimited-timer word and verifies u32 pairs differently. The
 * canonical order (docs/index.html + Android app) writes unlimited first
 * with per-halfword write+readback. This tool follows the canonical order.
 *
 * Build (arm64):  cc -O2 -Wall -Wextra -arch arm64 -o s5-unlocker \
 *                     s5_unlocker.c -framework IOKit -framework CoreFoundation
 * Or: make -C mac-cli
 */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "optb_image.h"

#define TOOL_VERSION "1.7.0-macos.1"

/* ---------------- USB constants (identical to reference) ---------------- */
#define PANASONIC_VID 0x04DA
#define SERVICE_PID   0x2370
#define TIMEOUT_MS    4000
#define BLOCK_TIMEOUT_MS 8000
#define FRAME_BYTES   16
#define BLOCK_BYTES   1024
#define BLOCK_FRAME   1030

/* ---------------- Addresses (identical to reference) ---------------- */
#define SRC_RAM       0x870F0000u
#define RB_RAM        0x870F2000u
#define PB2_RAM_BASE  0x876E7000u
#define PB2_SIZE      0x20000u
#define LOADER_ADDR   0x870DD000u
#define STATUS_ADDR   0x870DD100u
#define HOOK_ADDR     0x866930EAu
#define HOOK_REGION   0x866930E8u
#define NEW_NTSC_UI   0x870DD200u
#define NEW_PAL_UI    0x870DD360u
#define NEW_NTSC_WL   0x870DD440u
#define NEW_PAL_WL    0x870DD4C0u
#define LIVE_UI_BASE  0x08A6836Cu

static const char *STOCK_PB2_SHA256 =
    "76a627dc813626668f242f3da0001e9c7df6cb6983a68093951b42d4b8a81284";

/* ---------------- ANSI colors ---------------- */
static int g_color = 1;
#define C_RST    (g_color ? "\033[0m" : "")
#define C_BOLD   (g_color ? "\033[1m" : "")
#define C_DIM    (g_color ? "\033[2m" : "")
#define C_RED    (g_color ? "\033[31m" : "")
#define C_GREEN  (g_color ? "\033[32m" : "")
#define C_YELLOW (g_color ? "\033[33m" : "")
#define C_CYAN   (g_color ? "\033[36m" : "")
#define C_WHITE  (g_color ? "\033[37m" : "")
#define C_CRIMSON (g_color ? "\033[38;5;131m" : "")
#define C_BG_RED (g_color ? "\033[41m" : "")

/* ---------------- Error state ---------------- */
static char g_err[512];
static void set_err(const char *m) {
    snprintf(g_err, sizeof g_err, "%s", m);
}
/* (error strings are built at each call site; see verify_* for examples) */

/* ---------------- UI / whitelist tables (identical to reference) ----------------
 * NTSC_UI_NEW = [00ED,80ED,00F4,80F4,00F1,80F1, ...NTSC_UI_STK] (40 entries)
 * PAL_UI_NEW  = [00F3,80F3, ...PAL_UI_STK] (24 entries)                        */
static const uint16_t NTSC_UI_STK[34] = {
    0x0092u, 0x8092u, 0x0094u, 0x8094u, 0x0095u, 0x8095u, 0x0096u, 0x8096u,
    0x0097u, 0x8097u, 0x009Bu, 0x809Bu, 0x009Fu, 0x809Fu, 0x00A1u, 0x80A1u,
    0x00A5u, 0x80A5u, 0x00A8u, 0x80A8u, 0x00A7u, 0x80A7u, 0x00BEu, 0x80BEu,
    0x00C0u, 0x80C0u, 0x00CCu, 0x80CCu, 0x00CEu, 0x80CEu, 0x00D1u, 0x80D1u,
    0x00D2u, 0x80D2u
};
static const uint16_t NTSC_UI_NEW[40] = {
    0x00EDu, 0x80EDu, 0x00F4u, 0x80F4u, 0x00F1u, 0x80F1u,
    0x0092u, 0x8092u, 0x0094u, 0x8094u, 0x0095u, 0x8095u, 0x0096u, 0x8096u,
    0x0097u, 0x8097u, 0x009Bu, 0x809Bu, 0x009Fu, 0x809Fu, 0x00A1u, 0x80A1u,
    0x00A5u, 0x80A5u, 0x00A8u, 0x80A8u, 0x00A7u, 0x80A7u, 0x00BEu, 0x80BEu,
    0x00C0u, 0x80C0u, 0x00CCu, 0x80CCu, 0x00CEu, 0x80CEu, 0x00D1u, 0x80D1u,
    0x00D2u, 0x80D2u
};
static const uint16_t PAL_UI_STK[22] = {
    0x0093u, 0x8093u, 0x00FEu, 0x80FEu, 0x00FFu, 0x80FFu, 0x009Cu, 0x809Cu,
    0x00A0u, 0x80A0u, 0x00A2u, 0x80A2u, 0x00A6u, 0x80A6u, 0x00BFu, 0x80BFu,
    0x00C1u, 0x80C1u, 0x00CDu, 0x80CDu, 0x00CFu, 0x80CFu
};
static const uint16_t PAL_UI_NEW[24] = {
    0x00F3u, 0x80F3u,
    0x0093u, 0x8093u, 0x00FEu, 0x80FEu, 0x00FFu, 0x80FFu, 0x009Cu, 0x809Cu,
    0x00A0u, 0x80A0u, 0x00A2u, 0x80A2u, 0x00A6u, 0x80A6u, 0x00BFu, 0x80BFu,
    0x00C1u, 0x80C1u, 0x00CDu, 0x80CDu, 0x00CFu, 0x80CFu
};
static const uint16_t NTSC_WL_NEW[58] = {
    0x00EDu, 0x80EDu, 0x00F4u, 0x80F4u, 0x00F1u, 0x80F1u,
    0x0021u, 0x8021u, 0x0023u, 0x8023u, 0x0026u, 0x8026u, 0x001Cu, 0x801Cu,
    0x001Fu, 0x801Fu, 0x00F9u, 0x80F9u, 0x00FBu, 0x80FBu, 0x00FDu, 0x80FDu,
    0x008Bu, 0x808Bu, 0x0092u, 0x8092u, 0x0094u, 0x8094u, 0x0095u, 0x8095u,
    0x0096u, 0x8096u, 0x0097u, 0x8097u, 0x009Fu, 0x809Fu, 0x00A5u, 0x80A5u,
    0x00A7u, 0x80A7u, 0x009Bu, 0x809Bu, 0x00A1u, 0x80A1u, 0x00A8u, 0x80A8u,
    0x00BEu, 0x80BEu, 0x00C0u, 0x80C0u, 0x00CCu, 0x80CCu, 0x00CEu, 0x80CEu,
    0x00D1u, 0x80D1u, 0x00D2u, 0x80D2u
};
static const uint16_t PAL_WL_NEW[36] = {
    0x00F3u, 0x80F3u,
    0x0022u, 0x8022u, 0x0024u, 0x8024u, 0x001Du, 0x801Du, 0x00FAu, 0x80FAu,
    0x00FCu, 0x80FCu, 0x008Cu, 0x808Cu, 0x0093u, 0x8093u, 0x00FFu, 0x80FFu,
    0x00FEu, 0x80FEu, 0x00A0u, 0x80A0u, 0x00A6u, 0x80A6u, 0x009Cu, 0x809Cu,
    0x00A2u, 0x80A2u, 0x00BFu, 0x80BFu, 0x00C1u, 0x80C1u, 0x00CDu, 0x80CDu,
    0x00CFu, 0x80CFu
};

/* RAM_PATCHES: [addr, unlock, stock] in reference order (11 entries). */
typedef struct { uint32_t addr; uint16_t unl; uint16_t stk; const char *name; } patch_t;
static const patch_t RAM_PATCHES[11] = {
    { 0x86462C34u, 0xD440u, 0x5E30u, "ptr_ntsc_wl_lo" },
    { 0x86462C36u, 0x870Du, 0x06F1u, "ptr_ntsc_wl_hi" },
    { 0x86462C40u, 0xD4C0u, 0x5E9Au, "ptr_pal_wl_lo"  },
    { 0x86462C42u, 0x870Du, 0x06F1u, "ptr_pal_wl_hi"  },
    { 0x866931B8u, 0xD200u, 0x8B6Cu, "ptr_ntsc_ui_lo" },
    { 0x866931BAu, 0x870Du, 0x08A6u, "ptr_ntsc_ui_hi" },
    { 0x866931D4u, 0xD360u, 0x8C84u, "ptr_pal_ui_lo"  },
    { 0x866931D6u, 0x870Du, 0x08A6u, "ptr_pal_ui_hi"  },
    { 0x868261B6u, 0x2000u, 0x4628u, "unlimited_all"  },
    { 0x866ACC9Cu, 0xBF00u, 0xD1E7u, "hdmi_live_6k_1" },
    { 0x8664425Cu, 0xBF00u, 0xD17Bu, "hdmi_live_6k_2" },
};

/* CINEMA_PATCHES: [addr, unlock, stock] in reference order (12 entries). */
static const patch_t CINEMA_PATCHES[12] = {
    { 0x86514FBCu, 0xE006u, 0xD006u, "yctdnr_hw_bypass"  },
    { 0x8652DA84u, 0x200Bu, 0x201Du, "vlog_main_yc_sharp" },
    { 0x867CD284u, 0x200Bu, 0x201Eu, "vlog_sec_edge_sharp" },
    { 0x865256B4u, 0x2301u, 0x4879u, "vlog_nr_55c6_0"   },
    { 0x865256B6u, 0xE7F9u, 0xF24Du, "vlog_nr_55c6_1"   },
    { 0x865256B8u, 0xBF00u, 0x433Cu, "vlog_nr_55c6_2"   },
    { 0x867C936Au, 0x2001u, 0x488Bu, "vlog_nr_9332_0"   },
    { 0x867C936Cu, 0xE0B5u, 0xF64Du, "vlog_nr_9332_1"   },
    { 0x867C936Eu, 0xBF00u, 0x734Eu, "vlog_nr_9332_2"   },
    { 0x86524AA8u, 0x2001u, 0x4891u, "vlog_nr_4572_0"   },
    { 0x86524AAAu, 0xE006u, 0xF64Eu, "vlog_nr_4572_1"   },
    { 0x86524AACu, 0xBF00u, 0x61F4u, "vlog_nr_4572_2"   },
};

/* SIG_CHECKS: 47 static anchors in reference order. nallow=1 or 2. */
typedef struct { uint32_t addr; uint16_t allow[2]; int nallow; const char *name; } sigcheck_t;
static const sigcheck_t SIG_CHECKS[47] = {
    { 0x866B493Au, { 0x2801u, 0 }, 1, "rtos_sig1" },
    { 0x866B493Eu, { 0xB2E0u, 0 }, 1, "rtos_sig2" },
    { 0x866B4952u, { 0x486Bu, 0 }, 1, "rtos_sig3" },
    { 0x866B4956u, { 0xBD10u, 0 }, 1, "rtos_sig4" },
    { 0x868261B4u, { 0x460Du, 0 }, 1, "cap_prev" },
    { 0x868261B6u, { 0x4628u, 0x2000u }, 2, "cap_ret" },
    { 0x868261B8u, { 0xE8BDu, 0 }, 1, "cap_pop" },
    { 0x866ACC9Cu, { 0xD1E7u, 0xBF00u }, 2, "hdmi_6k_rec1" },
    { 0x8664425Cu, { 0xD17Bu, 0xBF00u }, 2, "hdmi_6k_rec2" },
    { 0x86462C34u, { 0x5E30u, 0xD440u }, 2, "ptr_ntsc_wl_lo" },
    { 0x86462C36u, { 0x06F1u, 0x870Du }, 2, "ptr_ntsc_wl_hi" },
    { 0x86462C40u, { 0x5E9Au, 0xD4C0u }, 2, "ptr_pal_wl_lo" },
    { 0x86462C42u, { 0x06F1u, 0x870Du }, 2, "ptr_pal_wl_hi" },
    { 0x866931B8u, { 0x8B6Cu, 0xD200u }, 2, "ptr_ntsc_ui_lo" },
    { 0x866931BAu, { 0x08A6u, 0x870Du }, 2, "ptr_ntsc_ui_hi" },
    { 0x866931D4u, { 0x8C84u, 0xD360u }, 2, "ptr_pal_ui_lo" },
    { 0x866931D6u, { 0x08A6u, 0x870Du }, 2, "ptr_pal_ui_hi" },
    { 0x86F15E84u, { 0x00C0u, 0 }, 1, "ntsc_wl_anchor" },
    { 0x86F15E98u, { 0xFFFFu, 0 }, 1, "ntsc_wl_sentinel" },
    { 0x86F15ED2u, { 0x00C1u, 0 }, 1, "pal_wl_anchor" },
    { 0x86F15EDEu, { 0xFFFFu, 0 }, 1, "pal_wl_sentinel" },
    { 0x870895E4u, { 0x00C0u, 0 }, 1, "ntsc_rom_anchor" },
    { 0x87089634u, { 0xFFFFu, 0 }, 1, "ntsc_rom_sentinel" },
    { 0x870896BCu, { 0x00C1u, 0 }, 1, "pal_rom_anchor" },
    { 0x870896ECu, { 0xFFFFu, 0 }, 1, "pal_rom_sentinel" },
    { 0x86809784u, { 0xE92Du, 0 }, 1, "flash_write" },
    { 0x86809992u, { 0xB570u, 0 }, 1, "flash_read" },
    { 0x86860964u, { 0x2801u, 0 }, 1, "flash_lock" },
    { 0x86860990u, { 0x2008u, 0 }, 1, "flash_unlock" },
    { 0x8640FF76u, { 0xB5F8u, 0 }, 1, "dcache_clean" },
    { 0x86410026u, { 0xB508u, 0 }, 1, "icache_inval" },
    { 0x877C20BEu, { 0xF241u, 0xF640u }, 2, "vcl_w_hw0" },
    { 0x877C20C0u, { 0x7040u, 0x4000u }, 2, "vcl_w_hw1" },
    { 0x877C20C4u, { 0xF640u, 0 }, 1, "vcl_h_hw0" },
    { 0x877C20C6u, { 0x7080u, 0x0000u }, 2, "vcl_h_hw1" },
    { 0x8656E0F0u, { 0xF241u, 0xF640u }, 2, "s3_w_hw0" },
    { 0x8656E0F2u, { 0x7040u, 0x4000u }, 2, "s3_w_hw1" },
    { 0x8656E0F6u, { 0xF640u, 0 }, 1, "s3_h_hw0" },
    { 0x8656E0F8u, { 0x7080u, 0x0000u }, 2, "s3_h_hw1" },
    { 0x866DF88Au, { 0xF241u, 0xF640u }, 2, "s4_w_hw0" },
    { 0x866DF88Cu, { 0x7140u, 0x4100u }, 2, "s4_w_hw1" },
    { 0x866DF890u, { 0xF640u, 0 }, 1, "s4_h_hw0" },
    { 0x866DF892u, { 0x7180u, 0x0100u }, 2, "s4_h_hw1" },
    { 0x86A08AF8u, { 0x1740u, 0x0C00u }, 2, "enc_w" },
    { 0x86A08B24u, { 0x03E9u, 0 }, 1, "enc_den" },
    { 0x86A10CC0u, { 0x0002u, 0 }, 1, "tile1_cores" },
    { 0x86A10E40u, { 0x0002u, 0 }, 1, "tile2_cores" },
};

/* FLASH_STUB: 88-byte flash-commit + readback stub (44 halfwords). */
static const uint16_t FLASH_STUB[44] = {
    0xB57Fu, 0x4C0Fu, 0x6820u, 0xB9B0u, 0x2001u, 0x6020u, 0xF732u, 0xD7B3u,
    0x480Cu, 0x490Du, 0x4A0Du, 0xF72Cu, 0xDBB5u, 0x6060u, 0x480Au, 0x490Cu,
    0x4A0Au, 0xF72Cu, 0xDCB6u, 0x60A0u, 0xF732u, 0xD7A5u, 0x4D09u, 0x6828u,
    0x60E0u, 0x68E8u, 0x6120u, 0xE8BDu, 0x407Fu, 0xF5D7u, 0x9C4Eu, 0xBF00u,
    0xD100u, 0x870Du, 0x0000u, 0x070Fu, 0x0000u, 0x4048u, 0x0800u, 0x0000u,
    0x2000u, 0x070Fu, 0x2000u, 0x870Fu
};

static const uint16_t HOOK_STOCK[2] = { 0xF021u, 0xFBF6u };
static const uint16_t HOOK_PATCH[2] = { 0xF249u, 0xDF89u };
static const uint16_t HOOKREG_STOCK[4] = { 0x2000u, 0xF021u, 0xFBF6u, 0xB188u };
static const uint16_t HOOKREG_TRAMP[4] = { 0xF8DFu, 0xF000u, 0x7029u, 0x076Eu };

/* ---------------- SHA-256 (FIPS 180-4, self-contained) ---------------- */
static const uint32_t SHA256_K[64] = {
    0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u,
    0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
    0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u,
    0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u,
    0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu,
    0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
    0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u,
    0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
    0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u,
    0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
    0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u,
    0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
    0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u,
    0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
    0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
    0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u
};
static uint32_t rotr32(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

static void sha256_raw(const uint8_t *msg, size_t len, uint8_t out[32]) {
    uint32_t h[8] = { 0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au,
                      0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u };
    size_t padded = (len + 9 + 63) & ~(size_t)63;
    uint8_t *m = (uint8_t *)calloc(1, padded ? padded : 64);
    if (!m) { memset(out, 0, 32); return; }
    memcpy(m, msg, len);
    m[len] = 0x80;
    uint64_t bits = (uint64_t)len * 8u;
    m[padded - 8] = (uint8_t)(bits >> 56); m[padded - 7] = (uint8_t)(bits >> 48);
    m[padded - 6] = (uint8_t)(bits >> 40); m[padded - 5] = (uint8_t)(bits >> 32);
    m[padded - 4] = (uint8_t)(bits >> 24); m[padded - 3] = (uint8_t)(bits >> 16);
    m[padded - 2] = (uint8_t)(bits >> 8);  m[padded - 1] = (uint8_t)(bits);
    for (size_t off = 0; off < padded; off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = ((uint32_t)m[off + i * 4] << 24) | ((uint32_t)m[off + i * 4 + 1] << 16) |
                   ((uint32_t)m[off + i * 4 + 2] << 8) | (uint32_t)m[off + i * 4 + 3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t t1 = hh + S1 + ch + SHA256_K[i] + w[i];
            uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    free(m);
    for (int i = 0; i < 8; i++) {
        out[i * 4] = (uint8_t)(h[i] >> 24); out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(h[i] >> 8); out[i * 4 + 3] = (uint8_t)h[i];
    }
}

static void sha256_hex(const uint8_t *msg, size_t len, char out[65]) {
    uint8_t d[32];
    static const char *HD = "0123456789abcdef";
    sha256_raw(msg, len, d);
    for (int i = 0; i < 32; i++) {
        out[i * 2] = HD[d[i] >> 4];
        out[i * 2 + 1] = HD[d[i] & 15];
    }
    out[64] = 0;
}

/* ---------------- USB layer (IOKit) ---------------- */
static IOUSBInterfaceInterface300 **g_usb = NULL;
static IOCFPlugInInterface **g_plug = NULL, **g_ifacePlug = NULL;
static IOUSBDeviceInterface300 **g_dev = NULL;
static io_service_t g_device = 0, g_ifaceSvc = 0;
static UInt8 g_outP = 0, g_inP = 0;

static uint8_t frame_sum(const uint8_t *p, size_t n) {
    uint8_t s = 0;
    for (size_t i = 1; i + 2 < n; ++i) s = (uint8_t)(s + p[i]);
    return s;
}

static int usb_prepare(UInt16 index) {
    IOUSBDevRequestTO r;
    memset(&r, 0, sizeof r);
    r.bmRequestType = 0x40; r.bRequest = 0; r.wIndex = index;
    r.noDataTimeout = TIMEOUT_MS; r.completionTimeout = TIMEOUT_MS;
    if ((*g_usb)->ControlRequestTO(g_usb, 0, &r) != kIOReturnSuccess) {
        set_err("prepare() control transfer failed");
        return -1;
    }
    return 0;
}

static int usb_open(void) {
    CFMutableDictionaryRef match = IOServiceMatching(kIOUSBDeviceClassName);
    if (!match) { set_err("IOServiceMatching failed"); return -1; }
    int vid = PANASONIC_VID, pid = SERVICE_PID;
    CFNumberRef nvid = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vid);
    CFNumberRef npid = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &pid);
    CFDictionarySetValue(match, CFSTR(kUSBVendorID), nvid);
    CFDictionarySetValue(match, CFSTR(kUSBProductID), npid);
    CFRelease(nvid); CFRelease(npid);
    g_device = IOServiceGetMatchingService(kIOMainPortDefault, match);
    if (!g_device) { set_err("No 04DA:2370 device found"); return -1; }

    SInt32 score = 0;
    if (IOCreatePlugInInterfaceForService(g_device, kIOUSBDeviceUserClientTypeID,
            kIOCFPlugInInterfaceID, &g_plug, &score) != kIOReturnSuccess || !g_plug) {
        set_err("plug-in create failed"); return -1;
    }
    if ((*g_plug)->QueryInterface(g_plug, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID300),
            (LPVOID *)&g_dev) != S_OK || !g_dev) {
        set_err("device interface query failed"); return -1;
    }
    IOUSBFindInterfaceRequest req;
    req.bInterfaceClass = kIOUSBFindInterfaceDontCare;
    req.bInterfaceSubClass = kIOUSBFindInterfaceDontCare;
    req.bInterfaceProtocol = kIOUSBFindInterfaceDontCare;
    req.bAlternateSetting = kIOUSBFindInterfaceDontCare;
    io_iterator_t it = 0;
    (*g_dev)->CreateInterfaceIterator(g_dev, &req, &it);
    g_ifaceSvc = IOIteratorNext(it);
    IOObjectRelease(it);
    if (!g_ifaceSvc) { set_err("no USB interface found"); return -1; }
    if (IOCreatePlugInInterfaceForService(g_ifaceSvc, kIOUSBInterfaceUserClientTypeID,
            kIOCFPlugInInterfaceID, &g_ifacePlug, &score) != kIOReturnSuccess || !g_ifacePlug) {
        set_err("interface plug-in create failed"); return -1;
    }
    if ((*g_ifacePlug)->QueryInterface(g_ifacePlug,
            CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID300),
            (LPVOID *)&g_usb) != S_OK || !g_usb) {
        set_err("interface query failed"); return -1;
    }
    if ((*g_usb)->USBInterfaceOpen(g_usb) != kIOReturnSuccess) {
        set_err("USBInterfaceOpen failed (is another program using the camera?)");
        return -1;
    }
    UInt8 nEp = 0;
    g_outP = 0; g_inP = 0;
    (*g_usb)->GetNumEndpoints(g_usb, &nEp);
    for (UInt8 i = 1; i <= nEp; ++i) {
        UInt8 dir = 0, num = 0, tt = 0, iv = 0; UInt16 mps = 0;
        (*g_usb)->GetPipeProperties(g_usb, i, &dir, &num, &tt, &mps, &iv);
        if (tt == kUSBBulk && dir == kUSBOut && !g_outP) g_outP = i;
        if (tt == kUSBBulk && dir == kUSBIn && !g_inP) g_inP = i;
    }
    if (!g_outP || !g_inP) { set_err("bulk endpoints missing"); return -1; }
    return 0;
}

static void usb_close(void) {
    if (g_usb) { (*g_usb)->USBInterfaceClose(g_usb); (*g_usb)->Release(g_usb); g_usb = NULL; }
    if (g_ifacePlug) { (*g_ifacePlug)->Release(g_ifacePlug); g_ifacePlug = NULL; }
    if (g_dev) { (*g_dev)->Release(g_dev); g_dev = NULL; }
    if (g_plug) { (*g_plug)->Release(g_plug); g_plug = NULL; }
    if (g_ifaceSvc) { IOObjectRelease(g_ifaceSvc); g_ifaceSvc = 0; }
    if (g_device) { IOObjectRelease(g_device); g_device = 0; }
    g_outP = 0; g_inP = 0;
}

/* readHalf: TX [8,0,FF,9B,addrLE,0,0,0,0,0,0,sum,0]; type byte 8; 16B reply. */
static int usb_read_half(uint32_t addr, uint16_t *out) {
    uint8_t tx[FRAME_BYTES], rx[64], type = 0;
    UInt32 len;
    memset(tx, 0, sizeof tx);
    tx[0] = 8; tx[2] = 0xFF; tx[3] = 0x9B;
    for (unsigned i = 0; i < 4; ++i) tx[4 + i] = (uint8_t)(addr >> (8 * i));
    tx[14] = frame_sum(tx, sizeof tx);
    if (usb_prepare(0)) return -1;
    if ((*g_usb)->WritePipeTO(g_usb, g_outP, tx, sizeof tx,
            TIMEOUT_MS, TIMEOUT_MS) != kIOReturnSuccess) {
        set_err("read bulk OUT failed"); return -1;
    }
    if (usb_prepare(1)) return -1;
    len = 1;
    if ((*g_usb)->ReadPipeTO(g_usb, g_inP, &type, &len,
            TIMEOUT_MS, TIMEOUT_MS) != kIOReturnSuccess || len != 1 || type != 8) {
        set_err("bad read type"); return -1;
    }
    if (usb_prepare(1)) return -1;
    len = sizeof rx;
    if ((*g_usb)->ReadPipeTO(g_usb, g_inP, rx, &len,
            TIMEOUT_MS, TIMEOUT_MS) != kIOReturnSuccess || len != FRAME_BYTES) {
        set_err("bad read len"); return -1;
    }
    if (rx[3] != 0x36 || rx[14] != frame_sum(rx, FRAME_BYTES) || rx[15] != 0) {
        set_err("bad read checksum"); return -1;
    }
    *out = (uint16_t)(rx[4] | (rx[5] << 8));
    return 0;
}

/* writeHalf: TX [8,0,FF,9C,addrLE,valLE,0,0,0,0,sum,0]; 1B ack type; ack frame. */
static int usb_write_half(uint32_t addr, uint16_t val) {
    uint8_t tx[FRAME_BYTES], rx[64], type = 0;
    UInt32 len;
    memset(tx, 0, sizeof tx);
    tx[0] = 8; tx[2] = 0xFF; tx[3] = 0x9C;
    for (unsigned i = 0; i < 4; ++i) tx[4 + i] = (uint8_t)(addr >> (8 * i));
    tx[8] = (uint8_t)val; tx[9] = (uint8_t)(val >> 8);
    tx[14] = frame_sum(tx, sizeof tx);
    if (usb_prepare(0)) return -1;
    if ((*g_usb)->WritePipeTO(g_usb, g_outP, tx, sizeof tx,
            TIMEOUT_MS, TIMEOUT_MS) != kIOReturnSuccess) {
        set_err("write bulk OUT failed"); return -1;
    }
    if (usb_prepare(1)) return -1;
    len = 1;
    /* Reference accepts any single ack type byte; camera sends 8 or 1. */
    if ((*g_usb)->ReadPipeTO(g_usb, g_inP, &type, &len,
            TIMEOUT_MS, TIMEOUT_MS) != kIOReturnSuccess || len != 1) {
        set_err("bad ack type"); return -1;
    }
    if (usb_prepare(1)) return -1;
    len = sizeof rx;
    if ((*g_usb)->ReadPipeTO(g_usb, g_inP, rx, &len,
            TIMEOUT_MS, TIMEOUT_MS) != kIOReturnSuccess || len < 6) {
        set_err("short ack"); return -1;
    }
    if (rx[0] != 8 || rx[2] != 0xFF || rx[len - 2] != frame_sum(rx, len) ||
            rx[len - 1] != 0 || (rx[3] != 0 && rx[3] != 1)) {
        set_err("write NACK"); return -1;
    }
    return 0;
}

static int usb_read_u32(uint32_t addr, uint32_t *out) {
    uint16_t lo = 0, hi = 0;
    if (usb_read_half(addr, &lo) || usb_read_half(addr + 2, &hi)) return -1;
    *out = (uint32_t)lo | ((uint32_t)hi << 16);
    return 0;
}

/* readBlock: TX [1,0,FF,5C,addrLE,endLE,zeros,sum,0] (1030B); type 1; 1030B. */
static int usb_read_block(uint32_t addr, uint8_t out[BLOCK_BYTES]) {
    static uint8_t tx[BLOCK_FRAME], rx[BLOCK_FRAME + 64];
    uint8_t type = 0;
    UInt32 len;
    uint32_t end = addr + BLOCK_BYTES;
    memset(tx, 0, sizeof tx);
    tx[0] = 1; tx[2] = 0xFF; tx[3] = 0x5C;
    tx[4] = (uint8_t)addr; tx[5] = (uint8_t)(addr >> 8);
    tx[6] = (uint8_t)(addr >> 16); tx[7] = (uint8_t)(addr >> 24);
    tx[8] = (uint8_t)end; tx[9] = (uint8_t)(end >> 8);
    tx[10] = (uint8_t)(end >> 16); tx[11] = (uint8_t)(end >> 24);
    tx[BLOCK_FRAME - 2] = frame_sum(tx, BLOCK_FRAME);
    if (usb_prepare(0)) return -1;
    if ((*g_usb)->WritePipeTO(g_usb, g_outP, tx, BLOCK_FRAME,
            BLOCK_TIMEOUT_MS, BLOCK_TIMEOUT_MS) != kIOReturnSuccess) {
        set_err("block bulk OUT failed"); return -1;
    }
    if (usb_prepare(1)) return -1;
    len = 1;
    if ((*g_usb)->ReadPipeTO(g_usb, g_inP, &type, &len,
            BLOCK_TIMEOUT_MS, BLOCK_TIMEOUT_MS) != kIOReturnSuccess ||
            len != 1 || type != 1) {
        set_err("bad block type"); return -1;
    }
    if (usb_prepare(1)) return -1;
    len = sizeof rx;
    if ((*g_usb)->ReadPipeTO(g_usb, g_inP, rx, &len,
            BLOCK_TIMEOUT_MS, BLOCK_TIMEOUT_MS) != kIOReturnSuccess ||
            len != BLOCK_FRAME) {
        set_err("bad block len"); return -1;
    }
    if (rx[0] != 1 || rx[1] != 0 || rx[2] != 0xFF || rx[3] != 0x05 ||
            rx[BLOCK_FRAME - 2] != frame_sum(rx, BLOCK_FRAME) ||
            rx[BLOCK_FRAME - 1] != 0) {
        set_err("bad block checksum"); return -1;
    }
    memcpy(out, rx + 4, BLOCK_BYTES);
    return 0;
}

typedef void (*progress_fn)(double frac);

static int usb_read_range(uint32_t base, uint8_t *out, size_t total, progress_fn prog) {
    size_t blocks = total / BLOCK_BYTES;
    for (size_t i = 0; i < blocks; i++) {
        if (usb_read_block(base + (uint32_t)(i * BLOCK_BYTES), out + i * BLOCK_BYTES))
            return -1;
        if (prog && (((i & 7) == 7) || i + 1 == blocks))
            prog((double)(i + 1) / (double)blocks);
    }
    return 0;
}

/* writeUiList: per entry (qid,flags,0,0), then (FFFF,0,0,0) terminator. */
static int usb_write_ui_list(uint32_t base, const uint16_t *qids, size_t n, uint16_t flags) {
    for (size_t k = 0; k < n; ++k) {
        uint32_t a = base + (uint32_t)(k * 8);
        if (usb_write_half(a + 0, qids[k]) || usb_write_half(a + 2, flags) ||
            usb_write_half(a + 4, 0x0000u) || usb_write_half(a + 6, 0x0000u))
            return -1;
    }
    uint32_t end = base + (uint32_t)(n * 8);
    if (usb_write_half(end + 0, 0xFFFFu) || usb_write_half(end + 2, 0x0000u) ||
        usb_write_half(end + 4, 0x0000u) || usb_write_half(end + 6, 0x0000u))
        return -1;
    return 0;
}

/* ---------------- Reference logic: signatures & state ---------------- */

static int verify_signatures(void) {
    for (size_t i = 0; i < 47; i++) {
        uint16_t v = 0;
        if (usb_read_half(SIG_CHECKS[i].addr, &v)) return -1;
        int ok = 0;
        for (int k = 0; k < SIG_CHECKS[i].nallow; k++)
            if (v == SIG_CHECKS[i].allow[k]) ok = 1;
        if (!ok) {
            char m[160];
            snprintf(m, sizeof m, "signature mismatch %s (0x%08X=0x%04X)",
                     SIG_CHECKS[i].name, SIG_CHECKS[i].addr, v);
            set_err(m);
            return -1;
        }
    }
    uint16_t prev0 = 0, next1 = 0;
    if (usb_read_half(0x866930E6u, &prev0) || usb_read_half(0x866930F0u, &next1))
        return -1;
    if (prev0 != 0xD00Au || next1 != 0x2805u) {
        set_err("hook boundary mismatch");
        return -1;
    }
    return 0;
}

static int verify_cinema_anchors(void) {
    for (size_t i = 0; i < 12; i++) {
        uint16_t v = 0;
        if (usb_read_half(CINEMA_PATCHES[i].addr, &v)) return -1;
        if (v != CINEMA_PATCHES[i].stk && v != CINEMA_PATCHES[i].unl) {
            char m[160];
            snprintf(m, sizeof m, "Cinema Mode anchor mismatch %s (0x%08X=0x%04X)",
                     CINEMA_PATCHES[i].name, CINEMA_PATCHES[i].addr, v);
            set_err(m);
            return -1;
        }
    }
    return 0;
}

static int detect_ram_state(int *ram, int *cinema) {
    uint16_t cap = 0, ntscLo = 0, palLo = 0, hdmi1 = 0, cin0 = 0, cin1 = 0;
    if (usb_read_half(0x868261B6u, &cap) ||
        usb_read_half(0x866931B8u, &ntscLo) ||
        usb_read_half(0x866931D4u, &palLo) ||
        usb_read_half(0x866ACC9Cu, &hdmi1))
        return -1;
    *ram = (cap == 0x2000u && ntscLo == 0xD200u && palLo == 0xD360u && hdmi1 == 0xBF00u);
    if (usb_read_half(CINEMA_PATCHES[0].addr, &cin0) ||
        usb_read_half(CINEMA_PATCHES[1].addr, &cin1))
        return -1;
    *cinema = (cin0 == CINEMA_PATCHES[0].unl && cin1 == CINEMA_PATCHES[1].unl);
    return 0;
}

/* ---------------- Reference logic: postboot2 images ---------------- */

static uint32_t get_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void set_u32le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* buildPostboot2Image: base image + optional 12 cinema rows at 0x150. */
static int build_postboot2_image(int include_cinema, uint8_t img[2048]) {
    memcpy(img, OPTB_IMAGE, 2048);
    if (!include_cinema) return 0;
    if (get_u32le(img + 0x150) != 0) {
        set_err("unexpected postboot2 table terminator");
        return -1;
    }
    size_t off = 0x150;
    for (size_t i = 0; i < 12; i++) {
        set_u32le(img + off, CINEMA_PATCHES[i].addr);
        set_u32le(img + off + 4, CINEMA_PATCHES[i].stk);
        set_u32le(img + off + 8, CINEMA_PATCHES[i].unl);
        off += 12;
    }
    set_u32le(img + off, 0); /* NULL terminator at +0x1E0 */
    return 0;
}

static void make_stock_pb2(uint8_t img[2048]) {
    static const uint32_t HDR[8] = { 0x44414F4Cu, 0x0B3C7000u, 0x0B3C7000u,
                                     0x0B3C7000u, 0x0B3C7000u, 0x076E701Cu,
                                     0x076E701Cu, 0xFFFFFFFFu };
    memset(img, 0xFF, 2048);
    for (int i = 0; i < 8; i++) set_u32le(img + i * 4, HDR[i]);
}

static int matches_pb2_image(const uint8_t *buf, const uint8_t *img) {
    for (size_t i = 0; i < 2048; i++) {
        if (i >= 0x7E0 && i < 0x7F0) continue; /* runtime telemetry block */
        if (buf[i] != img[i]) return 0;
    }
    for (size_t i = 2048; i < PB2_SIZE; i++) {
        if (buf[i] != 0xFF) return 0;
    }
    return 1;
}

typedef enum { PB2_STOCK, PB2_INSTALLED_STANDARD, PB2_INSTALLED_CINEMA, PB2_UPGRADE } pb2_t;

/* verifyPostboot2State: full 128 KiB read, SHA-256, image matching. */
static int verify_postboot2_state(pb2_t *state, progress_fn prog) {
    uint8_t *buf = (uint8_t *)malloc(PB2_SIZE);
    uint8_t img[2048];
    char digest[65];
    int rc = -1;
    if (!buf) { set_err("out of memory"); return -1; }
    if (usb_read_range(PB2_RAM_BASE, buf, PB2_SIZE, prog)) { free(buf); return -1; }
    sha256_hex(buf, PB2_SIZE, digest);
    if (strcmp(digest, STOCK_PB2_SHA256) == 0) {
        *state = PB2_STOCK; rc = 0;
    } else if (build_postboot2_image(0, img) == 0 && matches_pb2_image(buf, img)) {
        *state = PB2_INSTALLED_STANDARD; rc = 0;
    } else if (build_postboot2_image(1, img) == 0 && matches_pb2_image(buf, img)) {
        *state = PB2_INSTALLED_CINEMA; rc = 0;
    } else {
        int hdrMatch = 1;
        if (build_postboot2_image(0, img) != 0) hdrMatch = 0;
        for (size_t i = 0; hdrMatch && i < 0x28; i++)
            if (buf[i] != img[i]) hdrMatch = 0;
        if (hdrMatch)
            for (size_t i = 2048; i < PB2_SIZE; i++)
                if (buf[i] != 0xFF) { hdrMatch = 0; break; }
        if (hdrMatch) {
            *state = PB2_UPGRADE; rc = 0;
        } else {
            char m[160];
            snprintf(m, sizeof m, "unsupported camera memory (%.16s...) refusing to write", digest);
            set_err(m);
        }
    }
    free(buf);
    return rc;
}

/* ---------------- Reference logic: RAM unlock / restore ----------------
 * applyRamUnlock order (unlock=true):
 *   sigs, [cinema anchors], NTSC_UI, PAL_UI, NTSC_WL, PAL_WL tables,
 *   11 RAM words (write+readback each), [12 cinema words], live0 read,
 *   conditional live UI refresh. Restore skips the table writes.          */
static int apply_ram_unlock(int unlock, int want_cinema, int cinema_armed) {
    if (verify_signatures()) return -1;
    if ((unlock && want_cinema) || (!unlock && cinema_armed)) {
        if (verify_cinema_anchors()) return -1;
    }
    if (unlock) {
        if (usb_write_ui_list(NEW_NTSC_UI, NTSC_UI_NEW, 40, 0x0101u)) return -1;
        if (usb_write_ui_list(NEW_PAL_UI, PAL_UI_NEW, 24, 0x0101u)) return -1;
        for (size_t k = 0; k < 58; k++)
            if (usb_write_half(NEW_NTSC_WL + (uint32_t)(k * 2), NTSC_WL_NEW[k])) return -1;
        if (usb_write_half(NEW_NTSC_WL + 58u * 2u, 0xFFFFu)) return -1;
        for (size_t k = 0; k < 36; k++)
            if (usb_write_half(NEW_PAL_WL + (uint32_t)(k * 2), PAL_WL_NEW[k])) return -1;
        if (usb_write_half(NEW_PAL_WL + 36u * 2u, 0xFFFFu)) return -1;
    }
    for (size_t i = 0; i < 11; i++) {
        uint16_t target = unlock ? RAM_PATCHES[i].unl : RAM_PATCHES[i].stk;
        uint16_t rb = 0;
        if (usb_write_half(RAM_PATCHES[i].addr, target)) return -1;
        if (usb_read_half(RAM_PATCHES[i].addr, &rb)) return -1;
        if (rb != target) {
            char m[160];
            snprintf(m, sizeof m, "RAM verify mismatch %s (0x%08X: got 0x%04X, want 0x%04X)",
                     RAM_PATCHES[i].name, RAM_PATCHES[i].addr, rb, target);
            set_err(m);
            return -1;
        }
    }
    if ((unlock && want_cinema) || (!unlock && cinema_armed)) {
        for (size_t i = 0; i < 12; i++) {
            uint16_t target = want_cinema ? CINEMA_PATCHES[i].unl : CINEMA_PATCHES[i].stk;
            uint16_t rb = 0;
            if (usb_write_half(CINEMA_PATCHES[i].addr, target)) return -1;
            if (usb_read_half(CINEMA_PATCHES[i].addr, &rb)) return -1;
            if (rb != target) {
                char m[160];
                snprintf(m, sizeof m, "Cinema verify mismatch %s (0x%08X: got 0x%04X, want 0x%04X)",
                         CINEMA_PATCHES[i].name, CINEMA_PATCHES[i].addr, rb, target);
                set_err(m);
                return -1;
            }
        }
    }
    uint16_t live0 = 0;
    if (usb_read_half(LIVE_UI_BASE, &live0)) return -1;
    if (unlock) {
        if (live0 == 0x0092u || live0 == 0x00EDu) {
            if (usb_write_ui_list(LIVE_UI_BASE, NTSC_UI_NEW, 40, 0x0104u)) return -1;
        } else if (live0 == 0x0093u || live0 == 0x00F3u) {
            if (usb_write_ui_list(LIVE_UI_BASE, PAL_UI_NEW, 24, 0x0104u)) return -1;
        }
    } else {
        if (live0 == 0x00EDu) {
            if (usb_write_ui_list(LIVE_UI_BASE, NTSC_UI_STK, 34, 0x0104u)) return -1;
        } else if (live0 == 0x00F3u) {
            if (usb_write_ui_list(LIVE_UI_BASE, PAL_UI_STK, 22, 0x0104u)) return -1;
        }
    }
    return 0;
}

/* ---------------- Reference logic: flash stage / finish ---------------- */

static int prepare_hook_region(void) {
    uint16_t cur[4];
    for (int k = 0; k < 4; k++)
        if (usb_read_half(HOOK_REGION + (uint32_t)(k * 2), &cur[k])) return -1;
    int is_stock = 1, is_tramp = 1;
    for (int k = 0; k < 4; k++) {
        if (cur[k] != HOOKREG_STOCK[k]) is_stock = 0;
        if (cur[k] != HOOKREG_TRAMP[k]) is_tramp = 0;
    }
    if (is_stock) return 0;
    if (!is_tramp) {
        set_err("unexpected code at 0x866930E8 refusing");
        return -1;
    }
    for (int k = 0; k < 4; k++) {
        uint16_t rb = 0;
        if (usb_write_half(HOOK_REGION + (uint32_t)(k * 2), HOOKREG_STOCK[k])) return -1;
        if (usb_read_half(HOOK_REGION + (uint32_t)(k * 2), &rb)) return -1;
        if (rb != HOOKREG_STOCK[k]) {
            set_err("failed restoring stock instructions at 0x866930E8");
            return -1;
        }
    }
    return 1; /* restored from trampoline */
}

static int disarm_hook_and_scratch(void) {
    if (usb_write_half(HOOK_ADDR, HOOK_STOCK[0])) return -1;
    if (usb_write_half(HOOK_ADDR + 2, HOOK_STOCK[1])) return -1;
    for (size_t k = 0; k < 44; k++)
        if (usb_write_half(LOADER_ADDR + (uint32_t)(k * 2), 0xFFFFu)) return -1;
    for (int k = 0; k < 10; k++)
        if (usb_write_half(STATUS_ADDR + (uint32_t)(k * 2), 0xFFFFu)) return -1;
    return 0;
}

/* stageAndArm: hook-region prep, 2048B staging (+readback), status zeros,
 * stub words (+readback), hook patch. No readback on status/hook writes. */
static int stage_and_arm(const uint8_t img[2048], progress_fn prog) {
    int pr = prepare_hook_region();
    if (pr < 0) return -1;
    for (size_t off = 0; off < 2048; off += 2) {
        uint16_t v = (uint16_t)(img[off] | (img[off + 1] << 8));
        uint16_t rb = 0;
        if (usb_write_half(SRC_RAM + (uint32_t)off, v)) return -1;
        if (usb_read_half(SRC_RAM + (uint32_t)off, &rb)) return -1;
        if (rb != v) {
            char m[96];
            snprintf(m, sizeof m, "staging mismatch +0x%zX", off);
            set_err(m);
            return -1;
        }
        if (prog && ((off & 63) == 0)) prog((double)off / 2048.0);
    }
    for (int k = 0; k < 10; k++)
        if (usb_write_half(STATUS_ADDR + (uint32_t)(k * 2), 0)) return -1;
    for (size_t k = 0; k < 44; k++) {
        uint16_t rb = 0;
        if (usb_write_half(LOADER_ADDR + (uint32_t)(k * 2), FLASH_STUB[k])) return -1;
        if (usb_read_half(LOADER_ADDR + (uint32_t)(k * 2), &rb)) return -1;
        if (rb != FLASH_STUB[k]) { set_err("stub mismatch"); return -1; }
    }
    if (usb_write_half(HOOK_ADDR, HOOK_PATCH[0])) return -1;
    if (usb_write_half(HOOK_ADDR + 2, HOOK_PATCH[1])) return -1;
    if (prog) prog(1.0);
    return pr; /* 0 = was stock, 1 = restored trampoline */
}

/* verifyAndFinish: status regs, 2048B readback, always disarm, then judge. */
static int verify_and_finish(const uint8_t img[2048], char *commit_log,
                             size_t logsz, progress_fn prog) {
    uint32_t ran = 0, wrc = 0, rrc = 0, hdr0 = 0, dst = 0;
    if (usb_read_u32(STATUS_ADDR, &ran) ||
        usb_read_u32(STATUS_ADDR + 4, &wrc) ||
        usb_read_u32(STATUS_ADDR + 8, &rrc) ||
        usb_read_u32(STATUS_ADDR + 12, &hdr0) ||
        usb_read_u32(STATUS_ADDR + 16, &dst))
        return -1;
    snprintf(commit_log, logsz,
             "commit: ran=%u wrc=0x%08X rrc=0x%08X hdr0=0x%08X dst=0x%08X",
             ran, wrc, rrc, hdr0, dst);
    if (ran != 1) {
        set_err("Rec Quality was not opened yet open Menu -> Video -> Rec Quality, then finish again");
        return -1;
    }
    uint8_t rb[2048];
    long mism = 0;
    int rb_ok = (usb_read_range(RB_RAM, rb, 2048, prog) == 0);
    if (rb_ok) {
        for (size_t off = 0; off < 2048; off++)
            if (rb[off] != img[off]) mism++;
    }
    int cleanup_err = disarm_hook_and_scratch();
    if (!rb_ok) return -1;
    if (cleanup_err) {
        set_err("cleanup failed after save");
        return -1;
    }
    if (wrc != 0 || rrc != 0) {
        char m[96];
        snprintf(m, sizeof m, "save failed (wrc=0x%08X rrc=0x%08X) hook restored", wrc, rrc);
        set_err(m);
        return -1;
    }
    if (mism != 0) {
        char m[96];
        snprintf(m, sizeof m, "verification mismatch (%ld bytes) hook restored", mism);
        set_err(m);
        return -1;
    }
    return 0;
}

/* ---------------- Dump-plan mode (no USB; parity verification) ----------------
 * Prints the exact ordered USB traffic plan for an operation:
 *   W <addr8> <val4> <name>   halfword write
 *   R <addr8> <name>           halfword read (incl. interleaved readbacks)
 *   B <addr8> <len> <name>     block read (op 0x5C)
 *   # <section>                section marker
 * Camera-dependent branches (live UI refresh, hook trampoline restore) are
 * printed as labeled sections so a diff script can check every path.      */
static void plan_sigs(void) {
    printf("# sigs\n");
    for (size_t i = 0; i < 47; i++)
        printf("R %08X sig:%s\n", SIG_CHECKS[i].addr, SIG_CHECKS[i].name);
    printf("R 866930E6 sig:hook_prev\n");
    printf("R 866930F0 sig:hook_next\n");
}

static void plan_cinema_anchors(void) {
    printf("# cinema_anchors\n");
    for (size_t i = 0; i < 12; i++)
        printf("R %08X cinema_anchor:%s\n", CINEMA_PATCHES[i].addr, CINEMA_PATCHES[i].name);
}

static void plan_ui_list(uint32_t base, const uint16_t *qids, size_t n,
                         uint16_t flags, const char *tag) {
    for (size_t k = 0; k < n; k++) {
        uint32_t a = base + (uint32_t)(k * 8);
        printf("W %08X %04X %s:qid%zu\n", a, qids[k], tag, k);
        printf("W %08X %04X %s:flg%zu\n", a + 2, flags, tag, k);
        printf("W %08X 0000 %s:z1_%zu\n", a + 4, tag, k);
        printf("W %08X 0000 %s:z2_%zu\n", a + 6, tag, k);
    }
    uint32_t end = base + (uint32_t)(n * 8);
    printf("W %08X FFFF %s:term0\n", end, tag);
    printf("W %08X 0000 %s:term1\n", end + 2, tag);
    printf("W %08X 0000 %s:term2\n", end + 4, tag);
    printf("W %08X 0000 %s:term3\n", end + 6, tag);
}

static void plan_ram_op(int unlock, int want_cinema, int cinema_armed) {
    plan_sigs();
    if ((unlock && want_cinema) || (!unlock && cinema_armed))
        plan_cinema_anchors();
    if (unlock) {
        printf("# tables\n");
        plan_ui_list(NEW_NTSC_UI, NTSC_UI_NEW, 40, 0x0101u, "ntsc_ui");
        plan_ui_list(NEW_PAL_UI, PAL_UI_NEW, 24, 0x0101u, "pal_ui");
        for (size_t k = 0; k < 58; k++)
            printf("W %08X %04X ntsc_wl:%zu\n", NEW_NTSC_WL + (uint32_t)(k * 2),
                   NTSC_WL_NEW[k], k);
        printf("W %08X FFFF ntsc_wl:term\n", NEW_NTSC_WL + 58u * 2u);
        for (size_t k = 0; k < 36; k++)
            printf("W %08X %04X pal_wl:%zu\n", NEW_PAL_WL + (uint32_t)(k * 2),
                   PAL_WL_NEW[k], k);
        printf("W %08X FFFF pal_wl:term\n", NEW_PAL_WL + 36u * 2u);
    }
    printf("# ram_words\n");
    for (size_t i = 0; i < 11; i++) {
        uint16_t t = unlock ? RAM_PATCHES[i].unl : RAM_PATCHES[i].stk;
        printf("W %08X %04X ram:%s\n", RAM_PATCHES[i].addr, t, RAM_PATCHES[i].name);
        printf("R %08X ram_rb:%s\n", RAM_PATCHES[i].addr, RAM_PATCHES[i].name);
    }
    if ((unlock && want_cinema) || (!unlock && cinema_armed)) {
        printf("# cinema_words\n");
        for (size_t i = 0; i < 12; i++) {
            uint16_t t = want_cinema ? CINEMA_PATCHES[i].unl : CINEMA_PATCHES[i].stk;
            printf("W %08X %04X cinema:%s\n", CINEMA_PATCHES[i].addr, t,
                   CINEMA_PATCHES[i].name);
            printf("R %08X cinema_rb:%s\n", CINEMA_PATCHES[i].addr, CINEMA_PATCHES[i].name);
        }
    }
    printf("# live_probe\n");
    printf("R %08X live0\n", LIVE_UI_BASE);
    if (unlock) {
        printf("# live_branch:ntsc live0 in {0092,00ED}\n");
        plan_ui_list(LIVE_UI_BASE, NTSC_UI_NEW, 40, 0x0104u, "live_ntsc");
        printf("# live_branch:pal live0 in {0093,00F3}\n");
        plan_ui_list(LIVE_UI_BASE, PAL_UI_NEW, 24, 0x0104u, "live_pal");
        printf("# live_branch:other no writes\n");
    } else {
        printf("# live_branch:ntsc live0 == 00ED\n");
        plan_ui_list(LIVE_UI_BASE, NTSC_UI_STK, 34, 0x0104u, "live_ntsc_stk");
        printf("# live_branch:pal live0 == 00F3\n");
        plan_ui_list(LIVE_UI_BASE, PAL_UI_STK, 22, 0x0104u, "live_pal_stk");
        printf("# live_branch:other no writes\n");
    }
}

static void plan_stage(const uint8_t img[2048]) {
    printf("# hook_probe\n");
    for (int k = 0; k < 4; k++)
        printf("R %08X hookreg:%d\n", HOOK_REGION + (uint32_t)(k * 2), k);
    printf("# hook_branch:stock no writes\n");
    printf("# hook_branch:tramp restore stock + readback\n");
    for (int k = 0; k < 4; k++) {
        printf("W %08X %04X hookreg_rst:%d\n", HOOK_REGION + (uint32_t)(k * 2),
               HOOKREG_STOCK[k], k);
        printf("R %08X hookreg_rst_rb:%d\n", HOOK_REGION + (uint32_t)(k * 2), k);
    }
    printf("# staging\n");
    for (size_t off = 0; off < 2048; off += 2) {
        uint16_t v = (uint16_t)(img[off] | (img[off + 1] << 8));
        printf("W %08X %04X stage:+%04zX\n", SRC_RAM + (uint32_t)off, v, off);
        printf("R %08X stage_rb:+%04zX\n", SRC_RAM + (uint32_t)off, off);
    }
    printf("# status_zero\n");
    for (int k = 0; k < 10; k++)
        printf("W %08X 0000 status:%d\n", STATUS_ADDR + (uint32_t)(k * 2), k);
    printf("# stub\n");
    for (size_t k = 0; k < 44; k++) {
        printf("W %08X %04X stub:%zu\n", LOADER_ADDR + (uint32_t)(k * 2),
               FLASH_STUB[k], k);
        printf("R %08X stub_rb:%zu\n", LOADER_ADDR + (uint32_t)(k * 2), k);
    }
    printf("# hook_patch\n");
    printf("W %08X %04X hook:0\n", HOOK_ADDR, HOOK_PATCH[0]);
    printf("W %08X %04X hook:1\n", HOOK_ADDR + 2, HOOK_PATCH[1]);
}

static void plan_disarm(void) {
    printf("# disarm_hook\n");
    printf("W %08X %04X hook:0\n", HOOK_ADDR, HOOK_STOCK[0]);
    printf("W %08X %04X hook:1\n", HOOK_ADDR + 2, HOOK_STOCK[1]);
    printf("# disarm_stub\n");
    for (size_t k = 0; k < 44; k++)
        printf("W %08X FFFF stub_clr:%zu\n", LOADER_ADDR + (uint32_t)(k * 2), k);
    printf("# disarm_status\n");
    for (int k = 0; k < 10; k++)
        printf("W %08X FFFF status_clr:%d\n", STATUS_ADDR + (uint32_t)(k * 2), k);
}

static void plan_finish(void) {
    printf("# finish_status\n");
    for (int k = 0; k < 5; k++) {
        uint32_t a = STATUS_ADDR + (uint32_t)(k * 4);
        printf("R %08X status_u32:%d_lo\n", a, k);
        printf("R %08X status_u32:%d_hi\n", a + 2, k);
    }
    printf("# finish_readback\n");
    printf("B %08X 1024 rb:blk0\n", RB_RAM);
    printf("B %08X 1024 rb:blk1\n", RB_RAM + 1024);
    plan_disarm();
}

static void plan_pb2scan(void) {
    printf("# pb2_scan 128 blocks ascending\n");
    for (size_t i = 0; i < PB2_SIZE / BLOCK_BYTES; i++)
        printf("B %08X 1024 pb2:blk%zu\n", PB2_RAM_BASE + (uint32_t)(i * BLOCK_BYTES), i);
}

static int dump_plan(const char *op) {
    uint8_t img[2048];
    if (!strcmp(op, "sigs")) { plan_sigs(); return 0; }
    if (!strcmp(op, "cinema_anchors")) { plan_cinema_anchors(); return 0; }
    if (!strcmp(op, "ram_unlock")) { plan_ram_op(1, 0, 0); return 0; }
    if (!strcmp(op, "ram_unlock_cinema")) { plan_ram_op(1, 1, 0); return 0; }
    if (!strcmp(op, "ram_restore")) { plan_ram_op(0, 0, 0); return 0; }
    if (!strcmp(op, "ram_restore_cinema")) { plan_ram_op(0, 0, 1); return 0; }
    if (!strcmp(op, "stage_standard")) {
        if (build_postboot2_image(0, img)) return -1;
        plan_stage(img); return 0;
    }
    if (!strcmp(op, "stage_cinema")) {
        if (build_postboot2_image(1, img)) return -1;
        plan_stage(img); return 0;
    }
    if (!strcmp(op, "stage_stock")) {
        make_stock_pb2(img); plan_stage(img); return 0;
    }
    if (!strcmp(op, "disarm")) { plan_disarm(); return 0; }
    if (!strcmp(op, "finish")) { plan_finish(); return 0; }
    if (!strcmp(op, "pb2scan")) { plan_pb2scan(); return 0; }
    fprintf(stderr, "unknown plan '%s'\n", op);
    return -1;
}

/* ---------------- Self-test (no USB) ---------------- */
static int self_test(void) {
    int fails = 0;
    char hex[65];
    uint8_t img[2048], stk[2048];

#define CHECK(cond, label) do { \
    if (cond) printf("  %sPASS%s  %s\n", C_GREEN, C_RST, label); \
    else { printf("  %sFAIL%s  %s\n", C_RED, C_RST, label); fails++; } \
} while (0)

    printf("self-test (no camera needed):\n");
    sha256_hex((const uint8_t *)"abc", 3, hex);
    CHECK(!strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
          "sha256(\"abc\") vector");
    CHECK(OPTB_IMAGE[0] == 0x4C && OPTB_IMAGE[1] == 0x4F &&
          OPTB_IMAGE[2] == 0x41 && OPTB_IMAGE[3] == 0x44,
          "postboot2 image magic ('LOAD')");
    CHECK(get_u32le(OPTB_IMAGE + 0x150) == 0, "postboot2 terminator at 0x150");
    CHECK(build_postboot2_image(1, img) == 0, "cinema image builds");
    CHECK(get_u32le(img + 0x1E0) == 0, "cinema NULL terminator at 0x1E0");
    CHECK(get_u32le(img + 0x150) == CINEMA_PATCHES[0].addr &&
          get_u32le(img + 0x154) == CINEMA_PATCHES[0].stk &&
          get_u32le(img + 0x158) == CINEMA_PATCHES[0].unl,
          "cinema row 0 layout (addr,stock,unlock)");
    make_stock_pb2(stk);
    CHECK(get_u32le(stk) == 0x44414F4Cu && stk[32] == 0xFF && stk[2047] == 0xFF,
          "stock image header + padding");
    CHECK(sizeof(SIG_CHECKS) / sizeof(SIG_CHECKS[0]) == 47, "47 signature anchors");
    CHECK(sizeof(RAM_PATCHES) / sizeof(RAM_PATCHES[0]) == 11, "11 RAM patch words");
    CHECK(sizeof(CINEMA_PATCHES) / sizeof(CINEMA_PATCHES[0]) == 12, "12 cinema words");
    CHECK(sizeof(FLASH_STUB) / sizeof(FLASH_STUB[0]) == 44, "44 stub words");
    CHECK(sizeof(NTSC_UI_NEW) / 2 == 40 && sizeof(PAL_UI_NEW) / 2 == 24 &&
          sizeof(NTSC_WL_NEW) / 2 == 58 && sizeof(PAL_WL_NEW) / 2 == 36,
          "table sizes 40/24/58/36");
    CHECK(NTSC_UI_NEW[0] == 0x00EDu && PAL_UI_NEW[0] == 0x00F3u &&
          NTSC_UI_STK[0] == 0x0092u && PAL_UI_STK[0] == 0x0093u,
          "table heads (00ED/00F3 unlock, 0092/0093 stock)");
    sha256_hex(OPTB_IMAGE, 2048, hex);
    printf("  %sinfo%s  postboot2 sha256: %s\n", C_CYAN, C_RST, hex);
#undef CHECK
    printf(fails ? "self-test: %d FAILURES\n" : "self-test: all passed\n", fails);
    return fails ? 1 : 0;
}

/* ---------------- Terminal UI helpers ---------------- */
static void progress_bar(double frac) {
    int w = 34, fill = (int)(frac * w + 0.5);
    if (fill < 0) fill = 0;
    if (fill > w) fill = w;
    printf("\r  %s[", C_CRIMSON);
    for (int i = 0; i < w; i++) printf(i < fill ? "=" : "-");
    printf("]%s %3d%%", C_RST, (int)(frac * 100.0 + 0.5));
    fflush(stdout);
    if (frac >= 1.0) printf("\n");
}

static void progress_quiet(double frac) { (void)frac; }

static int prompt_line(const char *prompt, char *buf, size_t n) {
    printf("%s%s%s", C_BOLD, prompt, C_RST);
    fflush(stdout);
    if (!fgets(buf, (int)n, stdin)) return -1;
    buf[strcspn(buf, "\r\n")] = 0;
    return 0;
}

static void print_banner(void) {
    printf("\n");
    printf("  %s+============================================+%s\n", C_CRIMSON, C_RST);
    printf("  %s|%s  %sS5 Unlocker%s for macOS %s(v%s)%s         %s|%s\n",
           C_CRIMSON, C_RST, C_BOLD, C_RST, C_DIM, TOOL_VERSION, C_RST, C_CRIMSON,
           C_RST);
    printf("  %s|%s  Lumix DC-S5 (FW 2.9)  %s04DA:2370%s         %s|%s\n",
           C_CRIMSON, C_RST, C_DIM, C_RST, C_CRIMSON, C_RST);
    printf("  %s+============================================+%s\n", C_CRIMSON, C_RST);
}

static void print_state(int ram, int cinema, pb2_t pb2) {
    const char *flash =
        pb2 == PB2_INSTALLED_CINEMA ? "permanent + cinema" :
        pb2 == PB2_INSTALLED_STANDARD ? "permanent" :
        pb2 == PB2_UPGRADE ? "older build" : "stock";
    printf("\n  %sCamera state%s   RAM: %s%-8s%s  Cinema: %s%-6s%s  Flash: %s%s%s\n",
           C_BOLD, C_RST,
           ram ? C_GREEN : C_DIM, ram ? "unlocked" : "stock", C_RST,
           cinema ? C_GREEN : C_DIM, cinema ? "active" : "off", C_RST,
           C_CYAN, flash, C_RST);
}

/* ---------------- Session ---------------- */
static int g_ram = 0, g_cinema = 0;
static pb2_t g_pb2 = PB2_STOCK;
static int g_have_pb2 = 0;
static int g_connected = 0;

static void log_ok(const char *msg) { printf("  %sOK%s  %s\n", C_GREEN, C_RST, msg); }
static void log_fail(void) { printf("  %sFAIL%s  %s\n", C_RED, C_RST, g_err); }
static void log_info(const char *msg) { printf("  %s....%s  %s\n", C_DIM, C_RST, msg); }

/* inspectAndRoute (read-only): sigs + RAM state + 128 KiB flash state. */
static int do_inspect(int show_progress) {
    log_info("verifying 47 signature anchors...");
    if (verify_signatures()) { log_fail(); return -1; }
    log_ok("signatures verified (MC801 2.9, 47 anchors)");
    if (detect_ram_state(&g_ram, &g_cinema)) { log_fail(); return -1; }
    log_info("reading 128 KiB internal memory for SHA-256 check...");
    if (show_progress) progress_bar(0.0);
    if (verify_postboot2_state(&g_pb2, show_progress ? progress_bar : progress_quiet)) {
        if (show_progress) printf("\n");
        log_fail();
        return -1;
    }
    g_have_pb2 = 1;
    log_ok(g_pb2 == PB2_STOCK ? "internal memory verified 100% stock" :
           g_pb2 == PB2_INSTALLED_STANDARD ? "permanent unlock installed (standard)" :
           g_pb2 == PB2_INSTALLED_CINEMA ? "permanent unlock + Cinema Mode installed" :
           "older permanent build detected (upgrade available)");
    return 0;
}

static int do_connect(void) {
    if (g_connected) return 0;
    if (usb_open()) {
        printf("\n  %sNo camera found%s (04DA:2370).\n", C_YELLOW, C_RST);
        printf("  1. Camera OFF, drive dial to Single Shot\n");
        printf("  2. Hold PLAYBACK + AF ON while turning on\n");
        printf("  3. Connect USB-C data cable, then retry\n");
        return -1;
    }
    g_connected = 1;
    printf("  %sconnected%s  04DA:2370 (OUT=%d IN=%d)\n", C_GREEN, C_RST, g_outP, g_inP);
    return 0;
}

static void do_disconnect(void) {
    if (g_connected) usb_close();
    g_connected = 0;
    g_have_pb2 = 0;
}

static int refresh_ram_state(void) {
    if (detect_ram_state(&g_ram, &g_cinema)) { log_fail(); return -1; }
    return 0;
}

/* ---------------- Flash two-step flow ---------------- */
static void flash_warning(const char *action) {
    printf("\n  %s%sWARNING%s  %s writes to internal camera memory.\n", C_BG_RED, C_WHITE,
           C_RST, action);
    printf("  Original Lumix DC-S5 (FW 2.9) only. Battery above 50%%.\n");
    printf("  Do NOT unplug USB until verification finishes, or the\n");
    printf("  camera may stop booting and require service.\n");
}

static int confirm_word(const char *word) {
    char buf[64];
    printf("  Type %s%s%s to continue (anything else aborts): ", C_BOLD, word, C_RST);
    fflush(stdout);
    if (!fgets(buf, sizeof buf, stdin)) return 0;
    buf[strcspn(buf, "\r\n")] = 0;
    return !strcmp(buf, word);
}

/* Stage, prompt for the on-camera menu step, then finish (or cancel). */
static int do_flash_flow(const uint8_t img[2048], const char *action, int with_cinema,
                         pb2_t resulting_state) {
    if (!g_have_pb2) {
        set_err("flash state unknown; run Check compatibility first");
        log_fail();
        return -1;
    }
    flash_warning(action);
    if (!confirm_word("WRITE")) { printf("  aborted.\n"); return 1; }
    if (!strcmp(action, "Install") && with_cinema) {
        log_info("verifying Cinema Mode anchors...");
        if (verify_cinema_anchors()) { log_fail(); return -1; }
    }
    log_info("staging 2048 bytes + stub, arming hook...");
    progress_bar(0.0);
    int pr = stage_and_arm(img, progress_bar);
    if (pr < 0) { printf("\n"); log_fail(); return -1; }
    if (pr == 1) log_info("trampoline detected; restored stock code before staging");
    log_ok("staged and armed. On the camera, open:");
    printf("\n  %sMenu -> Video -> Rec Quality%s  (just open it once, keep USB plugged in)\n\n",
           C_BOLD, C_RST);
    char buf[64];
    if (prompt_line("  Press Enter when the Rec Quality list is open (or type 'cancel'): ",
                    buf, sizeof buf)) return 1;
    if (!strcmp(buf, "cancel") || !strcmp(buf, "c") || !strcmp(buf, "q")) {
        log_info("cancelling: disarming hook and scratch...");
        if (disarm_hook_and_scratch()) { log_fail(); return -1; }
        log_ok("cancelled; hook and scratchpad disarmed, nothing committed");
        return 1;
    }
    log_info("checking commit status...");
    char clog[160] = "";
    if (verify_and_finish(img, clog, sizeof clog, progress_bar)) {
        printf("  %s%s%s\n", C_DIM, clog, C_RST);
        log_fail();
        if (!strcmp(g_err, "Rec Quality was not opened yet open Menu -> Video -> Rec Quality, then finish again")) {
            printf("  (Hook is still armed. Open the menu, then choose this action again\n");
            printf("   to retry Finish, or pick Cancel to disarm.)\n");
        }
        return -1;
    }
    printf("  %s%s%s\n", C_DIM, clog, C_RST);
    g_pb2 = resulting_state;
    if (resulting_state == PB2_STOCK) {
        log_ok("VERIFIED 2048/2048 bytes: factory stock restored in flash");
        printf("  Unplug USB and remove the battery for 5 seconds to finish.\n");
    } else {
        log_ok(with_cinema ? "VERIFIED 2048/2048 bytes: permanent + Cinema Mode installed"
                           : "VERIFIED 2048/2048 bytes: permanent unlock installed");
        printf("  1. Unplug USB and remove the battery for 5 seconds\n");
        printf("  2. Power on normally, open Rec Quality once: 6K/5.9K at the top\n");
    }
    return 0;
}

/* ---------------- Menu actions ---------------- */
static void act_check(void) {
    if (do_connect()) return;
    printf("\n");
    if (do_inspect(1)) return;
    print_state(g_ram, g_cinema, g_pb2);
    if (g_pb2 == PB2_INSTALLED_CINEMA)
        printf("  Permanent unlock + Cinema Mode is installed.\n");
    else if (g_pb2 == PB2_INSTALLED_STANDARD)
        printf("  Permanent unlock is already installed.\n");
    else if (g_ram)
        printf("  %s\n", g_cinema ? "Temporary unlock + Cinema Mode is active."
                                  : "Temporary unlock is currently active.");
    else
        printf("  Factory stock and ready to unlock.\n");
}

static void act_ram(int unlock, int want_cinema) {
    if (do_connect()) return;
    if (unlock)
        printf("\n  Unlocking (RAM only, zero flash writes)%s...\n",
               want_cinema ? " + Cinema Mode" : "");
    else
        printf("\n  Removing temporary unlock (RAM only)...\n");
    if (apply_ram_unlock(unlock, want_cinema, g_cinema)) { log_fail(); return; }
    if (refresh_ram_state()) return;
    if (unlock) {
        log_ok(want_cinema ? "temporary unlock + Cinema Mode armed (verified)"
                           : "temporary unlock armed (verified)");
        printf("  Set Rec File Format to MOV; 6K/5.9K are at the top of Rec Quality.\n");
        printf("  Survives the power switch; pulling the battery resets to stock.\n");
    } else {
        log_ok("temporary unlock removed; camera is back to stock RAM state");
    }
    print_state(g_ram, g_cinema, g_pb2);
}

static void act_flash_install(int with_cinema) {
    uint8_t img[2048];
    if (do_connect()) return;
    if (!g_have_pb2) { printf("\n"); if (do_inspect(1)) return; }
    if (build_postboot2_image(with_cinema, img)) { log_fail(); return; }
    printf("\n");
    int rc = do_flash_flow(img, "Install", with_cinema,
                           with_cinema ? PB2_INSTALLED_CINEMA : PB2_INSTALLED_STANDARD);
    if (rc == 0) print_state(g_ram, g_cinema, g_pb2);
}

static void act_flash_uninstall(void) {
    uint8_t img[2048];
    if (do_connect()) return;
    if (!g_have_pb2) { printf("\n"); if (do_inspect(1)) return; }
    make_stock_pb2(img);
    printf("\n");
    int rc = do_flash_flow(img, "Uninstall", 0, PB2_STOCK);
    if (rc == 0) print_state(g_ram, g_cinema, g_pb2);
}

static void act_disarm(void) {
    if (do_connect()) return;
    printf("\n  Disarming any staged hook/scratch (safe: RAM only)...\n");
    if (disarm_hook_and_scratch()) { log_fail(); return; }
    log_ok("hook and scratchpad disarmed");
}

static void print_menu(void) {
    printf("\n");
    printf("  %s1%s  Check compatibility %s(read-only, no writes)%s\n", C_BOLD, C_RST,
           C_DIM, C_RST);
    printf("  %s2%s  Temporary unlock %s(RAM only, recommended)%s\n", C_BOLD, C_RST,
           C_DIM, C_RST);
    printf("  %s3%s  Temporary unlock + Cinema Mode %s(RAM only)%s\n", C_BOLD, C_RST,
           C_DIM, C_RST);
    printf("  %s4%s  Remove temporary unlock\n", C_BOLD, C_RST);
    printf("  %s5%s  Install permanent unlock %s(writes flash, advanced)%s\n", C_BOLD,
           C_RST, C_YELLOW, C_RST);
    printf("  %s6%s  Install permanent + Cinema Mode %s(writes flash)%s\n", C_BOLD, C_RST,
           C_YELLOW, C_RST);
    printf("  %s7%s  Uninstall permanent unlock %s(writes flash)%s\n", C_BOLD, C_RST,
           C_YELLOW, C_RST);
    printf("  %s8%s  Disarm staged hook %s(if a flash step was interrupted)%s\n", C_BOLD,
           C_RST, C_DIM, C_RST);
    printf("  %s9%s  Re-scan camera\n", C_BOLD, C_RST);
    printf("  %s0%s  Quit\n", C_BOLD, C_RST);
}

static void run_menu(void) {
    char buf[64];
    print_banner();
    printf("  %sTip:%s connect the camera in Service Mode first\n", C_DIM, C_RST);
    printf("  %s(HOLD PLAYBACK + AF ON while powering on).%s\n", C_DIM, C_RST);
    for (;;) {
        if (g_connected) print_state(g_ram, g_cinema, g_pb2);
        print_menu();
        if (prompt_line("\n  Select [0-9]: ", buf, sizeof buf) || !strcmp(buf, "0") ||
            !strcmp(buf, "q") || !strcmp(buf, "quit")) {
            printf("  bye.\n");
            return;
        }
        if (!strcmp(buf, "1")) act_check();
        else if (!strcmp(buf, "2")) act_ram(1, 0);
        else if (!strcmp(buf, "3")) act_ram(1, 1);
        else if (!strcmp(buf, "4")) act_ram(0, 0);
        else if (!strcmp(buf, "5")) act_flash_install(0);
        else if (!strcmp(buf, "6")) act_flash_install(1);
        else if (!strcmp(buf, "7")) act_flash_uninstall();
        else if (!strcmp(buf, "8")) act_disarm();
        else if (!strcmp(buf, "9")) {
            do_disconnect();
            printf("  re-scanning...\n");
            if (!do_connect()) {
                printf("\n");
                if (!do_inspect(1)) print_state(g_ram, g_cinema, g_pb2);
            }
        } else {
            printf("  ? unknown choice '%s'\n", buf);
        }
    }
}

/* ---------------- CLI ---------------- */
static void usage(const char *prog) {
    printf("S5 Unlocker %s for macOS (Apple Silicon)\n", TOOL_VERSION);
    printf("Reference: docs/index.html v1.7 (byte-perfect patch order)\n\n");
    printf("Usage:\n");
    printf("  %s                       interactive colored menu\n", prog);
    printf("  %s check                  read-only compatibility check\n", prog);
    printf("  %s unlock [--cinema]      temporary RAM unlock\n", prog);
    printf("  %s restore                remove temporary RAM unlock\n", prog);
    printf("  %s install [--cinema]     permanent flash install (two-step)\n", prog);
    printf("  %s uninstall              restore factory stock flash (two-step)\n", prog);
    printf("  %s disarm                 disarm staged hook (RAM only)\n", prog);
    printf("  %s --self-test            built-in checks, no camera needed\n", prog);
    printf("  %s --dump-plan <op>       print exact USB traffic plan, no camera\n", prog);
    printf("  %s --no-color ...         disable ANSI colors\n", prog);
    printf("\nPlans: sigs cinema_anchors ram_unlock ram_unlock_cinema ram_restore\n");
    printf("       ram_restore_cinema stage_standard stage_cinema stage_stock\n");
    printf("       disarm finish pb2scan\n");
}

int main(int argc, char **argv) {
    const char *cmd = NULL;
    int want_cinema = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--no-color")) g_color = 0;
        else if (!strcmp(argv[i], "--cinema") || !strcmp(argv[i], "--cinema-mode"))
            want_cinema = 1;
        else if (!cmd) cmd = argv[i];
        else if (!strcmp(cmd, "--dump-plan")) { cmd = argv[i - 1]; break; }
    }
    /* Re-scan dump-plan argument simply. */
    const char *plan = NULL;
    for (int i = 1; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--dump-plan")) plan = argv[i + 1];
    if (!isatty(STDOUT_FILENO)) g_color = 0;

    if (plan) return dump_plan(plan) ? 1 : 0;
    if (cmd && !strcmp(cmd, "--self-test")) return self_test();
    if (cmd && (!strcmp(cmd, "--help") || !strcmp(cmd, "-h") || !strcmp(cmd, "help"))) {
        usage(argv[0]);
        return 0;
    }
    if (cmd && (!strcmp(cmd, "--version") || !strcmp(cmd, "-V"))) {
        printf("s5-unlocker %s (macOS arm64, reference v1.7)\n", TOOL_VERSION);
        return 0;
    }
    if (!cmd) {
        run_menu();
        do_disconnect();
        return 0;
    }
    /* Non-interactive single commands (flash flows still prompt). */
    print_banner();
    if (!strcmp(cmd, "check")) {
        if (do_connect()) return 1;
        printf("\n");
        int rc = do_inspect(1);
        if (!rc) print_state(g_ram, g_cinema, g_pb2);
        do_disconnect();
        return rc ? 1 : 0;
    }
    if (!strcmp(cmd, "unlock")) {
        if (do_connect()) return 1;
        printf("\n");
        int rc = apply_ram_unlock(1, want_cinema, 0);
        if (rc) { log_fail(); do_disconnect(); return 1; }
        log_ok("unlock applied and verified");
        do_disconnect();
        return 0;
    }
    if (!strcmp(cmd, "restore")) {
        if (do_connect()) return 1;
        printf("\n");
        int ram = 0, cin = 0;
        if (detect_ram_state(&ram, &cin)) { log_fail(); do_disconnect(); return 1; }
        int rc = apply_ram_unlock(0, 0, cin);
        if (rc) { log_fail(); do_disconnect(); return 1; }
        log_ok("stock RAM state restored and verified");
        do_disconnect();
        return 0;
    }
    if (!strcmp(cmd, "install")) {
        uint8_t img[2048];
        if (do_connect()) return 1;
        printf("\n");
        if (do_inspect(1)) { do_disconnect(); return 1; }
        if (build_postboot2_image(want_cinema, img)) {
            log_fail(); do_disconnect(); return 1;
        }
        int rc = do_flash_flow(img, "Install", want_cinema,
                               want_cinema ? PB2_INSTALLED_CINEMA : PB2_INSTALLED_STANDARD);
        do_disconnect();
        return rc ? 1 : 0;
    }
    if (!strcmp(cmd, "uninstall")) {
        uint8_t img[2048];
        if (do_connect()) return 1;
        printf("\n");
        if (do_inspect(1)) { do_disconnect(); return 1; }
        make_stock_pb2(img);
        int rc = do_flash_flow(img, "Uninstall", 0, PB2_STOCK);
        do_disconnect();
        return rc ? 1 : 0;
    }
    if (!strcmp(cmd, "disarm")) {
        if (do_connect()) return 1;
        int rc = disarm_hook_and_scratch();
        if (rc) log_fail(); else log_ok("hook and scratchpad disarmed");
        do_disconnect();
        return rc ? 1 : 0;
    }
    usage(argv[0]);
    return 1;
}
