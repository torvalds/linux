
#define TEST_NAME "keygen"
#include "cmptest.h"

typedef struct KeygenTV_ {
    void (*fn)(unsigned char *k);
    size_t key_len;
} KeygenTV;

static void
tv_keygen(void)
{
    static const KeygenTV tvs[] = {
        { crypto_auth_keygen, crypto_auth_KEYBYTES },
        { crypto_auth_hmacsha256_keygen, crypto_auth_hmacsha256_KEYBYTES },
        { crypto_aead_aes256gcm_keygen, crypto_aead_aes256gcm_KEYBYTES },
        { crypto_auth_hmacsha512_keygen, crypto_auth_hmacsha512_KEYBYTES },
        { crypto_auth_hmacsha512256_keygen, crypto_auth_hmacsha512256_KEYBYTES },
        { crypto_generichash_keygen, crypto_generichash_KEYBYTES },
        { crypto_generichash_blake2b_keygen, crypto_generichash_blake2b_KEYBYTES },
        { crypto_kdf_keygen, crypto_kdf_KEYBYTES },
        { crypto_onetimeauth_keygen, crypto_onetimeauth_KEYBYTES },
        { crypto_onetimeauth_poly1305_keygen, crypto_onetimeauth_poly1305_KEYBYTES },
        { crypto_aead_chacha20poly1305_ietf_keygen, crypto_aead_chacha20poly1305_ietf_KEYBYTES },
        { crypto_aead_chacha20poly1305_keygen, crypto_aead_chacha20poly1305_KEYBYTES },
        { crypto_aead_chacha20poly1305_ietf_keygen, crypto_aead_chacha20poly1305_ietf_KEYBYTES },
        { crypto_aead_xchacha20poly1305_ietf_keygen, crypto_aead_xchacha20poly1305_ietf_KEYBYTES },
        { crypto_secretbox_xsalsa20poly1305_keygen, crypto_secretbox_xsalsa20poly1305_KEYBYTES },
        { crypto_secretbox_keygen, crypto_secretbox_KEYBYTES },
        { crypto_secretstream_xchacha20poly1305_keygen, crypto_secretstream_xchacha20poly1305_KEYBYTES },
        { crypto_shorthash_keygen, crypto_shorthash_KEYBYTES },
        { crypto_stream_keygen, crypto_stream_KEYBYTES },
        { crypto_stream_chacha20_keygen, crypto_stream_chacha20_KEYBYTES },
        { crypto_stream_chacha20_ietf_keygen, crypto_stream_chacha20_ietf_KEYBYTES },
        { crypto_stream_salsa20_keygen, crypto_stream_salsa20_KEYBYTES },
        { crypto_stream_xsalsa20_keygen, crypto_stream_xsalsa20_KEYBYTES }
    };
    const KeygenTV *tv;
    unsigned char  *key;
    size_t          i;
    int             j;

    for (i = 0; i < (sizeof tvs) / (sizeof tvs[0]); i++) {
        tv = &tvs[i];
        key = (unsigned char *) sodium_malloc(tv->key_len);
        key[tv->key_len - 1U] = 0;
        for (j = 0; j < 10000; j++) {
            tv->fn(key);
            if (key[tv->key_len - 1U] != 0) {
                break;
            }
        }
        sodium_free(key);
        if (j >= 10000) {
            printf("Buffer underflow with test vector %u\n", (unsigned int) i);
        }
    }
    printf("tv_keygen: ok\n");
}

int
main(void)
{
    tv_keygen();

    return 0;
}
