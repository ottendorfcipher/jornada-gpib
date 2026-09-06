/* TNT4882C chip driver, portable core. See include/gpib/tnt4882.h. */
#include "gpib/tnt4882.h"

#define R(c, off) ((c)->io->read8((c)->io->ctx, (off)))
#define W(c, off, v) ((c)->io->write8((c)->io->ctx, (off), (uint8_t)(v)))

#define ADDRESS_MASK 0x1F
#define TCA_SPIN_US 100
#define IFC_PULSE_US 200

static void delay_us(tnt_chip *c, unsigned us)
{
    c->io->delay_us(c->io->ctx, us);
}

static uint32_t now_ms(tnt_chip *c)
{
    return c->io->now_ms(c->io->ctx);
}

static void yield(tnt_chip *c)
{
    c->io->yield(c->io->ctx);
}

/* Consecutive AUXMR and CMDR writes must be at least four chip clocks apart. */
static void auxmr(tnt_chip *c, uint8_t v)
{
    delay_us(c, 1);
    W(c, TNT_AUXMR, v);
}

static void cmdr(tnt_chip *c, uint8_t v)
{
    W(c, TNT_CMDR, v);
    delay_us(c, 1);
}

/* Reading ISR1/ISR2 clears them, so everything seen is accumulated until consumed. */
static void poll_status(tnt_chip *c)
{
    c->isr1 |= R(c, TNT_ISR1);
    c->isr2 |= R(c, TNT_ISR2);
}

typedef struct deadline {
    uint32_t at;
    int enabled;
} deadline;

static deadline deadline_start(tnt_chip *c)
{
    deadline d;
    d.enabled = c->timeout_ms != 0;
    d.at = now_ms(c) + c->timeout_ms;
    return d;
}

static int deadline_expired(tnt_chip *c, const deadline *d)
{
    return d->enabled && (int32_t)(now_ms(c) - d->at) >= 0;
}

static void set_count(tnt_chip *c, uint32_t n)
{
    uint32_t v = (uint32_t)(-(int32_t)n);
    W(c, TNT_CNT0, v);
    W(c, TNT_CNT1, v >> 8);
    W(c, TNT_CNT2, v >> 16);
    W(c, TNT_CNT3, v >> 24);
}

static uint32_t remaining_count(tnt_chip *c)
{
    uint32_t v = (uint32_t)R(c, TNT_CNT0) | ((uint32_t)R(c, TNT_CNT1) << 8) |
                 ((uint32_t)R(c, TNT_CNT2) << 16) | ((uint32_t)R(c, TNT_CNT3) << 24);
    return (uint32_t)(-(int32_t)v);
}

static void set_handshake_mode(tnt_chip *c, uint8_t mode)
{
    c->auxra = (uint8_t)((c->auxra & ~(HR_HLDA | HR_HLDE)) | mode);
    auxmr(c, c->auxra);
}

/* Let the talker continue after an RFD holdoff, choosing the holdoff mode that the chip
 * releases cleanly from its current acceptor state (same logic as the Linux driver). */
static void release_holdoff(tnt_chip *c)
{
    uint8_t sasr = R(c, TNT_SASR);
    if (sasr & TNT_AEHS) {
        set_handshake_mode(c, HR_HLDE);
        auxmr(c, AUX_FH);
    } else if (sasr & TNT_ANHS1) {
        set_handshake_mode(c, HR_HLDA);
        auxmr(c, AUX_FH);
        set_handshake_mode(c, HR_HLDE);
    } else {
        set_handshake_mode(c, HR_HLDE);
        auxmr(c, AUX_FH);
    }
}

void tnt_bind(tnt_chip *c, const tnt_io *io)
{
    c->io = io;
    c->pad = 0;
    c->sad = -1;
    c->auxra = AUX_AUXRA | HR_HLDA;
    c->auxrb = AUX_AUXRB;
    c->auxrg = AUX_AUXRG;
    c->auxri = AUX_AUXRI;
    c->isr1 = 0;
    c->isr2 = 0;
    c->eos = 0;
    c->eos_flags = 0;
    c->timeout_ms = TNT_DEFAULT_TIMEOUT_MS;
    c->t1_ns = 2000;
    c->csr = 0;
}

/* Reset the Turbo488 side and the 7210 core, entering one-chip mode. Shared by probe and
 * init; leaves the chip offline (pon asserted). */
