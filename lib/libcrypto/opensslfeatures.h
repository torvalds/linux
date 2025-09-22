/* $OpenBSD: opensslfeatures.h,v 1.44 2024/08/31 10:38:49 tb Exp $ */
/*
 * Feature flags for LibreSSL... so you can actually tell when things
 * are enabled, rather than not being able to tell when things are
 * enabled (or possibly not yet not implemented, or removed!).
 */
#define LIBRESSL_HAS_QUIC
#define LIBRESSL_HAS_TLS1_3
#define LIBRESSL_HAS_DTLS1_2

#define OPENSSL_THREADS

#define OPENSSL_NO_BUF_FREELISTS
#define OPENSSL_NO_DEPRECATED
#define OPENSSL_NO_EC2M
#define OPENSSL_NO_GMP
#define OPENSSL_NO_JPAKE
#define OPENSSL_NO_KRB5
#define OPENSSL_NO_RSAX
#define OPENSSL_NO_SHA0
#define OPENSSL_NO_SSL2
#define OPENSSL_NO_STORE

/*
 * OPENSSL_NO_* flags that currently appear in OpenSSL.
 */

/* #define OPENSSL_NO_AFALGENG */
/* #define OPENSSL_NO_ALGORITHMS */
/* #define OPENSSL_NO_ARIA */
/* #define OPENSSL_NO_ASM */
#define OPENSSL_NO_ASYNC
/* #define OPENSSL_NO_AUTOALGINIT */
/* #define OPENSSL_NO_AUTOERRINIT */
/* #define OPENSSL_NO_AUTOLOAD_CONFIG */
/* #define OPENSSL_NO_BF */
#define OPENSSL_NO_BLAKE2
#define OPENSSL_NO_BROTLI
/* #define OPENSSL_NO_BUILTIN_OVERFLOW_CHECKING */
/* #define OPENSSL_NO_CAMELLIA */
#define OPENSSL_NO_CAPIENG
/* #define OPENSSL_NO_CAST */
/* #define OPENSSL_NO_CHACHA */
/* #define OPENSSL_NO_CMAC */
/* #define OPENSSL_NO_CMP */
/* #define OPENSSL_NO_CMS */
#define OPENSSL_NO_COMP
/* #define OPENSSL_NO_COMP_ALG */
/* #define OPENSSL_NO_CRMF */
/* #define OPENSSL_NO_CRYPTO_MDEBUG */
/* #define OPENSSL_NO_CRYPTO_MDEBUG_BACKTRACE */
/* #define OPENSSL_NO_CT */
/* #define OPENSSL_NO_DECC_INIT */
/* #define OPENSSL_NO_DES */
/* #define OPENSSL_NO_DEVCRYPTOENG */
/* #define OPENSSL_NO_DGRAM */
/* #define OPENSSL_NO_DH */
/* #define OPENSSL_NO_DSA */
#define OPENSSL_NO_DSO
/* #define OPENSSL_NO_DTLS */
#define OPENSSL_NO_DTLS1
#ifndef LIBRESSL_HAS_DTLS1_2
#define OPENSSL_NO_DTLS1_2
#endif
/* #define OPENSSL_NO_DTLS1_2_METHOD */
/* #define OPENSSL_NO_DTLS1_METHOD */
#define OPENSSL_NO_DYNAMIC_ENGINE
/* #define OPENSSL_NO_EC */
#define OPENSSL_NO_EC_NISTP_64_GCC_128
#define OPENSSL_NO_EGD
#define OPENSSL_NO_ENGINE
/* #define OPENSSL_NO_ERR */
/* #define OPENSSL_NO_FILENAMES */
/* #define OPENSSL_NO_FUZZ_AFL */
/* #define OPENSSL_NO_FUZZ_LIBFUZZER */
#define OPENSSL_NO_GOST
#define OPENSSL_NO_HEARTBEATS
/* #define OPENSSL_NO_HW */
/* #define OPENSSL_NO_HW_PADLOCK */
/* #define OPENSSL_NO_IDEA */
/* #define OPENSSL_NO_INLINE_ASM */
/* #define OPENSSL_NO_KEYPARAMS */
#define OPENSSL_NO_KTLS
/* #define OPENSSL_NO_KTLS_RX */
/* #define OPENSSL_NO_KTLS_ZC_TX */
/* #define OPENSSL_NO_LOCALE */
#define OPENSSL_NO_MD2
/* #define OPENSSL_NO_MD4 */
/* #define OPENSSL_NO_MD5 */
#define OPENSSL_NO_MDC2
/* #define OPENSSL_NO_MULTIBLOCK */
/* #define OPENSSL_NO_NEXTPROTONEG */
/* #define OPENSSL_NO_OCB */
/* #define OPENSSL_NO_OCSP */
/* #define OPENSSL_NO_PADLOCKENG */
/* #define OPENSSL_NO_PINSHARED */
/* #define OPENSSL_NO_POLY1305 */
/* #define OPENSSL_NO_POSIX_IO */
#define OPENSSL_NO_PSK
#define OPENSSL_NO_QUIC
/* #define OPENSSL_NO_RC2 */
/* #define OPENSSL_NO_RC4 */
#define OPENSSL_NO_RC5
/* #define OPENSSL_NO_RDRAND */
/* #define OPENSSL_NO_RFC3779 */
/* #define OPENSSL_NO_RMD160 */
/* #define OPENSSL_NO_RSA */
#define OPENSSL_NO_SCRYPT
#define OPENSSL_NO_SCTP
/* #define OPENSSL_NO_SECURE_MEMORY */
#define OPENSSL_NO_SEED
/* #define OPENSSL_NO_SIPHASH */
/* #define OPENSSL_NO_SIV */
/* #define OPENSSL_NO_SM2 */
/* #define OPENSSL_NO_SM3 */
/* #define OPENSSL_NO_SM4 */
/* #define OPENSSL_NO_SOCK */
#define OPENSSL_NO_SRP
/* #define OPENSSL_NO_SRTP */
#define OPENSSL_NO_SSL3
#define OPENSSL_NO_SSL3_METHOD
#define OPENSSL_NO_SSL_TRACE
/* #define OPENSSL_NO_STATIC_ENGINE */
/* #define OPENSSL_NO_STDIO */
/* #define OPENSSL_NO_THREAD_POOL */
/* #define OPENSSL_NO_TLS */
#define OPENSSL_NO_TLS1
#define OPENSSL_NO_TLS1_1
#define OPENSSL_NO_TLS1_METHOD
#define OPENSSL_NO_TLS1_1_METHOD
/* #define OPENSSL_NO_TLS1_2 */
/* #define OPENSSL_NO_TLS1_2_METHOD */
#ifndef LIBRESSL_HAS_TLS1_3
#define OPENSSL_NO_TLS1_3
#endif
/* #define OPENSSL_NO_TLS1_METHOD */
/* #define OPENSSL_NO_TRACE */
/* #define OPENSSL_NO_TS */
/* #define OPENSSL_NO_UI_CONSOLE */
/* #define OPENSSL_NO_UNIT_TEST */
/* #define OPENSSL_NO_UNIX_SOCK */
/* #define OPENSSL_NO_WEAK_SSL_CIPHERS */
#define OPENSSL_NO_WHIRLPOOL
/* #define OPENSSL_NO_WINSTORE */
#define OPENSSL_NO_ZLIB
/* #define OPENSSL_NO_ZSTD */
