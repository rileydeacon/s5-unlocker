/*
 * tools/s5_v15_test.c — S5 Unlocker v1.5 prototype:
 *   - 6K Open Gate 23.98p, 5.9K 29.97p, 5.9K 23.98p at the VERY TOP of NTSC MOV menu
 *   - 5.9K 25.00p at the VERY TOP of PAL MOV menu
 *   - ZERO stock modes removed (all 17 NTSC + 11 PAL stock MOV modes 100% preserved!)
 *   - Fixed recording timers removed + live HDMI output during 6K/5.9K recording
 *   - 100% volatile RAM only.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PANASONIC_VID 0x04DA
#define SERVICE_PID   0x2370
#define TIMEOUT_MS    4000
#define FRAME_BYTES   16

#define PTR_NTSC_WL   0x86462C34u /* stock 0x06F15E30 -> 0x870DD440 */
#define PTR_PAL_WL    0x86462C40u /* stock 0x06F15E9A -> 0x870DD4C0 */
#define PTR_NTSC_UI   0x866931B8u /* stock 0x08A68B6C -> 0x870DD200 */
#define PTR_PAL_UI    0x866931D4u /* stock 0x08A68C84 -> 0x870DD360 */
#define LIVE_UI_BASE  0x08A6836Cu

#define NEW_NTSC_UI   0x870DD200u
#define NEW_PAL_UI    0x870DD360u
#define NEW_NTSC_WL   0x870DD440u
#define NEW_PAL_WL    0x870DD4C0u

static const uint16_t NTSC_UI_NEW[40] = { 0x00EDu, 0x80EDu, 0x00F4u, 0x80F4u, 0x00F1u, 0x80F1u, 0x0092u, 0x8092u, 0x0094u, 0x8094u, 0x0095u, 0x8095u, 0x0096u, 0x8096u, 0x0097u, 0x8097u, 0x009Bu, 0x809Bu, 0x009Fu, 0x809Fu, 0x00A1u, 0x80A1u, 0x00A5u, 0x80A5u, 0x00A8u, 0x80A8u, 0x00A7u, 0x80A7u, 0x00BEu, 0x80BEu, 0x00C0u, 0x80C0u, 0x00CCu, 0x80CCu, 0x00CEu, 0x80CEu, 0x00D1u, 0x80D1u, 0x00D2u, 0x80D2u };
static const uint16_t PAL_UI_NEW[24]   = { 0x00F3u, 0x80F3u, 0x0093u, 0x8093u, 0x00FEu, 0x80FEu, 0x00FFu, 0x80FFu, 0x009Cu, 0x809Cu, 0x00A0u, 0x80A0u, 0x00A2u, 0x80A2u, 0x00A6u, 0x80A6u, 0x00BFu, 0x80BFu, 0x00C1u, 0x80C1u, 0x00CDu, 0x80CDu, 0x00CFu, 0x80CFu };
static const uint16_t NTSC_UI_STK[34]     = { 0x0092u, 0x8092u, 0x0094u, 0x8094u, 0x0095u, 0x8095u, 0x0096u, 0x8096u, 0x0097u, 0x8097u, 0x009Bu, 0x809Bu, 0x009Fu, 0x809Fu, 0x00A1u, 0x80A1u, 0x00A5u, 0x80A5u, 0x00A8u, 0x80A8u, 0x00A7u, 0x80A7u, 0x00BEu, 0x80BEu, 0x00C0u, 0x80C0u, 0x00CCu, 0x80CCu, 0x00CEu, 0x80CEu, 0x00D1u, 0x80D1u, 0x00D2u, 0x80D2u };
static const uint16_t PAL_UI_STK[22]       = { 0x0093u, 0x8093u, 0x00FEu, 0x80FEu, 0x00FFu, 0x80FFu, 0x009Cu, 0x809Cu, 0x00A0u, 0x80A0u, 0x00A2u, 0x80A2u, 0x00A6u, 0x80A6u, 0x00BFu, 0x80BFu, 0x00C1u, 0x80C1u, 0x00CDu, 0x80CDu, 0x00CFu, 0x80CFu };
static const uint16_t NTSC_WL_NEW[58] = { 0x00EDu, 0x80EDu, 0x00F4u, 0x80F4u, 0x00F1u, 0x80F1u, 0x0021u, 0x8021u, 0x0023u, 0x8023u, 0x0026u, 0x8026u, 0x001Cu, 0x801Cu, 0x001Fu, 0x801Fu, 0x00F9u, 0x80F9u, 0x00FBu, 0x80FBu, 0x00FDu, 0x80FDu, 0x008Bu, 0x808Bu, 0x0092u, 0x8092u, 0x0094u, 0x8094u, 0x0095u, 0x8095u, 0x0096u, 0x8096u, 0x0097u, 0x8097u, 0x009Fu, 0x809Fu, 0x00A5u, 0x80A5u, 0x00A7u, 0x80A7u, 0x009Bu, 0x809Bu, 0x00A1u, 0x80A1u, 0x00A8u, 0x80A8u, 0x00BEu, 0x80BEu, 0x00C0u, 0x80C0u, 0x00CCu, 0x80CCu, 0x00CEu, 0x80CEu, 0x00D1u, 0x80D1u, 0x00D2u, 0x80D2u };
static const uint16_t PAL_WL_NEW[36]   = { 0x00F3u, 0x80F3u, 0x0022u, 0x8022u, 0x0024u, 0x8024u, 0x001Du, 0x801Du, 0x00FAu, 0x80FAu, 0x00FCu, 0x80FCu, 0x008Cu, 0x808Cu, 0x0093u, 0x8093u, 0x00FFu, 0x80FFu, 0x00FEu, 0x80FEu, 0x00A0u, 0x80A0u, 0x00A6u, 0x80A6u, 0x009Cu, 0x809Cu, 0x00A2u, 0x80A2u, 0x00BFu, 0x80BFu, 0x00C1u, 0x80C1u, 0x00CDu, 0x80CDu, 0x00CFu, 0x80CFu };