static void reset_sequence(tnt_chip *c)
{
    cmdr(c, TNT_SOFT_RESET);
    W(c, TNT_HSSEL, TNT_NODMA);
    W(c, TNT_ACCWR, 0);
    W(c, TNT_AUXCR, AUX_7210);            /* leave 9914 mode if we were in it */
    delay_us(c, 1);
    W(c, TNT_SWAPPED_AUXCR, AUX_7210);    /* the registers might be swapped */
    delay_us(c, 1);
    W(c, TNT_HSSEL, TNT_NODMA | TNT_ONE_CHIP);
    auxmr(c, AUX_CR);
    W(c, TNT_KEYREG, 0);
    W(c, TNT_IMR1, 0);
    W(c, TNT_IMR2, 0);
    W(c, TNT_SPMR, 0);
    (void)R(c, TNT_CPTR);
    (void)R(c, TNT_ISR1);
    (void)R(c, TNT_ISR2);
    c->isr1 = 0;
    c->isr2 = 0;
}

int tnt_probe(tnt_chip *c, tnt_probe_result *r)
{
    reset_sequence(c);
    r->csr = R(c, TNT_CSR);
    r->sts1 = R(c, TNT_STS1);
    r->sts2 = R(c, TNT_STS2);
    r->isr3 = R(c, TNT_ISR3);
    r->adsr = R(c, TNT_ADSR);
    r->isr0 = R(c, TNT_ISR0);
    c->csr = r->csr;
    r->ok = r->sts1 == TNT_RESET_STS1 && r->sts2 == TNT_RESET_STS2 &&
            r->isr3 == TNT_RESET_ISR3 && r->adsr == TNT_RESET_ADSR;
    return r->ok ? TNT_OK : TNT_EIO;
}

int tnt_set_address(tnt_chip *c, uint8_t pad, int sad)
{
    if (pad > 30 || sad > 30) {
        return TNT_EINVAL;
    }
    c->pad = pad;
    c->sad = sad;
    W(c, TNT_ADR, pad & ADDRESS_MASK);
    if (sad < 0) {
        W(c, TNT_ADR, HR_ARS | HR_DT | HR_DL);            /* disable ADR1 */
        W(c, TNT_ADMR, HR_TRM0 | HR_TRM1 | HR_ADM0);       /* mode 1 */
    } else {
        W(c, TNT_ADR, HR_ARS | ((uint8_t)sad & ADDRESS_MASK));
        W(c, TNT_ADMR, HR_TRM0 | HR_TRM1 | HR_ADM1);       /* mode 2 */
    }
    return TNT_OK;
}

int tnt_set_t1(tnt_chip *c, unsigned ns)
{
    c->t1_ns = ns;
    if (ns <= 500) {
        c->auxrb |= HR_TRI;
    } else {
        c->auxrb &= (uint8_t)~HR_TRI;
    }
    auxmr(c, c->auxrb);
    W(c, TNT_KEYREG, ns <= 350 ? TNT_MSTD : 0);
    if (ns > 500 && ns <= 1100) {
        c->auxri |= HR_USTD;
    } else {
        c->auxri &= (uint8_t)~HR_USTD;
    }
    auxmr(c, c->auxri);
    return TNT_OK;
}

int tnt_set_eos(tnt_chip *c, uint8_t eos, unsigned flags)
{
    c->eos = eos;
    c->eos_flags = flags;
    W(c, TNT_EOSR, eos);
    c->auxra &= (uint8_t)~(HR_REOS | HR_BIN | HR_XEOS);
    if (flags & TNT_EOS_REOS) {
        c->auxra |= HR_REOS;
    }
    if (flags & TNT_EOS_BIN) {
        c->auxra |= HR_BIN;
    }
    if (flags & TNT_EOS_XEOS) {
        c->auxra |= HR_XEOS;
    }
    auxmr(c, c->auxra);
    return TNT_OK;
}

void tnt_set_timeout(tnt_chip *c, uint32_t ms)
{
    c->timeout_ms = ms;
}

