
#define TEST_NAME "chacha20"
#include "cmptest.h"

static
void tv(void)
{
    static struct {
        const char *key_hex;
        const char *nonce_hex;
    } tests[]
      = { { "0000000000000000000000000000000000000000000000000000000000000000",
            "0000000000000000" },
          { "0000000000000000000000000000000000000000000000000000000000000001",
            "0000000000000000" },
          { "0000000000000000000000000000000000000000000000000000000000000000",
            "0000000000000001" },
          { "0000000000000000000000000000000000000000000000000000000000000000",
            "0100000000000000" },
          { "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
            "0001020304050607" } };
    unsigned char  key[crypto_stream_chacha20_KEYBYTES];
    unsigned char  nonce[crypto_stream_chacha20_NONCEBYTES];
    unsigned char *part;
    unsigned char  out[160];
    unsigned char  zero[160];
    char           out_hex[160 * 2 + 1];
    size_t         i = 0U;
    size_t         plen;

    memset(zero, 0, sizeof zero);
    do {
        sodium_hex2bin((unsigned char *)key, sizeof key, tests[i].key_hex,
                       strlen(tests[i].key_hex), NULL, NULL, NULL);
        sodium_hex2bin(nonce, sizeof nonce, tests[i].nonce_hex,
                       strlen(tests[i].nonce_hex), NULL, NULL, NULL);
        crypto_stream_chacha20(out, sizeof out, nonce, key);
        sodium_bin2hex(out_hex, sizeof out_hex, out, sizeof out);
        printf("[%s]\n", out_hex);
        for (plen = 1U; plen < sizeof out; plen++) {
            part = (unsigned char *) sodium_malloc(plen);
            crypto_stream_chacha20_xor(part, out, plen, nonce, key);
            if (memcmp(part, zero, plen) != 0) {
                printf("Failed with length %lu\n", (unsigned long) plen);
            }
            sodium_free(part);
        }
    } while (++i < (sizeof tests) / (sizeof tests[0]));
    assert(66 <= sizeof out);
    for (plen = 1U; plen < 66; plen += 3) {
        memset(out, (int) (plen & 0xff), sizeof out);
        crypto_stream_chacha20(out, plen, nonce, key);
        sodium_bin2hex(out_hex, sizeof out_hex, out, sizeof out);
        printf("[%s]\n", out_hex);
    }
    randombytes_buf(out, sizeof out);
    crypto_stream_chacha20(out, sizeof out, nonce, key);
    sodium_bin2hex(out_hex, sizeof out_hex, out, sizeof out);
    printf("[%s]\n", out_hex);

    assert(crypto_stream_chacha20(out, 0U, nonce, key) == 0);
    assert(crypto_stream_chacha20_xor(out, out, 0U, nonce, key) == 0);
    assert(crypto_stream_chacha20_xor(out, out, 0U, nonce, key) == 0);
    assert(crypto_stream_chacha20_xor_ic(out, out, 0U, nonce, 1U, key) == 0);

    memset(out, 0x42, sizeof out);
    crypto_stream_chacha20_xor(out, out, sizeof out, nonce, key);
    sodium_bin2hex(out_hex, sizeof out_hex, out, sizeof out);
    printf("[%s]\n", out_hex);

    crypto_stream_chacha20_xor_ic(out, out, sizeof out, nonce, 0U, key);
    sodium_bin2hex(out_hex, sizeof out_hex, out, sizeof out);
    printf("[%s]\n", out_hex);

    crypto_stream_chacha20_xor_ic(out, out, sizeof out, nonce, 1U, key);
    sodium_bin2hex(out_hex, sizeof out_hex, out, sizeof out);
    printf("[%s]\n", out_hex);
}

