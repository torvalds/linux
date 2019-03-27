
#define TEST_NAME "box_seal"
#include "cmptest.h"

int
main(void)
{
    unsigned char  pk[crypto_box_PUBLICKEYBYTES];
    unsigned char  sk[crypto_box_SECRETKEYBYTES];
    unsigned char *c;
    unsigned char *m;
    unsigned char *m2;
    size_t         m_len;
    size_t         c_len;

    crypto_box_keypair(pk, sk);
    m_len = (size_t) randombytes_uniform(1000);
    c_len = crypto_box_SEALBYTES + m_len;
    m     = (unsigned char *) sodium_malloc(m_len);
    m2    = (unsigned char *) sodium_malloc(m_len);
    c     = (unsigned char *) sodium_malloc(c_len);
    randombytes_buf(m, m_len);
    if (crypto_box_seal(c, m, m_len, pk) != 0) {
        printf("crypto_box_seal() failure\n");
        return 1;
    }
    if (crypto_box_seal_open(m2, c, c_len, pk, sk) != 0) {
        printf("crypto_box_seal_open() failure\n");
        return 1;
    }
    printf("%d\n", memcmp(m, m2, m_len));

    printf("%d\n", crypto_box_seal_open(m, c, 0U, pk, sk));
    printf("%d\n", crypto_box_seal_open(m, c, c_len - 1U, pk, sk));
    printf("%d\n", crypto_box_seal_open(m, c, c_len, sk, pk));

    sodium_free(c);
    sodium_free(m);
    sodium_free(m2);

    assert(crypto_box_sealbytes() == crypto_box_SEALBYTES);

    return 0;
}
