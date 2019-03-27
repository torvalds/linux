
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

#include <sys/types.h>

#ifdef __EMSCRIPTEN__
# include <emscripten.h>
#endif

#include "core.h"
#include "crypto_stream_chacha20.h"
#include "randombytes.h"
#ifdef RANDOMBYTES_DEFAULT_IMPLEMENTATION
# include "randombytes_default.h"
#else
# ifdef __native_client__
#  include "randombytes_nativeclient.h"
# else
#  include "randombytes_sysrandom.h"
# endif
#endif
#include "private/common.h"

/* C++Builder defines a "random" macro */
#undef random

static const randombytes_implementation *implementation;

#ifndef RANDOMBYTES_DEFAULT_IMPLEMENTATION
# ifdef __EMSCRIPTEN__
#  define RANDOMBYTES_DEFAULT_IMPLEMENTATION NULL
# else
#  ifdef __native_client__
#   define RANDOMBYTES_DEFAULT_IMPLEMENTATION &randombytes_nativeclient_implementation;
#  else
#   define RANDOMBYTES_DEFAULT_IMPLEMENTATION &randombytes_sysrandom_implementation;
#  endif
# endif
#endif

static void
randombytes_init_if_needed(void)
{
    if (implementation == NULL) {
        implementation = RANDOMBYTES_DEFAULT_IMPLEMENTATION;
        randombytes_stir();
    }
}

int
randombytes_set_implementation(randombytes_implementation *impl)
{
    implementation = impl;

    return 0;
}

const char *
randombytes_implementation_name(void)
{
#ifndef __EMSCRIPTEN__
    randombytes_init_if_needed();
    return implementation->implementation_name();
#else
    return "js";
#endif
}

uint32_t
randombytes_random(void)
{
#ifndef __EMSCRIPTEN__
    randombytes_init_if_needed();
    return implementation->random();
#else
    return EM_ASM_INT_V({
        return Module.getRandomValue();
    });
#endif
}

void
randombytes_stir(void)
{
#ifndef __EMSCRIPTEN__
    randombytes_init_if_needed();
    if (implementation->stir != NULL) {
        implementation->stir();
    }
#else
    EM_ASM({
        if (Module.getRandomValue === undefined) {
            try {
                var window_ = 'object' === typeof window ? window : self;
                var crypto_ = typeof window_.crypto !== 'undefined' ? window_.crypto : window_.msCrypto;
                var randomValuesStandard = function() {
                    var buf = new Uint32Array(1);
                    crypto_.getRandomValues(buf);
                    return buf[0] >>> 0;
                };
                randomValuesStandard();
                Module.getRandomValue = randomValuesStandard;
            } catch (e) {
                try {
                    var crypto = require('crypto');
                    var randomValueNodeJS = function() {
                        var buf = crypto['randomBytes'](4);
                        return (buf[0] << 24 | buf[1] << 16 | buf[2] << 8 | buf[3]) >>> 0;
                    };
                    randomValueNodeJS();
                    Module.getRandomValue = randomValueNodeJS;
                } catch (e) {
                    throw 'No secure random number generator found';
                }
            }
        }
    });
#endif
}

uint32_t
randombytes_uniform(const uint32_t upper_bound)
{
    uint32_t min;
    uint32_t r;

#ifndef __EMSCRIPTEN__
    randombytes_init_if_needed();
    if (implementation->uniform != NULL) {
        return implementation->uniform(upper_bound);
    }
#endif
    if (upper_bound < 2) {
        return 0;
    }
    min = (1U + ~upper_bound) % upper_bound; /* = 2**32 mod upper_bound */
    do {
        r = randombytes_random();
    } while (r < min);
    /* r is now clamped to a set whose size mod upper_bound == 0
     * the worst case (2**31+1) requires ~ 2 attempts */

    return r % upper_bound;
}

void
randombytes_buf(void * const buf, const size_t size)
{
#ifndef __EMSCRIPTEN__
    randombytes_init_if_needed();
    if (size > (size_t) 0U) {
        implementation->buf(buf, size);
    }
#else
    unsigned char *p = (unsigned char *) buf;
    size_t         i;

    for (i = (size_t) 0U; i < size; i++) {
        p[i] = (unsigned char) randombytes_random();
    }
#endif
}

void
randombytes_buf_deterministic(void * const buf, const size_t size,
                              const unsigned char seed[randombytes_SEEDBYTES])
{
    static const unsigned char nonce[crypto_stream_chacha20_ietf_NONCEBYTES] = {
        'L', 'i', 'b', 's', 'o', 'd', 'i', 'u', 'm', 'D', 'R', 'G'
    };

    COMPILER_ASSERT(randombytes_SEEDBYTES == crypto_stream_chacha20_ietf_KEYBYTES);
#if SIZE_MAX > 0x4000000000ULL
    COMPILER_ASSERT(randombytes_BYTES_MAX <= 0x4000000000ULL);
    if (size > 0x4000000000ULL) {
        sodium_misuse();
    }
#endif
    crypto_stream_chacha20_ietf((unsigned char *) buf, (unsigned long long) size,
                                nonce, seed);
}

size_t
randombytes_seedbytes(void)
{
    return randombytes_SEEDBYTES;
}

int
randombytes_close(void)
{
    if (implementation != NULL && implementation->close != NULL) {
        return implementation->close();
    }
    return 0;
}

void
randombytes(unsigned char * const buf, const unsigned long long buf_len)
{
    assert(buf_len <= SIZE_MAX);
    randombytes_buf(buf, (size_t) buf_len);
}
