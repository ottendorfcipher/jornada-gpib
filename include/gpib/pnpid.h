/* Windows CE PC Card Plug and Play identifier: "<Manufacturer>-<Product>-<CRC4>".
 *
 * This is the key name device.exe looks for under HKLM\Drivers\PCMCIA. The string comes from
 * the card's CISTPL_VERS_1 manufacturer and product strings (spaces and commas replaced by
 * underscores, other characters outside 0x21..0x7E dropped) and a four-digit CRC-16 over the
 * data bytes of the CISTPL_DEVICE, VERS_1, CONFIG, CFTABLE_ENTRY and MANFID tuples in CIS
 * order. Portable C, shared by the device tools and host tests.
 */
#ifndef GPIB_PNPID_H
#define GPIB_PNPID_H

#include <stdint.h>

#define PNPID_MAX 128
#define PNPID_MANF_MAX 115
#define PNPID_PROD_MAX 64

typedef struct pnpid_builder {
    uint16_t crc;
    char manufacturer[PNPID_MANF_MAX + 1];
    char product[PNPID_PROD_MAX + 1];
    int have_vers1;
} pnpid_builder;

void pnpid_begin(pnpid_builder *b);
/* Feed one tuple (code + data bytes, without the code/link header) in CIS order. */
void pnpid_add_tuple(pnpid_builder *b, uint8_t code, const uint8_t *data, unsigned len);
/* Render the identifier; returns its length (0 if out is too small). */
unsigned pnpid_finish(const pnpid_builder *b, char *out, unsigned cap);

/* CRC-16/ARC (polynomial 0x8005 reflected, init 0), the CE "Windows 95" card CRC. */
uint16_t pnpid_crc16(uint16_t crc, const uint8_t *data, unsigned len);

#endif
