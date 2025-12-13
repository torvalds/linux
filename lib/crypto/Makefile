# SPDX-License-Identifier: GPL-2.0

aflags-thumb2-$(CONFIG_THUMB2_KERNEL)  := -U__thumb2__ -D__thumb2__=1

quiet_cmd_perlasm = PERLASM $@
      cmd_perlasm = $(PERL) $(<) > $(@)

quiet_cmd_perlasm_with_args = PERLASM $@
      cmd_perlasm_with_args = $(PERL) $(<) void $(@)

obj-$(CONFIG_KUNIT)				+= tests/

obj-$(CONFIG_CRYPTO_HASH_INFO)			+= hash_info.o

obj-$(CONFIG_CRYPTO_LIB_UTILS)			+= libcryptoutils.o
libcryptoutils-y				:= memneq.o utils.o

obj-$(CONFIG_CRYPTO_LIB_AES)			+= libaes.o
libaes-y					:= aes.o

obj-$(CONFIG_CRYPTO_LIB_AESCFB)			+= libaescfb.o
libaescfb-y					:= aescfb.o

obj-$(CONFIG_CRYPTO_LIB_AESGCM)			+= libaesgcm.o
libaesgcm-y					:= aesgcm.o

obj-$(CONFIG_CRYPTO_LIB_ARC4)			+= libarc4.o
libarc4-y					:= arc4.o

obj-$(CONFIG_CRYPTO_LIB_GF128MUL)		+= gf128mul.o

################################################################################

# blake2s is used by the /dev/random driver which is always builtin
obj-y += blake2s.o
ifeq ($(CONFIG_CRYPTO_LIB_BLAKE2S_ARCH),y)
CFLAGS_blake2s.o += -I$(src)/$(SRCARCH)
obj-$(CONFIG_ARM) += arm/blake2s-core.o
obj-$(CONFIG_X86) += x86/blake2s-core.o
endif

################################################################################

# chacha20_block() is used by the /dev/random driver which is always builtin
obj-y += chacha-block-generic.o

obj-$(CONFIG_CRYPTO_LIB_CHACHA) += libchacha.o
libchacha-y := chacha.o

ifeq ($(CONFIG_CRYPTO_LIB_CHACHA_ARCH),y)
CFLAGS_chacha.o += -I$(src)/$(SRCARCH)

ifeq ($(CONFIG_ARM),y)
libchacha-y += arm/chacha-scalar-core.o
libchacha-$(CONFIG_KERNEL_MODE_NEON) += arm/chacha-neon-core.o
endif

libchacha-$(CONFIG_ARM64) += arm64/chacha-neon-core.o

ifeq ($(CONFIG_MIPS),y)
libchacha-y += mips/chacha-core.o
AFLAGS_mips/chacha-core.o += -O2 # needed to fill branch delay slots
endif

libchacha-$(CONFIG_PPC) += powerpc/chacha-p10le-8x.o
libchacha-$(CONFIG_RISCV) += riscv/chacha-riscv64-zvkb.o
libchacha-$(CONFIG_S390) += s390/chacha-s390.o
libchacha-$(CONFIG_X86) += x86/chacha-ssse3-x86_64.o \
			   x86/chacha-avx2-x86_64.o \
			   x86/chacha-avx512vl-x86_64.o
endif # CONFIG_CRYPTO_LIB_CHACHA_ARCH

################################################################################

obj-$(CONFIG_CRYPTO_LIB_CHACHA20POLY1305)	+= libchacha20poly1305.o
libchacha20poly1305-y				+= chacha20poly1305.o
libchacha20poly1305-$(CONFIG_CRYPTO_SELFTESTS)	+= chacha20poly1305-selftest.o

################################################################################

obj-$(CONFIG_CRYPTO_LIB_CURVE25519) += libcurve25519.o
libcurve25519-y := curve25519.o

# Disable GCOV in odd or sensitive code
GCOV_PROFILE_curve25519.o := n

ifeq ($(CONFIG_ARCH_SUPPORTS_INT128),y)
libcurve25519-$(CONFIG_CRYPTO_LIB_CURVE25519_GENERIC) += curve25519-hacl64.o
else
libcurve25519-$(CONFIG_CRYPTO_LIB_CURVE25519_GENERIC) += curve25519-fiat32.o
endif
# clang versions prior to 18 may blow out the stack with KASAN
ifeq ($(CONFIG_CC_IS_CLANG)_$(call clang-min-version, 180000),y_)
KASAN_SANITIZE_curve25519-hacl64.o := n
endif

ifeq ($(CONFIG_CRYPTO_LIB_CURVE25519_ARCH),y)
CFLAGS_curve25519.o += -I$(src)/$(SRCARCH)
libcurve25519-$(CONFIG_ARM) += arm/curve25519-core.o
libcurve25519-$(CONFIG_PPC) += powerpc/curve25519-ppc64le_asm.o
endif

