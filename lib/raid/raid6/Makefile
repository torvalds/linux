# SPDX-License-Identifier: GPL-2.0

ccflags-y			+= -I $(src)

ifeq ($(CONFIG_RAID6_PQ_ARCH),y)
CFLAGS_algos.o			+= -I$(src)/$(SRCARCH)
endif

obj-$(CONFIG_RAID6_PQ)		+= raid6_pq.o tests/

raid6_pq-y			+= algos.o tables.o

# generic integer generation and recovery implementation
raid6_pq-y			+= int1.o int2.o int4.o int8.o
raid6_pq-y			+= recov.o

# architecture-specific generation and recovery implementations:
raid6_pq-$(CONFIG_KERNEL_MODE_NEON) += arm/neon.o \
				   arm/neon1.o \
				   arm/neon2.o \
				   arm/neon4.o \
				   arm/neon8.o \
				   arm/recov_neon.o \
				   arm/recov_neon_inner.o
raid6_pq-$(CONFIG_LOONGARCH)	+= loongarch/loongarch_simd.o \
				   loongarch/recov_loongarch_simd.o
raid6_pq-$(CONFIG_ALTIVEC)	+= powerpc/altivec1.o \
				   powerpc/altivec2.o \
				   powerpc/altivec4.o \
				   powerpc/altivec8.o \
				   powerpc/vpermxor1.o \
				   powerpc/vpermxor2.o \
				   powerpc/vpermxor4.o \
				   powerpc/vpermxor8.o
raid6_pq-$(CONFIG_RISCV_ISA_V)	+= riscv/rvv.o \
				   riscv/recov_rvv.o
raid6_pq-$(CONFIG_S390)		+= s390/s390vx8.o \
				   s390/recov_s390xc.o
ifeq ($(CONFIG_X86),y)
raid6_pq-$(CONFIG_X86_32)	+= x86/mmx.o \
				   x86/sse1.o
endif
raid6_pq-$(CONFIG_X86)		+= x86/sse2.o \
				   x86/avx2.o \
				   x86/avx512.o \
				   x86/recov_ssse3.o \
				   x86/recov_avx2.o \
				   x86/recov_avx512.o

hostprogs			+= mktables

CFLAGS_arm/neon1.o += $(CC_FLAGS_FPU)
CFLAGS_arm/neon2.o += $(CC_FLAGS_FPU)
CFLAGS_arm/neon4.o += $(CC_FLAGS_FPU)
CFLAGS_arm/neon8.o += $(CC_FLAGS_FPU)
CFLAGS_arm/recov_neon_inner.o += $(CC_FLAGS_FPU)
CFLAGS_REMOVE_arm/neon1.o += $(CC_FLAGS_NO_FPU)
CFLAGS_REMOVE_arm/neon2.o += $(CC_FLAGS_NO_FPU)
CFLAGS_REMOVE_arm/neon4.o += $(CC_FLAGS_NO_FPU)
CFLAGS_REMOVE_arm/neon8.o += $(CC_FLAGS_NO_FPU)
CFLAGS_REMOVE_arm/recov_neon_inner.o += $(CC_FLAGS_NO_FPU)

ifeq ($(CONFIG_ALTIVEC),y)
altivec_flags := -maltivec $(call cc-option,-mabi=altivec)
# Enable <altivec.h>
altivec_flags += -isystem $(shell $(CC) -print-file-name=include)

CFLAGS_powerpc/altivec1.o += $(altivec_flags)
CFLAGS_powerpc/altivec2.o += $(altivec_flags)
CFLAGS_powerpc/altivec4.o += $(altivec_flags)
CFLAGS_powerpc/altivec8.o += $(altivec_flags)
CFLAGS_powerpc/vpermxor1.o += $(altivec_flags)
CFLAGS_powerpc/vpermxor2.o += $(altivec_flags)
CFLAGS_powerpc/vpermxor4.o += $(altivec_flags)
CFLAGS_powerpc/vpermxor8.o += $(altivec_flags)

ifdef CONFIG_CC_IS_CLANG
# clang ppc port does not yet support -maltivec when -msoft-float is
# enabled. A future release of clang will resolve this
# https://llvm.org/pr31177
CFLAGS_REMOVE_powerpc/altivec1.o  += -msoft-float
CFLAGS_REMOVE_powerpc/altivec2.o  += -msoft-float
CFLAGS_REMOVE_powerpc/altivec4.o  += -msoft-float
CFLAGS_REMOVE_powerpc/altivec8.o  += -msoft-float
CFLAGS_REMOVE_powerpc/vpermxor1.o += -msoft-float
CFLAGS_REMOVE_powerpc/vpermxor2.o += -msoft-float
CFLAGS_REMOVE_powerpc/vpermxor4.o += -msoft-float
CFLAGS_REMOVE_powerpc/vpermxor8.o += -msoft-float
endif # CONFIG_CC_IS_CLANG
endif # CONFIG_ALTIVEC

quiet_cmd_mktable = TABLE   $@
      cmd_mktable = $(obj)/mktables > $@

targets += tables.c
$(obj)/tables.c: $(obj)/mktables FORCE
	$(call if_changed,mktable)

quiet_cmd_unroll = UNROLL  $@
      cmd_unroll = $(AWK) -v N=$* -f $(src)/unroll.awk < $< > $@

targets += int1.c int2.c int4.c int8.c
$(obj)/int%.c: $(src)/int.uc $(src)/unroll.awk FORCE
	$(call if_changed,unroll)

targets += arm/neon1.c arm/neon2.c arm/neon4.c arm/neon8.c
$(obj)/arm/neon%.c: $(src)/arm/neon.uc $(src)/unroll.awk FORCE
	$(call if_changed,unroll)

targets += powerpc/altivec1.c \
	   powerpc/altivec2.c \
	   powerpc/altivec4.c \
	   powerpc/altivec8.c
$(obj)/powerpc/altivec%.c: $(src)/powerpc/altivec.uc $(src)/unroll.awk FORCE
	$(call if_changed,unroll)

targets += powerpc/vpermxor1.c \
	   powerpc/vpermxor2.c \
	   powerpc/vpermxor4.c \
	   powerpc/vpermxor8.c
$(obj)/powerpc/vpermxor%.c: $(src)/powerpc/vpermxor.uc $(src)/unroll.awk FORCE
	$(call if_changed,unroll)

targets += s390/s390vx8.c
$(obj)/s390/s390vx%.c: $(src)/s390/s390vx.uc $(src)/unroll.awk FORCE
	$(call if_changed,unroll)
