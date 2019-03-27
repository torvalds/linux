
#define TEST_NAME "generichash3"
#include "cmptest.h"

int
main(void)
{
#define MAXLEN 64
    crypto_generichash_blake2b_state st;
    unsigned char salt[crypto_generichash_blake2b_SALTBYTES]
        = { '5', 'b', '6', 'b', '4', '1', 'e', 'd',
            '9', 'b', '3', '4', '3', 'f', 'e', '0' };
    unsigned char personal[crypto_generichash_blake2b_PERSONALBYTES]
        = { '5', '1', '2', '6', 'f', 'b', '2', 'a',
            '3', '7', '4', '0', '0', 'd', '2', 'a' };
    unsigned char in[MAXLEN];
    unsigned char out[crypto_generichash_blake2b_BYTES_MAX];
    unsigned char k[crypto_generichash_blake2b_KEYBYTES_MAX];
    size_t        h;
    size_t        i;
    size_t        j;

    assert(crypto_generichash_blake2b_statebytes() >= sizeof st);
    for (h = 0; h < crypto_generichash_blake2b_KEYBYTES_MAX; ++h) {
        k[h] = (unsigned char) h;
    }

    for (i = 0; i < MAXLEN; ++i) {
        in[i] = (unsigned char) i;
        crypto_generichash_blake2b_init_salt_personal(
            &st, k, 1 + i % crypto_generichash_blake2b_KEYBYTES_MAX,
            1 + i % crypto_generichash_blake2b_BYTES_MAX, salt, personal);
        crypto_generichash_blake2b_update(&st, in, (unsigned long long) i);
        crypto_generichash_blake2b_final(
            &st, out, 1 + i % crypto_generichash_blake2b_BYTES_MAX);
        for (j = 0; j < 1 + i % crypto_generichash_blake2b_BYTES_MAX; ++j) {
            printf("%02x", (unsigned int) out[j]);
        }
        printf("\n");
    }

    memset(out, 0, sizeof out);
    crypto_generichash_blake2b_init_salt_personal(
        &st, k, 0U, crypto_generichash_blake2b_BYTES_MAX, salt, personal);
    crypto_generichash_blake2b_update(&st, in, MAXLEN);
    crypto_generichash_blake2b_final(&st, out,
                                     crypto_generichash_blake2b_BYTES_MAX);
    for (j = 0; j < crypto_generichash_blake2b_BYTES_MAX; ++j) {
        printf("%02x", (unsigned int) out[j]);
    }
    printf("\n");

    memset(out, 0, sizeof out);
    crypto_generichash_blake2b_init_salt_personal(
        &st, NULL, 1U, crypto_generichash_blake2b_BYTES_MAX, salt, personal);
    crypto_generichash_blake2b_update(&st, in, MAXLEN);
    crypto_generichash_blake2b_final(&st, out,
                                     crypto_generichash_blake2b_BYTES_MAX);
    for (j = 0; j < crypto_generichash_blake2b_BYTES_MAX; ++j) {
        printf("%02x", (unsigned int) out[j]);
    }
    printf("\n");

    memset(out, 0, sizeof out);
    crypto_generichash_blake2b_init_salt_personal(
        &st, k, crypto_generichash_blake2b_KEYBYTES_MAX,
    crypto_generichash_blake2b_BYTES_MAX, NULL, personal);
    crypto_generichash_blake2b_update(&st, in, MAXLEN);
    crypto_generichash_blake2b_final(&st, out,
                                     crypto_generichash_blake2b_BYTES_MAX);
    for (j = 0; j < crypto_generichash_blake2b_BYTES_MAX; ++j) {
        printf("%02x", (unsigned int) out[j]);
    }
    printf("\n");

    memset(out, 0, sizeof out);
    crypto_generichash_blake2b_init_salt_personal(
        &st, k, crypto_generichash_blake2b_KEYBYTES_MAX,
        crypto_generichash_blake2b_BYTES_MAX, salt, NULL);
    crypto_generichash_blake2b_update(&st, in, MAXLEN);
    crypto_generichash_blake2b_final(
        &st, out, crypto_generichash_blake2b_BYTES_MAX);
    for (j = 0; j < crypto_generichash_blake2b_BYTES_MAX; ++j) {
        printf("%02x", (unsigned int) out[j]);
    }
    printf("\n");

    memset(out, 0, sizeof out);
    crypto_generichash_blake2b_salt_personal(
        out, crypto_generichash_blake2b_BYTES_MAX, in, MAXLEN,
        k, 0U, salt, personal);
    for (j = 0; j < crypto_generichash_blake2b_BYTES_MAX; ++j) {
        printf("%02x", (unsigned int) out[j]);
    }
    printf("\n");

    memset(out, 0, sizeof out);
    crypto_generichash_blake2b_salt_personal(
        out, crypto_generichash_blake2b_BYTES_MAX, in, MAXLEN,
        NULL, 0U, salt, personal);
    for (j = 0; j < crypto_generichash_blake2b_BYTES_MAX; ++j) {
        printf("%02x", (unsigned int) out[j]);
    }
    printf("\n");

    memset(out, 0, sizeof out);
    crypto_generichash_blake2b_salt_personal(
        out, crypto_generichash_blake2b_BYTES_MAX, in, MAXLEN,
        k, crypto_generichash_blake2b_KEYBYTES_MAX, salt, personal);
    for (j = 0; j < crypto_generichash_blake2b_BYTES_MAX; ++j) {
        printf("%02x", (unsigned int) out[j]);
    }
    printf("\n");

    memset(out, 0, sizeof out);
    crypto_generichash_blake2b_salt_personal(
        out, crypto_generichash_blake2b_BYTES_MAX, in, MAXLEN,
        k, crypto_generichash_blake2b_KEYBYTES_MAX, NULL, personal);
    for (j = 0; j < crypto_generichash_blake2b_BYTES_MAX; ++j) {
        printf("%02x", (unsigned int) out[j]);
    }
    printf("\n");

    memset(out, 0, sizeof out);
    crypto_generichash_blake2b_salt_personal(
        out, crypto_generichash_blake2b_BYTES_MAX, in, MAXLEN,
        k, crypto_generichash_blake2b_KEYBYTES_MAX, salt, NULL);
    for (j = 0; j < crypto_generichash_blake2b_BYTES_MAX; ++j) {
        printf("%02x", (unsigned int) out[j]);
    }
    printf("\n");

    assert(crypto_generichash_blake2b_salt_personal
           (NULL, 0,
            in, (unsigned long long) sizeof in,
            k, sizeof k, NULL, NULL) == -1);
    assert(crypto_generichash_blake2b_salt_personal
           (NULL, crypto_generichash_BYTES_MAX + 1,
            in, (unsigned long long) sizeof in,
            k, sizeof k, NULL, NULL) == -1);
    assert(crypto_generichash_blake2b_salt_personal
           (NULL, (unsigned long long) sizeof in,
            in, (unsigned long long) sizeof in,
            k, crypto_generichash_KEYBYTES_MAX + 1, NULL, NULL) == -1);

    crypto_generichash_blake2b_init_salt_personal(&st, NULL, 0U, crypto_generichash_BYTES,
                                                  NULL, personal);
    crypto_generichash_blake2b_update(&st, in, MAXLEN);
    crypto_generichash_blake2b_final(&st, out, crypto_generichash_blake2b_BYTES_MAX);
    for (j = 0; j < crypto_generichash_blake2b_BYTES_MAX; ++j) {
        printf("%02x", (unsigned int) out[j]);
    }
    printf("\n");

    crypto_generichash_blake2b_init_salt_personal(&st, NULL, 0U, crypto_generichash_BYTES,
                                                  salt, NULL);
    crypto_generichash_blake2b_update(&st, in, MAXLEN);
    crypto_generichash_blake2b_final(&st, out, crypto_generichash_blake2b_BYTES_MAX);
    for (j = 0; j < crypto_generichash_blake2b_BYTES_MAX; ++j) {
        printf("%02x", (unsigned int) out[j]);
    }
    printf("\n");

    assert(crypto_generichash_blake2b_init_salt_personal
           (&st, k, sizeof k, 0, NULL, NULL) == -1);
    assert(crypto_generichash_blake2b_init_salt_personal
           (&st, k, sizeof k, crypto_generichash_blake2b_BYTES_MAX + 1, NULL, NULL) == -1);
    assert(crypto_generichash_blake2b_init_salt_personal
           (&st, k, crypto_generichash_blake2b_KEYBYTES_MAX + 1, sizeof out, NULL, NULL) == -1);

    assert(crypto_generichash_blake2b_init_salt_personal(&st, k, sizeof k, crypto_generichash_BYTES,
                                                         NULL, personal) == 0);
    assert(crypto_generichash_blake2b_init_salt_personal(&st, k, sizeof k, crypto_generichash_BYTES,
                                                         salt, NULL) == 0);
    return 0;
}
