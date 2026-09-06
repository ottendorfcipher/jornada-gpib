/* TNT4882C chip driver (National Instruments PCMCIA-GPIB), portable core.
 *
 * The chip is run in one-chip mode: data and command bytes move through the on-chip FIFO,
 * addressing and bus management use the 7210-compatible register page. Everything is polled;
 * no interrupts are used. Timing and register access are abstracted through tnt_io so the
 * same code runs on the Jornada (register window mapped by Card Services) and on a host
 * simulator for testing.
 *
 * References: NI TNT4882 Programmer Reference Manual 370872A-01 (public T/L part);
 * controller-side behaviour (CIC, SETSC, CFG.COMMAND) as exercised by the Linux tnt4882 and
 * FreeBSD drivers on the same silicon.
 */
#ifndef GPIB_TNT4882_H
#define GPIB_TNT4882_H

#include <stdint.h>

/* Register offsets within the card's 32-byte I/O window. 7210-page registers sit at
 * (7210 index) * 2; Turbo488-page registers at their own offsets. All accesses are 8-bit. */
enum tnt_reg {
    TNT_CDOR = 0x00, TNT_DIR = 0x00,
    TNT_IMR1 = 0x02, TNT_ISR1 = 0x02,
    TNT_IMR2 = 0x04, TNT_ISR2 = 0x04,
    TNT_SPMR = 0x06, TNT_SPSR = 0x06,
    TNT_ADMR = 0x08, TNT_ADSR = 0x08,
    TNT_AUXMR = 0x0A, TNT_CPTR = 0x0A,
    TNT_ADR = 0x0C, TNT_ADR0 = 0x0C,
    TNT_EOSR = 0x0E, TNT_ADR1 = 0x0E,
    TNT_ACCWR = 0x05,
    TNT_AUXCR = 0x06,           /* 9914-mode auxiliary command register */
    TNT_INTRT = 0x07,
    TNT_CNT2 = 0x09,
    TNT_SWAPPED_AUXCR = 0x0A,
    TNT_CNT3 = 0x0B,
    TNT_HSSEL = 0x0D,
    TNT_CFG = 0x10, TNT_STS1 = 0x10,
    TNT_IMR3 = 0x12,
    TNT_CNT0 = 0x14,
    TNT_CNT1 = 0x16,
    TNT_KEYREG = 0x17, TNT_CSR = 0x17,
    TNT_FIFOB = 0x18,
    TNT_FIFOA = 0x19,
    TNT_CCR = 0x1A, TNT_ISR3 = 0x1A,
    TNT_SASR = 0x1B,
    TNT_CMDR = 0x1C, TNT_STS2 = 0x1C,
    TNT_IMR0 = 0x1D, TNT_ISR0 = 0x1D,
    TNT_TIMER = 0x1E,
    TNT_BCR = 0x1F, TNT_BSR = 0x1F,
};

