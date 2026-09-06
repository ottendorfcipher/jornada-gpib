/* TNT4882C behavioural simulator. See sim_tnt4882.h. */
#include "sim_tnt4882.h"

#include <stdio.h>
#include <string.h>

#define ADDR(b) ((b) & 0x1F)
#define IS_LAD(b) (((b) & 0x60) == 0x20 && ADDR(b) != 0x1F)
#define IS_TAD(b) (((b) & 0x60) == 0x40 && ADDR(b) != 0x1F)
#define CMD_UNL 0x3F
#define CMD_UNT 0x5F

static void core_reset(sim_tnt *s)
{
    s->imr1 = s->imr2 = s->isr1 = s->isr2 = 0;
    s->spmr = s->admr = s->adr0 = s->adr1 = s->eosr = 0;
    s->auxra = s->auxrb = s->auxrg = s->auxri = s->auxre = s->ppr = 0;
    s->pon = 1;
    s->cic = s->atn = 0;
    s->talker = s->listener = 0;
    s->holdoff = 0;
    s->sasr = 0;
}

static void turbo_reset(sim_tnt *s)
{
    s->cfg = 0;
    s->hssel = 0;
    s->imr3 = 0;
    s->fifo_len = 0;
    s->go = 0;
    s->stop = s->halt = s->gsync = 1;
    s->cnt = -1;                 /* 16-bit mode: CNT0/1 = 0xFFFF, CNT2/3 = 0xFF */
    s->cnt32 = 0;
    s->isr0 = TNT_ISR0_SYNC;
}

void sim_reset(sim_tnt *s)
{
    memset(s, 0, sizeof *s);
    s->mode7210 = 1;
    s->imr0 = TNT_IMR0_ALWAYS;
    core_reset(s);
    turbo_reset(s);
    s->sc = 0;
    s->dev.pad = 5;
}

void sim_device_present(sim_tnt *s, int pad, int present)
{
    s->dev.present = present;
    s->dev.pad = pad;
}

void sim_device_will_talk(sim_tnt *s, const uint8_t *data, unsigned len, int eoi_on_last)
{
    s->dev.tx = data;
    s->dev.tx_len = len;
    s->dev.tx_pos = 0;
    s->dev.tx_eoi_last = eoi_on_last;
}

static int fifo_push(sim_tnt *s, uint8_t b)
{
    if (s->fifo_len >= SIM_FIFO_DEPTH) {
        return 0;
    }
    s->fifo[s->fifo_len++] = b;
    return 1;
}

static uint8_t fifo_pop(sim_tnt *s)
{
    uint8_t b;
    if (s->fifo_len == 0) {
        return 0xFF;
    }
    b = s->fifo[0];
    memmove(s->fifo, s->fifo + 1, --s->fifo_len);
    return b;
}

static void count_byte(sim_tnt *s)
{
    if (s->cnt32) {
        s->cnt++;
    } else {
        s->cnt = (int32_t)(int16_t)((int16_t)s->cnt + 1);
    }
}

static int count_done(const sim_tnt *s)
{
    return s->cnt == 0;
}

static void interface_clear(sim_tnt *s)
{
    s->dev.listening = s->dev.talking = s->dev.spe = s->dev.spe_sent = 0;
    s->talker = s->listener = 0;
    s->cic = 1;
    s->atn = 1;
}

static void device_command(sim_tnt *s, uint8_t b)
{
    sim_device *d = &s->dev;
    if (d->ncmd < SIM_CMD_MAX) {
        d->commands[d->ncmd++] = b;
    }
    if (b == CMD_UNL) {
        d->listening = 0;
    } else if (b == CMD_UNT) {
        d->talking = 0;
    } else if (IS_LAD(b)) {
        if (ADDR(b) == d->pad) {
            d->listening = 1;
        }
    } else if (IS_TAD(b)) {
        d->talking = ADDR(b) == d->pad;
        if (d->talking && d->spe) {
            d->spe_sent = 0;
        }
    } else if (b == 0x18) {
        d->spe = 1;
    } else if (b == 0x19) {
        d->spe = 0;
        d->spe_sent = 0;
    } else if (b == 0x14) {
        d->cleared++;
    } else if (b == 0x04 && d->listening) {
        d->cleared++;
    } else if (b == 0x08 && d->listening) {
        d->triggered++;
    } else if (b == 0x01 && d->listening) {
        d->local = 1;
    } else if (b == 0x11) {
        d->lockout = 1;
    }
}

