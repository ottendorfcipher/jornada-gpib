/* Windows CE PC Card PnP identifier construction. See include/gpib/pnpid.h. */
#include "gpib/pnpid.h"

#define CISTPL_DEVICE_CODE        0x01
#define CISTPL_VERS_1_CODE        0x15
#define CISTPL_CONFIG_CODE        0x1A
#define CISTPL_CFTABLE_ENTRY_CODE 0x1B
#define CISTPL_MANFID_CODE        0x20

/* Windows computes the card CRC with a nibble-table CRC-16 (reflected polynomial 0x8005,
 * init 0) whose high-nibble table carries a historical typo: the entry for 0xF0 is 0x4600
 * where the polynomial gives 0x4400. Windows 95 shipped it that way and Windows CE copied
 * the algorithm, so every PCMCIA identifier they generate depends on the typo. We reproduce
 * it exactly; the identifier this yields for the PCMCIA-GPIB was confirmed on the device. */
#define CRC_POLY_REFLECTED 0xA001
#define CRC_HIGH_NIBBLE_TYPO_INDEX 15
#define CRC_HIGH_NIBBLE_TYPO_VALUE 0x4600

static uint16_t crc_table_entry(unsigned index)
{
    uint16_t c = (uint16_t)index;
    int bit;
    for (bit = 0; bit < 8; bit++) {
        c = (c & 1) ? (uint16_t)((c >> 1) ^ CRC_POLY_REFLECTED) : (uint16_t)(c >> 1);
    }
    return c;
}

uint16_t pnpid_crc16(uint16_t crc, const uint8_t *data, unsigned len)
{
    unsigned i;
    for (i = 0; i < len; i++) {
        unsigned tmp = (unsigned)(data[i] ^ (crc & 0xFF));
        unsigned hi = tmp >> 4;
        uint16_t high = hi == CRC_HIGH_NIBBLE_TYPO_INDEX ? CRC_HIGH_NIBBLE_TYPO_VALUE
                                                          : crc_table_entry(hi << 4);
        crc = (uint16_t)((crc >> 8) ^ crc_table_entry(tmp & 0x0F) ^ high);
    }
    return crc;
}

void pnpid_begin(pnpid_builder *b)
{
    b->crc = 0;
    b->manufacturer[0] = 0;
    b->product[0] = 0;
    b->have_vers1 = 0;
}

static unsigned copy_filtered(char *dst, unsigned cap, const uint8_t *src, unsigned len, unsigned *consumed)
{
    unsigned i;
    unsigned n = 0;
    for (i = 0; i < len && src[i] != 0; i++) {
        uint8_t c = src[i];
        if (c == ' ' || c == ',') {
            c = '_';
        } else if (c <= 0x20 || c >= 0x7F) {
            continue;
        }
        if (n + 1 < cap) {
            dst[n++] = (char)c;
        }
    }
    dst[n] = 0;
    *consumed = i < len ? i + 1 : len;   /* include the terminating NUL when present */
    return n;
}

static void add_vers1(pnpid_builder *b, const uint8_t *data, unsigned len)
{
    unsigned used = 2;
    unsigned consumed = 0;
    unsigned crc_len;
    if (len <= 4) {
        return;
    }
    copy_filtered(b->manufacturer, sizeof b->manufacturer, data + used, len - used, &consumed);
    used += consumed;
    if (used < len) {
        copy_filtered(b->product, sizeof b->product, data + used, len - used, &consumed);
        used += consumed;
    }
    crc_len = used <= len ? used : len;
    b->crc = pnpid_crc16(b->crc, data, crc_len);
    b->have_vers1 = 1;
}

void pnpid_add_tuple(pnpid_builder *b, uint8_t code, const uint8_t *data, unsigned len)
{
    switch (code) {
    case CISTPL_VERS_1_CODE:
        if (!b->have_vers1) {
            add_vers1(b, data, len);
        } else {
            b->crc = pnpid_crc16(b->crc, data, len);
        }
        break;
    case CISTPL_DEVICE_CODE:
    case CISTPL_CONFIG_CODE:
    case CISTPL_CFTABLE_ENTRY_CODE:
    case CISTPL_MANFID_CODE:
        b->crc = pnpid_crc16(b->crc, data, len);
        break;
    default:
        break;
    }
}

static unsigned append(char *out, unsigned cap, unsigned n, const char *s)
{
    while (*s != 0 && n + 1 < cap) {
        out[n++] = *s++;
    }
    return n;
}

unsigned pnpid_finish(const pnpid_builder *b, char *out, unsigned cap)
{
    static const char hex[] = "0123456789ABCDEF";
    char crc[5];
    unsigned n = 0;
    if (cap < 8) {
        return 0;
    }
    crc[0] = hex[(b->crc >> 12) & 15];
    crc[1] = hex[(b->crc >> 8) & 15];
    crc[2] = hex[(b->crc >> 4) & 15];
    crc[3] = hex[b->crc & 15];
    crc[4] = 0;
    if (b->manufacturer[0] == 0) {
        n = append(out, cap, n, "UNKNOWN_MANUFACTURER");
    } else {
        n = append(out, cap, n, b->manufacturer);
        if (b->product[0] != 0) {
            n = append(out, cap, n, "-");
            n = append(out, cap, n, b->product);
        }
    }
    n = append(out, cap, n, "-");
    n = append(out, cap, n, crc);
    out[n] = 0;
    return n;
}
