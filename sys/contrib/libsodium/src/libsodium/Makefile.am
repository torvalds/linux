lib_LTLIBRARIES = \
	libsodium.la

libsodium_la_SOURCES = \
	crypto_aead/chacha20poly1305/sodium/aead_chacha20poly1305.c \
	crypto_aead/xchacha20poly1305/sodium/aead_xchacha20poly1305.c \
	crypto_auth/crypto_auth.c \
	crypto_auth/hmacsha256/auth_hmacsha256.c \
	crypto_auth/hmacsha512/auth_hmacsha512.c \
	crypto_auth/hmacsha512256/auth_hmacsha512256.c \
	crypto_box/crypto_box.c \
	crypto_box/crypto_box_easy.c \
	crypto_box/crypto_box_seal.c \
	crypto_box/curve25519xsalsa20poly1305/box_curve25519xsalsa20poly1305.c \
	crypto_core/ed25519/ref10/ed25519_ref10.c \
	crypto_core/hchacha20/core_hchacha20.c \
	crypto_core/hsalsa20/ref2/core_hsalsa20_ref2.c \
	crypto_core/hsalsa20/core_hsalsa20.c \
	crypto_core/salsa/ref/core_salsa_ref.c \
	crypto_generichash/crypto_generichash.c \
	crypto_generichash/blake2b/generichash_blake2.c \
	crypto_generichash/blake2b/ref/blake2.h \
	crypto_generichash/blake2b/ref/blake2b-compress-ref.c \
	crypto_generichash/blake2b/ref/blake2b-load-sse2.h \
	crypto_generichash/blake2b/ref/blake2b-load-sse41.h \
	crypto_generichash/blake2b/ref/blake2b-load-avx2.h \
	crypto_generichash/blake2b/ref/blake2b-ref.c \
	crypto_generichash/blake2b/ref/generichash_blake2b.c \
	crypto_hash/crypto_hash.c \
	crypto_hash/sha256/hash_sha256.c \
	crypto_hash/sha256/cp/hash_sha256_cp.c \
	crypto_hash/sha512/hash_sha512.c \
	crypto_hash/sha512/cp/hash_sha512_cp.c \
	crypto_kdf/blake2b/kdf_blake2b.c \
	crypto_kdf/crypto_kdf.c \
	crypto_kx/crypto_kx.c \
	crypto_onetimeauth/crypto_onetimeauth.c \
	crypto_onetimeauth/poly1305/onetimeauth_poly1305.c \
	crypto_onetimeauth/poly1305/onetimeauth_poly1305.h \
	crypto_onetimeauth/poly1305/donna/poly1305_donna.h \
	crypto_onetimeauth/poly1305/donna/poly1305_donna32.h \
	crypto_onetimeauth/poly1305/donna/poly1305_donna64.h \
	crypto_onetimeauth/poly1305/donna/poly1305_donna.c \
	crypto_pwhash/argon2/argon2-core.c \
	crypto_pwhash/argon2/argon2-core.h \
	crypto_pwhash/argon2/argon2-encoding.c \
	crypto_pwhash/argon2/argon2-encoding.h \
	crypto_pwhash/argon2/argon2-fill-block-ref.c \
	crypto_pwhash/argon2/argon2.c \
	crypto_pwhash/argon2/argon2.h \
	crypto_pwhash/argon2/blake2b-long.c \
	crypto_pwhash/argon2/blake2b-long.h \
	crypto_pwhash/argon2/blamka-round-ref.h \
	crypto_pwhash/argon2/pwhash_argon2i.c \
	crypto_pwhash/argon2/pwhash_argon2id.c \
	crypto_pwhash/crypto_pwhash.c \
	crypto_scalarmult/crypto_scalarmult.c \
	crypto_scalarmult/curve25519/ref10/x25519_ref10.c \
	crypto_scalarmult/curve25519/ref10/x25519_ref10.h \
	crypto_scalarmult/curve25519/scalarmult_curve25519.c \
	crypto_scalarmult/curve25519/scalarmult_curve25519.h \
	crypto_secretbox/crypto_secretbox.c \
	crypto_secretbox/crypto_secretbox_easy.c \
	crypto_secretbox/xsalsa20poly1305/secretbox_xsalsa20poly1305.c \
	crypto_secretstream/xchacha20poly1305/secretstream_xchacha20poly1305.c \
	crypto_shorthash/crypto_shorthash.c \
	crypto_shorthash/siphash24/shorthash_siphash24.c \
	crypto_shorthash/siphash24/ref/shorthash_siphash24_ref.c \
	crypto_shorthash/siphash24/ref/shorthash_siphash_ref.h \
	crypto_sign/crypto_sign.c \
	crypto_sign/ed25519/sign_ed25519.c \
	crypto_sign/ed25519/ref10/keypair.c \
	crypto_sign/ed25519/ref10/open.c \
	crypto_sign/ed25519/ref10/sign.c \
	crypto_sign/ed25519/ref10/sign_ed25519_ref10.h \
	crypto_stream/chacha20/stream_chacha20.c \
	crypto_stream/chacha20/stream_chacha20.h \
	crypto_stream/chacha20/ref/chacha20_ref.h \
	crypto_stream/chacha20/ref/chacha20_ref.c \
	crypto_stream/crypto_stream.c \
	crypto_stream/salsa20/stream_salsa20.c \
	crypto_stream/salsa20/stream_salsa20.h \
	crypto_stream/xsalsa20/stream_xsalsa20.c \
	crypto_verify/sodium/verify.c \
	include/sodium/private/common.h \
	include/sodium/private/ed25519_ref10.h \
	include/sodium/private/implementations.h \
	include/sodium/private/mutex.h \
	include/sodium/private/sse2_64_32.h \
	randombytes/randombytes.c \
	sodium/codecs.c \
	sodium/core.c \
	sodium/runtime.c \
	sodium/utils.c \
	sodium/version.c

