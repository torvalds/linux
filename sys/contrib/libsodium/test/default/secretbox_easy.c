
#define TEST_NAME "secretbox_easy"
#include "cmptest.h"

static unsigned char firstkey[32] = { 0x1b, 0x27, 0x55, 0x64, 0x73, 0xe9, 0x85,
                                      0xd4, 0x62, 0xcd, 0x51, 0x19, 0x7a, 0x9a,
                                      0x46, 0xc7, 0x60, 0x09, 0x54, 0x9e, 0xac,
                                      0x64, 0x74, 0xf2, 0x06, 0xc4, 0xee, 0x08,
                                      0x44, 0xf6, 0x83, 0x89 };

static unsigned char nonce[24] = { 0x69, 0x69, 0x6e, 0xe9, 0x55, 0xb6,
                                   0x2b, 0x73, 0xcd, 0x62, 0xbd, 0xa8,
                                   0x75, 0xfc, 0x73, 0xd6, 0x82, 0x19,
                                   0xe0, 0x03, 0x6b, 0x7a, 0x0b, 0x37 };

static unsigned char m[131] = {
    0xbe, 0x07, 0x5f, 0xc5, 0x3c, 0x81, 0xf2, 0xd5, 0xcf, 0x14, 0x13, 0x16,
    0xeb, 0xeb, 0x0c, 0x7b, 0x52, 0x28, 0xc5, 0x2a, 0x4c, 0x62, 0xcb, 0xd4,
    0x4b, 0x66, 0x84, 0x9b, 0x64, 0x24, 0x4f, 0xfc, 0xe5, 0xec, 0xba, 0xaf,
    0x33, 0xbd, 0x75, 0x1a, 0x1a, 0xc7, 0x28, 0xd4, 0x5e, 0x6c, 0x61, 0x29,
    0x6c, 0xdc, 0x3c, 0x01, 0x23, 0x35, 0x61, 0xf4, 0x1d, 0xb6, 0x6c, 0xce,
    0x31, 0x4a, 0xdb, 0x31, 0x0e, 0x3b, 0xe8, 0x25, 0x0c, 0x46, 0xf0, 0x6d,
    0xce, 0xea, 0x3a, 0x7f, 0xa1, 0x34, 0x80, 0x57, 0xe2, 0xf6, 0x55, 0x6a,
    0xd6, 0xb1, 0x31, 0x8a, 0x02, 0x4a, 0x83, 0x8f, 0x21, 0xaf, 0x1f, 0xde,
    0x04, 0x89, 0x77, 0xeb, 0x48, 0xf5, 0x9f, 0xfd, 0x49, 0x24, 0xca, 0x1c,
    0x60, 0x90, 0x2e, 0x52, 0xf0, 0xa0, 0x89, 0xbc, 0x76, 0x89, 0x70, 0x40,
    0xe0, 0x82, 0xf9, 0x37, 0x76, 0x38, 0x48, 0x64, 0x5e, 0x07, 0x05
};

int
main(void)
{
    unsigned char *c;
    unsigned char *mac;
    size_t         i;

    c   = (unsigned char *) sodium_malloc(131 + crypto_secretbox_MACBYTES + 1);
    mac = (unsigned char *) sodium_malloc(crypto_secretbox_MACBYTES);
    assert(c != NULL && mac != NULL);

    crypto_secretbox_easy(c, m, 131, nonce, firstkey);
    for (i = 0; i < 131 + crypto_secretbox_MACBYTES; ++i) {
        printf(",0x%02x", (unsigned int) c[i]);
    }
    printf("\n");

    crypto_secretbox_detached(c, mac, m, 131, nonce, firstkey);
    for (i = 0; i < crypto_secretbox_MACBYTES; ++i) {
        printf(",0x%02x", (unsigned int) mac[i]);
    }
    for (i = 0; i < 131; ++i) {
        printf(",0x%02x", (unsigned int) c[i]);
    }
    printf("\n");

    /* Same test, with c and m overlapping */

    memcpy(c + 1, m, 131);
    crypto_secretbox_easy(c, c + 1, 131, nonce, firstkey);
    for (i = 0; i < 131 + crypto_secretbox_MACBYTES; ++i) {
        printf(",0x%02x", (unsigned int) c[i]);
    }
    printf("\n");

    memcpy(c, m, 131);
    crypto_secretbox_easy(c + 1, c, 131, nonce, firstkey);
    for (i = 0; i < 131 + crypto_secretbox_MACBYTES; ++i) {
        printf(",0x%02x", (unsigned int) c[i + 1]);
    }
    printf("\n");

    memcpy(c, m, 131);
    crypto_secretbox_easy(c, c, 131, nonce, firstkey);
    for (i = 0; i < 131 + crypto_secretbox_MACBYTES; ++i) {
        printf(",0x%02x", (unsigned int) c[i]);
    }
    printf("\n");

    assert(crypto_secretbox_easy(c, m, 0, nonce, firstkey) == 0);

    /* Null message */

    crypto_secretbox_easy(c, c, 0, nonce, firstkey);
    for (i = 0; i < crypto_secretbox_MACBYTES + 1; ++i) {
        printf(",0x%02x", (unsigned int) c[i]);
    }
    printf("\n");
    if (crypto_secretbox_open_easy(c, c, crypto_secretbox_MACBYTES, nonce,
                                   firstkey) != 0) {
        printf("Null crypto_secretbox_open_easy() failed\n");
    }
    for (i = 0; i < crypto_secretbox_MACBYTES + 1; ++i) {
        printf(",0x%02x", (unsigned int) c[i]);
    }
    printf("\n");
    c[randombytes_uniform(crypto_secretbox_MACBYTES)]++;
    if (crypto_secretbox_open_easy(c, c, crypto_secretbox_MACBYTES, nonce,
                                   firstkey) != -1) {
        printf("Null tampered crypto_secretbox_open_easy() failed\n");
    }

    /* No overlap, but buffers are next to each other */

    memset(c, 0, 131 + crypto_secretbox_MACBYTES + 1);
    memcpy(c, m, 20);
    crypto_secretbox_easy(c, c + 10, 10, nonce, firstkey);
    for (i = 0; i < 10 + crypto_secretbox_MACBYTES; ++i) {
        printf(",0x%02x", (unsigned int) c[i]);
    }
    printf("\n");

    memset(c, 0, 131 + crypto_secretbox_MACBYTES + 1);
    memcpy(c, m, 20);
    crypto_secretbox_easy(c + 10, c, 10, nonce, firstkey);
    for (i = 0; i < 10 + crypto_secretbox_MACBYTES; ++i) {
        printf(",0x%02x", (unsigned int) c[i]);
    }
    printf("\n");

    sodium_free(mac);
    sodium_free(c);

    return 0;
}