################################################################################

obj-$(CONFIG_CRYPTO_LIB_DES)			+= libdes.o
libdes-y					:= des.o

################################################################################

obj-$(CONFIG_CRYPTO_LIB_MD5) += libmd5.o
libmd5-y := md5.o
ifeq ($(CONFIG_CRYPTO_LIB_MD5_ARCH),y)
CFLAGS_md5.o += -I$(src)/$(SRCARCH)
libmd5-$(CONFIG_PPC) += powerpc/md5-asm.o
libmd5-$(CONFIG_SPARC) += sparc/md5_asm.o
endif # CONFIG_CRYPTO_LIB_MD5_ARCH

################################################################################

obj-$(CONFIG_CRYPTO_LIB_POLY1305) += libpoly1305.o
libpoly1305-y := poly1305.o
ifeq ($(CONFIG_ARCH_SUPPORTS_INT128),y)
libpoly1305-$(CONFIG_CRYPTO_LIB_POLY1305_GENERIC) += poly1305-donna64.o
else
libpoly1305-$(CONFIG_CRYPTO_LIB_POLY1305_GENERIC) += poly1305-donna32.o
endif

ifeq ($(CONFIG_CRYPTO_LIB_POLY1305_ARCH),y)
CFLAGS_poly1305.o += -I$(src)/$(SRCARCH)

ifeq ($(CONFIG_ARM),y)
libpoly1305-y += arm/poly1305-core.o
$(obj)/arm/poly1305-core.S: $(src)/arm/poly1305-armv4.pl
	$(call cmd,perlasm)
# massage the perlasm code a bit so we only get the NEON routine if we need it
poly1305-aflags-$(CONFIG_CPU_V7) := -U__LINUX_ARM_ARCH__ -D__LINUX_ARM_ARCH__=5
poly1305-aflags-$(CONFIG_KERNEL_MODE_NEON) := -U__LINUX_ARM_ARCH__ -D__LINUX_ARM_ARCH__=7
AFLAGS_arm/poly1305-core.o += $(poly1305-aflags-y) $(aflags-thumb2-y)
endif

ifeq ($(CONFIG_ARM64),y)
libpoly1305-y += arm64/poly1305-core.o
$(obj)/arm64/poly1305-core.S: $(src)/arm64/poly1305-armv8.pl
	$(call cmd,perlasm_with_args)
endif

ifeq ($(CONFIG_MIPS),y)
libpoly1305-y += mips/poly1305-core.o
poly1305-perlasm-flavour-$(CONFIG_32BIT) := o32
poly1305-perlasm-flavour-$(CONFIG_64BIT) := 64
quiet_cmd_perlasm_poly1305 = PERLASM $@
      cmd_perlasm_poly1305 = $(PERL) $< $(poly1305-perlasm-flavour-y) $@
# Use if_changed instead of cmd, in case the flavour changed.
$(obj)/mips/poly1305-core.S: $(src)/mips/poly1305-mips.pl FORCE
	$(call if_changed,perlasm_poly1305)
targets += mips/poly1305-core.S
endif

libpoly1305-$(CONFIG_PPC) += powerpc/poly1305-p10le_64.o

ifeq ($(CONFIG_RISCV),y)
libpoly1305-y += riscv/poly1305-core.o
poly1305-perlasm-flavour-$(CONFIG_32BIT) := 32
poly1305-perlasm-flavour-$(CONFIG_64BIT) := 64
quiet_cmd_perlasm_poly1305 = PERLASM $@
      cmd_perlasm_poly1305 = $(PERL) $< $(poly1305-perlasm-flavour-y) $@
# Use if_changed instead of cmd, in case the flavour changed.
$(obj)/riscv/poly1305-core.S: $(src)/riscv/poly1305-riscv.pl FORCE
	$(call if_changed,perlasm_poly1305)
targets += riscv/poly1305-core.S
AFLAGS_riscv/poly1305-core.o += -Dpoly1305_init=poly1305_block_init
endif

ifeq ($(CONFIG_X86),y)
libpoly1305-y += x86/poly1305-x86_64-cryptogams.o
$(obj)/x86/poly1305-x86_64-cryptogams.S: $(src)/x86/poly1305-x86_64-cryptogams.pl
	$(call cmd,perlasm)
endif

endif # CONFIG_CRYPTO_LIB_POLY1305_ARCH

# clean-files must be defined unconditionally
clean-files += arm/poly1305-core.S \
	       arm64/poly1305-core.S \
	       mips/poly1305-core.S \
	       riscv/poly1305-core.S \
	       x86/poly1305-x86_64-cryptogams.S

