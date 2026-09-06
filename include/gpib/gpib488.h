/* Controller-level IEEE 488 operations on top of the TNT4882C chip driver.
 *
 * All functions assume this interface is the system controller and controller in charge
 * (tnt_init + gpib_interface_clear). Device addresses are primary (0..30) plus an optional
 * secondary address (0..30, or -1 for none).
 */
#ifndef GPIB_GPIB488_H
#define GPIB_GPIB488_H

#include "gpib/tnt4882.h"

/* Multiline command bytes */
#define GPIB_GTL 0x01
#define GPIB_SDC 0x04
#define GPIB_PPC 0x05
#define GPIB_GET 0x08
#define GPIB_TCT 0x09
#define GPIB_LLO 0x11
#define GPIB_DCL 0x14
#define GPIB_PPU 0x15
#define GPIB_SPE 0x18
#define GPIB_SPD 0x19
#define GPIB_UNL 0x3F
#define GPIB_UNT 0x5F
#define GPIB_LAD(pad) ((uint8_t)(0x20 | ((pad) & 0x1F)))
#define GPIB_TAD(pad) ((uint8_t)(0x40 | ((pad) & 0x1F)))
#define GPIB_SAD(sad) ((uint8_t)(0x60 | ((sad) & 0x1F)))

#define GPIB_MAX_CMD 8

typedef struct gpib_ctl {
    tnt_chip *chip;
} gpib_ctl;

void gpib_bind(gpib_ctl *g, tnt_chip *chip);

int gpib_interface_clear(gpib_ctl *g);
int gpib_remote_enable(gpib_ctl *g, int on);
int gpib_send_commands(gpib_ctl *g, const uint8_t *cmd, unsigned n);

/* Address a device as listener with us talking, then write; EOI with the last byte if eoi. */
int gpib_write(gpib_ctl *g, int pad, int sad, const uint8_t *buf, unsigned n, int eoi, unsigned *sent);
/* Address a device as talker with us listening, then read until EOI/EOS, cap or timeout. */
int gpib_read(gpib_ctl *g, int pad, int sad, uint8_t *buf, unsigned cap, unsigned *got, int *end);

int gpib_clear(gpib_ctl *g, int pad, int sad);        /* SDC, or DCL for every device when pad < 0 */
int gpib_trigger(gpib_ctl *g, int pad, int sad);      /* GET */
int gpib_local(gpib_ctl *g, int pad, int sad);        /* GTL */
int gpib_local_lockout(gpib_ctl *g);                  /* LLO */
int gpib_serial_poll(gpib_ctl *g, int pad, int sad, uint8_t *status);
int gpib_pass_control(gpib_ctl *g, int pad);          /* TCT: hand the bus to another controller */

uint8_t gpib_lines(gpib_ctl *g);
int gpib_srq_asserted(gpib_ctl *g);

/* Build the addressing prefix [UNL, TAD/LAD dev, SAD?, MLA/MTA us]; returns its length. */
unsigned gpib_build_address(const gpib_ctl *g, int pad, int sad, int device_talks, uint8_t *out);

#endif /* GPIB_GPIB488_H */