static void self_command(sim_tnt *s, uint8_t b)
{
    uint8_t mypad = ADDR(s->adr0);
    if (b == CMD_UNL) {
        s->listener = 0;
    } else if (b == CMD_UNT) {
        s->talker = 0;
    } else if (IS_LAD(b)) {
        if (ADDR(b) == mypad) {
            s->listener = 1;
        }
    } else if (IS_TAD(b)) {
        s->talker = ADDR(b) == mypad;
    }
}

static int eos_match(const sim_tnt *s, uint8_t b)
{
    if (!(s->auxra & HR_REOS)) {
        return 0;
    }
    if (s->auxra & HR_BIN) {
        return b == s->eosr;
    }
    return (b & 0x7F) == (s->eosr & 0x7F);
}

static void end_received(sim_tnt *s)
{
    s->isr1 |= HR_END;
    if ((s->auxra & (HR_HLDA | HR_HLDE)) == HR_HLDE) {
        s->holdoff = 1;
        s->sasr = TNT_AEHS;
    }
}

/* Move bytes between the FIFO and the bus as far as the current state allows. */
static void engine(sim_tnt *s)
{
    if (!s->go || s->halt) {
        return;
    }
    if (!(s->cfg & TNT_IN)) {
        int command = (s->cfg & TNT_COMMAND) != 0;
        while (s->fifo_len > 0) {
            uint8_t b;
            if (command ? !(s->cic && s->atn) : !(s->talker && !s->atn)) {
                return;
            }
            b = s->fifo[0];
            if (command) {
                if (!s->dev.present) {
                    s->isr1 |= HR_ERR;
                    s->halt = 1;
                    return;
                }
                fifo_pop(s);
                device_command(s, b);
                self_command(s, b);
            } else {
                if (!(s->dev.present && s->dev.listening)) {
                    s->isr1 |= HR_ERR;
                    s->halt = 1;
                    return;
                }
                fifo_pop(s);
                count_byte(s);
                if (s->dev.rx_len < SIM_RX_MAX) {
                    s->dev.rx[s->dev.rx_len] = b;
                    s->dev.rx_eoi[s->dev.rx_len] = count_done(s) && (s->cfg & TNT_CCEN);
                    s->dev.rx_len++;
                }
                if (count_done(s)) {
                    s->stop = 1;
                    s->gsync = 1;
                    s->go = 0;
                }
                continue;
            }
            count_byte(s);
            if (count_done(s)) {
                s->stop = 1;
                s->gsync = 1;
                s->go = 0;
            }
        }
        return;
    }
    /* GPIB read: bytes flow from the device into the FIFO. */
    while (!s->holdoff && s->fifo_len < SIM_FIFO_DEPTH && s->listener && !s->atn &&
           s->dev.present && s->dev.talking) {
        uint8_t b;
        int eoi = 0;
        if (s->dev.spe) {
            if (s->dev.spe_sent) {
                return;
            }
            b = s->dev.stb;
            s->dev.spe_sent = 1;
        } else {
            if (s->dev.tx_pos >= s->dev.tx_len) {
                return;
            }
            b = s->dev.tx[s->dev.tx_pos++];
            eoi = s->dev.tx_eoi_last && s->dev.tx_pos == s->dev.tx_len;
        }
        fifo_push(s, b);
        count_byte(s);
        if (eoi || eos_match(s, b)) {
            end_received(s);
        }
        if ((s->auxra & (HR_HLDA | HR_HLDE)) == HR_HLDA) {
            s->holdoff = 1;
            s->sasr = TNT_ANHS1;
        }
        if (count_done(s)) {
            s->stop = 1;
            s->go = 0;
            if (s->cfg & TNT_CCEN) {
                s->holdoff = 1;
                s->sasr = TNT_ANHS1;
            }
        }
    }
}