/* ISR1 */
#define HR_DI   0x01
#define HR_DO   0x02
#define HR_ERR  0x04
#define HR_DEC  0x08
#define HR_END  0x10
#define HR_DET  0x20
#define HR_APT  0x40
#define HR_CPT  0x80
/* ISR2 */
#define HR_ADSC 0x01
#define HR_REMC 0x02
#define HR_LOKC 0x04
#define HR_CO   0x08
#define HR_REM  0x10
#define HR_LOK  0x20
#define HR_SRQI 0x40
#define HR_INT  0x80
/* ADSR */
#define HR_MJMN 0x01
#define HR_TA   0x02
#define HR_LA   0x04
#define HR_TPAS 0x08
#define HR_LPAS 0x10
#define HR_SPMS 0x20
#define HR_NATN 0x40
#define HR_CIC  0x80
/* ADMR */
#define HR_ADM0 0x01
#define HR_ADM1 0x02
#define HR_TRM0 0x10
#define HR_TRM1 0x20
#define HR_LON  0x40
#define HR_TON  0x80
/* ADR */
#define HR_DL   0x20
#define HR_DT   0x40
#define HR_ARS  0x80
/* AUXMR hidden registers and auxiliary commands */
#define AUX_PON   0x00
#define AUX_CPPF  0x01
#define AUX_CR    0x02
#define AUX_FH    0x03
#define AUX_TRIG  0x04
#define AUX_RTL   0x05
#define AUX_SEOI  0x06
#define AUX_NVAL  0x07
#define AUX_SPPF  0x09
#define AUX_VAL   0x0F
#define AUX_GTS   0x10
#define AUX_TCA   0x11
#define AUX_TCS   0x12
#define AUX_LTN   0x13
#define AUX_DSC   0x14
#define AUX_CIFC  0x16
#define AUX_CREN  0x17
#define AUX_REQT  0x18
#define AUX_REQF  0x19
#define AUX_TCSE  0x1A
#define AUX_LTNC  0x1B
#define AUX_LUN   0x1C
#define AUX_EPP   0x1D
#define AUX_SIFC  0x1E
#define AUX_SREN  0x1F
#define AUX_PAGEIN 0x50
#define AUX_HLDI  0x51
#define AUX_CLEAR_END 0x55
#define AUX_7210  0x99
#define AUX_ICR   0x20
#define AUX_AUXRG 0x40
#define AUX_PPR   0x60
#define AUX_AUXRA 0x80
#define AUX_AUXRB 0xA0
#define AUX_AUXRE 0xC0
#define AUX_AUXRI 0xE0
/* AUXRA */
#define HR_HLDA 0x01
#define HR_HLDE 0x02
#define HR_REOS 0x04
#define HR_XEOS 0x08
#define HR_BIN  0x10
/* AUXRB */
#define HR_CPTE  0x01
#define HR_SPEOI 0x02
#define HR_TRI   0x04
#define HR_INV   0x08
#define HR_ISS   0x10
/* AUXRG */
#define HR_CHES 0x01
#define HR_RPP2 0x04
#define HR_NTNL 0x08
/* AUXRI */
#define HR_SISB 0x01
#define HR_PP2  0x04
#define HR_USTD 0x08
/* PPR */
#define HR_PPS 0x08
#define HR_PPU 0x10
/* CFG */
#define TNT_COMMAND 0x80
#define TNT_TLCHLTE 0x40
#define TNT_IN      0x20
#define TNT_A_B     0x10
#define TNT_CCEN    0x08
#define TNT_TMOE    0x04
#define TNT_TIM_BYTN 0x02
#define TNT_B_16BIT 0x01
/* CMDR */
#define TNT_CLRSC      0x02
#define TNT_SETSC      0x03
#define TNT_GO         0x04
#define TNT_STOP       0x08
#define TNT_RESET_FIFO 0x10
#define TNT_SOFT_RESET 0x22
/* HSSEL */
#define TNT_ONE_CHIP 0x01
#define TNT_NODMA    0x10
/* IMR0 */
#define TNT_IMR0_ALWAYS 0x80
/* ISR0 */
#define TNT_ISR0_SYNC 0x01
#define TNT_ISR0_TO   0x02
#define TNT_ISR0_ATNI 0x04
#define TNT_ISR0_IFCI 0x08
/* ISR3 */
#define TNT_HR_DONE 0x01
#define TNT_HR_TLCI 0x02
#define TNT_HR_NEF  0x04
#define TNT_HR_NFF  0x08
#define TNT_HR_STOP 0x10
/* STS1 */
#define TNT_S_DONE  0x80
#define TNT_S_SC    0x40
#define TNT_S_IN    0x20
#define TNT_S_DRQ   0x10
#define TNT_S_STOP  0x08
#define TNT_S_NDAV  0x04
#define TNT_S_HALT  0x02
#define TNT_S_GSYNC 0x01
/* STS2 */
#define TNT_AFFN 0x08
#define TNT_AEFN 0x04
#define TNT_BFFN 0x02
#define TNT_BEFN 0x01
/* SASR */
#define TNT_ACRDY 0x04
#define TNT_ADHS  0x08
#define TNT_ANHS2 0x10
#define TNT_ANHS1 0x20
#define TNT_AEHS  0x40
/* KEYREG */
#define TNT_MSTD 0x20
/* BSR (bus line status) */
#define TNT_BSR_REN  0x01
#define TNT_BSR_IFC  0x02
#define TNT_BSR_SRQ  0x04
#define TNT_BSR_EOI  0x08
#define TNT_BSR_NRFD 0x10
#define TNT_BSR_NDAC 0x20
#define TNT_BSR_DAV  0x40
#define TNT_BSR_ATN  0x80