################################################################################

obj-$(CONFIG_CRYPTO_LIB_SHA1) += libsha1.o
libsha1-y := sha1.o
ifeq ($(CONFIG_CRYPTO_LIB_SHA1_ARCH),y)
CFLAGS_sha1.o += -I$(src)/$(SRCARCH)
ifeq ($(CONFIG_ARM),y)
libsha1-y += arm/sha1-armv4-large.o
libsha1-$(CONFIG_KERNEL_MODE_NEON) += arm/sha1-armv7-neon.o \
				      arm/sha1-ce-core.o
endif
libsha1-$(CONFIG_ARM64) += arm64/sha1-ce-core.o
ifeq ($(CONFIG_PPC),y)
libsha1-y += powerpc/sha1-powerpc-asm.o
libsha1-$(CONFIG_SPE) += powerpc/sha1-spe-asm.o
endif
libsha1-$(CONFIG_SPARC) += sparc/sha1_asm.o
libsha1-$(CONFIG_X86) += x86/sha1-ssse3-and-avx.o \
			 x86/sha1-avx2-asm.o \
			 x86/sha1-ni-asm.o
endif # CONFIG_CRYPTO_LIB_SHA1_ARCH

################################################################################

obj-$(CONFIG_CRYPTO_LIB_SHA256) += libsha256.o
libsha256-y := sha256.o
ifeq ($(CONFIG_CRYPTO_LIB_SHA256_ARCH),y)
CFLAGS_sha256.o += -I$(src)/$(SRCARCH)

ifeq ($(CONFIG_ARM),y)
libsha256-y += arm/sha256-ce.o arm/sha256-core.o
$(obj)/arm/sha256-core.S: $(src)/arm/sha256-armv4.pl
	$(call cmd,perlasm)
AFLAGS_arm/sha256-core.o += $(aflags-thumb2-y)
endif

ifeq ($(CONFIG_ARM64),y)
libsha256-y += arm64/sha256-core.o
$(obj)/arm64/sha256-core.S: $(src)/arm64/sha2-armv8.pl
	$(call cmd,perlasm_with_args)
libsha256-$(CONFIG_KERNEL_MODE_NEON) += arm64/sha256-ce.o
endif

libsha256-$(CONFIG_PPC) += powerpc/sha256-spe-asm.o
libsha256-$(CONFIG_RISCV) += riscv/sha256-riscv64-zvknha_or_zvknhb-zvkb.o
libsha256-$(CONFIG_SPARC) += sparc/sha256_asm.o
libsha256-$(CONFIG_X86) += x86/sha256-ssse3-asm.o \
			   x86/sha256-avx-asm.o \
			   x86/sha256-avx2-asm.o \
			   x86/sha256-ni-asm.o
endif # CONFIG_CRYPTO_LIB_SHA256_ARCH

################################################################################

obj-$(CONFIG_CRYPTO_LIB_SHA512) += libsha512.o
libsha512-y := sha512.o
ifeq ($(CONFIG_CRYPTO_LIB_SHA512_ARCH),y)
CFLAGS_sha512.o += -I$(src)/$(SRCARCH)

ifeq ($(CONFIG_ARM),y)
libsha512-y += arm/sha512-core.o
$(obj)/arm/sha512-core.S: $(src)/arm/sha512-armv4.pl
	$(call cmd,perlasm)
AFLAGS_arm/sha512-core.o += $(aflags-thumb2-y)
endif

ifeq ($(CONFIG_ARM64),y)
libsha512-y += arm64/sha512-core.o
$(obj)/arm64/sha512-core.S: $(src)/arm64/sha2-armv8.pl
	$(call cmd,perlasm_with_args)
libsha512-$(CONFIG_KERNEL_MODE_NEON) += arm64/sha512-ce-core.o
endif

libsha512-$(CONFIG_RISCV) += riscv/sha512-riscv64-zvknhb-zvkb.o
libsha512-$(CONFIG_SPARC) += sparc/sha512_asm.o
libsha512-$(CONFIG_X86) += x86/sha512-ssse3-asm.o \
			   x86/sha512-avx-asm.o \
			   x86/sha512-avx2-asm.o
endif # CONFIG_CRYPTO_LIB_SHA512_ARCH

################################################################################

obj-$(CONFIG_MPILIB) += mpi/

obj-$(CONFIG_CRYPTO_SELFTESTS_FULL)		+= simd.o

obj-$(CONFIG_CRYPTO_LIB_SM3)			+= libsm3.o
libsm3-y					:= sm3.o

# clean-files must be defined unconditionally
clean-files += arm/sha256-core.S arm/sha512-core.S
clean-files += arm64/sha256-core.S arm64/sha512-core.S
