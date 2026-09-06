/* Controller-level IEEE 488 sequences. See include/gpib/gpib488.h. */
#include "gpib/gpib488.h"

void gpib_bind(gpib_ctl *g, tnt_chip *chip)
{
    g->chip = chip;
}

static int valid_address(int pad, int sad)
{
    return pad >= 0 && pad <= 30 && sad >= -1 && sad <= 30;
}

unsigned gpib_build_address(const gpib_ctl *g, int pad, int sad, int device_talks, uint8_t *out)
{
    unsigned n = 0;
    out[n++] = GPIB_UNL;
    out[n++] = device_talks ? GPIB_TAD(pad) : GPIB_LAD(pad);
    if (sad >= 0) {
        out[n++] = GPIB_SAD(sad);
    }
    out[n++] = device_talks ? GPIB_LAD(g->chip->pad) : GPIB_TAD(g->chip->pad);
    if (g->chip->sad >= 0) {
        out[n++] = GPIB_SAD(g->chip->sad);
    }
    return n;
}

int gpib_interface_clear(gpib_ctl *g)
{
    int rc = tnt_interface_clear(g->chip);
    if (rc != TNT_OK) {
        return rc;
    }
    /* IFC made us controller in charge; assert ATN so the bus is quiet. */
    return tnt_take_control(g->chip, 0);
}

int gpib_remote_enable(gpib_ctl *g, int on)
{
    return tnt_remote_enable(g->chip, on);
}

int gpib_send_commands(gpib_ctl *g, const uint8_t *cmd, unsigned n)
{
    unsigned sent = 0;
    int rc = tnt_command(g->chip, cmd, n, &sent);
    if (rc == TNT_OK && sent != n) {
        rc = TNT_EIO;
    }
    return rc;
}

static int address_device(gpib_ctl *g, int pad, int sad, int device_talks)
{
    uint8_t cmd[GPIB_MAX_CMD];
    unsigned n;
    if (!valid_address(pad, sad)) {
        return TNT_EINVAL;
    }
    n = gpib_build_address(g, pad, sad, device_talks, cmd);
    return gpib_send_commands(g, cmd, n);
}

int gpib_write(gpib_ctl *g, int pad, int sad, const uint8_t *buf, unsigned n, int eoi, unsigned *sent)
{
    int rc = address_device(g, pad, sad, 0);
    *sent = 0;
    if (rc != TNT_OK) {
        return rc;
    }
    rc = tnt_go_to_standby(g->chip);
    if (rc != TNT_OK) {
        return rc;
    }
    rc = tnt_write(g->chip, buf, n, eoi, sent);
    /* Regain ATN afterwards whatever happened, so the bus is quiet; keep the first error. */
    if (tnt_take_control(g->chip, 0) != TNT_OK && rc == TNT_OK) {
        rc = TNT_EIO;
    }
    return rc;
}

int gpib_read(gpib_ctl *g, int pad, int sad, uint8_t *buf, unsigned cap, unsigned *got, int *end)
{
    int rc = address_device(g, pad, sad, 1);
    *got = 0;
    *end = 0;
    if (rc != TNT_OK) {
        return rc;
    }
    rc = tnt_go_to_standby(g->chip);
    if (rc != TNT_OK) {
        return rc;
    }
    rc = tnt_read(g->chip, buf, cap, got, end);
    if (tnt_take_control(g->chip, 0) != TNT_OK && rc == TNT_OK) {
        rc = TNT_EIO;
    }
    return rc;
}

static int addressed_command(gpib_ctl *g, int pad, int sad, uint8_t command)
{
    uint8_t cmd[GPIB_MAX_CMD];
    unsigned n = 0;
    if (!valid_address(pad, sad)) {
        return TNT_EINVAL;
    }
    cmd[n++] = GPIB_UNL;
    cmd[n++] = GPIB_LAD(pad);
    if (sad >= 0) {
        cmd[n++] = GPIB_SAD(sad);
    }
    cmd[n++] = command;
    return gpib_send_commands(g, cmd, n);
}

int gpib_clear(gpib_ctl *g, int pad, int sad)
{
    uint8_t dcl = GPIB_DCL;
    if (pad < 0) {
        return gpib_send_commands(g, &dcl, 1);
    }
    return addressed_command(g, pad, sad, GPIB_SDC);
}

int gpib_trigger(gpib_ctl *g, int pad, int sad)
{
    return addressed_command(g, pad, sad, GPIB_GET);
}

int gpib_local(gpib_ctl *g, int pad, int sad)
{
    return addressed_command(g, pad, sad, GPIB_GTL);
}

int gpib_local_lockout(gpib_ctl *g)
{
    uint8_t llo = GPIB_LLO;
    return gpib_send_commands(g, &llo, 1);
}

int gpib_serial_poll(gpib_ctl *g, int pad, int sad, uint8_t *status)
{
    uint8_t cmd[GPIB_MAX_CMD];
    uint8_t tail[2];
    unsigned n = 0;
    unsigned got = 0;
    int end = 0;
    int rc;

    *status = 0;
    if (!valid_address(pad, sad)) {
        return TNT_EINVAL;
    }
    cmd[n++] = GPIB_UNL;
    cmd[n++] = GPIB_LAD(g->chip->pad);
    cmd[n++] = GPIB_SPE;
    cmd[n++] = GPIB_TAD(pad);
    if (sad >= 0) {
        cmd[n++] = GPIB_SAD(sad);
    }
    rc = gpib_send_commands(g, cmd, n);
    if (rc == TNT_OK) {
        rc = tnt_go_to_standby(g->chip);
    }
    if (rc == TNT_OK) {
        rc = tnt_read(g->chip, status, 1, &got, &end);
        if (rc == TNT_OK && got != 1) {
            rc = TNT_EIO;
        }
    }
    if (tnt_take_control(g->chip, 0) != TNT_OK && rc == TNT_OK) {
        rc = TNT_EIO;
    }
    tail[0] = GPIB_SPD;
    tail[1] = GPIB_UNT;
    if (gpib_send_commands(g, tail, 2) != TNT_OK && rc == TNT_OK) {
        rc = TNT_EIO;
    }
    return rc;
}

int gpib_pass_control(gpib_ctl *g, int pad)
{
    uint8_t cmd[2];
    int rc;
    if (!valid_address(pad, -1)) {
        return TNT_EINVAL;
    }
    cmd[0] = GPIB_TAD(pad);
    cmd[1] = GPIB_TCT;
    rc = gpib_send_commands(g, cmd, 2);
    if (rc == TNT_OK) {
        rc = tnt_go_to_standby(g->chip);
    }
    return rc;
}

uint8_t gpib_lines(gpib_ctl *g)
{
    return tnt_bus_lines(g->chip);
}

int gpib_srq_asserted(gpib_ctl *g)
{
    return (tnt_bus_lines(g->chip) & TNT_BSR_SRQ) != 0;
}