/* Values the chip shows right after a reset (NI application note 095 self-test). */
#define TNT_RESET_STS1 0x8B
#define TNT_RESET_STS2 0x9A
#define TNT_RESET_ISR3 0x19
#define TNT_RESET_ADSR 0x40
#define TNT_CSR_VERSION_TNT4882 0x30
#define TNT_CSR_VERSION_TNT5004 0x40

/* Return codes (negative on failure). */
#define TNT_OK           0
#define TNT_ETIMEOUT    -1   /* no progress within the timeout */
#define TNT_ENOLISTENER -2   /* bus error: byte not accepted by anyone (ERR) */
#define TNT_ENOTCIC     -3   /* need to be controller in charge */
#define TNT_EIO         -4   /* chip did not respond as expected */
#define TNT_EINVAL      -5
#define TNT_ENOTSC      -6   /* need to be system controller */

#define TNT_DEFAULT_TIMEOUT_MS 10000
#define TNT_MAX_TRANSFER 0x00FFFFFFu

/* Transfer flags */
#define TNT_XF_EOI     0x01  /* assert EOI with the last byte */
#define TNT_XF_COMMAND 0x02  /* bytes are GPIB commands (ATN asserted) */

/* EOS flags */
#define TNT_EOS_REOS 0x01  /* terminate reads on the EOS byte */
#define TNT_EOS_BIN  0x02  /* compare all 8 bits, else 7 */
#define TNT_EOS_XEOS 0x04  /* send EOI with an EOS byte on writes */

typedef struct tnt_io {
    void *ctx;
    uint8_t (*read8)(void *ctx, unsigned off);
    void (*write8)(void *ctx, unsigned off, uint8_t val);
    void (*delay_us)(void *ctx, unsigned us);   /* short busy delay for register timing */
    uint32_t (*now_ms)(void *ctx);              /* monotonic millisecond clock */
    void (*yield)(void *ctx);                   /* give other threads a chance while polling */
} tnt_io;

typedef struct tnt_chip {
    const tnt_io *io;
    uint8_t pad;                /* primary address */
    int sad;                    /* secondary address or -1 */
    uint8_t auxra, auxrb, auxrg, auxri;
    uint8_t isr1, isr2;         /* status bits seen and not yet consumed */
    uint8_t eos;
    unsigned eos_flags;
    uint32_t timeout_ms;
    unsigned t1_ns;
    uint8_t csr;
} tnt_chip;

typedef struct tnt_probe_result {
    uint8_t csr, sts1, sts2, isr3, adsr, isr0;
    int ok;                     /* 1 if the reset signature matched */
} tnt_probe_result;

void tnt_bind(tnt_chip *c, const tnt_io *io);
int  tnt_probe(tnt_chip *c, tnt_probe_result *r);
int  tnt_init(tnt_chip *c, uint8_t pad);
void tnt_shutdown(tnt_chip *c);

void tnt_set_timeout(tnt_chip *c, uint32_t ms);
int  tnt_set_address(tnt_chip *c, uint8_t pad, int sad);
int  tnt_set_eos(tnt_chip *c, uint8_t eos, unsigned flags);
int  tnt_set_t1(tnt_chip *c, unsigned ns);

int  tnt_interface_clear(tnt_chip *c);
int  tnt_remote_enable(tnt_chip *c, int on);
int  tnt_take_control(tnt_chip *c, int synchronous);
int  tnt_go_to_standby(tnt_chip *c);
int  tnt_command(tnt_chip *c, const uint8_t *cmd, unsigned n, unsigned *sent);
int  tnt_write(tnt_chip *c, const uint8_t *buf, unsigned n, int eoi, unsigned *sent);
int  tnt_read(tnt_chip *c, uint8_t *buf, unsigned cap, unsigned *got, int *end);
int  tnt_parallel_poll(tnt_chip *c, uint8_t *response);

uint8_t tnt_bus_lines(tnt_chip *c);   /* BSR */
uint8_t tnt_adsr(tnt_chip *c);
int  tnt_is_cic(tnt_chip *c);

/* Low-level transfer used by command/write; exposed for tests. */
int  tnt_transfer_out(tnt_chip *c, const uint8_t *buf, unsigned n, unsigned flags, unsigned *sent);

#endif /* GPIB_TNT4882_H */