static
void tv_ietf(void)
{
    static struct {
        const char *key_hex;
        const char *nonce_hex;
        uint32_t    ic;
    } tests[]
      = { { "0000000000000000000000000000000000000000000000000000000000000000",
            "000000000000000000000000",
            0U },
          { "0000000000000000000000000000000000000000000000000000000000000000",
            "000000000000000000000000",
            1U },
          { "0000000000000000000000000000000000000000000000000000000000000001",
            "000000000000000000000000",
            1U },
          { "00ff000000000000000000000000000000000000000000000000000000000000",
            "000000000000000000000000",
            2U },
          { "0000000000000000000000000000000000000000000000000000000000000000",
            "000000000000000000000002",
            0U },
          { "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
            "000000090000004a00000000",
            1U },
          { "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
            "000000090000004a00000000",
            0xffffffff }};
    unsigned char  key[crypto_stream_chacha20_KEYBYTES];
    unsigned char  nonce[crypto_stream_chacha20_IETF_NONCEBYTES];
    unsigned char *part;
    unsigned char  out[160];
    unsigned char  zero[160];
    char           out_hex[160 * 2 + 1];
    size_t         i = 0U;
    size_t         plen;

    memset(zero, 0, sizeof zero);
    do {
        sodium_hex2bin((unsigned char *)key, sizeof key, tests[i].key_hex,
                       strlen(tests[i].key_hex), ": ", NULL, NULL);
        sodium_hex2bin(nonce, sizeof nonce, tests[i].nonce_hex,
                       strlen(tests[i].nonce_hex), ": ", NULL, NULL);
        memset(out, 0, sizeof out);
        crypto_stream_chacha20_ietf_xor_ic(out, out, sizeof out, nonce, tests[i].ic, key);
        sodium_bin2hex(out_hex, sizeof out_hex, out, sizeof out);
        printf("[%s]\n", out_hex);
        for (plen = 1U; plen < sizeof out; plen++) {
            part = (unsigned char *) sodium_malloc(plen);
            crypto_stream_chacha20_ietf_xor_ic(part, out, plen, nonce, tests[i].ic, key);
            if (memcmp(part, zero, plen) != 0) {
                printf("Failed with length %lu\n", (unsigned long) plen);
            }
            sodium_free(part);
        }
    } while (++i < (sizeof tests) / (sizeof tests[0]));
    assert(66 <= sizeof out);
    for (plen = 1U; plen < 66; plen += 3) {
        memset(out, (int) (plen & 0xff), sizeof out);
        crypto_stream_chacha20(out, plen, nonce, key);
        sodium_bin2hex(out_hex, sizeof out_hex, out, sizeof out);
        printf("[%s]\n", out_hex);
    }
    randombytes_buf(out, sizeof out);
    crypto_stream_chacha20_ietf(out, sizeof out, nonce, key);
    sodium_bin2hex(out_hex, sizeof out_hex, out, sizeof out);
    printf("[%s]\n", out_hex);

    assert(crypto_stream_chacha20_ietf(out, 0U, nonce, key) == 0);
    assert(crypto_stream_chacha20_ietf_xor(out, out, 0U, nonce, key) == 0);
    assert(crypto_stream_chacha20_ietf_xor(out, out, 0U, nonce, key) == 0);
    assert(crypto_stream_chacha20_ietf_xor_ic(out, out, 0U, nonce, 1U, key) == 0);

    memset(out, 0x42, sizeof out);
    crypto_stream_chacha20_ietf_xor(out, out, sizeof out, nonce, key);
    sodium_bin2hex(out_hex, sizeof out_hex, out, sizeof out);
    printf("[%s]\n", out_hex);

    crypto_stream_chacha20_ietf_xor_ic(out, out, sizeof out, nonce, 0U, key);
    sodium_bin2hex(out_hex, sizeof out_hex, out, sizeof out);
    printf("[%s]\n", out_hex);

    crypto_stream_chacha20_ietf_xor_ic(out, out, sizeof out, nonce, 1U, key);
    sodium_bin2hex(out_hex, sizeof out_hex, out, sizeof out);
    printf("[%s]\n", out_hex);
}

int
main(void)
{
    tv();
    tv_ietf();

    assert(crypto_stream_chacha20_keybytes() > 0U);
    assert(crypto_stream_chacha20_keybytes() == crypto_stream_chacha20_KEYBYTES);
    assert(crypto_stream_chacha20_noncebytes() > 0U);
    assert(crypto_stream_chacha20_noncebytes() == crypto_stream_chacha20_NONCEBYTES);
    assert(crypto_stream_chacha20_messagebytes_max() == crypto_stream_chacha20_MESSAGEBYTES_MAX);
    assert(crypto_stream_chacha20_ietf_keybytes() > 0U);
    assert(crypto_stream_chacha20_ietf_keybytes() == crypto_stream_chacha20_ietf_KEYBYTES);
    assert(crypto_stream_chacha20_ietf_noncebytes() > 0U);
    assert(crypto_stream_chacha20_ietf_noncebytes() == crypto_stream_chacha20_ietf_NONCEBYTES);
    assert(crypto_stream_chacha20_ietf_messagebytes_max() == crypto_stream_chacha20_ietf_MESSAGEBYTES_MAX);

    return 0;
}
