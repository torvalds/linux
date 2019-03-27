
#define TEST_NAME "aead_xchacha20poly1305"
#include "cmptest.h"

static int
tv(void)
{
#undef  MLEN
#define MLEN 114U
#undef  ADLEN
#define ADLEN 12U
#undef  CLEN
#define CLEN (MLEN + crypto_aead_xchacha20poly1305_ietf_ABYTES)
    static const unsigned char firstkey[crypto_aead_xchacha20poly1305_ietf_KEYBYTES]
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
    static const unsigned char nonce[crypto_aead_xchacha20poly1305_ietf_NPUBBYTES]
        = { 0x07, 0x00, 0x00, 0x00, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
            0x48, 0x49, 0x4a, 0x4b };
    static const unsigned char ad[ADLEN]
        = { 0x50, 0x51, 0x52, 0x53, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7 };
    unsigned char *c = (unsigned char *) sodium_malloc(CLEN);
    unsigned char *detached_c = (unsigned char *) sodium_malloc(MLEN);
    unsigned char *key2 = (unsigned char *) sodium_malloc(crypto_aead_xchacha20poly1305_ietf_KEYBYTES);
    unsigned char *mac = (unsigned char *) sodium_malloc(crypto_aead_xchacha20poly1305_ietf_ABYTES);
    unsigned char *m2 = (unsigned char *) sodium_malloc(MLEN);
    unsigned long long found_clen;
    unsigned long long found_maclen;
    unsigned long long m2len;
    size_t i;

    assert(sizeof MESSAGE - 1U == MLEN);
    memcpy(m, MESSAGE, MLEN);
    crypto_aead_xchacha20poly1305_ietf_encrypt(c, &found_clen, m, MLEN,
                                               ad, ADLEN,
                                               NULL, nonce, firstkey);
    if (found_clen != MLEN + crypto_aead_xchacha20poly1305_ietf_abytes()) {
        printf("found_clen is not properly set\n");
    }
    for (i = 0U; i < CLEN; ++i) {
        printf(",0x%02x", (unsigned int) c[i]);
        if (i % 8 == 7) {
            printf("\n");
        }
    }
    printf("\n");
    crypto_aead_xchacha20poly1305_ietf_encrypt_detached(detached_c,
                                                        mac, &found_maclen,
                                                        m, MLEN,
                                                        ad, ADLEN,
                                                        NULL, nonce, firstkey);
    if (found_maclen != crypto_aead_xchacha20poly1305_ietf_abytes()) {
        printf("found_maclen is not properly set\n");
    }
    if (memcmp(detached_c, c, MLEN) != 0) {
        printf("detached ciphertext is bogus\n");
    }

    if (crypto_aead_xchacha20poly1305_ietf_decrypt(m2, &m2len, NULL, c, CLEN, ad,
                                                   ADLEN, nonce, firstkey) != 0) {
        printf("crypto_aead_xchacha20poly1305_ietf_decrypt() failed\n");
    }
    if (m2len != MLEN) {
        printf("m2len is not properly set\n");
    }
    if (memcmp(m, m2, MLEN) != 0) {
        printf("m != m2\n");
    }
    memset(m2, 0, m2len);
    if (crypto_aead_xchacha20poly1305_ietf_decrypt_detached(m2, NULL,
                                                            c, MLEN, mac,
                                                            ad, ADLEN,
                                                            nonce, firstkey) != 0) {
        printf("crypto_aead_xchacha20poly1305_ietf_decrypt_detached() failed\n");
    }
    if (memcmp(m, m2, MLEN) != 0) {
        printf("detached m != m2\n");
    }

    for (i = 0U; i < CLEN; i++) {
        c[i] ^= (i + 1U);
        if (crypto_aead_xchacha20poly1305_ietf_decrypt(m2, NULL, NULL, c, CLEN,
                                                       ad, ADLEN, nonce, firstkey)
            == 0 || memcmp(m, m2, MLEN) == 0) {
            printf("message can be forged\n");
        }
        c[i] ^= (i + 1U);
    }
    crypto_aead_xchacha20poly1305_ietf_encrypt(c, &found_clen, m, MLEN,
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
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(m2, &m2len, NULL, c, CLEN,
                                                   NULL, 0U, nonce, firstkey) != 0) {
        printf("crypto_aead_xchacha20poly1305_ietf_decrypt() failed (adlen=0)\n");
    }
    if (m2len != MLEN) {
        printf("m2len is not properly set (adlen=0)\n");
    }
    if (memcmp(m, m2, MLEN) != 0) {
        printf("m != m2 (adlen=0)\n");
    }
    m2len = 1;
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(
            m2, &m2len, NULL, NULL,
            randombytes_uniform(crypto_aead_xchacha20poly1305_ietf_ABYTES),
            NULL, 0U, nonce, firstkey) != -1) {
        printf("crypto_aead_xchacha20poly1305_ietf_decrypt() worked with a short "
               "ciphertext\n");
    }
    if (m2len != 0) {
        printf("Message length should have been set to zero after a failure\n");
    }
    m2len = 1;
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(m2, &m2len, NULL, c, 0U, NULL, 0U,
                                                  nonce, firstkey) != -1) {
        printf("crypto_aead_xchacha20poly1305_ietf_decrypt() worked with an empty "
               "ciphertext\n");
    }
    if (m2len != 0) {
        printf("Message length should have been set to zero after a failure\n");
    }

    memcpy(c, m, MLEN);
    crypto_aead_xchacha20poly1305_ietf_encrypt(c, &found_clen, c, MLEN,
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

    if (crypto_aead_xchacha20poly1305_ietf_decrypt(c, &m2len, NULL, c, CLEN,
                                                   NULL, 0U, nonce, firstkey) != 0) {
        printf("crypto_aead_xchacha20poly1305_ietf_decrypt() failed (adlen=0)\n");
    }
    if (m2len != MLEN) {
        printf("m2len is not properly set (adlen=0)\n");
    }
    if (memcmp(m, c, MLEN) != 0) {
        printf("m != c (adlen=0)\n");
    }

    crypto_aead_xchacha20poly1305_ietf_keygen(key2);
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(c, &m2len, NULL, c, CLEN,
                                                   NULL, 0U, nonce, key2) == 0) {
        printf("crypto_aead_xchacha20poly1305_ietf_decrypt() with a wrong key should have failed\n");
    }

    sodium_free(c);
    sodium_free(detached_c);
    sodium_free(key2);
    sodium_free(mac);
    sodium_free(m2);
    sodium_free(m);

    assert(crypto_aead_xchacha20poly1305_ietf_abytes() == crypto_aead_xchacha20poly1305_ietf_ABYTES);
    assert(crypto_aead_xchacha20poly1305_ietf_keybytes() == crypto_aead_xchacha20poly1305_ietf_KEYBYTES);
    assert(crypto_aead_xchacha20poly1305_ietf_npubbytes() == crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
    assert(crypto_aead_xchacha20poly1305_ietf_nsecbytes() == 0U);
    assert(crypto_aead_xchacha20poly1305_ietf_nsecbytes() == crypto_aead_xchacha20poly1305_ietf_NSECBYTES);
    assert(crypto_aead_xchacha20poly1305_ietf_messagebytes_max() == crypto_aead_xchacha20poly1305_ietf_MESSAGEBYTES_MAX);
    assert(crypto_aead_xchacha20poly1305_IETF_KEYBYTES  == crypto_aead_xchacha20poly1305_ietf_KEYBYTES);
    assert(crypto_aead_xchacha20poly1305_IETF_NSECBYTES == crypto_aead_xchacha20poly1305_ietf_NSECBYTES);
    assert(crypto_aead_xchacha20poly1305_IETF_NPUBBYTES == crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
    assert(crypto_aead_xchacha20poly1305_IETF_ABYTES    == crypto_aead_xchacha20poly1305_ietf_ABYTES);
    assert(crypto_aead_xchacha20poly1305_IETF_MESSAGEBYTES_MAX == crypto_aead_xchacha20poly1305_ietf_MESSAGEBYTES_MAX);

    return 0;
}

int
main(void)
{
    tv();

    return 0;
}
