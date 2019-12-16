# SPDX-License-Identifier: GPL-2.0

# chacha is used by the /dev/random driver which is always builtin
obj-y						+= chacha.o
obj-$(CONFIG_CRYPTO_LIB_CHACHA_GENERIC)		+= libchacha.o

obj-$(CONFIG_CRYPTO_LIB_POLY1305_GENERIC)	+= libpoly1305.o
libpoly1305-y					:= poly1305.o

obj-$(CONFIG_CRYPTO_LIB_BLAKE2S_GENERIC)	+= libblake2s-generic.o
libblake2s-generic-y				+= blake2s-generic.o

obj-$(CONFIG_CRYPTO_LIB_BLAKE2S)		+= libblake2s.o
libblake2s-y					+= blake2s.o

obj-$(CONFIG_CRYPTO_LIB_CHACHA20POLY1305)	+= libchacha20poly1305.o
libchacha20poly1305-y				+= chacha20poly1305.o

obj-$(CONFIG_CRYPTO_LIB_CURVE25519_GENERIC)	+= libcurve25519.o
libcurve25519-y					:= curve25519-fiat32.o
libcurve25519-$(CONFIG_ARCH_SUPPORTS_INT128)	:= curve25519-hacl64.o
libcurve25519-y					+= curve25519.o

ifneq ($(CONFIG_CRYPTO_MANAGER_DISABLE_TESTS),y)
libblake2s-y					+= blake2s-selftest.o
libchacha20poly1305-y				+= chacha20poly1305-selftest.o
libcurve25519-y					+= curve25519-selftest.o
endif