static void auxiliary(sim_tnt *s, uint8_t v)
{
    switch (v & 0xE0) {
    case 0x00:
        switch (v) {
        case AUX_PON: s->pon = 0; break;
        case AUX_CR: core_reset(s); break;
        case AUX_FH: s->holdoff = 0; s->sasr = 0; break;
        case AUX_GTS: if (s->cic) { s->atn = 0; } break;
        case AUX_TCA: case AUX_TCS: if (s->cic) { s->atn = 1; } break;
        case AUX_DSC: s->sc = 0; break;
        case AUX_CIFC: s->ifc = 0; break;
        case AUX_CREN: if (s->sc) { s->ren = 0; } break;
        case AUX_SIFC: if (s->sc) { s->ifc = 1; interface_clear(s); } break;
        case AUX_SREN: if (s->sc) { s->ren = 1; } break;
        default: break;
        }
        break;
    case 0x20: s->icr = v; break;
    case 0x40:
        if (v == AUX_HLDI) {
            s->holdoff = 1;
            s->sasr = TNT_ANHS2;
        } else if (v == AUX_PAGEIN || v == AUX_CLEAR_END) {
            /* not modelled */
        } else {
            s->auxrg = v;
        }
        break;
    case 0x60: s->ppr = v; break;
    case 0x80: s->auxra = v; break;
    case 0xA0: s->auxrb = v; break;
    case 0xC0: s->auxre = v; break;
    default:   s->auxri = v; break;
    }
}

static void command_register(sim_tnt *s, uint8_t v)
{
    switch (v) {
    case TNT_SOFT_RESET: turbo_reset(s); break;
    case TNT_RESET_FIFO: s->fifo_len = 0; break;
    case TNT_GO:
        s->go = 1;
        s->halt = 0;
        s->stop = 0;
        s->gsync = 0;
        engine(s);
        break;
    case TNT_STOP:
        s->stop = 1;
        s->halt = 1;
        s->go = 0;
        s->gsync = 1;
        break;
    case TNT_SETSC: s->sc = 1; break;
    case TNT_CLRSC: s->sc = 0; break;
    default: break;
    }
}

static uint8_t sts1(const sim_tnt *s)
{
    int done;
    if (s->cfg & TNT_IN) {
        done = !s->go && s->fifo_len == 0;
    } else {
        done = !s->go || (count_done(s) && s->fifo_len == 0);
    }
    return (uint8_t)((done ? TNT_S_DONE : 0) | (s->sc ? TNT_S_SC : 0) |
                     ((s->cfg & TNT_IN) ? TNT_S_IN : 0) | (s->stop ? TNT_S_STOP : 0) |
                     (s->halt ? TNT_S_HALT : 0) | (s->gsync ? TNT_S_GSYNC : 0));
}

static uint8_t sts2(const sim_tnt *s)
{
    uint8_t v = 0x90;
    if (s->fifo_len < SIM_FIFO_DEPTH) {
        v |= TNT_AFFN | TNT_BFFN;
    }
    if (s->fifo_len > 0) {
        v |= TNT_AEFN | TNT_BEFN;
    }
    return v;
}

static uint8_t isr3(const sim_tnt *s)
{
    return (uint8_t)(((sts1(s) & TNT_S_DONE) ? TNT_HR_DONE : 0) |
                     (s->fifo_len > 0 ? TNT_HR_NEF : 0) |
                     (s->fifo_len < SIM_FIFO_DEPTH ? TNT_HR_NFF : 0) |
                     (s->stop ? TNT_HR_STOP : 0));
}

