
#define TEST_NAME "misuse"
#include "cmptest.h"

#ifdef HAVE_CATCHABLE_ABRT
# include <signal.h>

static void
sigabrt_handler_15(int sig)
{
    (void) sig;
    exit(0);
}

# ifndef SODIUM_LIBRARY_MINIMAL
static void
sigabrt_handler_14(int sig)
{
    (void) sig;
    signal(SIGABRT, sigabrt_handler_15);
    assert(crypto_box_curve25519xchacha20poly1305_easy
           (NULL, NULL, crypto_stream_xchacha20_MESSAGEBYTES_MAX - 1,
            NULL, NULL, NULL) == -1);
    exit(1);
}

static void
sigabrt_handler_13(int sig)
{
    (void) sig;
    signal(SIGABRT, sigabrt_handler_14);
    assert(crypto_box_curve25519xchacha20poly1305_easy_afternm
           (NULL, NULL, crypto_stream_xchacha20_MESSAGEBYTES_MAX - 1,
            NULL, NULL) == -1);
    exit(1);
}
# endif

static void
sigabrt_handler_12(int sig)
{
    (void) sig;
# ifdef SODIUM_LIBRARY_MINIMAL
    signal(SIGABRT, sigabrt_handler_15);
# else
    signal(SIGABRT, sigabrt_handler_13);
# endif
    assert(crypto_pwhash_str_alg(NULL, "", 0U, 1U, 1U, -1) == -1);
    exit(1);
}

static void
sigabrt_handler_11(int sig)
{
    (void) sig;
    signal(SIGABRT, sigabrt_handler_12);
    assert(crypto_box_easy(NULL, NULL, crypto_stream_xsalsa20_MESSAGEBYTES_MAX,
                           NULL, NULL, NULL) == -1);
    exit(1);
}

static void
sigabrt_handler_10(int sig)
{
    (void) sig;
    signal(SIGABRT, sigabrt_handler_11);
    assert(crypto_box_easy_afternm(NULL, NULL, crypto_stream_xsalsa20_MESSAGEBYTES_MAX,
                                   NULL, NULL) == -1);
    exit(1);
}

static void
sigabrt_handler_9(int sig)
{
    (void) sig;
    signal(SIGABRT, sigabrt_handler_10);
    assert(sodium_base642bin(NULL, 1, NULL, 1, NULL, NULL, NULL, -1) == -1);
    exit(1);
}

static void
sigabrt_handler_8(int sig)
{
    (void) sig;
    signal(SIGABRT, sigabrt_handler_9);
    assert(sodium_bin2base64(NULL, 1, NULL, 1, sodium_base64_VARIANT_ORIGINAL) == NULL);
    exit(1);
}

static void
sigabrt_handler_7(int sig)
{
    (void) sig;
    signal(SIGABRT, sigabrt_handler_8);
    assert(sodium_bin2base64(NULL, 1, NULL, 1, -1) == NULL);
    exit(1);
}

static void
sigabrt_handler_6(int sig)
{
    (void) sig;
    signal(SIGABRT, sigabrt_handler_7);
    assert(sodium_pad(NULL, NULL, SIZE_MAX, 16, 1) == -1);
    exit(1);
}

static void
sigabrt_handler_5(int sig)
{
    (void) sig;
    signal(SIGABRT, sigabrt_handler_6);
    assert(crypto_aead_xchacha20poly1305_ietf_encrypt(NULL, NULL, NULL, UINT64_MAX,
                                                      NULL, 0, NULL, NULL, NULL) == -1);
    exit(1);
}

static void
sigabrt_handler_4(int sig)
{
    (void) sig;
    signal(SIGABRT, sigabrt_handler_5);
    assert(crypto_aead_chacha20poly1305_ietf_encrypt(NULL, NULL, NULL, UINT64_MAX,
                                                     NULL, 0, NULL, NULL, NULL) == -1);
    exit(1);
}

static void
sigabrt_handler_3(int sig)
{
    (void) sig;
    signal(SIGABRT, sigabrt_handler_4);
    assert(crypto_aead_chacha20poly1305_encrypt(NULL, NULL, NULL, UINT64_MAX,
                                                NULL, 0, NULL, NULL, NULL) == -1);
    exit(1);
}

static void
sigabrt_handler_2(int sig)
{
    (void) sig;
    signal(SIGABRT, sigabrt_handler_3);
#if SIZE_MAX > 0x4000000000ULL
    randombytes_buf_deterministic(NULL, 0x4000000001ULL, NULL);
#else
    abort();
#endif
    exit(1);
}

static void
sigabrt_handler_1(int sig)
{
    (void) sig;
    signal(SIGABRT, sigabrt_handler_2);
    assert(crypto_kx_server_session_keys(NULL, NULL, NULL, NULL, NULL) == -1);
    exit(1);
}

int
main(void)
{
    signal(SIGABRT, sigabrt_handler_1);
    assert(crypto_kx_client_session_keys(NULL, NULL, NULL, NULL, NULL) == -1);
    return 1;
}
#else
int
main(void)
{
    return 0;
}
#endif
