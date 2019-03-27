#define TEST_NAME "codecs"
#include "cmptest.h"

int
main(void)
{
    unsigned char  buf1[1000];
    char           buf3[33];
    unsigned char  buf4[4];
    const char    *b64;
    char          *b64_;
    const char    *b64_end;
    unsigned char *bin;
    unsigned char *bin_padded;
    const char    *hex;
    const char    *hex_end;
    size_t         b64_len;
    size_t         bin_len, bin_len2;
    unsigned int   i;

    printf("%s\n",
           sodium_bin2hex(buf3, 33U, (const unsigned char *) "0123456789ABCDEF",
                          16U));
    hex = "Cafe : 6942";
    sodium_hex2bin(buf4, sizeof buf4, hex, strlen(hex), ": ", &bin_len,
                   &hex_end);
    printf("%lu:%02x%02x%02x%02x\n", (unsigned long) bin_len,
           buf4[0], buf4[1], buf4[2], buf4[3]);
    printf("dt1: %ld\n", (long) (hex_end - hex));

    hex = "Cafe : 6942";
    sodium_hex2bin(buf4, sizeof buf4, hex, strlen(hex), ": ", &bin_len, NULL);
    printf("%lu:%02x%02x%02x%02x\n", (unsigned long) bin_len,
           buf4[0], buf4[1], buf4[2], buf4[3]);

    hex = "deadbeef";
    if (sodium_hex2bin(buf1, 1U, hex, 8U, NULL, &bin_len, &hex_end) != -1) {
        printf("sodium_hex2bin() overflow not detected\n");
    }
    printf("dt2: %ld\n", (long) (hex_end - hex));

    hex = "de:ad:be:eff";
    if (sodium_hex2bin(buf1, 4U, hex, 12U, ":", &bin_len, &hex_end) != -1) {
        printf(
            "sodium_hex2bin() with an odd input length and a short output "
            "buffer\n");
    }
    printf("dt3: %ld\n", (long) (hex_end - hex));

    hex = "de:ad:be:eff";
    if (sodium_hex2bin(buf1, sizeof buf1, hex, 12U, ":",
                       &bin_len, &hex_end) != -1) {
        printf("sodium_hex2bin() with an odd input length\n");
    }
    printf("dt4: %ld\n", (long) (hex_end - hex));

    hex = "de:ad:be:eff";
    if (sodium_hex2bin(buf1, sizeof buf1, hex, 13U, ":",
                       &bin_len, &hex_end) != -1) {
        printf("sodium_hex2bin() with an odd input length (2)\n");
    }
    printf("dt5: %ld\n", (long) (hex_end - hex));

    hex = "de:ad:be:eff";
    if (sodium_hex2bin(buf1, sizeof buf1, hex, 12U, ":",
                       &bin_len, NULL) != -1) {
        printf("sodium_hex2bin() with an odd input length and no end pointer\n");
    }

    hex = "de:ad:be:ef*";
    if (sodium_hex2bin(buf1, sizeof buf1, hex, 12U, ":",
                       &bin_len, &hex_end) != 0) {
        printf("sodium_hex2bin() with an extra character and an end pointer\n");
    }
    printf("dt6: %ld\n", (long) (hex_end - hex));

    hex = "de:ad:be:ef*";
    if (sodium_hex2bin(buf1, sizeof buf1, hex, 12U, ":",
                       &bin_len, NULL) != -1) {
        printf("sodium_hex2bin() with an extra character and no end pointer\n");
    }

    printf("%s\n",
           sodium_bin2base64(buf3, 31U, (const unsigned char *) "\xfb\xf0\xf1" "0123456789ABCDEFab",
                             21U, sodium_base64_VARIANT_ORIGINAL));
    printf("%s\n",
           sodium_bin2base64(buf3, 33U, (const unsigned char *) "\xfb\xf0\xf1" "0123456789ABCDEFabc",
                             22U, sodium_base64_VARIANT_ORIGINAL_NO_PADDING));
    printf("%s\n",
           sodium_bin2base64(buf3, 31U, (const unsigned char *) "\xfb\xf0\xf1" "0123456789ABCDEFab",
                             21U, sodium_base64_VARIANT_URLSAFE));
    printf("%s\n",
           sodium_bin2base64(buf3, 33U, (const unsigned char *) "\xfb\xf0\xf1" "0123456789ABCDEFabc",
                             22U, sodium_base64_VARIANT_URLSAFE_NO_PADDING));
    printf("%s\n",
           sodium_bin2base64(buf3, 1U, NULL,
                             0U, sodium_base64_VARIANT_ORIGINAL));
    printf("%s\n",
           sodium_bin2base64(buf3, 5U, (const unsigned char *) "a",
                             1U, sodium_base64_VARIANT_ORIGINAL));
    printf("%s\n",
           sodium_bin2base64(buf3, 5U, (const unsigned char *) "ab",
                             2U, sodium_base64_VARIANT_ORIGINAL));
    printf("%s\n",
           sodium_bin2base64(buf3, 5U, (const unsigned char *) "abc",
                             3U, sodium_base64_VARIANT_ORIGINAL));
    printf("%s\n",
           sodium_bin2base64(buf3, 1U, NULL,
                             0U, sodium_base64_VARIANT_ORIGINAL_NO_PADDING));
    printf("%s\n",
           sodium_bin2base64(buf3, 3U, (const unsigned char *) "a",
                             1U, sodium_base64_VARIANT_ORIGINAL_NO_PADDING));
    printf("%s\n",
           sodium_bin2base64(buf3, 4U, (const unsigned char *) "ab",
                             2U, sodium_base64_VARIANT_ORIGINAL_NO_PADDING));
    printf("%s\n",
           sodium_bin2base64(buf3, 5U, (const unsigned char *) "abc",
                             3U, sodium_base64_VARIANT_ORIGINAL_NO_PADDING));

    b64 = "VGhpcyBpcyBhIGpvdXJu" "\n" "ZXkgaW50by" " " "Bzb3VuZA==";
    memset(buf4, '*', sizeof buf4);
    assert(sodium_base642bin(buf4, sizeof buf4, b64, strlen(b64), "\n\r ", &bin_len,
                             &b64_end, sodium_base64_VARIANT_ORIGINAL) == -1);
    buf4[bin_len] = 0;
    printf("[%s]\n", (const char *) buf4);
    printf("[%s]\n", b64_end);

    memset(buf1, '*', sizeof buf1);
    assert(sodium_base642bin(buf1, sizeof buf1, b64, strlen(b64), "\n\r ", &bin_len,
                             &b64_end, sodium_base64_VARIANT_ORIGINAL) == 0);
    buf1[bin_len] = 0;
    printf("[%s]\n", (const char *) buf1);
    assert(*b64_end == 0);

    memset(buf1, '*', sizeof buf1);
    assert(sodium_base642bin(buf1, sizeof buf1, b64, strlen(b64), NULL, &bin_len,
                             &b64_end, sodium_base64_VARIANT_ORIGINAL) == 0);
    buf1[bin_len] = 0;
    printf("[%s]\n", (const char *) buf1);
    printf("[%s]\n", b64_end);

    assert(sodium_base642bin(buf1, sizeof buf1, b64, strlen(b64), NULL, NULL,
                             &b64_end, sodium_base64_VARIANT_ORIGINAL) == 0);
    assert(sodium_base642bin(buf1, sizeof buf1, b64, strlen(b64), NULL, NULL,
                             &b64_end, sodium_base64_VARIANT_ORIGINAL_NO_PADDING) == 0);
    assert(sodium_base642bin(buf1, sizeof buf1, b64, strlen(b64), " \r\n", NULL,
                             &b64_end, sodium_base64_VARIANT_ORIGINAL_NO_PADDING) == 0);
    assert(sodium_base642bin(buf1, sizeof buf1, b64, strlen(b64), NULL, NULL,
                             &b64_end, sodium_base64_VARIANT_URLSAFE_NO_PADDING) == 0);
    assert(sodium_base642bin(buf1, sizeof buf1, b64, strlen(b64), " \r\n", NULL,
                             &b64_end, sodium_base64_VARIANT_URLSAFE_NO_PADDING) == 0);

    assert(sodium_base642bin(buf1, sizeof buf1, b64, strlen(b64), NULL, NULL,
                             NULL, sodium_base64_VARIANT_ORIGINAL) == -1);
    assert(sodium_base642bin(buf1, sizeof buf1, b64, strlen(b64), NULL, NULL,
                             NULL, sodium_base64_VARIANT_ORIGINAL_NO_PADDING) == -1);
    assert(sodium_base642bin(buf1, sizeof buf1, b64, strlen(b64), " \r\n", NULL,
                             NULL, sodium_base64_VARIANT_ORIGINAL_NO_PADDING) == -1);
    assert(sodium_base642bin(buf1, sizeof buf1, b64, strlen(b64), NULL, NULL,
                             NULL, sodium_base64_VARIANT_URLSAFE_NO_PADDING) == -1);
    assert(sodium_base642bin(buf1, sizeof buf1, b64, strlen(b64), " \r\n", NULL,
                             NULL, sodium_base64_VARIANT_URLSAFE_NO_PADDING) == -1);

    assert(sodium_base642bin(NULL, (size_t) 10U, "a=", (size_t) 2U, NULL, NULL, NULL,
                             sodium_base64_VARIANT_URLSAFE) == -1);
    assert(sodium_base642bin(NULL, (size_t) 10U, "a*", (size_t) 2U, NULL, NULL, NULL,
                             sodium_base64_VARIANT_URLSAFE) == -1);
    assert(sodium_base642bin(NULL, (size_t) 10U, "a*", (size_t) 2U, "~", NULL, NULL,
                             sodium_base64_VARIANT_URLSAFE) == -1);
    assert(sodium_base642bin(NULL, (size_t) 10U, "a*", (size_t) 2U, "*", NULL, NULL,
                             sodium_base64_VARIANT_URLSAFE) == -1);
    assert(sodium_base642bin(NULL, (size_t) 10U, "a==", (size_t) 3U, NULL, NULL, NULL,
                             sodium_base64_VARIANT_URLSAFE) == -1);
    assert(sodium_base642bin(NULL, (size_t) 10U, "a=*", (size_t) 3U, NULL, NULL, NULL,
                             sodium_base64_VARIANT_URLSAFE) == -1);
    assert(sodium_base642bin(NULL, (size_t) 10U, "a=*", (size_t) 3U, "~", NULL, NULL,
                             sodium_base64_VARIANT_URLSAFE) == -1);
    assert(sodium_base642bin(NULL, (size_t) 10U, "a=*", (size_t) 3U, "*", NULL, NULL,
                             sodium_base64_VARIANT_URLSAFE) == -1);

    assert(sodium_base642bin(buf1, sizeof buf1, "O1R", (size_t) 3U, NULL, NULL, NULL,
                             sodium_base64_VARIANT_ORIGINAL_NO_PADDING) == -1);
    assert(sodium_base642bin(buf1, sizeof buf1, "O1Q", (size_t) 3U, NULL, NULL, NULL,
                             sodium_base64_VARIANT_ORIGINAL_NO_PADDING) == 0);
    assert(sodium_base642bin(buf1, sizeof buf1, "O1", (size_t) 2U, NULL, NULL, NULL,
                             sodium_base64_VARIANT_ORIGINAL_NO_PADDING) == -1);
    assert(sodium_base642bin(buf1, sizeof buf1, "Ow", (size_t) 2U, NULL, NULL, NULL,
                             sodium_base64_VARIANT_ORIGINAL_NO_PADDING) == 0);
    assert(sodium_base642bin(buf1, sizeof buf1, "O", (size_t) 1U, NULL, NULL, NULL,
                             sodium_base64_VARIANT_ORIGINAL_NO_PADDING) == -1);

    assert(sodium_base642bin(buf1, sizeof buf1, "kaw", (size_t) 3U, NULL, NULL, NULL,
                             sodium_base64_VARIANT_ORIGINAL) == -1);
    assert(sodium_base642bin(buf1, sizeof buf1, "kQ*", (size_t) 3U, "@", NULL, NULL,
                             sodium_base64_VARIANT_ORIGINAL) == -1);
    assert(sodium_base642bin(buf1, sizeof buf1, "kQ*", (size_t) 3U, "*", NULL, NULL,
                             sodium_base64_VARIANT_ORIGINAL) == -1);
    assert(sodium_base642bin(buf1, sizeof buf1, "kaw=**", (size_t) 6U, "*", NULL, NULL,
                             sodium_base64_VARIANT_ORIGINAL) == 0);
    assert(sodium_base642bin(buf1, sizeof buf1, "kaw*=*", (size_t) 6U, "~*", NULL, NULL,
                             sodium_base64_VARIANT_ORIGINAL) == 0);
    assert(sodium_base642bin(buf1, sizeof buf1, "ka*w*=*", (size_t) 7U, "*~", NULL, NULL,
                             sodium_base64_VARIANT_ORIGINAL) == 0);

    for (i = 0; i < 1000; i++) {
        assert(sizeof buf1 >= 100);
        bin_len = (size_t) randombytes_uniform(100);
        bin = (unsigned char *) sodium_malloc(bin_len);
        b64_len = (bin_len + 2U) / 3U * 4U + 1U;
        assert(b64_len == sodium_base64_encoded_len(bin_len, sodium_base64_VARIANT_URLSAFE));
        b64_ = (char *) sodium_malloc(b64_len);
        randombytes_buf(bin, bin_len);
        memcpy(buf1, bin, bin_len);
        b64 = sodium_bin2base64(b64_, b64_len, bin, bin_len,
                                sodium_base64_VARIANT_URLSAFE);
        assert(b64 != NULL);
        assert(sodium_base642bin(bin, bin_len + 10, b64, b64_len,
                                 NULL, NULL, &b64_end,
                                 sodium_base64_VARIANT_URLSAFE) == 0);
        assert(b64_end == &b64[b64_len - 1]);
        assert(memcmp(bin, buf1, bin_len) == 0);
        sodium_free(bin);
        sodium_free(b64_);
    }
    return 0;
}
