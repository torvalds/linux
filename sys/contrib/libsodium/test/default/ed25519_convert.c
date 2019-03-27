
#define TEST_NAME "ed25519_convert"
#include "cmptest.h"

static const unsigned char keypair_seed[crypto_sign_ed25519_SEEDBYTES] = {
    0x42, 0x11, 0x51, 0xa4, 0x59, 0xfa, 0xea, 0xde, 0x3d, 0x24, 0x71,
    0x15, 0xf9, 0x4a, 0xed, 0xae, 0x42, 0x31, 0x81, 0x24, 0x09, 0x5a,
    0xfa, 0xbe, 0x4d, 0x14, 0x51, 0xa5, 0x59, 0xfa, 0xed, 0xee
};

int
main(void)
{
    unsigned char ed25519_pk[crypto_sign_ed25519_PUBLICKEYBYTES];
    unsigned char ed25519_skpk[crypto_sign_ed25519_SECRETKEYBYTES];
    unsigned char curve25519_pk[crypto_scalarmult_curve25519_BYTES];
    unsigned char curve25519_pk2[crypto_scalarmult_curve25519_BYTES];
    unsigned char curve25519_sk[crypto_scalarmult_curve25519_BYTES];
    char          curve25519_pk_hex[crypto_scalarmult_curve25519_BYTES * 2 + 1];
    char          curve25519_sk_hex[crypto_scalarmult_curve25519_BYTES * 2 + 1];
    unsigned char hseed[crypto_hash_sha512_BYTES];
    unsigned int  i;

    assert(crypto_sign_ed25519_SEEDBYTES <= crypto_hash_sha512_BYTES);
#ifdef ED25519_NONDETERMINISTIC
    crypto_hash_sha512(hseed, keypair_seed, crypto_sign_ed25519_SEEDBYTES);
#else
    memcpy(hseed, keypair_seed, crypto_sign_ed25519_SEEDBYTES);
#endif
    crypto_sign_ed25519_seed_keypair(ed25519_pk, ed25519_skpk, hseed);

    if (crypto_sign_ed25519_pk_to_curve25519(curve25519_pk, ed25519_pk) != 0) {
        printf("conversion failed\n");
    }
    crypto_sign_ed25519_sk_to_curve25519(curve25519_sk, ed25519_skpk);
    sodium_bin2hex(curve25519_pk_hex, sizeof curve25519_pk_hex, curve25519_pk,
                   sizeof curve25519_pk);
    sodium_bin2hex(curve25519_sk_hex, sizeof curve25519_sk_hex, curve25519_sk,
                   sizeof curve25519_sk);

    printf("curve25519 pk: [%s]\n", curve25519_pk_hex);
    printf("curve25519 sk: [%s]\n", curve25519_sk_hex);

    for (i = 0U; i < 500U; i++) {
        crypto_sign_ed25519_keypair(ed25519_pk, ed25519_skpk);
        if (crypto_sign_ed25519_pk_to_curve25519(curve25519_pk, ed25519_pk) !=
            0) {
            printf("conversion failed\n");
        }
        crypto_sign_ed25519_sk_to_curve25519(curve25519_sk, ed25519_skpk);
        crypto_scalarmult_curve25519_base(curve25519_pk2, curve25519_sk);
        if (memcmp(curve25519_pk, curve25519_pk2, sizeof curve25519_pk) != 0) {
            printf("conversion failed\n");
        }
    }

    sodium_hex2bin(ed25519_pk, crypto_sign_ed25519_PUBLICKEYBYTES,
                   "0000000000000000000000000000000000000000000000000000000000000000"
                   "0000000000000000000000000000000000000000000000000000000000000000",
                   64, NULL, NULL, NULL);
    assert(crypto_sign_ed25519_pk_to_curve25519(curve25519_pk, ed25519_pk) == -1);
    sodium_hex2bin(ed25519_pk, crypto_sign_ed25519_PUBLICKEYBYTES,
                   "0200000000000000000000000000000000000000000000000000000000000000"
                   "0000000000000000000000000000000000000000000000000000000000000000",
                   64, NULL, NULL, NULL);
    assert(crypto_sign_ed25519_pk_to_curve25519(curve25519_pk, ed25519_pk) == -1);
    sodium_hex2bin(ed25519_pk, crypto_sign_ed25519_PUBLICKEYBYTES,
                   "0500000000000000000000000000000000000000000000000000000000000000"
                   "0000000000000000000000000000000000000000000000000000000000000000",
                   64, NULL, NULL, NULL);
    assert(crypto_sign_ed25519_pk_to_curve25519(curve25519_pk, ed25519_pk) == -1);

    printf("ok\n");

    return 0;
}