uint8_t sim_read(void *ctx, unsigned off)
{
    sim_tnt *s = ctx;
    uint8_t v = 0xFF;
    s->accesses++;
    engine(s);
    switch (off) {
    case TNT_DIR: v = 0; break;
    case TNT_ISR1: v = s->isr1; if (!(s->auxri & HR_SISB)) { s->isr1 = 0; } break;
    case TNT_ISR2: v = s->isr2; if (!(s->auxri & HR_SISB)) { s->isr2 = 0; } break;
    case TNT_SPSR: v = s->spmr; break;
    case TNT_ADSR:
        v = (uint8_t)((s->cic ? HR_CIC : 0) | (s->atn ? 0 : HR_NATN) |
                      (s->talker ? HR_TA : 0) | (s->listener ? HR_LA : 0));
        break;
    case TNT_CPTR: v = s->cptr; break;
    case TNT_ADR0: v = s->adr0; break;
    case TNT_ADR1: v = s->adr1; break;
    case TNT_CNT2: v = (uint8_t)((uint32_t)s->cnt >> 16); break;
    case TNT_CNT3: v = (uint8_t)((uint32_t)s->cnt >> 24); break;
    case TNT_STS1: v = sts1(s); break;
    case TNT_CNT0: v = (uint8_t)s->cnt; break;
    case TNT_CNT1: v = (uint8_t)((uint32_t)s->cnt >> 8); break;
    case TNT_CSR: v = TNT_CSR_VERSION_TNT4882; break;
    case TNT_FIFOB: v = fifo_pop(s); engine(s); break;
    case TNT_ISR3: v = isr3(s); break;
    case TNT_SASR: v = s->holdoff ? s->sasr : 0; break;
    case TNT_STS2: v = sts2(s); break;
    case TNT_ISR0: v = s->isr0; s->isr0 = 0; break;
    case TNT_BSR:
        v = (uint8_t)((s->atn ? TNT_BSR_ATN : 0) | (s->ren ? TNT_BSR_REN : 0) |
                      (s->ifc ? TNT_BSR_IFC : 0) | (s->dev.srq ? TNT_BSR_SRQ : 0) |
                      (s->holdoff ? TNT_BSR_NRFD : 0));
        break;
    default: break;
    }
    if (s->trace) {
        fprintf(stderr, "  R[%02x] -> %02x\n", off, v);
    }
    return v;
}

void sim_write(void *ctx, unsigned off, uint8_t val)
{
    sim_tnt *s = ctx;
    s->accesses++;
    if (s->trace) {
        fprintf(stderr, "  W[%02x] <- %02x\n", off, val);
    }
    switch (off) {
    case TNT_CDOR: break;                       /* unused in one-chip mode */
    case TNT_IMR1: s->imr1 = val; break;
    case TNT_IMR2: s->imr2 = val; break;
    case TNT_SPMR: s->spmr = val; break;
    case TNT_ADMR: s->admr = val; break;
    case TNT_AUXMR: auxiliary(s, val); break;
    case TNT_ADR:
        if (val & HR_ARS) {
            s->adr1 = val;
        } else {
            s->adr0 = val;
        }
        break;
    case TNT_EOSR: s->eosr = val; break;
    case TNT_ACCWR: s->accwr = val; break;
    case TNT_INTRT: s->intrt = val; break;
    case TNT_CNT2: s->cnt = (int32_t)(((uint32_t)s->cnt & 0xFF00FFFFu) | ((uint32_t)val << 16)); s->cnt32 = 1; break;
    case TNT_CNT3: s->cnt = (int32_t)(((uint32_t)s->cnt & 0x00FFFFFFu) | ((uint32_t)val << 24)); s->cnt32 = 1; break;
    case TNT_HSSEL: s->hssel = val; s->one_chip = (val & TNT_ONE_CHIP) != 0; break;
    case TNT_CFG: s->cfg = val; break;
    case TNT_IMR3: s->imr3 = val; break;
    case TNT_CNT0: s->cnt = (int32_t)(((uint32_t)s->cnt & 0xFFFFFF00u) | val); break;
    case TNT_CNT1: s->cnt = (int32_t)(((uint32_t)s->cnt & 0xFFFF00FFu) | ((uint32_t)val << 8)); break;
    case TNT_KEYREG: s->keyreg = val; break;
    case TNT_FIFOB: fifo_push(s, val); engine(s); break;
    case TNT_CCR: s->ccr = val; break;
    case TNT_CMDR: command_register(s, val); break;
    case TNT_IMR0: s->imr0 = val; break;
    case TNT_TIMER: s->timer = val; break;
    case TNT_BCR: break;
    default: break;
    }
}

static void sim_delay_us(void *ctx, unsigned us)
{
    sim_tnt *s = ctx;
    s->time_us += us;
}

static uint32_t sim_now_ms(void *ctx)
{
    sim_tnt *s = ctx;
    return s->time_us / 1000u;
}

static void sim_yield(void *ctx)
{
    sim_tnt *s = ctx;
    s->time_us += 1000;
    engine(s);
}

const tnt_io *sim_io(sim_tnt *s)
{
    static tnt_io io;
    io.ctx = s;
    io.read8 = sim_read;
    io.write8 = sim_write;
    io.delay_us = sim_delay_us;
    io.now_ms = sim_now_ms;
    io.yield = sim_yield;
    return &io;
}