if HAVE_TI_MODE
libsodium_la_SOURCES += \
	crypto_core/ed25519/ref10/fe_51/base.h \
	crypto_core/ed25519/ref10/fe_51/base2.h \
	crypto_core/ed25519/ref10/fe_51/constants.h \
	crypto_core/ed25519/ref10/fe_51/fe.h \
	include/sodium/private/ed25519_ref10_fe_51.h
else
libsodium_la_SOURCES += \
	crypto_core/ed25519/ref10/fe_25_5/base.h \
	crypto_core/ed25519/ref10/fe_25_5/base2.h \
	crypto_core/ed25519/ref10/fe_25_5/constants.h \
	crypto_core/ed25519/ref10/fe_25_5/fe.h \
	include/sodium/private/ed25519_ref10_fe_25_5.h
endif

if HAVE_AMD64_ASM
libsodium_la_SOURCES += \
	crypto_stream/salsa20/xmm6/salsa20_xmm6-asm.S \
	crypto_stream/salsa20/xmm6/salsa20_xmm6.c \
	crypto_stream/salsa20/xmm6/salsa20_xmm6.h
else
libsodium_la_SOURCES += \
	crypto_stream/salsa20/ref/salsa20_ref.c \
	crypto_stream/salsa20/ref/salsa20_ref.h
endif

noinst_HEADERS = \
	crypto_scalarmult/curve25519/sandy2x/consts.S \
	crypto_scalarmult/curve25519/sandy2x/fe51_mul.S \
	crypto_scalarmult/curve25519/sandy2x/fe51_nsquare.S \
	crypto_scalarmult/curve25519/sandy2x/fe51_pack.S \
	crypto_scalarmult/curve25519/sandy2x/ladder.S \
	crypto_scalarmult/curve25519/sandy2x/ladder_base.S

if HAVE_AVX_ASM
libsodium_la_SOURCES += \
	crypto_scalarmult/curve25519/sandy2x/consts_namespace.h \
	crypto_scalarmult/curve25519/sandy2x/curve25519_sandy2x.c \
	crypto_scalarmult/curve25519/sandy2x/curve25519_sandy2x.h \
	crypto_scalarmult/curve25519/sandy2x/fe.h \
	crypto_scalarmult/curve25519/sandy2x/fe51.h \
	crypto_scalarmult/curve25519/sandy2x/fe51_invert.c \
	crypto_scalarmult/curve25519/sandy2x/fe51_namespace.h \
	crypto_scalarmult/curve25519/sandy2x/fe_frombytes_sandy2x.c \
	crypto_scalarmult/curve25519/sandy2x/ladder.h \
	crypto_scalarmult/curve25519/sandy2x/ladder_base.h \
	crypto_scalarmult/curve25519/sandy2x/ladder_base_namespace.h \
	crypto_scalarmult/curve25519/sandy2x/ladder_namespace.h \
	crypto_scalarmult/curve25519/sandy2x/sandy2x.S
