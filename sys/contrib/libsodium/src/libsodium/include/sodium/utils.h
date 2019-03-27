
#ifndef sodium_utils_H
#define sodium_utils_H

#include <stddef.h>

#include "export.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SODIUM_C99
# if defined(__cplusplus) || !defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L
#  define SODIUM_C99(X)
# else
#  define SODIUM_C99(X) X
# endif
#endif

SODIUM_EXPORT
void sodium_memzero(void * const pnt, const size_t len);

SODIUM_EXPORT
void sodium_stackzero(const size_t len);

/*
 * WARNING: sodium_memcmp() must be used to verify if two secret keys
 * are equal, in constant time.
 * It returns 0 if the keys are equal, and -1 if they differ.
 * This function is not designed for lexicographical comparisons.
 */
SODIUM_EXPORT
int sodium_memcmp(const void * const b1_, const void * const b2_, size_t len)
            __attribute__ ((warn_unused_result));

/*
 * sodium_compare() returns -1 if b1_ < b2_, 1 if b1_ > b2_ and 0 if b1_ == b2_
 * It is suitable for lexicographical comparisons, or to compare nonces
 * and counters stored in little-endian format.
 * However, it is slower than sodium_memcmp().
 */
SODIUM_EXPORT
int sodium_compare(const unsigned char *b1_, const unsigned char *b2_,
                   size_t len)
            __attribute__ ((warn_unused_result));

SODIUM_EXPORT
int sodium_is_zero(const unsigned char *n, const size_t nlen);

SODIUM_EXPORT
void sodium_increment(unsigned char *n, const size_t nlen);

SODIUM_EXPORT
void sodium_add(unsigned char *a, const unsigned char *b, const size_t len);

SODIUM_EXPORT
char *sodium_bin2hex(char * const hex, const size_t hex_maxlen,
                     const unsigned char * const bin, const size_t bin_len);

SODIUM_EXPORT
int sodium_hex2bin(unsigned char * const bin, const size_t bin_maxlen,
                   const char * const hex, const size_t hex_len,
                   const char * const ignore, size_t * const bin_len,
                   const char ** const hex_end);

#define sodium_base64_VARIANT_ORIGINAL            1
#define sodium_base64_VARIANT_ORIGINAL_NO_PADDING 3
#define sodium_base64_VARIANT_URLSAFE             5
#define sodium_base64_VARIANT_URLSAFE_NO_PADDING  7

/*
 * Computes the required length to encode BIN_LEN bytes as a base64 string
 * using the given variant. The computed length includes a trailing \0.
 */
#define sodium_base64_ENCODED_LEN(BIN_LEN, VARIANT) \
    (((BIN_LEN) / 3U) * 4U + \
    ((((BIN_LEN) - ((BIN_LEN) / 3U) * 3U) | (((BIN_LEN) - ((BIN_LEN) / 3U) * 3U) >> 1)) & 1U) * \
     (4U - (~((((VARIANT) & 2U) >> 1) - 1U) & (3U - ((BIN_LEN) - ((BIN_LEN) / 3U) * 3U)))) + 1U)

SODIUM_EXPORT
size_t sodium_base64_encoded_len(const size_t bin_len, const int variant);

SODIUM_EXPORT
char *sodium_bin2base64(char * const b64, const size_t b64_maxlen,
                        const unsigned char * const bin, const size_t bin_len,
                        const int variant);

SODIUM_EXPORT
int sodium_base642bin(unsigned char * const bin, const size_t bin_maxlen,
                      const char * const b64, const size_t b64_len,
                      const char * const ignore, size_t * const bin_len,
                      const char ** const b64_end, const int variant);

SODIUM_EXPORT
int sodium_mlock(void * const addr, const size_t len);

SODIUM_EXPORT
int sodium_munlock(void * const addr, const size_t len);

/* WARNING: sodium_malloc() and sodium_allocarray() are not general-purpose
 * allocation functions.
 *
 * They return a pointer to a region filled with 0xd0 bytes, immediately
 * followed by a guard page.
 * As a result, accessing a single byte after the requested allocation size
 * will intentionally trigger a segmentation fault.
 *
 * A canary and an additional guard page placed before the beginning of the
 * region may also kill the process if a buffer underflow is detected.
 *
 * The memory layout is:
 * [unprotected region size (read only)][guard page (no access)][unprotected pages (read/write)][guard page (no access)]
 * With the layout of the unprotected pages being:
 * [optional padding][16-bytes canary][user region]
 *
 * However:
 * - These functions are significantly slower than standard functions
 * - Each allocation requires 3 or 4 additional pages
 * - The returned address will not be aligned if the allocation size is not
 *   a multiple of the required alignment. For this reason, these functions
 *   are designed to store data, such as secret keys and messages.
 *
 * sodium_malloc() can be used to allocate any libsodium data structure.
 *
 * The crypto_generichash_state structure is packed and its length is
 * either 357 or 361 bytes. For this reason, when using sodium_malloc() to
 * allocate a crypto_generichash_state structure, padding must be added in
 * order to ensure proper alignment. crypto_generichash_statebytes()
 * returns the rounded up structure size, and should be prefered to sizeof():
 * state = sodium_malloc(crypto_generichash_statebytes());
 */

SODIUM_EXPORT
void *sodium_malloc(const size_t size)
            __attribute__ ((malloc));

SODIUM_EXPORT
void *sodium_allocarray(size_t count, size_t size)
            __attribute__ ((malloc));

SODIUM_EXPORT
void sodium_free(void *ptr);

SODIUM_EXPORT
int sodium_mprotect_noaccess(void *ptr);

SODIUM_EXPORT
int sodium_mprotect_readonly(void *ptr);

SODIUM_EXPORT
int sodium_mprotect_readwrite(void *ptr);

SODIUM_EXPORT
int sodium_pad(size_t *padded_buflen_p, unsigned char *buf,
               size_t unpadded_buflen, size_t blocksize, size_t max_buflen);

SODIUM_EXPORT
int sodium_unpad(size_t *unpadded_buflen_p, const unsigned char *buf,
                 size_t padded_buflen, size_t blocksize);

/* -------- */

int _sodium_alloc_init(void);

#ifdef __cplusplus
}
#endif

#endif