static uint8_t g_sum(const uint8_t *p, size_t n) {
    uint8_t s = 0; for (size_t i = 1; i + 2 < n; ++i) s = (uint8_t)(s + p[i]); return s;
}
static int prepare(IOUSBInterfaceInterface300 **usb, UInt16 index) {
    IOUSBDevRequestTO r; memset(&r, 0, sizeof r);
    r.bmRequestType = 0x40; r.bRequest = 0; r.wIndex = index;
    r.noDataTimeout = TIMEOUT_MS; r.completionTimeout = TIMEOUT_MS;
    return (*usb)->ControlRequestTO(usb, 0, &r) == kIOReturnSuccess ? 0 : -1;
}
static int read_pipe(IOUSBInterfaceInterface300 **usb, UInt8 pipe, void *data, UInt32 *len) {
    UInt32 cap = *len;
    return ((*usb)->ReadPipeTO(usb, pipe, data, len, TIMEOUT_MS, TIMEOUT_MS) == kIOReturnSuccess && *len <= cap) ? 0 : -1;
}
static int write_pipe(IOUSBInterfaceInterface300 **usb, UInt8 pipe, const void *data, UInt32 len) {
    return (*usb)->WritePipeTO(usb, pipe, (void *)data, len, TIMEOUT_MS, TIMEOUT_MS) == kIOReturnSuccess ? 0 : -1;
}
static int read_half(IOUSBInterfaceInterface300 **usb, UInt8 outP, UInt8 inP, uint32_t addr, uint16_t *out) {
    uint8_t tx[FRAME_BYTES] = {0}, rx[FRAME_BYTES] = {0}, type = 0; UInt32 len = 1;
    tx[0] = 8; tx[2] = 0xFF; tx[3] = 0x9B;
    for (unsigned i = 0; i < 4; ++i) tx[4 + i] = (uint8_t)(addr >> (8 * i));
    tx[14] = g_sum(tx, sizeof tx);
    if (prepare(usb, 0) || write_pipe(usb, outP, tx, sizeof tx)) return -1;
    if (prepare(usb, 1) || read_pipe(usb, inP, &type, &len) || len != 1 || type != 8) return -1;
    if (prepare(usb, 1)) return -1;
    len = sizeof rx;
    if (read_pipe(usb, inP, rx, &len) || len != FRAME_BYTES || rx[3] != 0x36) return -1;
    if (rx[14] != g_sum(rx, sizeof rx) || rx[15] != 0) return -1;
    *out = (uint16_t)(rx[4] | (rx[5] << 8));
    return 0;
}
static int write_half(IOUSBInterfaceInterface300 **usb, UInt8 outP, UInt8 inP, uint32_t addr, uint16_t val) {
    uint8_t tx[FRAME_BYTES] = {0}, rx[FRAME_BYTES] = {0}, type = 0; UInt32 len = 1;
    tx[0] = 8; tx[2] = 0xFF; tx[3] = 0x9C;
    for (unsigned i = 0; i < 4; ++i) tx[4 + i] = (uint8_t)(addr >> (8 * i));
    tx[8] = (uint8_t)val; tx[9] = (uint8_t)(val >> 8);
    tx[14] = g_sum(tx, sizeof tx);
    if (prepare(usb, 0) || write_pipe(usb, outP, tx, sizeof tx)) return -1;
    if (prepare(usb, 1) || read_pipe(usb, inP, &type, &len) || len != 1 || (type != 8 && type != 1)) return -1;
    if (prepare(usb, 1)) return -1;
    len = sizeof rx;
    if (read_pipe(usb, inP, rx, &len) || len != FRAME_BYTES || rx[14] != g_sum(rx, sizeof rx) || rx[15] != 0) return -1;
    return 0;
}
static int read_u32(IOUSBInterfaceInterface300 **usb, UInt8 o, UInt8 i, uint32_t addr, uint32_t *out) {
    uint16_t lo = 0, hi = 0;
    if (read_half(usb, o, i, addr, &lo) || read_half(usb, o, i, addr + 2, &hi)) return -1;
    *out = (uint32_t)lo | ((uint32_t)hi << 16);
    return 0;
}
static int write_u32(IOUSBInterfaceInterface300 **usb, UInt8 o, UInt8 i, uint32_t addr, uint32_t val) {
    uint32_t rb = 0;
    if (write_half(usb, o, i, addr, (uint16_t)(val & 0xFFFFu)) ||
        write_half(usb, o, i, addr + 2, (uint16_t)(val >> 16)) ||
        read_u32(usb, o, i, addr, &rb) || rb != val) return -1;
    return 0;
}
static int write_ui_list(IOUSBInterfaceInterface300 **usb, UInt8 o, UInt8 i, uint32_t base,
                         const uint16_t *qids, size_t n, uint16_t flags) {
    for (size_t k = 0; k < n; ++k) {
        uint32_t a = base + (uint32_t)(k * 8);
        if (write_half(usb, o, i, a + 0, qids[k]) ||
            write_half(usb, o, i, a + 2, flags) ||
            write_half(usb, o, i, a + 4, 0x0000u) ||
            write_half(usb, o, i, a + 6, 0x0000u)) return -1;
    }
    uint32_t end = base + (uint32_t)(n * 8);
    if (write_half(usb, o, i, end + 0, 0xFFFFu) ||
        write_half(usb, o, i, end + 2, 0x0000u) ||
        write_half(usb, o, i, end + 4, 0x0000u) ||
        write_half(usb, o, i, end + 6, 0x0000u)) return -1;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "Usage: %s check | enable --confirm-write | restore --confirm-write\n", argv[0]); return 1; }
    const char *mode = argv[1];
    int confirmed = (argc >= 3 && !strcmp(argv[2], "--confirm-write"));
    if ((!strcmp(mode, "enable") || !strcmp(mode, "restore")) && !confirmed) {
        fprintf(stderr, "Refusing write without --confirm-write\n"); return 1;
    }

    CFMutableDictionaryRef match = IOServiceMatching(kIOUSBDeviceClassName);
    int vid = PANASONIC_VID, pid = SERVICE_PID;
    CFDictionarySetValue(match, CFSTR(kUSBVendorID), CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vid));
    CFDictionarySetValue(match, CFSTR(kUSBProductID), CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &pid));
    io_service_t device = IOServiceGetMatchingService(kIOMainPortDefault, match);
    if (!device) { fprintf(stderr, "No 04DA:2370 device found.\n"); return 1; }

    IOCFPlugInInterface **plug = NULL; SInt32 score = 0;
    IOCreatePlugInInterfaceForService(device, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plug, &score);
    IOUSBDeviceInterface300 **dev = NULL;
    (*plug)->QueryInterface(plug, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID300), (LPVOID *)&dev);

    IOUSBFindInterfaceRequest req = {kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare, kIOUSBFindInterfaceDontCare};
    io_iterator_t it = 0; (*dev)->CreateInterfaceIterator(dev, &req, &it);
    io_service_t ifaceSvc = IOIteratorNext(it); IOObjectRelease(it);
    IOCFPlugInInterface **ifacePlug = NULL;
    IOCreatePlugInInterfaceForService(ifaceSvc, kIOUSBInterfaceUserClientTypeID, kIOCFPlugInInterfaceID, &ifacePlug, &score);
    IOUSBInterfaceInterface300 **usb = NULL;
    (*ifacePlug)->QueryInterface(ifacePlug, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID300), (LPVOID *)&usb);
    if ((*usb)->USBInterfaceOpen(usb) != kIOReturnSuccess) { fprintf(stderr, "USBInterfaceOpen failed.\n"); return 1; }

    UInt8 nEp = 0, outP = 0, inP = 0; (*usb)->GetNumEndpoints(usb, &nEp);
    for (UInt8 i = 1; i <= nEp; ++i) {
        UInt8 dir = 0, num = 0, tt = 0, iv = 0; UInt16 mps = 0;
        (*usb)->GetPipeProperties(usb, i, &dir, &num, &tt, &mps, &iv);
        if (tt == kUSBBulk && dir == kUSBOut && !outP) outP = i;
        if (tt == kUSBBulk && dir == kUSBIn  && !inP)  inP  = i;
    }

    uint32_t p_nwl=0, p_pwl=0, p_nui=0, p_pui=0;
    uint16_t live0=0, hdmi1=0, hdmi2=0, unlim=0;
    int rc = 0;
    if (read_u32(usb, outP, inP, PTR_NTSC_WL, &p_nwl) ||
        read_u32(usb, outP, inP, PTR_PAL_WL,  &p_pwl) ||
        read_u32(usb, outP, inP, PTR_NTSC_UI, &p_nui) ||
        read_u32(usb, outP, inP, PTR_PAL_UI,  &p_pui) ||
        read_half(usb, outP, inP, LIVE_UI_BASE, &live0) ||
        read_half(usb, outP, inP, 0x866ACC9Cu, &hdmi1) ||
        read_half(usb, outP, inP, 0x8664425Cu, &hdmi2) ||
        read_half(usb, outP, inP, 0x868261B6u, &unlim)) {
        fprintf(stderr, "Signature read failed.\n"); rc = 1;
    } else if ((p_nwl != 0x06F15E30u && p_nwl != NEW_NTSC_WL) ||
               (p_pwl != 0x06F15E9Au && p_pwl != NEW_PAL_WL) ||
               (p_nui != 0x08A68B6Cu && p_nui != NEW_NTSC_UI) ||
               (p_pui != 0x08A68C84u && p_pui != NEW_PAL_UI)) {
        fprintf(stderr, "SIG MISMATCH: nwl=%08X pwl=%08X nui=%08X pui=%08X\n", p_nwl, p_pwl, p_nui, p_pui); rc = 1;
    } else if (!strcmp(mode, "check")) {
        printf("OK: nwl=%08X pwl=%08X nui=%08X pui=%08X live0=%04X hdmi=%04X/%04X unlim=%04X\n",
               p_nwl, p_pwl, p_nui, p_pui, live0, hdmi1, hdmi2, unlim);
    } else if (!strcmp(mode, "enable")) {
        int err = 0;
        err |= write_ui_list(usb, outP, inP, NEW_NTSC_UI, NTSC_UI_NEW, 40, 0x0101u);
        err |= write_ui_list(usb, outP, inP, NEW_PAL_UI,  PAL_UI_NEW,  24,  0x0101u);
        for (size_t k = 0; k < 58 && !err; ++k)
            err |= write_half(usb, outP, inP, NEW_NTSC_WL + (uint32_t)(k * 2), NTSC_WL_NEW[k]);
        err |= write_half(usb, outP, inP, NEW_NTSC_WL + 116u, 0xFFFFu);
        for (size_t k = 0; k < 36 && !err; ++k)
            err |= write_half(usb, outP, inP, NEW_PAL_WL + (uint32_t)(k * 2), PAL_WL_NEW[k]);
        err |= write_half(usb, outP, inP, NEW_PAL_WL + 72u, 0xFFFFu);

        err |= write_u32(usb, outP, inP, PTR_NTSC_WL, NEW_NTSC_WL);
        err |= write_u32(usb, outP, inP, PTR_PAL_WL,  NEW_PAL_WL);
        err |= write_u32(usb, outP, inP, PTR_NTSC_UI, NEW_NTSC_UI);
        err |= write_u32(usb, outP, inP, PTR_PAL_UI,  NEW_PAL_UI);
        err |= write_half(usb, outP, inP, 0x866ACC9Cu, 0xBF00u);
        err |= write_half(usb, outP, inP, 0x8664425Cu, 0xBF00u);
        err |= write_half(usb, outP, inP, 0x868261B6u, 0x2000u);

        /* Live-refresh UI table immediately if currently in NTSC or PAL MOV */
        if (!err && (live0 == 0x0092u || live0 == 0x00EDu)) {
            err |= write_ui_list(usb, outP, inP, LIVE_UI_BASE, NTSC_UI_NEW, 40, 0x0104u);
        } else if (!err && (live0 == 0x0093u || live0 == 0x00F3u)) {
            err |= write_ui_list(usb, outP, inP, LIVE_UI_BASE, PAL_UI_NEW,  24,  0x0104u);
        }
        if (err) { fprintf(stderr, "enable failed.\n"); rc = 1; }
        else printf("OK: v1.5 armed. Unlocked modes appear at the top of MOV Rec Quality. Fixed recording timers removed.\n");
    } else if (!strcmp(mode, "restore")) {
        int err = 0;
        err |= write_u32(usb, outP, inP, PTR_NTSC_WL, 0x06F15E30u);
        err |= write_u32(usb, outP, inP, PTR_PAL_WL,  0x06F15E9Au);
        err |= write_u32(usb, outP, inP, PTR_NTSC_UI, 0x08A68B6Cu);
        err |= write_u32(usb, outP, inP, PTR_PAL_UI,  0x08A68C84u);
        err |= write_half(usb, outP, inP, 0x866ACC9Cu, 0xD1E7u);
        err |= write_half(usb, outP, inP, 0x8664425Cu, 0xD17Bu);
        err |= write_half(usb, outP, inP, 0x868261B6u, 0x4628u);
        if (!err && live0 == 0x00EDu)
            err |= write_ui_list(usb, outP, inP, LIVE_UI_BASE, NTSC_UI_STK, 34, 0x0104u);
        else if (!err && live0 == 0x00F3u)
            err |= write_ui_list(usb, outP, inP, LIVE_UI_BASE, PAL_UI_STK,  22,  0x0104u);
        if (err) { fprintf(stderr, "restore failed.\n"); rc = 1; }
        else printf("OK: Restored 100%% stock.\n");
    }

    (*usb)->USBInterfaceClose(usb);
    (*usb)->Release(usb); (*ifacePlug)->Release(ifacePlug);
    (*dev)->Release(dev); (*plug)->Release(plug);
    IOObjectRelease(ifaceSvc); IOObjectRelease(device);
    return rc;
}
