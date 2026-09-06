/* Host tests for the TNT4882C chip driver and the controller sequences, against the simulator. */
#include <string.h>

#include "check.h"
#include "gpib/gpib488.h"
#include "gpib/tnt4882.h"
#include "sim_tnt4882.h"

static sim_tnt sim;
static tnt_chip chip;
static gpib_ctl ctl;

static void setup(int device_present)
{
    sim_reset(&sim);
    sim_device_present(&sim, 5, device_present);
    tnt_bind(&chip, sim_io(&sim));
    gpib_bind(&ctl, &chip);
    tnt_set_timeout(&chip, 500);
}

static void test_probe_reset_signature(void)
{
    tnt_probe_result r;
    setup(1);
    CHECK_EQ_INT(tnt_probe(&chip, &r), TNT_OK);
    CHECK(r.ok);
    CHECK_EQ_INT(r.sts1, 0x8B);
    CHECK_EQ_INT(r.sts2, 0x9A);
    CHECK_EQ_INT(r.isr3, 0x19);
    CHECK_EQ_INT(r.adsr, 0x40);
    CHECK_EQ_INT(r.csr & 0xF0, TNT_CSR_VERSION_TNT4882);
    CHECK(sim.one_chip);
}

static void test_init_state(void)
{
    setup(1);
    CHECK_EQ_INT(tnt_init(&chip, 0), TNT_OK);
    CHECK(sim.sc);
    CHECK(!sim.pon);
    CHECK_EQ_INT(sim.adr0 & 0x1F, 0);
    CHECK_EQ_INT(sim.adr1, HR_ARS | HR_DT | HR_DL);
    CHECK_EQ_INT(sim.admr, HR_TRM0 | HR_TRM1 | HR_ADM0);
    CHECK_EQ_INT(sim.imr1, 0);
    CHECK_EQ_INT(sim.imr2, 0);
    CHECK_EQ_INT(sim.imr3, 0);
    CHECK_EQ_INT(sim.auxrg, AUX_AUXRG);        /* NTNL clear in one-chip mode */
    CHECK(sim.holdoff);                        /* HLDI: nothing accepted yet */
    CHECK(!tnt_is_cic(&chip));
    CHECK_EQ_INT(tnt_set_address(&chip, 31, -1), TNT_EINVAL);
    CHECK_EQ_INT(tnt_set_address(&chip, 3, 7), TNT_OK);
    CHECK_EQ_INT(sim.adr1, HR_ARS | 7);
    CHECK_EQ_INT(sim.admr, HR_TRM0 | HR_TRM1 | HR_ADM1);
}

static void test_ifc_and_ren(void)
{
    setup(1);
    tnt_init(&chip, 0);
    CHECK_EQ_INT(gpib_interface_clear(&ctl), TNT_OK);
    CHECK(tnt_is_cic(&chip));
    CHECK(tnt_bus_lines(&chip) & TNT_BSR_ATN);
    CHECK(!(tnt_bus_lines(&chip) & TNT_BSR_IFC));  /* pulse released */
    CHECK_EQ_INT(gpib_remote_enable(&ctl, 1), TNT_OK);
    CHECK(tnt_bus_lines(&chip) & TNT_BSR_REN);
    CHECK_EQ_INT(gpib_remote_enable(&ctl, 0), TNT_OK);
    CHECK(!(tnt_bus_lines(&chip) & TNT_BSR_REN));
    /* not system controller: IFC refused */
    sim.sc = 0;
    CHECK_EQ_INT(tnt_interface_clear(&chip), TNT_ENOTSC);
    CHECK_EQ_INT(tnt_remote_enable(&chip, 1), TNT_ENOTSC);
}

static void test_commands_require_cic(void)
{
    uint8_t cmd = GPIB_UNL;
    unsigned sent = 9;
    setup(1);
    tnt_init(&chip, 0);
    CHECK_EQ_INT(tnt_command(&chip, &cmd, 1, &sent), TNT_ENOTCIC);
    CHECK_EQ_INT(sent, 0);
    CHECK_EQ_INT(tnt_take_control(&chip, 0), TNT_ENOTCIC);
    CHECK_EQ_INT(tnt_go_to_standby(&chip), TNT_ENOTCIC);
}