int tnt_init(tnt_chip *c, uint8_t pad)
{
    tnt_probe_result pr;
    int rc = tnt_probe(c, &pr);
    if (rc != TNT_OK) {
        return rc;
    }
    auxmr(c, AUX_PPR | HR_PPU);           /* parallel poll unconfigured */
    c->auxra = AUX_AUXRA | HR_HLDA;       /* hold off on every byte until we read */
    auxmr(c, c->auxra);
    auxmr(c, AUX_AUXRE);
    c->auxrb = AUX_AUXRB;                 /* no command pass-through, slow T1 */
    auxmr(c, c->auxrb);
    c->auxri = AUX_AUXRI;
    auxmr(c, c->auxri);
    (void)R(c, TNT_ISR0);
    W(c, TNT_IMR3, 0);
    W(c, TNT_IMR0, TNT_IMR0_ALWAYS);
    auxmr(c, AUX_HLDI);                   /* accept nothing until the first read */
    c->auxrg = AUX_AUXRG;                 /* NTNL must be 0 in one-chip mode */
    auxmr(c, c->auxrg);
    rc = tnt_set_address(c, pad, -1);
    if (rc != TNT_OK) {
        return rc;
    }
    W(c, TNT_EOSR, 0);
    auxmr(c, AUX_PON);                    /* go online */
    tnt_set_t1(c, c->t1_ns);
    cmdr(c, TNT_SETSC);                   /* we are the system controller */
    return (R(c, TNT_STS1) & TNT_S_SC) ? TNT_OK : TNT_EIO;
}

void tnt_shutdown(tnt_chip *c)
{
    W(c, TNT_IMR0, TNT_IMR0_ALWAYS);
    W(c, TNT_IMR3, 0);
    (void)R(c, TNT_ISR0);
    auxmr(c, AUX_CREN);
    cmdr(c, TNT_CLRSC);
    auxmr(c, AUX_CR);
    W(c, TNT_IMR1, 0);
    W(c, TNT_IMR2, 0);
}

uint8_t tnt_bus_lines(tnt_chip *c)
{
    return R(c, TNT_BSR);
}

uint8_t tnt_adsr(tnt_chip *c)
{
    return R(c, TNT_ADSR);
}

int tnt_is_cic(tnt_chip *c)
{
    return (R(c, TNT_ADSR) & HR_CIC) != 0;
}

int tnt_interface_clear(tnt_chip *c)
{
    if (!(R(c, TNT_STS1) & TNT_S_SC)) {
        return TNT_ENOTSC;
    }
    auxmr(c, AUX_SIFC);
    delay_us(c, IFC_PULSE_US);
    auxmr(c, AUX_CIFC);
    return TNT_OK;
}

int tnt_remote_enable(tnt_chip *c, int on)
{
    if (!(R(c, TNT_STS1) & TNT_S_SC)) {
        return TNT_ENOTSC;
    }
    auxmr(c, on ? AUX_SREN : AUX_CREN);
    return TNT_OK;
}

static int wait_atn(tnt_chip *c, int asserted)
{
    unsigned spin;
    deadline d;
    for (spin = 0; spin < TCA_SPIN_US; spin++) {
        int natn = (R(c, TNT_ADSR) & HR_NATN) != 0;
        if (natn == !asserted) {
            return TNT_OK;
        }
        delay_us(c, 1);
    }
    d = deadline_start(c);
    for (;;) {
        int natn = (R(c, TNT_ADSR) & HR_NATN) != 0;
        if (natn == !asserted) {
            return TNT_OK;
        }
        if (deadline_expired(c, &d)) {
            return TNT_ETIMEOUT;
        }
        yield(c);
    }
}

int tnt_take_control(tnt_chip *c, int synchronous)
{
    if (!tnt_is_cic(c)) {
        return TNT_ENOTCIC;
    }
    auxmr(c, synchronous ? AUX_TCS : AUX_TCA);
    return wait_atn(c, 1);
}

int tnt_go_to_standby(tnt_chip *c)
{
    if (!tnt_is_cic(c)) {
        return TNT_ENOTCIC;
    }
    auxmr(c, AUX_GTS);
    return wait_atn(c, 0);
}

