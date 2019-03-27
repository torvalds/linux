
#define TEST_NAME "aead_chacha20poly1305"
#include "cmptest.h"

static int
tv(void)
{
#undef  MLEN
#define MLEN 10U
#undef  ADLEN
#define ADLEN 10U
#undef  CLEN
#define CLEN (MLEN + crypto_aead_chacha20poly1305_ABYTES)
    static const unsigned char firstkey[crypto_aead_chacha20poly1305_KEYBYTES]
        = { 0x42, 0x90, 0xbc, 0xb1, 0x54, 0x17, 0x35, 0x31, 0xf3, 0x14, 0xaf,
            0x57, 0xf3, 0xbe, 0x3b, 0x50, 0x06, 0xda, 0x37, 0x1e, 0xce, 0x27,
            0x2a, 0xfa, 0x1b, 0x5d, 0xbd, 0xd1, 0x10, 0x0a, 0x10, 0x07 };
    static const unsigned char m[MLEN]
        = { 0x86, 0xd0, 0x99, 0x74, 0x84, 0x0b, 0xde, 0xd2, 0xa5, 0xca };
    static const unsigned char nonce[crypto_aead_chacha20poly1305_NPUBBYTES]
        = { 0xcd, 0x7c, 0xf6, 0x7b, 0xe3, 0x9c, 0x79, 0x4a };
    static const unsigned char ad[ADLEN]
        = { 0x87, 0xe2, 0x29, 0xd4, 0x50, 0x08, 0x45, 0xa0, 0x79, 0xc0 };
    unsigned char *c = (unsigned char *) sodium_malloc(CLEN);
    unsigned char *detached_c = (unsigned char *) sodium_malloc(MLEN);
    unsigned char *mac = (unsigned char *) sodium_malloc(crypto_aead_chacha20poly1305_ABYTES);
    unsigned char *m2 = (unsigned char *) sodium_malloc(MLEN);
    unsigned long long found_clen;
    unsigned long long found_maclen;
    unsigned long long m2len;
    size_t i;

    crypto_aead_chacha20poly1305_encrypt(c, &found_clen, m, MLEN,
                                         ad, ADLEN,
                                         NULL, nonce, firstkey);
    if (found_clen != CLEN) {
        printf("found_clen is not properly set\n");
    }
    for (i = 0U; i < CLEN; ++i) {
        printf(",0x%02x", (unsigned int) c[i]);
        if (i % 8 == 7) {
            printf("\n");
        }
    }
    printf("\n");
    crypto_aead_chacha20poly1305_encrypt_detached(detached_c,
                                                  mac, &found_maclen,
                                                  m, MLEN, ad, ADLEN,
                                                  NULL, nonce, firstkey);
    if (found_maclen != crypto_aead_chacha20poly1305_abytes()) {
        printf("found_maclen is not properly set\n");
    }
    if (memcmp(detached_c, c, MLEN) != 0) {
        printf("detached ciphertext is bogus\n");
    }

    if (crypto_aead_chacha20poly1305_decrypt(m2, &m2len, NULL, c, CLEN,
                                             ad, ADLEN,
                                             nonce, firstkey) != 0) {
        printf("crypto_aead_chacha20poly1305_decrypt() failed\n");
    }
    if (m2len != MLEN) {
        printf("m2len is not properly set\n");
    }
    if (memcmp(m, m2, MLEN) != 0) {
        printf("m != m2\n");
    }
    memset(m2, 0, m2len);
    assert(crypto_aead_chacha20poly1305_decrypt_detached(NULL, NULL,
                                                         c, MLEN, mac,
                                                         ad, ADLEN,
                                                         nonce, firstkey) == 0);
    if (crypto_aead_chacha20poly1305_decrypt_detached(m2, NULL,
                                                      c, MLEN, mac,
                                                      ad, ADLEN,
                                                      nonce, firstkey) != 0) {
        printf("crypto_aead_chacha20poly1305_decrypt_detached() failed\n");
    }
    if (memcmp(m, m2, MLEN) != 0) {
        printf("detached m != m2\n");
    }

    for (i = 0U; i < CLEN; i++) {
        c[i] ^= (i + 1U);
        if (crypto_aead_chacha20poly1305_decrypt(m2, NULL, NULL, c, CLEN,
                                                 ad, ADLEN, nonce, firstkey)
            == 0 || memcmp(m, m2, MLEN) == 0) {
            printf("message can be forged\n");
        }
        c[i] ^= (i + 1U);
    }

    crypto_aead_chacha20poly1305_encrypt(c, &found_clen, m, MLEN,
                                         NULL, 0U, NULL, nonce, firstkey);
    if (found_clen != CLEN) {
        printf("found_clen is not properly set (adlen=0)\n");
    }
    for (i = 0U; i < CLEN; ++i) {
        printf(",0x%02x", (unsigned int) c[i]);
        if (i % 8 == 7) {
            printf("\n");
        }
    }
    printf("\n");

    if (crypto_aead_chacha20poly1305_decrypt(m2, &m2len, NULL, c, CLEN,
                                             NULL, 0U, nonce, firstkey) != 0) {
        printf("crypto_aead_chacha20poly1305_decrypt() failed (adlen=0)\n");
    }
    if (m2len != MLEN) {
        printf("m2len is not properly set (adlen=0)\n");
    }
    if (memcmp(m, m2, MLEN) != 0) {
        printf("m != m2 (adlen=0)\n");
    }
    m2len = 1;
    if (crypto_aead_chacha20poly1305_decrypt(
            m2, &m2len, NULL, NULL,
            randombytes_uniform(crypto_aead_chacha20poly1305_ABYTES),
            NULL, 0U, nonce, firstkey) != -1) {
        printf("crypto_aead_chacha20poly1305_decrypt() worked with a short "
               "ciphertext\n");
    }
    if (m2len != 0) {
        printf("Message length should have been set to zero after a failure\n");
    }
    m2len = 1;
    if (crypto_aead_chacha20poly1305_decrypt(m2, &m2len, NULL, c, 0U, NULL, 0U,
                                             nonce, firstkey) != -1) {
        printf("crypto_aead_chacha20poly1305_decrypt() worked with an empty "
               "ciphertext\n");
    }
    if (m2len != 0) {
        printf("Message length should have been set to zero after a failure\n");
    }

    memcpy(c, m, MLEN);
    crypto_aead_chacha20poly1305_encrypt(c, &found_clen, c, MLEN,
                                         NULL, 0U, NULL, nonce, firstkey);
    if (found_clen != CLEN) {
        printf("found_clen is not properly set (adlen=0)\n");
    }
    for (i = 0U; i < CLEN; ++i) {
        printf(",0x%02x", (unsigned int) c[i]);
        if (i % 8 == 7) {
            printf("\n");
        }
    }
    printf("\n");

    if (crypto_aead_chacha20poly1305_decrypt(c, &m2len, NULL, c, CLEN,
                                             NULL, 0U, nonce, firstkey) != 0) {
        printf("crypto_aead_chacha20poly1305_decrypt() failed (adlen=0)\n");
    }
    if (m2len != MLEN) {
        printf("m2len is not properly set (adlen=0)\n");
    }
    if (memcmp(m, c, MLEN) != 0) {
        printf("m != c (adlen=0)\n");
    }

    sodium_free(c);
    sodium_free(detached_c);
    sodium_free(mac);
    sodium_free(m2);

    assert(crypto_aead_chacha20poly1305_keybytes() > 0U);
    assert(crypto_aead_chacha20poly1305_npubbytes() > 0U);
    assert(crypto_aead_chacha20poly1305_nsecbytes() == 0U);
    assert(crypto_aead_chacha20poly1305_messagebytes_max() > 0U);
    assert(crypto_aead_chacha20poly1305_messagebytes_max() == crypto_aead_chacha20poly1305_MESSAGEBYTES_MAX);
    assert(crypto_aead_chacha20poly1305_keybytes() == crypto_aead_chacha20poly1305_KEYBYTES);
    assert(crypto_aead_chacha20poly1305_nsecbytes() == crypto_aead_chacha20poly1305_NSECBYTES);
    assert(crypto_aead_chacha20poly1305_npubbytes() == crypto_aead_chacha20poly1305_NPUBBYTES);
    assert(crypto_aead_chacha20poly1305_abytes() == crypto_aead_chacha20poly1305_ABYTES);

    return 0;
}

