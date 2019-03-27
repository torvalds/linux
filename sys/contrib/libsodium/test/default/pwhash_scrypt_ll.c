
#define TEST_NAME "pwhash_scrypt_ll"
#include "cmptest.h"

static const char *   passwd1 = "";
static const char *   salt1   = "";
static const uint64_t N1      = 16U;
static const uint32_t r1      = 1U;
static const uint32_t p1      = 1U;

static const char *   passwd2 = "password";
static const char *   salt2   = "NaCl";
static const uint64_t N2      = 1024U;
static const uint32_t r2      = 8U;
static const uint32_t p2      = 16U;

static const char *   passwd3 = "pleaseletmein";
static const char *   salt3   = "SodiumChloride";
static const uint64_t N3      = 16384U;
static const uint32_t r3      = 8U;
static const uint32_t p3      = 1U;

static void
tv(const char *passwd, const char *salt, uint64_t N, uint32_t r, uint32_t p)
{
    uint8_t data[64];
    size_t  i;
    size_t  olen       = (sizeof data / sizeof data[0]);
    size_t  passwd_len = strlen(passwd);
    size_t  salt_len   = strlen(salt);
    int     line_items  = 0;

    if (crypto_pwhash_scryptsalsa208sha256_ll(
            (const uint8_t *) passwd, passwd_len, (const uint8_t *) salt,
            salt_len, N, r, p, data, olen) != 0) {
        printf("pwhash_scryptsalsa208sha256_ll([%s],[%s]) failure\n", passwd,
               salt);
        return;
    }

    printf("scrypt('%s', '%s', %lu, %lu, %lu, %lu) =\n", passwd, salt,
           (unsigned long) N, (unsigned long) r, (unsigned long) p,
           (unsigned long) olen);

    for (i = 0; i < olen; i++) {
        printf("%02x%c", data[i], line_items < 15 ? ' ' : '\n');
        line_items = line_items < 15 ? line_items + 1 : 0;
    }
}

int
main(void)
{
    tv(passwd1, salt1, N1, r1, p1);
    tv(passwd2, salt2, N2, r2, p2);
    tv(passwd3, salt3, N3, r3, p3);

    return 0;
}