static void test_addressing_and_write(void)
{
    static const uint8_t msg[] = "*IDN?\n";
    unsigned sent = 0;
    setup(1);
    tnt_init(&chip, 0);
    gpib_interface_clear(&ctl);
    CHECK_EQ_INT(gpib_write(&ctl, 5, -1, msg, 6, 1, &sent), TNT_OK);
    CHECK_EQ_INT(sent, 6);
    CHECK_EQ_INT(sim.dev.rx_len, 6);
    CHECK(memcmp(sim.dev.rx, msg, 6) == 0);
    CHECK(sim.dev.rx_eoi[5] && !sim.dev.rx_eoi[4]);
    /* the addressing commands: UNL, LAD 5, MTA 0 */
    CHECK_EQ_INT(sim.dev.ncmd, 3);
    CHECK_EQ_INT(sim.dev.commands[0], GPIB_UNL);
    CHECK_EQ_INT(sim.dev.commands[1], GPIB_LAD(5));
    CHECK_EQ_INT(sim.dev.commands[2], GPIB_TAD(0));
    CHECK(sim.atn);                               /* ATN reasserted afterwards */
    /* secondary addresses are inserted after the primary */
    CHECK_EQ_INT(gpib_write(&ctl, 5, 2, msg, 2, 0, &sent), TNT_OK);
    CHECK_EQ_INT(sim.dev.commands[3 + 2], GPIB_SAD(2));
    CHECK(!sim.dev.rx_eoi[7]);
}

static void test_write_without_listener(void)
{
    static const uint8_t msg[] = "hello";
    unsigned sent = 99;
    setup(1);
    tnt_init(&chip, 0);
    gpib_interface_clear(&ctl);
    /* address device 6, which does not exist: the addressing itself succeeds (device 5
     * accepts commands), but nobody listens to the data */
    CHECK_EQ_INT(gpib_write(&ctl, 6, -1, msg, 5, 1, &sent), TNT_ENOLISTENER);
    CHECK_EQ_INT(sent, 0);
    CHECK_EQ_INT(sim.dev.rx_len, 0);
    /* no device at all: commands fail */
    setup(0);
    tnt_init(&chip, 0);
    gpib_interface_clear(&ctl);
    CHECK_EQ_INT(gpib_write(&ctl, 5, -1, msg, 5, 1, &sent), TNT_ENOLISTENER);
    CHECK_EQ_INT(gpib_write(&ctl, 31, -1, msg, 5, 1, &sent), TNT_EINVAL);
}

static void test_read_with_eoi(void)
{
    static const uint8_t reply[] = "TEKTRONIX,TDS 340A,0,CF:91.1CT FV:v1.05\n";
    uint8_t buf[128];
    unsigned got = 0;
    int end = 0;
    setup(1);
    tnt_init(&chip, 0);
    gpib_interface_clear(&ctl);
    sim_device_will_talk(&sim, reply, sizeof reply - 1, 1);
    CHECK_EQ_INT(gpib_read(&ctl, 5, -1, buf, sizeof buf, &got, &end), TNT_OK);
    CHECK_EQ_INT(got, sizeof reply - 1);
    CHECK(end);
    CHECK(memcmp(buf, reply, got) == 0);
    /* addressing: UNL, TAD 5, MLA 0 */
    CHECK_EQ_INT(sim.dev.commands[0], GPIB_UNL);
    CHECK_EQ_INT(sim.dev.commands[1], GPIB_TAD(5));
    CHECK_EQ_INT(sim.dev.commands[2], GPIB_LAD(0));
    CHECK(sim.atn);
}

