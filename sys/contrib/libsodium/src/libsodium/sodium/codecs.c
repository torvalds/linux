#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "core.h"
#include "utils.h"

/* Derived from original code by CodesInChaos */
char *
sodium_bin2hex(char *const hex, const size_t hex_maxlen,
               const unsigned char *const bin, const size_t bin_len)
{
    size_t       i = (size_t) 0U;
    unsigned int x;
    int          b;
    int          c;

    if (bin_len >= SIZE_MAX / 2 || hex_maxlen <= bin_len * 2U) {
        sodium_misuse(); /* LCOV_EXCL_LINE */
    }
    while (i < bin_len) {
        c = bin[i] & 0xf;
        b = bin[i] >> 4;
        x = (unsigned char) (87U + c + (((c - 10U) >> 8) & ~38U)) << 8 |
            (unsigned char) (87U + b + (((b - 10U) >> 8) & ~38U));
        hex[i * 2U] = (char) x;
        x >>= 8;
        hex[i * 2U + 1U] = (char) x;
        i++;
    }
    hex[i * 2U] = 0U;

    return hex;
}

int
sodium_hex2bin(unsigned char *const bin, const size_t bin_maxlen,
               const char *const hex, const size_t hex_len,
               const char *const ignore, size_t *const bin_len,
               const char **const hex_end)
{
    size_t        bin_pos = (size_t) 0U;
    size_t        hex_pos = (size_t) 0U;
    int           ret     = 0;
    unsigned char c;
    unsigned char c_acc = 0U;
    unsigned char c_alpha0, c_alpha;
    unsigned char c_num0, c_num;
    unsigned char c_val;
    unsigned char state = 0U;

    while (hex_pos < hex_len) {
        c        = (unsigned char) hex[hex_pos];
        c_num    = c ^ 48U;
        c_num0   = (c_num - 10U) >> 8;
        c_alpha  = (c & ~32U) - 55U;
        c_alpha0 = ((c_alpha - 10U) ^ (c_alpha - 16U)) >> 8;
        if ((c_num0 | c_alpha0) == 0U) {
            if (ignore != NULL && state == 0U && strchr(ignore, c) != NULL) {
                hex_pos++;
                continue;
            }
            break;
        }
        c_val = (c_num0 & c_num) | (c_alpha0 & c_alpha);
        if (bin_pos >= bin_maxlen) {
            ret   = -1;
            errno = ERANGE;
            break;
        }
        if (state == 0U) {
            c_acc = c_val * 16U;
        } else {
            bin[bin_pos++] = c_acc | c_val;
        }
        state = ~state;
        hex_pos++;
    }
    if (state != 0U) {
        hex_pos--;
        errno = EINVAL;
        ret = -1;
    }
    if (ret != 0) {
        bin_pos = (size_t) 0U;
    }
    if (hex_end != NULL) {
        *hex_end = &hex[hex_pos];
    } else if (hex_pos != hex_len) {
        errno = EINVAL;
        ret = -1;
    }
    if (bin_len != NULL) {
        *bin_len = bin_pos;
    }
    return ret;
}

/*
 * Some macros for constant-time comparisons. These work over values in
 * the 0..255 range. Returned value is 0x00 on "false", 0xFF on "true".
 *
 * Original code by Thomas Pornin.
 */
#define EQ(x, y) \
    ((((0U - ((unsigned int) (x) ^ (unsigned int) (y))) >> 8) & 0xFF) ^ 0xFF)
#define GT(x, y) ((((unsigned int) (y) - (unsigned int) (x)) >> 8) & 0xFF)
#define GE(x, y) (GT(y, x) ^ 0xFF)
#define LT(x, y) GT(y, x)
#define LE(x, y) GE(y, x)

static int
b64_byte_to_char(unsigned int x)
{
    return (LT(x, 26) & (x + 'A')) |
           (GE(x, 26) & LT(x, 52) & (x + ('a' - 26))) |
           (GE(x, 52) & LT(x, 62) & (x + ('0' - 52))) | (EQ(x, 62) & '+') |
           (EQ(x, 63) & '/');
}

static unsigned int
b64_char_to_byte(int c)
{
    const unsigned int x =
        (GE(c, 'A') & LE(c, 'Z') & (c - 'A')) |
        (GE(c, 'a') & LE(c, 'z') & (c - ('a' - 26))) |
        (GE(c, '0') & LE(c, '9') & (c - ('0' - 52))) | (EQ(c, '+') & 62) |
        (EQ(c, '/') & 63);

    return x | (EQ(x, 0) & (EQ(c, 'A') ^ 0xFF));
}

static int
b64_byte_to_urlsafe_char(unsigned int x)
{
    return (LT(x, 26) & (x + 'A')) |
           (GE(x, 26) & LT(x, 52) & (x + ('a' - 26))) |
           (GE(x, 52) & LT(x, 62) & (x + ('0' - 52))) | (EQ(x, 62) & '-') |
           (EQ(x, 63) & '_');
}

static unsigned int
b64_urlsafe_char_to_byte(int c)
{
    const unsigned x =
        (GE(c, 'A') & LE(c, 'Z') & (c - 'A')) |
        (GE(c, 'a') & LE(c, 'z') & (c - ('a' - 26))) |
        (GE(c, '0') & LE(c, '9') & (c - ('0' - 52))) | (EQ(c, '-') & 62) |
        (EQ(c, '_') & 63);

    return x | (EQ(x, 0) & (EQ(c, 'A') ^ 0xFF));
}


#define VARIANT_NO_PADDING_MASK 0x2U
#define VARIANT_URLSAFE_MASK    0x4U

