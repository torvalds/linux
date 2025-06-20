# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_CRYPTO_LIB_UTILS)			+= libcryptoutils.o
libcryptoutils-y				:= memneq.o utils.o

# chacha is used by the /dev/random driver which is always builtin
obj-y						+= chacha.o
obj-$(CONFIG_CRYPTO_LIB_CHACHA_GENERIC)		+= libchacha.o

obj-$(CONFIG_CRYPTO_LIB_AES)			+= libaes.o
libaes-y					:= aes.o

obj-$(CONFIG_CRYPTO_LIB_AESCFB)			+= libaescfb.o
libaescfb-y					:= aescfb.o

obj-$(CONFIG_CRYPTO_LIB_AESGCM)			+= libaesgcm.o
libaesgcm-y					:= aesgcm.o

obj-$(CONFIG_CRYPTO_LIB_ARC4)			+= libarc4.o
libarc4-y					:= arc4.o

obj-$(CONFIG_CRYPTO_LIB_GF128MUL)		+= gf128mul.o

# blake2s is used by the /dev/random driver which is always builtin
obj-y						+= libblake2s.o
libblake2s-y					:= blake2s.o
libblake2s-$(CONFIG_CRYPTO_LIB_BLAKE2S_GENERIC)	+= blake2s-generic.o
libblake2s-$(CONFIG_CRYPTO_SELFTESTS)		+= blake2s-selftest.o

obj-$(CONFIG_CRYPTO_LIB_CHACHA20POLY1305)	+= libchacha20poly1305.o
libchacha20poly1305-y				+= chacha20poly1305.o
libchacha20poly1305-$(CONFIG_CRYPTO_SELFTESTS)	+= chacha20poly1305-selftest.o

obj-$(CONFIG_CRYPTO_LIB_CURVE25519_GENERIC)	+= libcurve25519-generic.o
libcurve25519-generic-y				:= curve25519-fiat32.o
libcurve25519-generic-$(CONFIG_ARCH_SUPPORTS_INT128)	:= curve25519-hacl64.o
libcurve25519-generic-y				+= curve25519-generic.o
# clang versions prior to 18 may blow out the stack with KASAN
ifeq ($(call clang-min-version, 180000),)
KASAN_SANITIZE_curve25519-hacl64.o := n
endif

obj-$(CONFIG_CRYPTO_LIB_CURVE25519)		+= libcurve25519.o
libcurve25519-y					+= curve25519.o
libcurve25519-$(CONFIG_CRYPTO_SELFTESTS)	+= curve25519-selftest.o

obj-$(CONFIG_CRYPTO_LIB_DES)			+= libdes.o
libdes-y					:= des.o

obj-$(CONFIG_CRYPTO_LIB_POLY1305)		+= libpoly1305.o
libpoly1305-y					+= poly1305.o

obj-$(CONFIG_CRYPTO_LIB_POLY1305_GENERIC)	+= libpoly1305-generic.o
libpoly1305-generic-y				:= poly1305-donna32.o
libpoly1305-generic-$(CONFIG_ARCH_SUPPORTS_INT128) := poly1305-donna64.o
libpoly1305-generic-y				+= poly1305-generic.o

obj-$(CONFIG_CRYPTO_LIB_SHA1)			+= libsha1.o
libsha1-y					:= sha1.o

obj-$(CONFIG_CRYPTO_LIB_SHA256)			+= libsha256.o
libsha256-y					:= sha256.o

obj-$(CONFIG_CRYPTO_LIB_SHA256_GENERIC)		+= libsha256-generic.o
libsha256-generic-y				:= sha256-generic.o

obj-$(CONFIG_MPILIB) += mpi/

obj-$(CONFIG_CRYPTO_SELFTESTS_FULL)		+= simd.o

obj-$(CONFIG_CRYPTO_LIB_SM3)			+= libsm3.o
libsm3-y					:= sm3.o
