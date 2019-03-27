
#define TEST_NAME "kdf"
#include "cmptest.h"

static void
tv_kdf(void)
{
    unsigned char *master_key;
    unsigned char *subkey;
    char          *context;
    char           hex[crypto_kdf_BYTES_MAX * 2 + 1];
    uint64_t       i;
    int            ret;

    context = (char *) sodium_malloc(crypto_kdf_CONTEXTBYTES);
    master_key = (unsigned char *) sodium_malloc(crypto_kdf_KEYBYTES);

    memcpy(context, "KDF test", strlen("KDF test"));
    for (i = 0; i < crypto_kdf_KEYBYTES; i++) {
        master_key[i] = i;
    }
    subkey = (unsigned char *) sodium_malloc(crypto_kdf_BYTES_MAX);
    for (i = 0; i < 10; i++) {
        ret = crypto_kdf_derive_from_key(subkey, crypto_kdf_BYTES_MAX,
                                         i, context, master_key);
        assert(ret == 0);
        sodium_bin2hex(hex, sizeof hex, subkey, crypto_kdf_BYTES_MAX);
        printf("%s\n", hex);
    }
    sodium_free(subkey);

    for (i = 0; i < crypto_kdf_BYTES_MAX + 2; i++) {
        subkey = (unsigned char *) sodium_malloc(crypto_kdf_BYTES_MAX);
        if (crypto_kdf_derive_from_key(subkey, (size_t) i,
                                       i, context, master_key) == 0) {
            sodium_bin2hex(hex, sizeof hex, subkey, (size_t) i);
            printf("%s\n", hex);
        } else {
            printf("Failure -- probably expected for output length=%u\n",
                   (unsigned int) i);
        }
        sodium_free(subkey);
    }

    sodium_free(master_key);
    sodium_free(context);

    assert(strcmp(crypto_kdf_primitive(), crypto_kdf_PRIMITIVE) == 0);
    assert(crypto_kdf_BYTES_MAX > 0);
    assert(crypto_kdf_BYTES_MIN <= crypto_kdf_BYTES_MAX);
    assert(crypto_kdf_bytes_min() == crypto_kdf_BYTES_MIN);
    assert(crypto_kdf_bytes_max() == crypto_kdf_BYTES_MAX);
    assert(crypto_kdf_CONTEXTBYTES > 0);
    assert(crypto_kdf_contextbytes() == crypto_kdf_CONTEXTBYTES);
    assert(crypto_kdf_KEYBYTES >= 16);
    assert(crypto_kdf_keybytes() == crypto_kdf_KEYBYTES);
    assert(crypto_kdf_bytes_min() == crypto_kdf_blake2b_bytes_min());
    assert(crypto_kdf_bytes_max() == crypto_kdf_blake2b_bytes_max());
    assert(crypto_kdf_contextbytes() == crypto_kdf_blake2b_contextbytes());
    assert(crypto_kdf_keybytes() == crypto_kdf_blake2b_keybytes());

    printf("tv_kdf: ok\n");
}

int
main(void)
{
    tv_kdf();

    return 0;
}
