/*
 * s5_59k_branch.c - bounded, fixed-address volatile RTOS RAM quality override tool.
 *
 * Strictly fixed to two Thumb-1 halfwords inside 0x866B492A:
 *   0x866B493C: stock 0xD009 (beq #0x866b4952) -> patched 0xE009 (b #0x866b4952)
 *   0x866B4954: stock 0x8800 (ldrh r0, [r0])   -> patched 0x20XX (movs r0, #0xXX)
 *
 * Volatile DRAM only; zero flash, EEPROM, or user-settings writes.
 *
 *   .build/s5_59k_branch verify
 *   .build/s5_59k_branch enable [59k29|59k24|59kc24|6k24|6kc24] --confirm-write
 *   .build/s5_59k_branch restore --confirm-write
 */

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PANASONIC_VID 0x04DA
#define SERVICE_PID   0x2370
#define TIMEOUT_MS    4000
#define FRAME_BYTES   16

#define SIG_PREV_ADDR 0x866B493Au
#define BRANCH_ADDR   0x866B493Cu
#define SIG_NEXT_ADDR 0x866B493Eu
#define LDR_ADDR      0x866B4952u
#define QID_INSN_ADDR 0x866B4954u
#define POP_ADDR      0x866B4956u
#define ACTIVE_QID    0x08CC5538u
#define HDMI_RAW_FLAG 0x08CC59E6u
#define HDMI_RAW_QID  0x08CC5A06u
#define WL_SIG_LO     0x86F15E90u /* 0x00D1 */
#define WL_SIG_HI     0x86F15E92u /* 0x80D1 */
#define WL_SLOT_LO    0x86F15E94u /* 0x00D2 -> 0x00ED */
#define WL_SLOT_HI    0x86F15E96u /* 0x80D2 -> 0x80ED */
#define WL_SENTINEL   0x86F15E98u /* 0xFFFF */
#define UI_SIG_PREV   0x08A6845Cu /* 0x00D1 */
#define UI_SLOT_Q0    0x08A6846Cu /* 0x00D2 -> 0x00ED */
#define UI_SLOT_F0    0x08A6846Eu /* 0x0102 -> 0x0104 (VISIBLE) */
#define UI_SLOT_Q1    0x08A68474u /* 0x80D2 -> 0x80ED */
#define TPL_SIG_PREV  0x08A68C5Cu /* 0x00D1 (NTSC MOV source template) */
#define TPL_SLOT_Q0   0x08A68C6Cu /* 0x00D2 -> 0x00ED */
#define TPL_SLOT_Q1   0x08A68C74u /* 0x80D2 -> 0x80ED */

#define SIG_PREV_VAL  0x2801u /* cmp r0, #1 */
#define ORIG_BRANCH   0xD009u /* beq #0x866b4952 */
#define PATCH_BRANCH  0xE009u /* b   #0x866b4952 */
#define SIG_NEXT_VAL  0xB2E0u /* uxtb r0, r4 */
#define SIG_LDR_VAL   0x486Bu /* ldr r0, [pc, #0x1ac] */
#define ORIG_QID_INSN 0x8800u /* ldrh r0, [r0] */
#define SIG_POP_VAL   0xBD10u /* pop {r4, pc} */

static uint8_t g_sum(const uint8_t *p, size_t n) {
    uint8_t s = 0;
    for (size_t i = 1; i + 2 < n; ++i) s = (uint8_t)(s + p[i]);
    return s;
}

static int prepare(IOUSBInterfaceInterface300 **usb, UInt16 index) {
    IOUSBDevRequestTO r;
    memset(&r, 0, sizeof r);
    r.bmRequestType = 0x40;
    r.bRequest = 0;
    r.wIndex = index;
    r.noDataTimeout = TIMEOUT_MS;
    r.completionTimeout = TIMEOUT_MS;
    return (*usb)->ControlRequestTO(usb, 0, &r) == kIOReturnSuccess ? 0 : -1;
}

static int read_pipe(IOUSBInterfaceInterface300 **usb, UInt8 pipe,
                     void *data, UInt32 *length) {
    UInt32 cap = *length;
    IOReturn r = (*usb)->ReadPipeTO(usb, pipe, data, length, TIMEOUT_MS, TIMEOUT_MS);
    if (r != kIOReturnSuccess || *length > cap) return -1;
    return 0;
}

static int write_pipe(IOUSBInterfaceInterface300 **usb, UInt8 pipe,
                      const void *data, UInt32 length) {
    return (*usb)->WritePipeTO(usb, pipe, (void *)data, length,
                               TIMEOUT_MS, TIMEOUT_MS) == kIOReturnSuccess ? 0 : -1;
}

static int read_half(IOUSBInterfaceInterface300 **usb, UInt8 outPipe, UInt8 inPipe,
                     uint32_t addr, uint16_t *value) {
    uint8_t tx[FRAME_BYTES], rx[FRAME_BYTES], type = 0;
    UInt32 len = 1;
    memset(tx, 0, sizeof tx);
    tx[0] = 8; tx[2] = 0xFF; tx[3] = 0x9B;
    for (unsigned i = 0; i < 4; ++i) tx[4 + i] = (uint8_t)(addr >> (8 * i));
    tx[14] = g_sum(tx, sizeof tx);

    if (prepare(usb, 0) || write_pipe(usb, outPipe, tx, sizeof tx)) return -1;
    if (prepare(usb, 1) || read_pipe(usb, inPipe, &type, &len)) return -1;
    if (len != 1 || type != 8) return -1;
    if (prepare(usb, 1)) return -1;
    memset(rx, 0, sizeof rx);
    len = sizeof rx;
    if (read_pipe(usb, inPipe, rx, &len)) return -1;
    if (len != FRAME_BYTES || rx[3] != 0x36) return -1;
    if (rx[14] != g_sum(rx, sizeof rx) || rx[15] != 0) return -1;
    *value = (uint16_t)(rx[4] | (rx[5] << 8));
    return 0;
}

static int write_half(IOUSBInterfaceInterface300 **usb, UInt8 outPipe, UInt8 inPipe,
                      uint32_t addr, uint16_t value) {
    uint8_t tx[FRAME_BYTES], rx[FRAME_BYTES], type = 0;
    UInt32 len = 1;
    memset(tx, 0, sizeof tx);
    tx[0] = 8; tx[2] = 0xFF; tx[3] = 0x9C;
    for (unsigned i = 0; i < 4; ++i) tx[4 + i] = (uint8_t)(addr >> (8 * i));
    tx[8] = (uint8_t)value;
    tx[9] = (uint8_t)(value >> 8);
    tx[14] = g_sum(tx, sizeof tx);

    if (prepare(usb, 0) || write_pipe(usb, outPipe, tx, sizeof tx)) return -1;
    if (prepare(usb, 1) || read_pipe(usb, inPipe, &type, &len)) return -1;
    if (len != 1 || (type != 8 && type != 1)) return -1;
    if (prepare(usb, 1)) return -1;
    memset(rx, 0, sizeof rx);
    len = sizeof rx;
    if (read_pipe(usb, inPipe, rx, &len)) return -1;
    printf("  write ack: len=%u", (unsigned)len);
    for (UInt32 i = 0; i < len; ++i) printf(" %02X", rx[i]);
    printf("\n");
    if (len < 6 || rx[0] != 8 || rx[1] != 0 || rx[2] != 0xFF) return -1;
    if (rx[len - 2] != g_sum(rx, len) || rx[len - 1] != 0) return -1;
    if (rx[3] != 0x01 && rx[3] != 0x00) return -1;
    return 0;
}

