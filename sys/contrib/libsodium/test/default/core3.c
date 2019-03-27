
#define TEST_NAME "core3"
#include "cmptest.h"

static unsigned char SECONDKEY[32] = { 0xdc, 0x90, 0x8d, 0xda, 0x0b, 0x93, 0x44,
                                       0xa9, 0x53, 0x62, 0x9b, 0x73, 0x38, 0x20,
                                       0x77, 0x88, 0x80, 0xf3, 0xce, 0xb4, 0x21,
                                       0xbb, 0x61, 0xb9, 0x1c, 0xbd, 0x4c, 0x3e,
                                       0x66, 0x25, 0x6c, 0xe4 };

static unsigned char NONCESUFFIX[8] = { 0x82, 0x19, 0xe0, 0x03,
                                        0x6b, 0x7a, 0x0b, 0x37 };

static unsigned char C[16] = { 0x65, 0x78, 0x70, 0x61, 0x6e, 0x64, 0x20, 0x33,
                               0x32, 0x2d, 0x62, 0x79, 0x74, 0x65, 0x20, 0x6b };

int
main(void)
{
    unsigned char *secondkey;
    unsigned char *c;
    unsigned char *noncesuffix;
    unsigned char *in;
    unsigned char *output;
    unsigned char *h;
    size_t         output_len = 64 * 256 * 256;
    size_t         pos = 0;
    int            i;

    pos = 0;
    secondkey = (unsigned char *) sodium_malloc(32);
    memcpy(secondkey, SECONDKEY, 32);
    noncesuffix = (unsigned char *) sodium_malloc(8);
    memcpy(noncesuffix, NONCESUFFIX, 8);
    c = (unsigned char *) sodium_malloc(16);
    memcpy(c, C, 16);
    in = (unsigned char *) sodium_malloc(16);
    output = (unsigned char *) sodium_malloc(output_len);
    h = (unsigned char *) sodium_malloc(32);

    for (i = 0; i < 8; i++) {
        in[i] = noncesuffix[i];
    }
    for (; i < 16; i++) {
        in[i] = 0;
    }
    do {
        do {
            crypto_core_salsa20(output + pos, in, secondkey, c);
            pos += 64;
            in[8]++;
        } while (in[8] != 0);
        in[9]++;
    } while (in[9] != 0);

    crypto_hash_sha256(h, output, output_len);

    for (i = 0; i < 32; ++i) {
        printf("%02x", h[i]);
    }
    printf("\n");

#ifndef SODIUM_LIBRARY_MINIMAL
    pos = 0;
    do {
        do {
            crypto_core_salsa2012(output + pos, in, secondkey, c);
            pos += 64;
            in[8]++;
        } while (in[8] != 0);
        in[9]++;
    } while (in[9] != 0);

    crypto_hash_sha256(h, output, output_len);

    for (i = 0; i < 32; ++i) {
        printf("%02x", h[i]);
    }
    printf("\n");

    pos = 0;
    do {
        do {
            crypto_core_salsa208(output + pos, in, secondkey, c);
            pos += 64;
            in[8]++;
        } while (in[8] != 0);
        in[9]++;
    } while (in[9] != 0);

    crypto_hash_sha256(h, output, output_len);

    for (i = 0; i < 32; ++i) {
        printf("%02x", h[i]);
    }
    printf("\n");
#else
    printf("a4e3147dddd2ba7775939b50208a22eb3277d4e4bad8a1cfbc999c6bd392b638\n"
           "017421baa9959cbe894bd003ec87938254f47c1e757eb66cf89c353d0c2b68de\n");
#endif

    sodium_free(h);
    sodium_free(output);
    sodium_free(in);
    sodium_free(c);
    sodium_free(noncesuffix);
    sodium_free(secondkey);

    assert(crypto_core_salsa20_outputbytes() == crypto_core_salsa20_OUTPUTBYTES);
    assert(crypto_core_salsa20_inputbytes() == crypto_core_salsa20_INPUTBYTES);
    assert(crypto_core_salsa20_keybytes() == crypto_core_salsa20_KEYBYTES);
    assert(crypto_core_salsa20_constbytes() == crypto_core_salsa20_CONSTBYTES);

    return 0;
}
