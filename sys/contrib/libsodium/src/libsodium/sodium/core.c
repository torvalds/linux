
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
# include <windows.h>
#elif defined(HAVE_PTHREAD)
# include <pthread.h>
#endif

#include "core.h"
#include "crypto_generichash.h"
#include "crypto_onetimeauth.h"
#include "crypto_scalarmult.h"
#include "crypto_stream_chacha20.h"
#include "crypto_stream_salsa20.h"
#include "randombytes.h"
#include "runtime.h"
#include "utils.h"
#include "private/implementations.h"
#include "private/mutex.h"

#if !defined(_MSC_VER) && 1
# warning *** This is unstable, untested, development code.
# warning It might not compile. It might not work as expected.
# warning It might be totally insecure.
# warning Do not use this in production.
# warning Use releases available at https://download.libsodium.org/libsodium/releases/ instead.
# warning Alternatively, use the "stable" branch in the git repository.
#endif

#if !defined(_MSC_VER) && (!defined(CONFIGURED) || CONFIGURED != 1)
# warning *** The library is being compiled using an undocumented method.
# warning This is not supported. It has not been tested, it might not
# warning work as expected, and performance is likely to be suboptimal.
#endif

static volatile int initialized;
static volatile int locked;

int
sodium_init(void)
{
    if (sodium_crit_enter() != 0) {
        return -1; /* LCOV_EXCL_LINE */
    }
    if (initialized != 0) {
        if (sodium_crit_leave() != 0) {
            return -1; /* LCOV_EXCL_LINE */
        }
        return 1;
    }
    _sodium_runtime_get_cpu_features();
    randombytes_stir();
    _sodium_alloc_init();
    _crypto_pwhash_argon2_pick_best_implementation();
    _crypto_generichash_blake2b_pick_best_implementation();
    _crypto_onetimeauth_poly1305_pick_best_implementation();
    _crypto_scalarmult_curve25519_pick_best_implementation();
    _crypto_stream_chacha20_pick_best_implementation();
    _crypto_stream_salsa20_pick_best_implementation();
    initialized = 1;
    if (sodium_crit_leave() != 0) {
        return -1; /* LCOV_EXCL_LINE */
    }
    return 0;
}

#ifdef _WIN32

static CRITICAL_SECTION _sodium_lock;
static volatile LONG    _sodium_lock_initialized;

int
_sodium_crit_init(void)
{
    LONG status = 0L;

    while ((status = InterlockedCompareExchange(&_sodium_lock_initialized,
                                                1L, 0L)) == 1L) {
        Sleep(0);
    }

    switch (status) {
    case 0L:
        InitializeCriticalSection(&_sodium_lock);
        return InterlockedExchange(&_sodium_lock_initialized, 2L) == 1L ? 0 : -1;
    case 2L:
        return 0;
    default: /* should never be reached */
        return -1;
    }
}

int
sodium_crit_enter(void)
{
    if (_sodium_crit_init() != 0) {
        return -1; /* LCOV_EXCL_LINE */
    }
    EnterCriticalSection(&_sodium_lock);
    assert(locked == 0);
    locked = 1;

    return 0;
}

int
sodium_crit_leave(void)
{
    if (locked == 0) {
# ifdef EPERM
        errno = EPERM;
# endif
        return -1;
    }
    locked = 0;
    LeaveCriticalSection(&_sodium_lock);

    return 0;
}

#elif defined(HAVE_PTHREAD) && !defined(__EMSCRIPTEN__)

static pthread_mutex_t _sodium_lock = PTHREAD_MUTEX_INITIALIZER;

int
sodium_crit_enter(void)
{
    int ret;

    if ((ret = pthread_mutex_lock(&_sodium_lock)) == 0) {
        assert(locked == 0);
        locked = 1;
    }
    return ret;
}

int
sodium_crit_leave(void)
{
    int ret;

    if (locked == 0) {
# ifdef EPERM
        errno = EPERM;
# endif
        return -1;
    }
    locked = 0;

    return pthread_mutex_unlock(&_sodium_lock);
}

#elif defined(HAVE_ATOMIC_OPS) && !defined(__EMSCRIPTEN__) && !defined(__native_client__)

static volatile int _sodium_lock;

int
sodium_crit_enter(void)
{
# ifdef HAVE_NANOSLEEP
    struct timespec q;
    memset(&q, 0, sizeof q);
# endif
    while (__sync_lock_test_and_set(&_sodium_lock, 1) != 0) {
# ifdef HAVE_NANOSLEEP
        (void) nanosleep(&q, NULL);
# elif defined(__x86_64__) || defined(__i386__)
        __asm__ __volatile__ ("pause");
# endif
    }
    return 0;
}

int
sodium_crit_leave(void)
{
    __sync_lock_release(&_sodium_lock);

    return 0;
}

#else

int
sodium_crit_enter(void)
{
    return 0;
}

int
sodium_crit_leave(void)
{
    return 0;
}

#endif

static void (*_misuse_handler)(void);

void
sodium_misuse(void)
{
    void (*handler)(void);

    (void) sodium_crit_leave();
    if (sodium_crit_enter() == 0) {
        handler = _misuse_handler;
        if (handler != NULL) {
            handler();
        }
    }
/* LCOV_EXCL_START */
    abort();
}
/* LCOV_EXCL_STOP */

int
sodium_set_misuse_handler(void (*handler)(void))
{
    if (sodium_crit_enter() != 0) {
        return -1; /* LCOV_EXCL_LINE */
    }
    _misuse_handler = handler;
    if (sodium_crit_leave() != 0) {
        return -1; /* LCOV_EXCL_LINE */
    }
    return 0;
}