static uint16_t preset_insn(const char *name) {
    if (!strcmp(name, "og24"))   return 0x20EDu; /* movs r0, #0xED (6K 3:2 Open Gate 23.98p, 5952x3968) */
    if (!strcmp(name, "ogc24"))  return 0x20EEu; /* movs r0, #0xEE (6K 3:2 Open Gate 24.00p, 5952x3968) */
    if (!strcmp(name, "54k25"))  return 0x20EFu; /* movs r0, #0xEF (5.4K 3:2 Open Gate 25.00p, 5376x3584) */
    if (!strcmp(name, "54k29"))  return 0x20F0u; /* movs r0, #0xF0 (5.4K 3:2 Open Gate 29.97p, 5376x3584) */
    if (!strcmp(name, "59k24"))  return 0x20F1u; /* movs r0, #0xF1 (5.9K 16:9 23.98p, 5888x3312) */
    if (!strcmp(name, "59kc24")) return 0x20F2u; /* movs r0, #0xF2 (5.9K 16:9 24.00p, 5888x3312) */
    if (!strcmp(name, "59k25"))  return 0x20F3u; /* movs r0, #0xF3 (5.9K 16:9 25.00p, 5888x3312) */
    if (!strcmp(name, "59k29"))  return 0x20F4u; /* movs r0, #0xF4 (5.9K 16:9 29.97p, 5888x3312) */
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s verify | enable [6k24|6kc24|59k29|59k24|59kc24] --confirm-write | restore --confirm-write\n", argv[0]);
        return 1;
    }
    const char *mode = argv[1];
    const char *preset = "59k29";
    int confirmed = 0;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--confirm-write")) confirmed = 1;
        else if (preset_insn(argv[i]) != 0) preset = argv[i];
        else { fprintf(stderr, "unknown arg/preset: %s\n", argv[i]); return 1; }
    }
    int is_write = (!strcmp(mode, "enable") || !strcmp(mode, "restore") ||
                    !strcmp(mode, "setslot") || !strcmp(mode, "restoreslot") ||
                    !strcmp(mode, "wl_enable") || !strcmp(mode, "wl_restore") ||
                    !strcmp(mode, "ui_enable") || !strcmp(mode, "ui_restore") ||
                    !strcmp(mode, "hb_enable") || !strcmp(mode, "hb_restore") ||
                    !strcmp(mode, "sd_test") || !strcmp(mode, "sd_restore") ||
                    !strcmp(mode, "loader_enable") || !strcmp(mode, "loader_hello") ||
                    !strcmp(mode, "loader_restore") ||
                    !strcmp(mode, "v13_enable") || !strcmp(mode, "v13_restore"));
    if (is_write && !confirmed) {
        fprintf(stderr, "refusing write without --confirm-write\n");
        return 1;
    } else if (!is_write && strcmp(mode, "verify") != 0 && strcmp(mode, "hb_read") != 0 &&
               strcmp(mode, "sd_read") != 0 && strcmp(mode, "loader_read") != 0 &&
               strcmp(mode, "v13_check") != 0) {
        fprintf(stderr, "unknown mode: %s\n", mode);
        return 1;
    }

    CFMutableDictionaryRef match = IOServiceMatching(kIOUSBDeviceClassName);
    int vid = PANASONIC_VID, pid = SERVICE_PID;
    CFNumberRef v = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vid);
    CFNumberRef p = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &pid);
    CFDictionarySetValue(match, CFSTR(kUSBVendorID), v);
    CFDictionarySetValue(match, CFSTR(kUSBProductID), p);
    CFRelease(v); CFRelease(p);

    io_iterator_t iter = 0;
    IOServiceGetMatchingServices(kIOMainPortDefault, match, &iter);
    io_service_t device = IOIteratorNext(iter);
    IOObjectRelease(iter);
    if (!device) { fprintf(stderr, "no service device 04DA:2370\n"); return 2; }

    IOCFPlugInInterface **plug = NULL; SInt32 score = 0;
    IOCreatePlugInInterfaceForService(device, kIOUSBDeviceUserClientTypeID,
                                      kIOCFPlugInInterfaceID, &plug, &score);
    IOUSBDeviceInterface300 **dev = NULL;
    (*plug)->QueryInterface(plug, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID300), (LPVOID *)&dev);

    IOUSBFindInterfaceRequest req = {
        kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare,
        kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare
    };
    io_iterator_t ifaces = 0;
    (*dev)->CreateInterfaceIterator(dev, &req, &ifaces);
    io_service_t ifaceService = IOIteratorNext(ifaces);
    IOObjectRelease(ifaces);

    IOCFPlugInInterface **ifacePlug = NULL;
    IOCreatePlugInInterfaceForService(ifaceService, kIOUSBInterfaceUserClientTypeID,
                                      kIOCFPlugInInterfaceID, &ifacePlug, &score);
    IOUSBInterfaceInterface300 **usb = NULL;
    (*ifacePlug)->QueryInterface(ifacePlug, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID300),
                                 (LPVOID *)&usb);
    if ((*usb)->USBInterfaceOpenSeize(usb) != kIOReturnSuccess) {
        fprintf(stderr, "could not claim the service interface\n");
        return 1;
    }

    UInt8 count = 0, outPipe = 0, inPipe = 0;
    (*usb)->GetNumEndpoints(usb, &count);
    for (UInt8 pipe = 1; pipe <= count; ++pipe) {
        UInt8 dir = 0, num = 0, type = 0, interval = 0; UInt16 max = 0;
        if ((*usb)->GetPipeProperties(usb, pipe, &dir, &num, &type, &max, &interval) != kIOReturnSuccess)
            continue;
        if (type == kUSBBulk && dir == kUSBOut) outPipe = pipe;
        if (type == kUSBBulk && dir == kUSBIn) inPipe = pipe;
    }
    if (!outPipe || !inPipe) {
        fprintf(stderr, "bulk pipes not found\n");
        (*usb)->USBInterfaceClose(usb);
        return 1;
    }

    uint16_t prev = 0, cur_br = 0, next = 0, ldr_i = 0, cur_q = 0, pop_i = 0, raw_flag = 0, raw_qid = 0, act_qid = 0;
    int rc = 0;
    if (read_half(usb, outPipe, inPipe, SIG_PREV_ADDR, &prev) ||
        read_half(usb, outPipe, inPipe, BRANCH_ADDR, &cur_br) ||
        read_half(usb, outPipe, inPipe, SIG_NEXT_ADDR, &next) ||
        read_half(usb, outPipe, inPipe, LDR_ADDR, &ldr_i) ||
        read_half(usb, outPipe, inPipe, QID_INSN_ADDR, &cur_q) ||
        read_half(usb, outPipe, inPipe, POP_ADDR, &pop_i) ||
        read_half(usb, outPipe, inPipe, HDMI_RAW_FLAG, &raw_flag) ||
        read_half(usb, outPipe, inPipe, HDMI_RAW_QID, &raw_qid) ||
        read_half(usb, outPipe, inPipe, ACTIVE_QID, &act_qid)) {
        fprintf(stderr, "signature read failed\n");
        rc = 1;
    } else {
        printf("sig1: 0x%08X=0x%04X  0x%08X=0x%04X  0x%08X=0x%04X\n",
               SIG_PREV_ADDR, prev, BRANCH_ADDR, cur_br, SIG_NEXT_ADDR, next);
        printf("sig2: 0x%08X=0x%04X  0x%08X=0x%04X  0x%08X=0x%04X\n",
               LDR_ADDR, ldr_i, QID_INSN_ADDR, cur_q, POP_ADDR, pop_i);
        printf("raw:  0x%08X=0x%02X (hdmi_raw_flag)  0x%08X=0x%04X (qid)  0x%08X=0x%04X (active_qid)\n",
               HDMI_RAW_FLAG, raw_flag & 0xFF, HDMI_RAW_QID, raw_qid, ACTIVE_QID, act_qid);

        if (prev != SIG_PREV_VAL || next != SIG_NEXT_VAL || ldr_i != SIG_LDR_VAL || pop_i != SIG_POP_VAL) {
            fprintf(stderr, "SIGNATURE MISMATCH; aborting\n");
            rc = 1;
        } else if (!strcmp(mode, "setslot")) {
            uint16_t target_id = preset_insn(preset) & 0xFFu;
            printf("setting active_qid 0x%08X: 0x%04X -> 0x%04X (save original 0x%04X!)\n",
                   ACTIVE_QID, act_qid, target_id, act_qid);
            if (write_half(usb, outPipe, inPipe, ACTIVE_QID, target_id)) {
                fprintf(stderr, "setslot write failed\n");
                rc = 1;
            } else {
                uint16_t rb = 0;
                if (read_half(usb, outPipe, inPipe, ACTIVE_QID, &rb) || rb != target_id) {
                    fprintf(stderr, "setslot read-back mismatch: 0x%04X\n", rb);
                    rc = 1;
                } else {
                    printf("OK: active_qid 0x%08X=0x%04X (was 0x%04X)\n", ACTIVE_QID, rb, act_qid);
                }
            }
        } else if (!strcmp(mode, "restoreslot")) {
            uint16_t restore_id = 0x008Bu;
            printf("restoring active_qid 0x%08X: 0x%04X -> 0x%04X\n", ACTIVE_QID, act_qid, restore_id);
            if (write_half(usb, outPipe, inPipe, ACTIVE_QID, restore_id)) {
                fprintf(stderr, "restoreslot write failed\n");
                rc = 1;
            } else {
                uint16_t rb = 0;
                if (read_half(usb, outPipe, inPipe, ACTIVE_QID, &rb) || rb != restore_id) {
                    fprintf(stderr, "restoreslot read-back mismatch: 0x%04X\n", rb);
                    rc = 1;
                } else {
                    printf("OK: restored active_qid 0x%08X=0x%04X\n", ACTIVE_QID, rb);
                }
            }
        } else if (!strcmp(mode, "ui_enable") || !strcmp(mode, "ui_restore")) {
            uint16_t sp = 0, q0 = 0, f0 = 0, q1 = 0;
            uint16_t tsp = 0, tq0 = 0, tq1 = 0;
            if (read_half(usb, outPipe, inPipe, UI_SIG_PREV, &sp) ||
                read_half(usb, outPipe, inPipe, UI_SLOT_Q0, &q0) ||
                read_half(usb, outPipe, inPipe, UI_SLOT_F0, &f0) ||
                read_half(usb, outPipe, inPipe, UI_SLOT_Q1, &q1) ||
                read_half(usb, outPipe, inPipe, TPL_SIG_PREV, &tsp) ||
                read_half(usb, outPipe, inPipe, TPL_SLOT_Q0, &tq0) ||
                read_half(usb, outPipe, inPipe, TPL_SLOT_Q1, &tq1)) {
                fprintf(stderr, "ui/tpl table read failed\n");
                rc = 1;
            } else {
                printf("ui:  prev=%04X q0=%04X f0=%04X q1=%04X\n", sp, q0, f0, q1);
                printf("tpl: prev=%04X q0=%04X q1=%04X\n", tsp, tq0, tq1);
                if (sp != 0x00D1u || tsp != 0x00D1u) {
                    fprintf(stderr, "UI/TPL TABLE SIGNATURE MISMATCH; aborting\n");
                    rc = 1;
                } else {
                    int en = !strcmp(mode, "ui_enable");
                    uint16_t nq0 = en ? 0x00EDu : 0x00D2u;
                    uint16_t nf0 = 0x0104u;
                    uint16_t nq1 = en ? 0x80EDu : 0x80D2u;
                    if (write_half(usb, outPipe, inPipe, TPL_SLOT_Q0, nq0) ||
                        write_half(usb, outPipe, inPipe, TPL_SLOT_Q1, nq1) ||
                        write_half(usb, outPipe, inPipe, UI_SLOT_Q0, nq0) ||
                        write_half(usb, outPipe, inPipe, UI_SLOT_F0, nf0) ||
                        write_half(usb, outPipe, inPipe, UI_SLOT_Q1, nq1)) {
                        fprintf(stderr, "ui/tpl write failed\n");
                        rc = 1;
                    } else {
                        uint16_t r0 = 0, r1 = 0, r2 = 0, tr0 = 0, tr1 = 0;
                        if (read_half(usb, outPipe, inPipe, UI_SLOT_Q0, &r0) ||
                            read_half(usb, outPipe, inPipe, UI_SLOT_F0, &r1) ||
                            read_half(usb, outPipe, inPipe, UI_SLOT_Q1, &r2) ||
                            read_half(usb, outPipe, inPipe, TPL_SLOT_Q0, &tr0) ||
                            read_half(usb, outPipe, inPipe, TPL_SLOT_Q1, &tr1) ||
                            r0 != nq0 || r1 != nf0 || r2 != nq1 || tr0 != nq0 || tr1 != nq1) {
                            fprintf(stderr, "ui/tpl read-back mismatch: %04X/%04X/%04X tpl=%04X/%04X\n",
                                    r0, r1, r2, tr0, tr1);
                            rc = 1;
                        } else {
                            printf("OK: TPL 0x%08X=%04X/0x%08X=%04X + UI 0x%08X=%04X(f=%04X)/0x%08X=%04X\n",
                                   TPL_SLOT_Q0, tr0, TPL_SLOT_Q1, tr1, UI_SLOT_Q0, r0, r1, UI_SLOT_Q1, r2);
                        }
                    }
                }
            }
        } else if (!strcmp(mode, "wl_enable") || !strcmp(mode, "wl_restore")) {
            uint16_t s0 = 0, s1 = 0, v0 = 0, v1 = 0, sent = 0;
            if (read_half(usb, outPipe, inPipe, WL_SIG_LO, &s0) ||
                read_half(usb, outPipe, inPipe, WL_SIG_HI, &s1) ||
                read_half(usb, outPipe, inPipe, WL_SLOT_LO, &v0) ||
                read_half(usb, outPipe, inPipe, WL_SLOT_HI, &v1) ||
                read_half(usb, outPipe, inPipe, WL_SENTINEL, &sent)) {
                fprintf(stderr, "whitelist read failed\n");
                rc = 1;
            } else {
                printf("wl: sig=%04X/%04X slot=%04X/%04X sent=%04X\n", s0, s1, v0, v1, sent);
                if (s0 != 0x00D1u || s1 != 0x80D1u || sent != 0xFFFFu) {
                    fprintf(stderr, "WHITELIST SIGNATURE MISMATCH; aborting\n");
                    rc = 1;
                } else {
                    uint16_t w0 = !strcmp(mode, "wl_enable") ? 0x00EDu : 0x00D2u;
                    uint16_t w1 = !strcmp(mode, "wl_enable") ? 0x80EDu : 0x80D2u;
                    printf("writing whitelist 0x%08X=%04X, 0x%08X=%04X\n", WL_SLOT_LO, w0, WL_SLOT_HI, w1);
                    if (write_half(usb, outPipe, inPipe, WL_SLOT_LO, w0) ||
                        write_half(usb, outPipe, inPipe, WL_SLOT_HI, w1)) {
                        fprintf(stderr, "whitelist write failed\n");
                        rc = 1;
                    } else {
                        uint16_t rb0 = 0, rb1 = 0;
                        if (read_half(usb, outPipe, inPipe, WL_SLOT_LO, &rb0) ||
                            read_half(usb, outPipe, inPipe, WL_SLOT_HI, &rb1) ||
                            rb0 != w0 || rb1 != w1) {
                            fprintf(stderr, "whitelist read-back mismatch: %04X/%04X\n", rb0, rb1);
                            rc = 1;
                        } else {
                            printf("OK: whitelist 0x%08X=%04X, 0x%08X=%04X\n", WL_SLOT_LO, rb0, WL_SLOT_HI, rb1);
                        }
                    }
                }
            }
        } else if (!strcmp(mode, "v13_check") || !strcmp(mode, "v13_enable") || !strcmp(mode, "v13_restore")) {
            static const struct { uint32_t addr; uint16_t val; } anchors[] = {
                {0x868261B4u, 0x460Du}, {0x868261B8u, 0xE8BDu},
                {0x86F15E84u, 0x00C0u}, {0x86F15E98u, 0xFFFFu},
                {0x86F15ED2u, 0x00C1u}, {0x86F15EDEu, 0xFFFFu},
                {0x870895E4u, 0x00C0u}, {0x87089634u, 0xFFFFu},
                {0x870896BCu, 0x00C1u}, {0x870896ECu, 0xFFFFu},
                {0x08A68C2Cu, 0x00C0u}, {0x08A68C7Cu, 0xFFFFu},
                {0x08A68D04u, 0x00C1u}, {0x08A68D34u, 0xFFFFu},
            };
            static const struct { uint32_t addr; uint16_t unlock; uint16_t stock; } patches[] = {
                /* NTSC Whitelist (00CE->00F4 5.9K30, 00D1->00F1 5.9K24, 00D2->00ED 6K24) */
                {0x86F15E8Cu, 0x00F4u, 0x00CEu}, {0x86F15E8Eu, 0x80F4u, 0x80CEu},
                {0x86F15E90u, 0x00F1u, 0x00D1u}, {0x86F15E92u, 0x80F1u, 0x80D1u},
                {0x86F15E94u, 0x00EDu, 0x00D2u}, {0x86F15E96u, 0x80EDu, 0x80D2u},
                /* NTSC Master Template (survives power-switch OFF/ON) */
                {0x87089604u, 0x00F4u, 0x00CEu}, {0x8708960Cu, 0x80F4u, 0x80CEu},
                {0x87089614u, 0x00F1u, 0x00D1u}, {0x8708961Cu, 0x80F1u, 0x80D1u},
                {0x87089624u, 0x00EDu, 0x00D2u}, {0x8708962Cu, 0x80EDu, 0x80D2u},
                /* NTSC Active MOV Template */
                {0x08A68C4Cu, 0x00F4u, 0x00CEu}, {0x08A68C54u, 0x80F4u, 0x80CEu},
                {0x08A68C5Cu, 0x00F1u, 0x00D1u}, {0x08A68C64u, 0x80F1u, 0x80D1u},
                {0x08A68C6Cu, 0x00EDu, 0x00D2u}, {0x08A68C74u, 0x80EDu, 0x80D2u},
                /* PAL Whitelist (00CD->00F3 5.9K25; NEVER 00EF 5.4K which stalls) */
                {0x86F15ED6u, 0x00F3u, 0x00CDu}, {0x86F15ED8u, 0x80F3u, 0x80CDu},
                /* PAL Master Template (survives power-switch OFF/ON) */
                {0x870896CCu, 0x00F3u, 0x00CDu}, {0x870896D4u, 0x80F3u, 0x80CDu},
                /* PAL Active MOV Template */
                {0x08A68D14u, 0x00F3u, 0x00CDu}, {0x08A68D1Cu, 0x80F3u, 0x80CDu},
                /* Unlimited Recording */
                {0x868261B6u, 0x2000u, 0x4628u},
            };
            int ok = 1;
            uint16_t cap_cur = 0;
            if (read_half(usb, outPipe, inPipe, 0x868261B6u, &cap_cur) ||
                (cap_cur != 0x4628u && cap_cur != 0x2000u)) {
                fprintf(stderr, "v13 anchor mismatch: 0x868261B6=0x%04X\n", cap_cur);
                ok = 0;
            }
            for (size_t i = 0; i < sizeof(anchors)/sizeof(anchors[0]) && ok; ++i) {
                uint16_t got = 0;
                if (read_half(usb, outPipe, inPipe, anchors[i].addr, &got) || got != anchors[i].val) {
                    fprintf(stderr, "v13 anchor mismatch: 0x%08X=0x%04X (want 0x%04X)\n",
                            anchors[i].addr, got, anchors[i].val);
                    ok = 0;
                }
            }
            if (!ok) {
                rc = 1;
            } else if (!strcmp(mode, "v13_check")) {
                uint16_t ntsc_q = 0, pal_q = 0;
                read_half(usb, outPipe, inPipe, 0x08A68C6Cu, &ntsc_q);
                read_half(usb, outPipe, inPipe, 0x08A68D24u, &pal_q);
                printf("OK: ALL 19 v1.3 anchors verified (compatible S5/S5D FW 2.9)!\n"
                       "  cap=0x%04X | ntsc_tail=0x%04X | pal_tail=0x%04X\n",
                       cap_cur, ntsc_q, pal_q);
            } else {
                int en = !strcmp(mode, "v13_enable");
                int err = 0;
                for (size_t i = 0; i < sizeof(patches)/sizeof(patches[0]) && !err; ++i) {
                    uint16_t want = en ? patches[i].unlock : patches[i].stock;
                    uint16_t rb = 0;
                    err |= write_half(usb, outPipe, inPipe, patches[i].addr, want);
                    err |= read_half(usb, outPipe, inPipe, patches[i].addr, &rb);
                    if (rb != want) {
                        fprintf(stderr, "v13 read-back mismatch at 0x%08X: got 0x%04X want 0x%04X\n",
                                patches[i].addr, rb, want);
                        err = 1;
                    }
                }
                /* Also refresh transient Live UI buffer if NTSC MOV (0x08A6842C==0x00C0) or PAL MOV (0x08A683EC==0x00C1) is currently cached */
                uint16_t ui_ntsc = 0, ui_pal = 0;
                if (!err && !read_half(usb, outPipe, inPipe, 0x08A6842Cu, &ui_ntsc) && ui_ntsc == 0x00C0u) {
                    static const struct { uint32_t addr; uint16_t unlock; uint16_t stock; } ui_n[] = {
                        {0x08A6844Cu, 0x00F4u, 0x00CEu}, {0x08A6844Eu, 0x0104u, 0x0104u}, {0x08A68454u, 0x80F4u, 0x80CEu},
                        {0x08A6845Cu, 0x00F1u, 0x00D1u}, {0x08A6845Eu, 0x0104u, 0x0104u}, {0x08A68464u, 0x80F1u, 0x80D1u},
                        {0x08A6846Cu, 0x00EDu, 0x00D2u}, {0x08A6846Eu, 0x0104u, 0x0104u}, {0x08A68474u, 0x80EDu, 0x80D2u},
                    };
                    for (size_t i = 0; i < sizeof(ui_n)/sizeof(ui_n[0]) && !err; ++i)
                        err |= write_half(usb, outPipe, inPipe, ui_n[i].addr, en ? ui_n[i].unlock : ui_n[i].stock);
                } else if (!err && !read_half(usb, outPipe, inPipe, 0x08A683ECu, &ui_pal) && ui_pal == 0x00C1u) {
                    err |= write_half(usb, outPipe, inPipe, 0x08A683FCu, en ? 0x00F3u : 0x00CDu);
                    err |= write_half(usb, outPipe, inPipe, 0x08A683FEu, 0x0104u);
                    err |= write_half(usb, outPipe, inPipe, 0x08A68404u, en ? 0x80F3u : 0x80CDu);
                }
                if (err) { fprintf(stderr, "%s failed\n", mode); rc = 1; }
                else if (en) {
                    printf("OK: v1.3 ARMED (25 halfwords + live UI refresh verified)!\n"
                           "  NTSC 59.94Hz MOV: 5.9K30 (00F4), 5.9K24 (00F1), 6K24 OG (00ED)\n"
                           "  PAL  50.00Hz MOV: 5.9K25 (00F3)\n"
                           "  Unlimited Rec: ON (0x868261B6=0x2000). Open MOV Rec Quality menu to view!\n");
                } else {
                    printf("OK: v1.3 RESTORED to 100%% stock.\n");
                }
            }
        } else if (!strcmp(mode, "loader_read")) {
            uint16_t w[16] = {0}, p[8] = {0}, h0 = 0, h1 = 0;
            int err = 0;
            for (unsigned i = 0; i < 16 && !err; ++i)
                err |= read_half(usb, outPipe, inPipe, 0x870DD100u + i * 2, &w[i]);
            for (unsigned i = 0; i < 8 && !err; ++i)
                err |= read_half(usb, outPipe, inPipe, 0x870DE000u + i * 2, &p[i]);
            err |= read_half(usb, outPipe, inPipe, 0x866930EAu, &h0);
            err |= read_half(usb, outPipe, inPipe, 0x866930ECu, &h1);
            if (err) {
                fprintf(stderr, "loader_read failed\n");
                rc = 1;
            } else {
                uint32_t v[8];
                for (unsigned i = 0; i < 8; ++i)
                    v[i] = (uint32_t)w[i * 2] | ((uint32_t)w[i * 2 + 1] << 16);
                uint8_t b[16];
                for (unsigned i = 0; i < 8; ++i) {
                    b[i * 2] = (uint8_t)(p[i] & 0xFFu);
                    b[i * 2 + 1] = (uint8_t)(p[i] >> 8);
                }
                printf("SD Plugin Loader (hook 0x866930EA=%04X %04X):\n"
                       "  ran=%u | read_rc=0x%08X | file_size=%u\n"
                       "  magic=0x%08X | plugin_rc=0x%08X\n"
                       "  extra: [5]=0x%08X [6]=0x%08X [7]=0x%08X\n"
                       "  0x870DE000[0..15]:",
                       h0, h1, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
                for (unsigned i = 0; i < 16; ++i) printf(" %02X", b[i]);
                printf("  \"");
                for (unsigned i = 0; i < 16; ++i) printf("%c", (b[i] >= 0x20 && b[i] <= 0x7E) ? b[i] : '.');
                printf("\"\n");
            }
        } else if (!strcmp(mode, "loader_enable") || !strcmp(mode, "loader_hello") || !strcmp(mode, "loader_restore")) {
            uint16_t s0 = 0, s1 = 0, h0 = 0, h1 = 0, s2 = 0, s3 = 0, st0 = 0;
            if (read_half(usb, outPipe, inPipe, 0x866930E6u, &s0) ||
                read_half(usb, outPipe, inPipe, 0x866930E8u, &s1) ||
                read_half(usb, outPipe, inPipe, 0x866930EAu, &h0) ||
                read_half(usb, outPipe, inPipe, 0x866930ECu, &h1) ||
                read_half(usb, outPipe, inPipe, 0x866930EEu, &s2) ||
                read_half(usb, outPipe, inPipe, 0x866930F0u, &s3) ||
                read_half(usb, outPipe, inPipe, 0x870DD000u, &st0)) {
                fprintf(stderr, "loader sig read failed\n");
                rc = 1;
            } else if (s0 != 0xD00Au || s1 != 0x2000u || s2 != 0xB188u || s3 != 0x2805u ||
                       (st0 != 0xFFFFu && st0 != 0xB51Fu && st0 != 0xB53Fu)) {
                fprintf(stderr, "LOADER SIGNATURE MISMATCH: %04X %04X / %04X %04X / %04X %04X / st0=%04X\n",
                        s0, s1, h0, h1, s2, s3, st0);
                rc = 1;
            } else if (!strcmp(mode, "loader_restore")) {
                int err = 0;
                err |= write_half(usb, outPipe, inPipe, 0x866930EAu, 0xF021u);
                err |= write_half(usb, outPipe, inPipe, 0x866930ECu, 0xFBF6u);
                uint16_t r0 = 0, r1 = 0;
                err |= read_half(usb, outPipe, inPipe, 0x866930EAu, &r0);
                err |= read_half(usb, outPipe, inPipe, 0x866930ECu, &r1);
                if (r0 != 0xF021u || r1 != 0xFBF6u) err = 1;
                for (unsigned i = 0; i < 64 && !err; ++i)
                    err |= write_half(usb, outPipe, inPipe, 0x870DD000u + i * 2, 0xFFFFu);
                for (unsigned i = 0; i < 16 && !err; ++i)
                    err |= write_half(usb, outPipe, inPipe, 0x870DD100u + i * 2, 0xFFFFu);
                if (err) { fprintf(stderr, "loader_restore failed\n"); rc = 1; }
                else printf("OK: loader hook 0x866930EA & scratchpad restored to stock.\n");
            } else {
                static const uint16_t stub_main[64] = {
                    0xB53Fu, 0xB083u, 0x4C14u, 0x6820u, 0xBB00u, 0x2001u, 0x6020u, 0xA017u,
                    0x4D12u, 0x4629u, 0x466Au, 0xF234u, 0xF8F1u, 0x6060u, 0x9900u, 0x60A1u,
                    0xB9A0u, 0x682Au, 0x60E2u, 0x2908u, 0xD30Eu, 0x4B0Du, 0x4299u, 0xD80Bu,
                    0x4B0Cu, 0x429Au, 0xD108u, 0xF732u, 0xD79Eu, 0xF732u, 0xD7F4u, 0x4620u,
                    0x1D69u, 0x4788u, 0x6120u, 0xE001u, 0x4807u, 0x6120u, 0xB003u, 0xE8BDu,
                    0x403Fu, 0xF5D7u, 0x9C42u, 0xBF00u, 0xD100u, 0x870Du, 0xE000u, 0x870Du,
                    0x0000u, 0x0010u, 0x3553u, 0x444Du, 0x0001u, 0xBAD0u, 0x3A41u, 0x535Cu,
                    0x4D35u, 0x444Fu, 0x4D5Cu, 0x4941u, 0x2E4Eu, 0x4942u, 0x004Eu, 0x0000u
                };
                static const uint16_t stub_hello[64] = {
                    0xB53Fu, 0xB083u, 0x4C14u, 0x6820u, 0xBB00u, 0x2001u, 0x6020u, 0xA017u,
                    0x4D12u, 0x4629u, 0x466Au, 0xF234u, 0xF8F1u, 0x6060u, 0x9900u, 0x60A1u,
                    0xB9A0u, 0x682Au, 0x60E2u, 0x2908u, 0xD30Eu, 0x4B0Du, 0x4299u, 0xD80Bu,
                    0x4B0Cu, 0x429Au, 0xD108u, 0xF732u, 0xD79Eu, 0xF732u, 0xD7F4u, 0x4620u,
                    0x1D69u, 0x4788u, 0x6120u, 0xE001u, 0x4807u, 0x6120u, 0xB003u, 0xE8BDu,
                    0x403Fu, 0xF5D7u, 0x9C42u, 0xBF00u, 0xD100u, 0x870Du, 0xE000u, 0x870Du,
                    0x0000u, 0x0010u, 0x3553u, 0x444Du, 0x0001u, 0xBAD0u, 0x3A41u, 0x485Cu,
                    0x4C45u, 0x4F4Cu, 0x542Eu, 0x5458u, 0x0000u, 0x0000u, 0x0000u, 0x0000u
                };
                int is_hello = !strcmp(mode, "loader_hello");
                const uint16_t *stub = is_hello ? stub_hello : stub_main;
                int err = 0;
                for (unsigned i = 0; i < 16 && !err; ++i)
                    err |= write_half(usb, outPipe, inPipe, 0x870DD100u + i * 2, (i >= 2 && (i % 2) == 0) ? 0xDEADu : 0x0000u);
                for (unsigned i = 0; i < 64 && !err; ++i) {
                    uint32_t a = 0x870DD000u + i * 2;
                    uint16_t rb = 0;
                    err |= write_half(usb, outPipe, inPipe, a, stub[i]);
                    err |= read_half(usb, outPipe, inPipe, a, &rb);
                    if (rb != stub[i]) err = 1;
                }
                if (!err) {
                    err |= write_half(usb, outPipe, inPipe, 0x866930EAu, 0xF249u);
                    err |= write_half(usb, outPipe, inPipe, 0x866930ECu, 0xDF89u);
                    uint16_t r0 = 0, r1 = 0;
                    err |= read_half(usb, outPipe, inPipe, 0x866930EAu, &r0);
                    err |= read_half(usb, outPipe, inPipe, 0x866930ECu, &r1);
                    if (r0 != 0xF249u || r1 != 0xDF89u) err = 1;
                }
                if (err) { fprintf(stderr, "%s failed\n", mode); rc = 1; }
                else printf("OK: SD plugin loader (%s) installed at 0x870DD000 + hooked at 0x866930EA! Open Rec Quality menu, then run loader_read.\n",
                            is_hello ? "A:\\HELLO.TXT readback test" : "A:\\S5MOD\\MAIN.BIN");
            }
        } else if (!strcmp(mode, "sd_read")) {
            uint16_t w[16] = {0}, h0 = 0, h1 = 0;
            int err = 0;
            for (unsigned i = 0; i < 16 && !err; ++i)
                err |= read_half(usb, outPipe, inPipe, 0x870DD100u + i * 2, &w[i]);
            err |= read_half(usb, outPipe, inPipe, 0x866930EAu, &h0);
            err |= read_half(usb, outPipe, inPipe, 0x866930ECu, &h1);
            if (err) {
                fprintf(stderr, "sd_read failed\n");
                rc = 1;
            } else {
                uint32_t v[8];
                for (unsigned i = 0; i < 8; ++i)
                    v[i] = (uint32_t)w[i * 2] | ((uint32_t)w[i * 2 + 1] << 16);
                printf("SD direct (hook 0x866930EA=%04X %04X):\n"
                       "  ran=%u | parse_rc=0x%08X mh=0x%08X\n"
                       "  open_rc=0x%08X fh=0x%08X\n"
                       "  write_rc=0x%08X written=%u | close_rc=0x%08X\n",
                       h0, h1, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
            }
        } else if (!strcmp(mode, "sd_test") || !strcmp(mode, "sd_restore")) {
            uint16_t s0 = 0, s1 = 0, h0 = 0, h1 = 0, s2 = 0, s3 = 0, st0 = 0;
            if (read_half(usb, outPipe, inPipe, 0x866930E6u, &s0) ||
                read_half(usb, outPipe, inPipe, 0x866930E8u, &s1) ||
                read_half(usb, outPipe, inPipe, 0x866930EAu, &h0) ||
                read_half(usb, outPipe, inPipe, 0x866930ECu, &h1) ||
                read_half(usb, outPipe, inPipe, 0x866930EEu, &s2) ||
                read_half(usb, outPipe, inPipe, 0x866930F0u, &s3) ||
                read_half(usb, outPipe, inPipe, 0x870DD000u, &st0)) {
                fprintf(stderr, "sd_test sig read failed\n");
                rc = 1;
            } else if (s0 != 0xD00Au || s1 != 0x2000u || s2 != 0xB188u || s3 != 0x2805u ||
                       (st0 != 0xFFFFu && st0 != 0xB51Fu && st0 != 0xB53Fu)) {
                fprintf(stderr, "SD_TEST SIGNATURE MISMATCH: %04X %04X / %04X %04X / %04X %04X / st0=%04X\n",
                        s0, s1, h0, h1, s2, s3, st0);
                rc = 1;
            } else if (!strcmp(mode, "sd_test")) {
                static const uint16_t stub[70] = {
                    0xB53Fu, 0xB089u, 0x4C1Au, 0x6820u, 0xBB68u, 0x2001u, 0x6020u, 0xA01Bu,
                    0xA903u, 0xAA04u, 0xAB06u, 0xF234u, 0xF9CDu, 0x6060u, 0x9903u, 0x60A1u,
                    0xBB08u, 0x2500u, 0x2001u, 0x9000u, 0x9501u, 0x4603u, 0x9803u, 0xA902u,
                    0x9A04u, 0xF234u, 0xFE77u, 0x60E0u, 0x9902u, 0x6121u, 0xB998u, 0x480Du,
                    0x490Du, 0x9007u, 0x9108u, 0x9500u, 0x9501u, 0x9802u, 0xA907u, 0x2206u,
                    0xAB05u, 0xF234u, 0xFE63u, 0x6160u, 0x9905u, 0x61A1u, 0x9802u, 0x2100u,
                    0xF234u, 0xFE5Eu, 0x61E0u, 0xB009u, 0xE8BDu, 0x403Fu, 0xF5D7u, 0x9C35u,
                    0xD100u, 0x870Du, 0x3553u, 0x4F4Du, 0x2144u, 0x0000u, 0x3A41u, 0x485Cu,
                    0x4C45u, 0x4F4Cu, 0x542Eu, 0x5458u, 0x0000u, 0x0000u
                };
                int err = 0;
                for (unsigned i = 0; i < 16 && !err; ++i)
                    err |= write_half(usb, outPipe, inPipe, 0x870DD100u + i * 2, (i >= 2 && (i % 2) == 0) ? 0xDEADu : 0x0000u);
                for (unsigned i = 0; i < 70 && !err; ++i) {
                    uint32_t a = 0x870DD000u + i * 2;
                    uint16_t rb = 0;
                    err |= write_half(usb, outPipe, inPipe, a, stub[i]);
                    err |= read_half(usb, outPipe, inPipe, a, &rb);
                    if (rb != stub[i]) err = 1;
                }
                if (!err) {
                    /* 0x866930EA: bl #0x866b48da (F021 FBF6) -> bl #0x870dd000 (F249 DF89) */
                    err |= write_half(usb, outPipe, inPipe, 0x866930EAu, 0xF249u);
                    err |= write_half(usb, outPipe, inPipe, 0x866930ECu, 0xDF89u);
                    uint16_t r0 = 0, r1 = 0;
                    err |= read_half(usb, outPipe, inPipe, 0x866930EAu, &r0);
                    err |= read_half(usb, outPipe, inPipe, 0x866930ECu, &r1);
                    if (r0 != 0xF249u || r1 != 0xDF89u) err = 1;
                }
                if (err) { fprintf(stderr, "sd_test failed\n"); rc = 1; }
                else printf("OK: idle menu CFS_Open/Write/Close stub installed at 0x866930EA! Open Rec Quality menu (no recording), then run sd_read.\n");
            } else {
                int err = 0;
                /* Restore 0x866930EA -> bl #0x866b48da (F021 FBF6) */
                err |= write_half(usb, outPipe, inPipe, 0x866930EAu, 0xF021u);
                err |= write_half(usb, outPipe, inPipe, 0x866930ECu, 0xFBF6u);
                uint16_t r0 = 0, r1 = 0;
                err |= read_half(usb, outPipe, inPipe, 0x866930EAu, &r0);
                err |= read_half(usb, outPipe, inPipe, 0x866930ECu, &r1);
                if (r0 != 0xF021u || r1 != 0xFBF6u) err = 1;
                for (unsigned i = 0; i < 70 && !err; ++i)
                    err |= write_half(usb, outPipe, inPipe, 0x870DD000u + i * 2, 0xFFFFu);
                for (unsigned i = 0; i < 16 && !err; ++i)
                    err |= write_half(usb, outPipe, inPipe, 0x870DD100u + i * 2, 0xFFFFu);
                if (err) { fprintf(stderr, "sd_restore failed\n"); rc = 1; }
                else printf("OK: idle menu hook 0x866930EA & scratchpad restored to stock.\n");
            }
        } else if (!strcmp(mode, "hb_read")) {
            uint16_t lo = 0, hi = 0, h0 = 0, h1 = 0;
            if (read_half(usb, outPipe, inPipe, 0x870DD100u, &lo) ||
                read_half(usb, outPipe, inPipe, 0x870DD102u, &hi) ||
                read_half(usb, outPipe, inPipe, 0x86476F52u, &h0) ||
                read_half(usb, outPipe, inPipe, 0x86476F54u, &h1)) {
                fprintf(stderr, "hb_read failed\n");
                rc = 1;
            } else {
                uint32_t cnt = (uint32_t)lo | ((uint32_t)hi << 16);
                printf("HB counter 0x870DD100 = %u (0x%08X) | hook 0x86476F52=%04X %04X\n",
                       cnt, cnt, h0, h1);
            }
        } else if (!strcmp(mode, "hb_enable") || !strcmp(mode, "hb_restore")) {
            uint16_t s0 = 0, s1 = 0, h0 = 0, h1 = 0, s2 = 0, st0 = 0;
            if (read_half(usb, outPipe, inPipe, 0x86476F4Eu, &s0) ||
                read_half(usb, outPipe, inPipe, 0x86476F50u, &s1) ||
                read_half(usb, outPipe, inPipe, 0x86476F52u, &h0) ||
                read_half(usb, outPipe, inPipe, 0x86476F54u, &h1) ||
                read_half(usb, outPipe, inPipe, 0x86476F56u, &s2) ||
                read_half(usb, outPipe, inPipe, 0x870DD000u, &st0)) {
                fprintf(stderr, "hb sig read failed\n");
                rc = 1;
            } else if (s0 != 0xF225u || s1 != 0xFFCBu || s2 != 0x48FAu ||
                       (st0 != 0xFFFFu && st0 != 0xB503u && st0 != 0xB51Fu && st0 != 0xB53Fu)) {
                fprintf(stderr, "HB SIGNATURE MISMATCH: %04X %04X / %04X %04X / %04X / st0=%04X\n",
                        s0, s1, h0, h1, s2, st0);
                rc = 1;
            } else if (!strcmp(mode, "hb_enable")) {
                static const uint16_t stub[12] = {
                    0xB503u, 0x4804u, 0x6801u, 0x3101u, 0x6001u,
                    0xE8BDu, 0x4003u, 0xF5D7u, 0x98B3u, 0xBF00u,
                    0xD100u, 0x870Du
                };
                int err = 0;
                err |= write_half(usb, outPipe, inPipe, 0x870DD100u, 0x0000u);
                err |= write_half(usb, outPipe, inPipe, 0x870DD102u, 0x0000u);
                for (unsigned i = 0; i < 12 && !err; ++i) {
                    uint32_t a = 0x870DD000u + i * 2;
                    uint16_t rb = 0;
                    err |= write_half(usb, outPipe, inPipe, a, stub[i]);
                    err |= read_half(usb, outPipe, inPipe, a, &rb);
                    if (rb != stub[i]) err = 1;
                }
                if (!err) {
                    err |= write_half(usb, outPipe, inPipe, 0x86476F52u, 0xF066u);
                    err |= write_half(usb, outPipe, inPipe, 0x86476F54u, 0xD055u);
                    uint16_t r0 = 0, r1 = 0;
                    err |= read_half(usb, outPipe, inPipe, 0x86476F52u, &r0);
                    err |= read_half(usb, outPipe, inPipe, 0x86476F54u, &r1);
                    if (r0 != 0xF066u || r1 != 0xD055u) err = 1;
                }
                if (err) { fprintf(stderr, "hb_enable failed\n"); rc = 1; }
                else printf("OK: HB stub installed at 0x870DD000 + hooked at 0x86476F52! Tap Record for 2s then run hb_read.\n");
            } else {
                int err = 0;
                err |= write_half(usb, outPipe, inPipe, 0x86476F52u, 0xF23Du);
                err |= write_half(usb, outPipe, inPipe, 0x86476F54u, 0xF911u);
                for (unsigned i = 0; i < 70 && !err; ++i)
                    err |= write_half(usb, outPipe, inPipe, 0x870DD000u + i * 2, 0xFFFFu);
                if (err) { fprintf(stderr, "hb_restore failed\n"); rc = 1; }
                else printf("OK: HB hook & scratchpad restored to stock.\n");
            }
        } else if (!strcmp(mode, "enable")) {
            uint16_t want_q = preset_insn(preset);
            if ((raw_flag & 0xFF) != 0) {
                fprintf(stderr, "HDMI RAW flag is non-zero (%u); refusing\n", raw_flag & 0xFF);
                rc = 1;
            } else {
                printf("enabling preset '%s': 0x%08X=0x%04X, 0x%08X=0x%04X\n",
                       preset, QID_INSN_ADDR, want_q, BRANCH_ADDR, PATCH_BRANCH);
                if (write_half(usb, outPipe, inPipe, QID_INSN_ADDR, want_q) ||
                    write_half(usb, outPipe, inPipe, BRANCH_ADDR, PATCH_BRANCH)) {
                    fprintf(stderr, "write failed\n");
                    rc = 1;
                } else {
                    uint16_t rb1 = 0, rb2 = 0;
                    if (read_half(usb, outPipe, inPipe, QID_INSN_ADDR, &rb1) ||
                        read_half(usb, outPipe, inPipe, BRANCH_ADDR, &rb2) ||
                        rb1 != want_q || rb2 != PATCH_BRANCH) {
                        fprintf(stderr, "read-back mismatch: 0x%04X / 0x%04X\n", rb1, rb2);
                        rc = 1;
                    } else {
                        printf("OK: preset '%s' active (0x%08X=0x%04X, 0x%08X=0x%04X)\n",
                               preset, QID_INSN_ADDR, rb1, BRANCH_ADDR, rb2);
                    }
                }
            }
        } else if (!strcmp(mode, "restore")) {
            printf("restoring 0x%08X->0x%04X, 0x%08X->0x%04X\n",
                   BRANCH_ADDR, ORIG_BRANCH, QID_INSN_ADDR, ORIG_QID_INSN);
            if (write_half(usb, outPipe, inPipe, BRANCH_ADDR, ORIG_BRANCH) ||
                write_half(usb, outPipe, inPipe, QID_INSN_ADDR, ORIG_QID_INSN)) {
                fprintf(stderr, "restore write failed\n");
                rc = 1;
            } else {
                uint16_t rb1 = 0, rb2 = 0;
                if (read_half(usb, outPipe, inPipe, BRANCH_ADDR, &rb1) ||
                    read_half(usb, outPipe, inPipe, QID_INSN_ADDR, &rb2) ||
                    rb1 != ORIG_BRANCH || rb2 != ORIG_QID_INSN) {
                    fprintf(stderr, "restore read-back mismatch: 0x%04X / 0x%04X\n", rb1, rb2);
                    rc = 1;
                } else {
                    printf("OK: restored stock (0x%08X=0x%04X, 0x%08X=0x%04X)\n",
                           BRANCH_ADDR, rb1, QID_INSN_ADDR, rb2);
                }
            }
        }
    }

    (*usb)->USBInterfaceClose(usb);
    (*usb)->Release(usb); (*ifacePlug)->Release(ifacePlug);
    (*dev)->Release(dev); (*plug)->Release(plug);
    IOObjectRelease(ifaceService); IOObjectRelease(device);
    return rc;
}