int tnt_transfer_out(tnt_chip *c, const uint8_t *buf, unsigned n, unsigned flags, unsigned *sent)
{
    unsigned queued = 0;
    uint8_t cfg = 0;
    int rc = TNT_OK;
    deadline d;
    uint32_t left;

    *sent = 0;
    if (n == 0) {
        return TNT_OK;
    }
    if (n > TNT_MAX_TRANSFER) {
        return TNT_EINVAL;
    }
    cmdr(c, TNT_RESET_FIFO);
    if (flags & TNT_XF_EOI) {
        cfg |= TNT_CCEN;
    }
    if (flags & TNT_XF_COMMAND) {
        cfg |= TNT_COMMAND;
    }
    W(c, TNT_CFG, cfg);
    set_count(c, n);
    poll_status(c);
    c->isr1 &= (uint8_t)~HR_ERR;
    cmdr(c, TNT_GO);
    d = deadline_start(c);
    while (queued < n) {
        if (R(c, TNT_STS2) & TNT_BFFN) {
            W(c, TNT_FIFOB, buf[queued++]);
            d = deadline_start(c);
            continue;
        }
        poll_status(c);
        if (c->isr1 & HR_ERR) {
            rc = TNT_ENOLISTENER;
            break;
        }
        if (deadline_expired(c, &d)) {
            rc = TNT_ETIMEOUT;
            break;
        }
        yield(c);
    }
    while (rc == TNT_OK && !(R(c, TNT_STS1) & TNT_S_DONE)) {
        poll_status(c);
        if (c->isr1 & HR_ERR) {
            rc = TNT_ENOLISTENER;
            break;
        }
        if (deadline_expired(c, &d)) {
            rc = TNT_ETIMEOUT;
            break;
        }
        yield(c);
    }
    cmdr(c, TNT_STOP);
    left = remaining_count(c);
    *sent = left <= n ? n - left : queued;
    c->isr1 &= (uint8_t)~HR_ERR;
    return rc;
}

int tnt_command(tnt_chip *c, const uint8_t *cmd, unsigned n, unsigned *sent)
{
    int rc;
    *sent = 0;
    if (!tnt_is_cic(c)) {
        return TNT_ENOTCIC;
    }
    if (R(c, TNT_ADSR) & HR_NATN) {
        rc = tnt_take_control(c, 0);
        if (rc != TNT_OK) {
            return rc;
        }
    }
    return tnt_transfer_out(c, cmd, n, TNT_XF_COMMAND, sent);
}

int tnt_write(tnt_chip *c, const uint8_t *buf, unsigned n, int eoi, unsigned *sent)
{
    return tnt_transfer_out(c, buf, n, eoi ? TNT_XF_EOI : 0, sent);
}

static unsigned drain_fifo(tnt_chip *c, uint8_t *buf, unsigned n, unsigned cap)
{
    while (n < cap && (R(c, TNT_STS2) & TNT_BEFN)) {
        buf[n++] = R(c, TNT_FIFOB);
    }
    return n;
}

int tnt_read(tnt_chip *c, uint8_t *buf, unsigned cap, unsigned *got, int *end)
{
    unsigned n = 0;
    int rc = TNT_OK;
    deadline d;

    *got = 0;
    *end = 0;
    if (cap == 0) {
        return TNT_OK;
    }
    if (cap > TNT_MAX_TRANSFER) {
        return TNT_EINVAL;
    }
    release_holdoff(c);
    cmdr(c, TNT_RESET_FIFO);
    W(c, TNT_CFG, TNT_IN | TNT_CCEN);      /* holdoff after the last counted byte */
    set_count(c, cap);
    poll_status(c);
    c->isr1 &= (uint8_t)~HR_END;
    cmdr(c, TNT_GO);
    d = deadline_start(c);
    while (n < cap) {
        unsigned before = n;
        n = drain_fifo(c, buf, n, cap);
        if (n != before) {
            d = deadline_start(c);
            continue;
        }
        poll_status(c);
        if (c->isr1 & HR_END) {
            n = drain_fifo(c, buf, n, cap);   /* the END byte may have landed meanwhile */
            *end = 1;
            break;
        }
        if (deadline_expired(c, &d)) {
            rc = TNT_ETIMEOUT;
            break;
        }
        yield(c);
    }
    if (n == cap && !*end) {
        poll_status(c);
        if (c->isr1 & HR_END) {
            *end = 1;
        }
    }
    cmdr(c, TNT_STOP);
    c->isr1 &= (uint8_t)~HR_END;
    if (rc == TNT_ETIMEOUT) {
        auxmr(c, AUX_HLDI);
    }
    *got = n;
    return rc;
}

int tnt_parallel_poll(tnt_chip *c, uint8_t *response)
{
    if (!tnt_is_cic(c)) {
        return TNT_ENOTCIC;
    }
    auxmr(c, c->auxrg | HR_RPP2);
    delay_us(c, 2);
    *response = R(c, TNT_CPTR);
    auxmr(c, c->auxrg);
    return TNT_OK;
}