endif

if !MINIMAL
libsodium_la_SOURCES += \
	crypto_box/curve25519xchacha20poly1305/box_curve25519xchacha20poly1305.c \
	crypto_box/curve25519xchacha20poly1305/box_seal_curve25519xchacha20poly1305.c \
	crypto_core/ed25519/core_ed25519.c \
	crypto_pwhash/scryptsalsa208sha256/crypto_scrypt-common.c \
	crypto_pwhash/scryptsalsa208sha256/crypto_scrypt.h \
	crypto_pwhash/scryptsalsa208sha256/scrypt_platform.c \
	crypto_pwhash/scryptsalsa208sha256/pbkdf2-sha256.c \
	crypto_pwhash/scryptsalsa208sha256/pbkdf2-sha256.h \
	crypto_pwhash/scryptsalsa208sha256/pwhash_scryptsalsa208sha256.c \
	crypto_pwhash/scryptsalsa208sha256/nosse/pwhash_scryptsalsa208sha256_nosse.c \
	crypto_scalarmult/ed25519/ref10/scalarmult_ed25519_ref10.c \
	crypto_secretbox/xchacha20poly1305/secretbox_xchacha20poly1305.c \
	crypto_shorthash/siphash24/shorthash_siphashx24.c \
	crypto_shorthash/siphash24/ref/shorthash_siphashx24_ref.c \
	crypto_sign/ed25519/ref10/obsolete.c \
	crypto_stream/salsa2012/ref/stream_salsa2012_ref.c \
	crypto_stream/salsa2012/stream_salsa2012.c \
	crypto_stream/salsa208/ref/stream_salsa208_ref.c \
	crypto_stream/salsa208/stream_salsa208.c \
	crypto_stream/xchacha20/stream_xchacha20.c
endif

randombytes_salsa20_randombytes_salsa20_random_CFLAGS = @CFLAGS_RDRAND@

libsodium_la_LDFLAGS = \
	$(AM_LDFLAGS) \
	-export-dynamic \
	-no-undefined \
	$(LIBTOOL_EXTRA_FLAGS)

libsodium_la_CPPFLAGS = \
	$(LTDLINCL) \
	-I$(srcdir)/include/sodium \
	-I$(builddir)/include/sodium

if HAVE_LD_OUTPUT_DEF
libsodium_la_LDFLAGS += -Wl,--output-def,libsodium-$(DLL_VERSION).def
defexecdir = $(bindir)
defexec_DATA = libsodium-$(DLL_VERSION).def
CLEANFILES = $(defexec_DATA)
libsodium-$(DLL_VERSION).def: libsodium.la
endif

SUBDIRS = \
	include

libsodium_la_LIBADD = libaesni.la libsse2.la libssse3.la libsse41.la libavx2.la libavx512f.la
noinst_LTLIBRARIES  = libaesni.la libsse2.la libssse3.la libsse41.la libavx2.la libavx512f.la

librdrand_la_LDFLAGS = $(libsodium_la_LDFLAGS)
librdrand_la_CPPFLAGS = $(libsodium_la_CPPFLAGS) \
	@CFLAGS_RDRAND@
librdrand_la_SOURCES = \
	randombytes/salsa20/randombytes_salsa20_random.c

if !EMSCRIPTEN
libsodium_la_LIBADD += librdrand.la
noinst_LTLIBRARIES  += librdrand.la

if NATIVECLIENT
libsodium_la_SOURCES += \
	randombytes/nativeclient/randombytes_nativeclient.c
else
libsodium_la_SOURCES += \
	randombytes/sysrandom/randombytes_sysrandom.c
endif

endif

libaesni_la_LDFLAGS = $(libsodium_la_LDFLAGS)
libaesni_la_CPPFLAGS = $(libsodium_la_CPPFLAGS) \
	@CFLAGS_SSE2@ @CFLAGS_SSSE3@ @CFLAGS_AESNI@ @CFLAGS_PCLMUL@
libaesni_la_SOURCES = \
	crypto_aead/aes256gcm/aesni/aead_aes256gcm_aesni.c

libsse2_la_LDFLAGS = $(libsodium_la_LDFLAGS)
libsse2_la_CPPFLAGS = $(libsodium_la_CPPFLAGS) \
	@CFLAGS_SSE2@