static void
sodium_base64_check_variant(const int variant)
{
    if ((((unsigned int) variant) & ~ 0x6U) != 0x1U) {
        sodium_misuse();
    }
}

size_t
sodium_base64_encoded_len(const size_t bin_len, const int variant)
{
    sodium_base64_check_variant(variant);

    return sodium_base64_ENCODED_LEN(bin_len, variant);
}

char *
sodium_bin2base64(char * const b64, const size_t b64_maxlen,
                  const unsigned char * const bin, const size_t bin_len,
                  const int variant)
{
    size_t       acc_len = (size_t) 0;
    size_t       b64_len;
    size_t       b64_pos = (size_t) 0;
    size_t       bin_pos = (size_t) 0;
    size_t       nibbles;
    size_t       remainder;
    unsigned int acc = 0U;

    sodium_base64_check_variant(variant);
    nibbles = bin_len / 3;
    remainder = bin_len - 3 * nibbles;
    b64_len = nibbles * 4;
    if (remainder != 0) {
        if ((((unsigned int) variant) & VARIANT_NO_PADDING_MASK) == 0U) {
            b64_len += 4;
        } else {
            b64_len += 2 + (remainder >> 1);
        }
    }
    if (b64_maxlen <= b64_len) {
        sodium_misuse();
    }
    if ((((unsigned int) variant) & VARIANT_URLSAFE_MASK) != 0U) {
        while (bin_pos < bin_len) {
            acc = (acc << 8) + bin[bin_pos++];
            acc_len += 8;
            while (acc_len >= 6) {
                acc_len -= 6;
                b64[b64_pos++] = (char) b64_byte_to_urlsafe_char((acc >> acc_len) & 0x3F);
            }
        }
        if (acc_len > 0) {
            b64[b64_pos++] = (char) b64_byte_to_urlsafe_char((acc << (6 - acc_len)) & 0x3F);
        }
    } else {
        while (bin_pos < bin_len) {
            acc = (acc << 8) + bin[bin_pos++];
            acc_len += 8;
            while (acc_len >= 6) {
                acc_len -= 6;
                b64[b64_pos++] = (char) b64_byte_to_char((acc >> acc_len) & 0x3F);
            }
        }
        if (acc_len > 0) {
            b64[b64_pos++] = (char) b64_byte_to_char((acc << (6 - acc_len)) & 0x3F);
        }
    }
    assert(b64_pos <= b64_len);
    while (b64_pos < b64_len) {
        b64[b64_pos++] = '=';
    }
    do {
        b64[b64_pos++] = 0U;
    } while (b64_pos < b64_maxlen);

    return b64;
}

static int
_sodium_base642bin_skip_padding(const char * const b64, const size_t b64_len,
                                size_t * const b64_pos_p,
                                const char * const ignore, size_t padding_len)
{
    int c;

    while (padding_len > 0) {
        if (*b64_pos_p >= b64_len) {
            errno = ERANGE;
            return -1;
        }
        c = b64[*b64_pos_p];
        if (c == '=') {
            padding_len--;
        } else if (ignore == NULL || strchr(ignore, c) == NULL) {
            errno = EINVAL;
            return -1;
        }
        (*b64_pos_p)++;
    }
    return 0;
}

int
sodium_base642bin(unsigned char * const bin, const size_t bin_maxlen,
                  const char * const b64, const size_t b64_len,
                  const char * const ignore, size_t * const bin_len,
                  const char ** const b64_end, const int variant)
{
    size_t       acc_len = (size_t) 0;
    size_t       b64_pos = (size_t) 0;
    size_t       bin_pos = (size_t) 0;
    int          is_urlsafe;
    int          ret = 0;
    unsigned int acc = 0U;
    unsigned int d;
    char         c;

    sodium_base64_check_variant(variant);
    is_urlsafe = ((unsigned int) variant) & VARIANT_URLSAFE_MASK;
    while (b64_pos < b64_len) {
        c = b64[b64_pos];
        if (is_urlsafe) {
            d = b64_urlsafe_char_to_byte(c);
        } else {
            d = b64_char_to_byte(c);
        }
        if (d == 0xFF) {
            if (ignore != NULL && strchr(ignore, c) != NULL) {
                b64_pos++;
                continue;
            }
            break;
        }
        acc = (acc << 6) + d;
        acc_len += 6;
        if (acc_len >= 8) {
            acc_len -= 8;
            if (bin_pos >= bin_maxlen) {
                errno = ERANGE;
                ret = -1;
                break;
            }
            bin[bin_pos++] = (acc >> acc_len) & 0xFF;
        }
        b64_pos++;
    }
    if (acc_len > 4U || (acc & ((1U << acc_len) - 1U)) != 0U) {
        ret = -1;
    } else if (ret == 0 &&
               (((unsigned int) variant) & VARIANT_NO_PADDING_MASK) == 0U) {
        ret = _sodium_base642bin_skip_padding(b64, b64_len, &b64_pos, ignore,
                                              acc_len / 2);
    }
    if (ret != 0) {
        bin_pos = (size_t) 0U;
    } else if (ignore != NULL) {
        while (b64_pos < b64_len && strchr(ignore, b64[b64_pos]) != NULL) {
            b64_pos++;
        }
    }
    if (b64_end != NULL) {
        *b64_end = &b64[b64_pos];
    } else if (b64_pos != b64_len) {
        errno = EINVAL;
        ret = -1;
    }
    if (bin_len != NULL) {
        *bin_len = bin_pos;
    }
    return ret;
}
