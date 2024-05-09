# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_RAID6_PQ)	+= raid6_pq.o

raid6_pq-y	+= algos.o recov.o tables.o int1.o int2.o int4.o \
		   int8.o

raid6_pq-$(CONFIG_X86) += recov_ssse3.o recov_avx2.o mmx.o sse1.o sse2.o avx2.o avx512.o recov_avx512.o
raid6_pq-$(CONFIG_ALTIVEC) += altivec1.o altivec2.o altivec4.o altivec8.o \
                              vpermxor1.o vpermxor2.o vpermxor4.o vpermxor8.o
raid6_pq-$(CONFIG_KERNEL_MODE_NEON) += neon.o neon1.o neon2.o neon4.o neon8.o recov_neon.o recov_neon_inner.o
raid6_pq-$(CONFIG_S390) += s390vx8.o recov_s390xc.o
raid6_pq-$(CONFIG_LOONGARCH) += loongarch_simd.o recov_loongarch_simd.o

hostprogs	+= mktables

ifeq ($(CONFIG_ALTIVEC),y)
altivec_flags := -maltivec $(call cc-option,-mabi=altivec)
# Enable <altivec.h>
altivec_flags += -isystem $(shell $(CC) -print-file-name=include)

ifdef CONFIG_CC_IS_CLANG
# clang ppc port does not yet support -maltivec when -msoft-float is
# enabled. A future release of clang will resolve this
# https://bugs.llvm.org/show_bug.cgi?id=31177
CFLAGS_REMOVE_altivec1.o  += -msoft-float
CFLAGS_REMOVE_altivec2.o  += -msoft-float
CFLAGS_REMOVE_altivec4.o  += -msoft-float
CFLAGS_REMOVE_altivec8.o  += -msoft-float
CFLAGS_REMOVE_vpermxor1.o += -msoft-float
CFLAGS_REMOVE_vpermxor2.o += -msoft-float
CFLAGS_REMOVE_vpermxor4.o += -msoft-float
CFLAGS_REMOVE_vpermxor8.o += -msoft-float
endif
endif

# The GCC option -ffreestanding is required in order to compile code containing
# ARM/NEON intrinsics in a non C99-compliant environment (such as the kernel)
ifeq ($(CONFIG_KERNEL_MODE_NEON),y)
NEON_FLAGS := -ffreestanding
# Enable <arm_neon.h>
NEON_FLAGS += -isystem $(shell $(CC) -print-file-name=include)
ifeq ($(ARCH),arm)
NEON_FLAGS += -march=armv7-a -mfloat-abi=softfp -mfpu=neon
endif
CFLAGS_recov_neon_inner.o += $(NEON_FLAGS)
ifeq ($(ARCH),arm64)
CFLAGS_REMOVE_recov_neon_inner.o += -mgeneral-regs-only
CFLAGS_REMOVE_neon1.o += -mgeneral-regs-only
CFLAGS_REMOVE_neon2.o += -mgeneral-regs-only
CFLAGS_REMOVE_neon4.o += -mgeneral-regs-only
CFLAGS_REMOVE_neon8.o += -mgeneral-regs-only
endif
endif

quiet_cmd_unroll = UNROLL  $@
      cmd_unroll = $(AWK) -v N=$* -f $(srctree)/$(src)/unroll.awk < $< > $@

targets += int1.c int2.c int4.c int8.c
$(obj)/int%.c: $(src)/int.uc $(src)/unroll.awk FORCE
	$(call if_changed,unroll)

CFLAGS_altivec1.o += $(altivec_flags)
CFLAGS_altivec2.o += $(altivec_flags)
CFLAGS_altivec4.o += $(altivec_flags)
CFLAGS_altivec8.o += $(altivec_flags)
targets += altivec1.c altivec2.c altivec4.c altivec8.c
$(obj)/altivec%.c: $(src)/altivec.uc $(src)/unroll.awk FORCE
	$(call if_changed,unroll)

CFLAGS_vpermxor1.o += $(altivec_flags)
CFLAGS_vpermxor2.o += $(altivec_flags)
CFLAGS_vpermxor4.o += $(altivec_flags)
CFLAGS_vpermxor8.o += $(altivec_flags)
targets += vpermxor1.c vpermxor2.c vpermxor4.c vpermxor8.c
$(obj)/vpermxor%.c: $(src)/vpermxor.uc $(src)/unroll.awk FORCE
	$(call if_changed,unroll)

CFLAGS_neon1.o += $(NEON_FLAGS)
CFLAGS_neon2.o += $(NEON_FLAGS)
CFLAGS_neon4.o += $(NEON_FLAGS)
CFLAGS_neon8.o += $(NEON_FLAGS)
targets += neon1.c neon2.c neon4.c neon8.c
$(obj)/neon%.c: $(src)/neon.uc $(src)/unroll.awk FORCE
	$(call if_changed,unroll)

targets += s390vx8.c
$(obj)/s390vx%.c: $(src)/s390vx.uc $(src)/unroll.awk FORCE
	$(call if_changed,unroll)

quiet_cmd_mktable = TABLE   $@
      cmd_mktable = $(obj)/mktables > $@

targets += tables.c
$(obj)/tables.c: $(obj)/mktables FORCE
	$(call if_changed,mktable)
