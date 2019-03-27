
#define TEST_NAME "hash"
#include "cmptest.h"

static unsigned char x[] = "testing\n";
static unsigned char x2[] =
    "The Conscience of a Hacker is a small essay written January 8, 1986 by a "
    "computer security hacker who went by the handle of The Mentor, who "
    "belonged to the 2nd generation of Legion of Doom.";
static unsigned char h[crypto_hash_BYTES];

int
main(void)
{
    size_t i;

    crypto_hash(h, x, sizeof x - 1U);
    for (i = 0; i < crypto_hash_BYTES; ++i) {
        printf("%02x", (unsigned int) h[i]);
    }
    printf("\n");
    crypto_hash(h, x2, sizeof x2 - 1U);
    for (i = 0; i < crypto_hash_BYTES; ++i) {
        printf("%02x", (unsigned int) h[i]);
    }
    printf("\n");
    crypto_hash_sha256(h, x, sizeof x - 1U);
    for (i = 0; i < crypto_hash_sha256_BYTES; ++i) {
        printf("%02x", (unsigned int) h[i]);
    }
    printf("\n");
    crypto_hash_sha256(h, x2, sizeof x2 - 1U);
    for (i = 0; i < crypto_hash_sha256_BYTES; ++i) {
        printf("%02x", (unsigned int) h[i]);
    }
    printf("\n");

    assert(crypto_hash_bytes() > 0U);
    assert(strcmp(crypto_hash_primitive(), "sha512") == 0);
    assert(crypto_hash_sha256_bytes() > 0U);
    assert(crypto_hash_sha512_bytes() >= crypto_hash_sha256_bytes());
    assert(crypto_hash_sha512_bytes() == crypto_hash_bytes());
    assert(crypto_hash_sha256_statebytes() == sizeof(crypto_hash_sha256_state));
    assert(crypto_hash_sha512_statebytes() == sizeof(crypto_hash_sha512_state));

    return 0;
}
