/* Host tests for the PnP identifier builder. */
#include <string.h>

#include "check.h"
#include "gpib/pnpid.h"

static void test_crc_check_value(void)
{
    /* CRC-16/ARC check value for "123456789" is 0xBB3D. */
    CHECK_EQ_INT(pnpid_crc16(0, (const uint8_t *)"123456789", 9), 0xBB3D);
    CHECK_EQ_INT(pnpid_crc16(0, (const uint8_t *)"", 0), 0);
}

static void test_strings_filtered(void)
{
    pnpid_builder b;
    char out[PNPID_MAX];
    static const uint8_t vers1[] = { 4, 1, 'N', 'a', 't', 'i', 'o', 'n', 'a', 'l', ' ', 'I', 'n', 's', 't', 0,
                                     'P', 'C', 'M', 'C', 'I', 'A', ',', 'G', 'P', 'I', 'B', 0x7F, 0x01, 0, 'x', 0 };
    pnpid_begin(&b);
    pnpid_add_tuple(&b, 0x15, vers1, sizeof vers1);
    CHECK(strcmp(b.manufacturer, "National_Inst") == 0);
    CHECK(strcmp(b.product, "PCMCIA_GPIB") == 0);
    CHECK(pnpid_finish(&b, out, sizeof out) > 0);
    CHECK(strncmp(out, "National_Inst-PCMCIA_GPIB-", 26) == 0);
    CHECK_EQ_INT(strlen(out), 30);
    /* the CRC covers the VERS_1 data only through the product's NUL (2 + 14 + 14 = 30 bytes) */
    {
        uint16_t expect = pnpid_crc16(0, vers1, 30);
        char want[5];
        static const char hex[] = "0123456789ABCDEF";
        want[0] = hex[expect >> 12]; want[1] = hex[(expect >> 8) & 15];
        want[2] = hex[(expect >> 4) & 15]; want[3] = hex[expect & 15]; want[4] = 0;
        CHECK(strcmp(out + 26, want) == 0);
    }
}

static void test_other_tuples_and_order(void)
{
    pnpid_builder b;
    char out[PNPID_MAX];
    static const uint8_t device[] = { 0xDF, 0x4A, 0x01, 0xFF };
    static const uint8_t manfid[] = { 0x0B, 0x01, 0x82, 0x48 };
    static const uint8_t vers1[] = { 4, 1, 'A', 0, 'B', 0 };
    static const uint8_t ignored[] = { 1, 2, 3 };
    uint16_t expect = 0;
    pnpid_begin(&b);
    pnpid_add_tuple(&b, 0x01, device, sizeof device);
    pnpid_add_tuple(&b, 0x21, ignored, sizeof ignored);      /* FUNCID: not part of the CRC */
    pnpid_add_tuple(&b, 0x15, vers1, sizeof vers1);
    pnpid_add_tuple(&b, 0x20, manfid, sizeof manfid);
    expect = pnpid_crc16(expect, device, sizeof device);
    expect = pnpid_crc16(expect, vers1, sizeof vers1);
    expect = pnpid_crc16(expect, manfid, sizeof manfid);
    CHECK_EQ_INT(b.crc, expect);
    pnpid_finish(&b, out, sizeof out);
    CHECK(strncmp(out, "A-B-", 4) == 0);
}

static void test_missing_strings(void)
{
    pnpid_builder b;
    char out[PNPID_MAX];
    static const uint8_t vers1[] = { 4, 1, 'O', 'n', 'l', 'y', 0, 0 };
    pnpid_begin(&b);
    pnpid_finish(&b, out, sizeof out);
    CHECK(strncmp(out, "UNKNOWN_MANUFACTURER-", 21) == 0);
    CHECK_EQ_INT(strlen(out), 25);
    pnpid_begin(&b);
    pnpid_add_tuple(&b, 0x15, vers1, sizeof vers1);
    pnpid_finish(&b, out, sizeof out);
    CHECK(strncmp(out, "Only-", 5) == 0);
    CHECK_EQ_INT(strlen(out), 9);
    CHECK_EQ_INT(pnpid_finish(&b, out, 4), 0);
}

static void test_truncation(void)
{
    pnpid_builder b;
    uint8_t vers1[2 + 200 + 1 + 100 + 1];
    unsigned i;
    vers1[0] = 4;
    vers1[1] = 1;
    for (i = 0; i < 200; i++) {
        vers1[2 + i] = 'M';
    }
    vers1[202] = 0;
    for (i = 0; i < 100; i++) {
        vers1[203 + i] = 'P';
    }
    vers1[303] = 0;
    pnpid_begin(&b);
    pnpid_add_tuple(&b, 0x15, vers1, sizeof vers1);
    CHECK_EQ_INT(strlen(b.manufacturer), PNPID_MANF_MAX);
    CHECK_EQ_INT(strlen(b.product), PNPID_PROD_MAX);
}

int main(void)
{
    test_crc_check_value();
    test_strings_filtered();
    test_other_tuples_and_order();
    test_missing_strings();
    test_truncation();
    return check_summary("test_pnpid");
}
