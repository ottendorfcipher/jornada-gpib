/* A behavioural simulator of the TNT4882C in one-chip mode with one instrument on the bus.
 *
 * Models the register interface the driver uses (Turbo488 FIFO engine, 7210 addressing and
 * auxiliary commands, status registers) and a simple device that can listen, talk, answer
 * serial polls and record the commands it received. Timing is virtual: delay_us and yield
 * advance a counter the driver reads through now_ms, so timeouts are deterministic.
 *
 * Not modelled: parallel poll, DMA, 9914 mode, interrupts, 16-bit FIFO access.
 */
#ifndef SIM_TNT4882_H
#define SIM_TNT4882_H

#include <stdint.h>
#include "gpib/tnt4882.h"

#define SIM_FIFO_DEPTH 16
#define SIM_RX_MAX 4096
#define SIM_CMD_MAX 256

typedef struct sim_device {
    int present;
    int pad;
    int listening, talking;
    int spe, spe_sent;
    uint8_t stb;
    int srq;
    uint8_t rx[SIM_RX_MAX];
    uint8_t rx_eoi[SIM_RX_MAX];
    unsigned rx_len;
    const uint8_t *tx;
    unsigned tx_len, tx_pos;
    int tx_eoi_last;
    uint8_t commands[SIM_CMD_MAX];
    unsigned ncmd;
    int cleared, triggered, local, lockout;
} sim_device;

typedef struct sim_tnt {
    /* 7210 core */
    uint8_t imr1, imr2, isr1, isr2, spmr, admr, adr0, adr1, eosr, cptr;
    uint8_t auxra, auxrb, auxrg, auxri, auxre, ppr, icr;
    int pon;
    int sc, cic, atn, ren, ifc;
    int talker, listener;
    int holdoff;
    uint8_t sasr;
    int mode7210, one_chip;
    /* Turbo488 side */
    uint8_t cfg, hssel, accwr, imr0, isr0, imr3, keyreg, intrt, ccr, timer;
    int32_t cnt;
    int cnt32;
    uint8_t fifo[SIM_FIFO_DEPTH];
    unsigned fifo_len;
    int go, stop, halt, gsync;
    /* bus */
    sim_device dev;
    uint32_t time_us;
    unsigned accesses;
    int trace;
} sim_tnt;

void sim_reset(sim_tnt *s);
void sim_device_present(sim_tnt *s, int pad, int present);
void sim_device_will_talk(sim_tnt *s, const uint8_t *data, unsigned len, int eoi_on_last);
const tnt_io *sim_io(sim_tnt *s);

/* Register access entry points (also used directly by tests). */
uint8_t sim_read(void *ctx, unsigned off);
void sim_write(void *ctx, unsigned off, uint8_t val);

#endif