static void test_read_in_pieces(void)
{
    static const uint8_t reply[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    uint8_t buf[20];
    unsigned got = 0;
    int end = 1;
    unsigned total = 0;
    setup(1);
    tnt_init(&chip, 0);
    gpib_interface_clear(&ctl);
    sim_device_will_talk(&sim, reply, 62, 1);
    CHECK_EQ_INT(gpib_read(&ctl, 5, -1, buf, 20, &got, &end), TNT_OK);
    CHECK_EQ_INT(got, 20);
    CHECK(!end);
    CHECK(memcmp(buf, reply, 20) == 0);
    total = got;
    CHECK_EQ_INT(gpib_read(&ctl, 5, -1, buf, 20, &got, &end), TNT_OK);
    CHECK_EQ_INT(got, 20);
    CHECK(!end);
    CHECK(memcmp(buf, reply + 20, 20) == 0);
    total += got;
    {
        int guard = 0;
        while (!end && total < 62 && guard++ < 8) {
            CHECK_EQ_INT(gpib_read(&ctl, 5, -1, buf, 20, &got, &end), TNT_OK);
            CHECK(memcmp(buf, reply + total, got) == 0);
            total += got;
        }
    }
    CHECK_EQ_INT(total, 62);
    CHECK(end);
}

static void test_read_timeout(void)
{
    uint8_t buf[16];
    unsigned got = 5;
    int end = 1;
    uint32_t t0;
    setup(1);
    tnt_init(&chip, 0);
    gpib_interface_clear(&ctl);
    tnt_set_timeout(&chip, 300);
    t0 = sim.time_us;
    CHECK_EQ_INT(gpib_read(&ctl, 5, -1, buf, sizeof buf, &got, &end), TNT_ETIMEOUT);
    CHECK_EQ_INT(got, 0);
    CHECK(!end);
    CHECK((sim.time_us - t0) >= 300000u && (sim.time_us - t0) < 400000u);
    CHECK(sim.holdoff);                /* HLDI after a timeout */
    /* a later read still works once the device talks */
    {
        static const uint8_t late[] = "late\n";
        sim_device_will_talk(&sim, late, 5, 1);
        CHECK_EQ_INT(gpib_read(&ctl, 5, -1, buf, sizeof buf, &got, &end), TNT_OK);
        CHECK_EQ_INT(got, 5);
        CHECK(end);
    }
}

static void test_eos_termination(void)
{
    static const uint8_t reply[] = "12.5\nignored";
    uint8_t buf[32];
    unsigned got = 0;
    int end = 0;
    setup(1);
    tnt_init(&chip, 0);
    gpib_interface_clear(&ctl);
    CHECK_EQ_INT(tnt_set_eos(&chip, '\n', TNT_EOS_REOS | TNT_EOS_BIN), TNT_OK);
    CHECK_EQ_INT(sim.eosr, '\n');
    CHECK(sim.auxra & HR_REOS);
    sim_device_will_talk(&sim, reply, sizeof reply - 1, 0);
    CHECK_EQ_INT(gpib_read(&ctl, 5, -1, buf, sizeof buf, &got, &end), TNT_OK);
    CHECK_EQ_INT(got, 5);
    CHECK(end);
    CHECK(memcmp(buf, "12.5\n", 5) == 0);
}

static void test_serial_poll_and_commands(void)
{
    uint8_t stb = 0;
    setup(1);
    tnt_init(&chip, 0);
    gpib_interface_clear(&ctl);
    sim.dev.stb = 0x50;
    sim.dev.srq = 1;
    CHECK(gpib_srq_asserted(&ctl));
    CHECK_EQ_INT(gpib_serial_poll(&ctl, 5, -1, &stb), TNT_OK);
    CHECK_EQ_INT(stb, 0x50);
    CHECK(!sim.dev.spe);                          /* SPD sent afterwards */
    CHECK_EQ_INT(sim.dev.commands[sim.dev.ncmd - 1], GPIB_UNT);
    CHECK_EQ_INT(gpib_clear(&ctl, 5, -1), TNT_OK);
    CHECK_EQ_INT(sim.dev.cleared, 1);
    CHECK_EQ_INT(gpib_clear(&ctl, -1, -1), TNT_OK);
    CHECK_EQ_INT(sim.dev.cleared, 2);
    CHECK_EQ_INT(gpib_trigger(&ctl, 5, -1), TNT_OK);
    CHECK_EQ_INT(sim.dev.triggered, 1);
    CHECK_EQ_INT(gpib_local(&ctl, 5, -1), TNT_OK);
    CHECK(sim.dev.local);
    CHECK_EQ_INT(gpib_local_lockout(&ctl), TNT_OK);
    CHECK(sim.dev.lockout);
    CHECK_EQ_INT(gpib_trigger(&ctl, 40, -1), TNT_EINVAL);
}

static void test_t1_and_timeout_settings(void)
{
    setup(1);
    tnt_init(&chip, 0);
    tnt_set_t1(&chip, 350);
    CHECK(sim.auxrb & HR_TRI);
    CHECK_EQ_INT(sim.keyreg, TNT_MSTD);
    tnt_set_t1(&chip, 1000);
    CHECK(!(sim.auxrb & HR_TRI));
    CHECK(sim.auxri & HR_USTD);
    tnt_set_t1(&chip, 2000);
    CHECK(!(sim.auxri & HR_USTD));
    CHECK_EQ_INT(sim.keyreg, 0);
    tnt_set_timeout(&chip, 0);
    CHECK_EQ_INT(chip.timeout_ms, 0);
}

static void test_transfer_edge_cases(void)
{
    uint8_t buf[4];
    unsigned n = 7;
    int end = 1;
    setup(1);
    tnt_init(&chip, 0);
    gpib_interface_clear(&ctl);
    CHECK_EQ_INT(tnt_transfer_out(&chip, buf, 0, 0, &n), TNT_OK);
    CHECK_EQ_INT(n, 0);
    CHECK_EQ_INT(tnt_read(&chip, buf, 0, &n, &end), TNT_OK);
    CHECK_EQ_INT(n, 0);
    CHECK(!end);
    CHECK_EQ_INT(tnt_read(&chip, buf, TNT_MAX_TRANSFER + 1, &n, &end), TNT_EINVAL);
    CHECK_EQ_INT(tnt_transfer_out(&chip, buf, TNT_MAX_TRANSFER + 1, 0, &n), TNT_EINVAL);
}

int main(void)
{
    test_probe_reset_signature();
    test_init_state();
    test_ifc_and_ren();
    test_commands_require_cic();
    test_addressing_and_write();
    test_write_without_listener();
    test_read_with_eoi();
    test_read_in_pieces();
    test_read_timeout();
    test_eos_termination();
    test_serial_poll_and_commands();
    test_t1_and_timeout_settings();
    test_transfer_edge_cases();
    return check_summary("test_tnt4882");
}