libsse2_la_SOURCES = \
	crypto_onetimeauth/poly1305/sse2/poly1305_sse2.c \
	crypto_onetimeauth/poly1305/sse2/poly1305_sse2.h
if !MINIMAL
libsse2_la_SOURCES += \
	crypto_pwhash/scryptsalsa208sha256/sse/pwhash_scryptsalsa208sha256_sse.c
endif

if !HAVE_AMD64_ASM
libsse2_la_SOURCES += \
	crypto_stream/salsa20/xmm6int/salsa20_xmm6int-sse2.c \
	crypto_stream/salsa20/xmm6int/salsa20_xmm6int-sse2.h \
	crypto_stream/salsa20/xmm6int/u0.h \
	crypto_stream/salsa20/xmm6int/u1.h \
	crypto_stream/salsa20/xmm6int/u4.h
endif

libssse3_la_LDFLAGS = $(libsodium_la_LDFLAGS)
libssse3_la_CPPFLAGS = $(libsodium_la_CPPFLAGS) \
	@CFLAGS_SSE2@ @CFLAGS_SSSE3@
libssse3_la_SOURCES = \
	crypto_generichash/blake2b/ref/blake2b-compress-ssse3.c \
	crypto_generichash/blake2b/ref/blake2b-compress-ssse3.h \
	crypto_pwhash/argon2/argon2-fill-block-ssse3.c \
	crypto_pwhash/argon2/blamka-round-ssse3.h \
	crypto_stream/chacha20/dolbeau/chacha20_dolbeau-ssse3.c \
	crypto_stream/chacha20/dolbeau/chacha20_dolbeau-ssse3.h \
	crypto_stream/chacha20/dolbeau/u0.h \
	crypto_stream/chacha20/dolbeau/u1.h \
	crypto_stream/chacha20/dolbeau/u4.h

libsse41_la_LDFLAGS = $(libsodium_la_LDFLAGS)
libsse41_la_CPPFLAGS = $(libsodium_la_CPPFLAGS) \
	@CFLAGS_SSE2@ @CFLAGS_SSSE3@ @CFLAGS_SSE41@
libsse41_la_SOURCES = \
	crypto_generichash/blake2b/ref/blake2b-compress-sse41.c \
	crypto_generichash/blake2b/ref/blake2b-compress-sse41.h

libavx2_la_LDFLAGS = $(libsodium_la_LDFLAGS)
libavx2_la_CPPFLAGS = $(libsodium_la_CPPFLAGS) \
	@CFLAGS_SSE2@ @CFLAGS_SSSE3@ @CFLAGS_SSE41@ @CFLAGS_AVX@ @CFLAGS_AVX2@
libavx2_la_SOURCES = \
	crypto_generichash/blake2b/ref/blake2b-compress-avx2.c \
	crypto_generichash/blake2b/ref/blake2b-compress-avx2.h \
	crypto_pwhash/argon2/argon2-fill-block-avx2.c \
	crypto_pwhash/argon2/blamka-round-avx2.h \
	crypto_stream/chacha20/dolbeau/chacha20_dolbeau-avx2.c \
	crypto_stream/chacha20/dolbeau/chacha20_dolbeau-avx2.h \
	crypto_stream/chacha20/dolbeau/u8.h \
	crypto_stream/salsa20/xmm6int/salsa20_xmm6int-avx2.c \
	crypto_stream/salsa20/xmm6int/salsa20_xmm6int-avx2.h \
	crypto_stream/salsa20/xmm6int/u0.h \
	crypto_stream/salsa20/xmm6int/u1.h \
	crypto_stream/salsa20/xmm6int/u4.h \
	crypto_stream/salsa20/xmm6int/u8.h

libavx512f_la_LDFLAGS = $(libsodium_la_LDFLAGS)
libavx512f_la_CPPFLAGS = $(libsodium_la_CPPFLAGS) \
	@CFLAGS_SSE2@ @CFLAGS_SSSE3@ @CFLAGS_SSE41@ @CFLAGS_AVX@ @CFLAGS_AVX2@ @CFLAGS_AVX512F@
libavx512f_la_SOURCES = \
	crypto_pwhash/argon2/argon2-fill-block-avx512f.c \
	crypto_pwhash/argon2/blamka-round-avx512f.h
