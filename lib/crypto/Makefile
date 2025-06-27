# SPDX-License-Identifier: GPL-2.0

obj-y += tests/

aflags-thumb2-$(CONFIG_THUMB2_KERNEL)  := -U__thumb2__ -D__thumb2__=1

quiet_cmd_perlasm = PERLASM $@
      cmd_perlasm = $(PERL) $(<) > $(@)

quiet_cmd_perlasm_with_args = PERLASM $@
      cmd_perlasm_with_args = $(PERL) $(<) void $(@)

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

obj-$(CONFIG_CRYPTO_LIB_SHA512) += libsha512.o
libsha512-y := sha512.o
ifeq ($(CONFIG_CRYPTO_LIB_SHA512_ARCH),y)
CFLAGS_sha512.o += -I$(src)/$(SRCARCH)

ifeq ($(CONFIG_ARM),y)
libsha512-y += arm/sha512-core.o
$(obj)/arm/sha512-core.S: $(src)/arm/sha512-armv4.pl
	$(call cmd,perlasm)
clean-files += arm/sha512-core.S
AFLAGS_arm/sha512-core.o += $(aflags-thumb2-y)
endif

ifeq ($(CONFIG_ARM64),y)
libsha512-y += arm64/sha512-core.o
$(obj)/arm64/sha512-core.S: $(src)/arm64/sha2-armv8.pl
	$(call cmd,perlasm_with_args)
clean-files += arm64/sha512-core.S
libsha512-$(CONFIG_KERNEL_MODE_NEON) += arm64/sha512-ce-core.o
endif

libsha512-$(CONFIG_RISCV) += riscv/sha512-riscv64-zvknhb-zvkb.o
libsha512-$(CONFIG_SPARC) += sparc/sha512_asm.o
libsha512-$(CONFIG_X86) += x86/sha512-ssse3-asm.o \
			   x86/sha512-avx-asm.o \
			   x86/sha512-avx2-asm.o
endif # CONFIG_CRYPTO_LIB_SHA512_ARCH

obj-$(CONFIG_MPILIB) += mpi/

obj-$(CONFIG_CRYPTO_SELFTESTS_FULL)		+= simd.o

obj-$(CONFIG_CRYPTO_LIB_SM3)			+= libsm3.o
libsm3-y					:= sm3.o

obj-$(CONFIG_ARM) += arm/
obj-$(CONFIG_ARM64) += arm64/
obj-$(CONFIG_MIPS) += mips/
obj-$(CONFIG_PPC) += powerpc/
obj-$(CONFIG_RISCV) += riscv/
obj-$(CONFIG_S390) += s390/
obj-$(CONFIG_SPARC) += sparc/
obj-$(CONFIG_X86) += x86/