static int
tv_ietf(void)
{
#undef  MLEN
#define MLEN 114U
#undef  ADLEN
#define ADLEN 12U
#undef  CLEN
#define CLEN (MLEN + crypto_aead_chacha20poly1305_ietf_ABYTES)
    static const unsigned char firstkey[crypto_aead_chacha20poly1305_ietf_KEYBYTES]
        = {
            0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
            0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
            0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
            0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f
        };
#undef  MESSAGE
#define MESSAGE "Ladies and Gentlemen of the class of '99: If I could offer you " \
"only one tip for the future, sunscreen would be it."
    unsigned char *m = (unsigned char *) sodium_malloc(MLEN);
    static const unsigned char nonce[crypto_aead_chacha20poly1305_ietf_NPUBBYTES]
        = { 0x07, 0x00, 0x00, 0x00,
            0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47 };
    static const unsigned char ad[ADLEN]
        = { 0x50, 0x51, 0x52, 0x53, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7 };
    unsigned char *c = (unsigned char *) sodium_malloc(CLEN);
    unsigned char *detached_c = (unsigned char *) sodium_malloc(MLEN);
    unsigned char *mac = (unsigned char *) sodium_malloc(crypto_aead_chacha20poly1305_ietf_ABYTES);
    unsigned char *m2 = (unsigned char *) sodium_malloc(MLEN);
    unsigned long long found_clen;
    unsigned long long found_maclen;
    unsigned long long m2len;
    size_t i;

    assert(sizeof MESSAGE - 1U == MLEN);
    memcpy(m, MESSAGE, MLEN);
    crypto_aead_chacha20poly1305_ietf_encrypt(c, &found_clen, m, MLEN,
                                              ad, ADLEN,
                                              NULL, nonce, firstkey);
    if (found_clen != MLEN + crypto_aead_chacha20poly1305_ietf_abytes()) {
        printf("found_clen is not properly set\n");
    }
    for (i = 0U; i < CLEN; ++i) {
        printf(",0x%02x", (unsigned int) c[i]);
        if (i % 8 == 7) {
            printf("\n");
        }
    }
    printf("\n");
    crypto_aead_chacha20poly1305_ietf_encrypt_detached(detached_c,
                                                       mac, &found_maclen,
                                                       m, MLEN,
                                                       ad, ADLEN,
                                                       NULL, nonce, firstkey);
    if (found_maclen != crypto_aead_chacha20poly1305_ietf_abytes()) {
        printf("found_maclen is not properly set\n");
    }
    if (memcmp(detached_c, c, MLEN) != 0) {
        printf("detached ciphertext is bogus\n");
    }

    if (crypto_aead_chacha20poly1305_ietf_decrypt(m2, &m2len, NULL, c, CLEN, ad,
                                                  ADLEN, nonce, firstkey) != 0) {
        printf("crypto_aead_chacha20poly1305_ietf_decrypt() failed\n");
    }
    if (m2len != MLEN) {
        printf("m2len is not properly set\n");
    }
    if (memcmp(m, m2, MLEN) != 0) {
        printf("m != m2\n");
    }
    memset(m2, 0, m2len);
    assert(crypto_aead_chacha20poly1305_ietf_decrypt_detached(NULL, NULL,
                                                              c, MLEN, mac,
                                                              ad, ADLEN,
                                                              nonce, firstkey) == 0);
    if (crypto_aead_chacha20poly1305_ietf_decrypt_detached(m2, NULL,
                                                           c, MLEN, mac,
                                                           ad, ADLEN,
                                                           nonce, firstkey) != 0) {
        printf("crypto_aead_chacha20poly1305_ietf_decrypt_detached() failed\n");
    }
    if (memcmp(m, m2, MLEN) != 0) {
        printf("detached m != m2\n");
    }

    for (i = 0U; i < CLEN; i++) {
        c[i] ^= (i + 1U);
        if (crypto_aead_chacha20poly1305_ietf_decrypt(m2, NULL, NULL, c, CLEN,
                                                      ad, ADLEN, nonce, firstkey)
            == 0 || memcmp(m, m2, MLEN) == 0) {
            printf("message can be forged\n");
        }
        c[i] ^= (i + 1U);
    }
    crypto_aead_chacha20poly1305_ietf_encrypt(c, &found_clen, m, MLEN,
                                              NULL, 0U, NULL, nonce, firstkey);
    if (found_clen != CLEN) {
        printf("clen is not properly set (adlen=0)\n");
    }
    for (i = 0U; i < CLEN; ++i) {
        printf(",0x%02x", (unsigned int) c[i]);
        if (i % 8 == 7) {
            printf("\n");
        }
    }
    printf("\n");
    if (crypto_aead_chacha20poly1305_ietf_decrypt(m2, &m2len, NULL, c, CLEN,
                                                  NULL, 0U, nonce, firstkey) != 0) {
        printf("crypto_aead_chacha20poly1305_ietf_decrypt() failed (adlen=0)\n");
    }
    if (m2len != MLEN) {
        printf("m2len is not properly set (adlen=0)\n");
    }
    if (memcmp(m, m2, MLEN) != 0) {
        printf("m != m2 (adlen=0)\n");
    }
    m2len = 1;
    if (crypto_aead_chacha20poly1305_ietf_decrypt(
            m2, &m2len, NULL, NULL,
            randombytes_uniform(crypto_aead_chacha20poly1305_ietf_ABYTES),
            NULL, 0U, nonce, firstkey) != -1) {
        printf("crypto_aead_chacha20poly1305_ietf_decrypt() worked with a short "
               "ciphertext\n");
    }
    if (m2len != 0) {
        printf("Message length should have been set to zero after a failure\n");
    }
    m2len = 1;
    if (crypto_aead_chacha20poly1305_ietf_decrypt(m2, &m2len, NULL, c, 0U, NULL, 0U,
                                                  nonce, firstkey) != -1) {
        printf("crypto_aead_chacha20poly1305_ietf_decrypt() worked with an empty "
               "ciphertext\n");
    }
    if (m2len != 0) {
        printf("Message length should have been set to zero after a failure\n");
    }

    memcpy(c, m, MLEN);
    crypto_aead_chacha20poly1305_ietf_encrypt(c, &found_clen, c, MLEN,
                                              NULL, 0U, NULL, nonce, firstkey);
    if (found_clen != CLEN) {
        printf("clen is not properly set (adlen=0)\n");
    }
    for (i = 0U; i < CLEN; ++i) {
        printf(",0x%02x", (unsigned int) c[i]);
        if (i % 8 == 7) {
            printf("\n");
        }
    }
    printf("\n");

    if (crypto_aead_chacha20poly1305_ietf_decrypt(c, &m2len, NULL, c, CLEN,
                                                  NULL, 0U, nonce, firstkey) != 0) {
        printf("crypto_aead_chacha20poly1305_ietf_decrypt() failed (adlen=0)\n");
    }
    if (m2len != MLEN) {
        printf("m2len is not properly set (adlen=0)\n");
    }
    if (memcmp(m, c, MLEN) != 0) {
        printf("m != c (adlen=0)\n");
    }

    sodium_free(c);
    sodium_free(detached_c);
    sodium_free(mac);
    sodium_free(m2);
    sodium_free(m);

    assert(crypto_aead_chacha20poly1305_ietf_keybytes() > 0U);
    assert(crypto_aead_chacha20poly1305_ietf_keybytes() == crypto_aead_chacha20poly1305_keybytes());
    assert(crypto_aead_chacha20poly1305_ietf_npubbytes() > 0U);
    assert(crypto_aead_chacha20poly1305_ietf_npubbytes() > crypto_aead_chacha20poly1305_npubbytes());
    assert(crypto_aead_chacha20poly1305_ietf_nsecbytes() == 0U);
    assert(crypto_aead_chacha20poly1305_ietf_nsecbytes() == crypto_aead_chacha20poly1305_nsecbytes());
    assert(crypto_aead_chacha20poly1305_ietf_messagebytes_max() == crypto_aead_chacha20poly1305_ietf_MESSAGEBYTES_MAX);
    assert(crypto_aead_chacha20poly1305_IETF_KEYBYTES  == crypto_aead_chacha20poly1305_ietf_KEYBYTES);
    assert(crypto_aead_chacha20poly1305_IETF_NSECBYTES == crypto_aead_chacha20poly1305_ietf_NSECBYTES);
    assert(crypto_aead_chacha20poly1305_IETF_NPUBBYTES == crypto_aead_chacha20poly1305_ietf_NPUBBYTES);
    assert(crypto_aead_chacha20poly1305_IETF_ABYTES    == crypto_aead_chacha20poly1305_ietf_ABYTES);
    assert(crypto_aead_chacha20poly1305_IETF_MESSAGEBYTES_MAX == crypto_aead_chacha20poly1305_ietf_MESSAGEBYTES_MAX);

    return 0;
}

int
main(void)
{
    tv();
    tv_ietf();

    return 0;
}
