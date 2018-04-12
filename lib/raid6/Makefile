# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_RAID6_PQ)	+= raid6_pq.o

raid6_pq-y	+= algos.o recov.o tables.o int1.o int2.o int4.o \
		   int8.o int16.o int32.o

raid6_pq-$(CONFIG_X86) += recov_ssse3.o recov_avx2.o mmx.o sse1.o sse2.o avx2.o avx512.o recov_avx512.o
raid6_pq-$(CONFIG_ALTIVEC) += altivec1.o altivec2.o altivec4.o altivec8.o
raid6_pq-$(CONFIG_KERNEL_MODE_NEON) += neon.o neon1.o neon2.o neon4.o neon8.o recov_neon.o recov_neon_inner.o
raid6_pq-$(CONFIG_S390) += s390vx8.o recov_s390xc.o

hostprogs-y	+= mktables

quiet_cmd_unroll = UNROLL  $@
      cmd_unroll = $(AWK) -f$(srctree)/$(src)/unroll.awk -vN=$(UNROLL) \
                   < $< > $@ || ( rm -f $@ && exit 1 )

ifeq ($(CONFIG_ALTIVEC),y)
altivec_flags := -maltivec $(call cc-option,-mabi=altivec)
endif

# The GCC option -ffreestanding is required in order to compile code containing
# ARM/NEON intrinsics in a non C99-compliant environment (such as the kernel)
ifeq ($(CONFIG_KERNEL_MODE_NEON),y)
NEON_FLAGS := -ffreestanding
ifeq ($(ARCH),arm)
NEON_FLAGS += -mfloat-abi=softfp -mfpu=neon
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

targets += int1.c
$(obj)/int1.c:   UNROLL := 1
$(obj)/int1.c:   $(src)/int.uc $(src)/unroll.awk FORCE
	$(call if_changed,unroll)

targets += int2.c
$(obj)/int2.c:   UNROLL := 2
$(obj)/int2.c:   $(src)/int.uc $(src)/unroll.awk FORCE
	$(call if_changed,unroll)

targets += int4.c
$(obj)/int4.c:   UNROLL := 4
$(obj)/int4.c:   $(src)/int.uc $(src)/unroll.awk FORCE
	$(call if_changed,unroll)

targets += int8.c
$(obj)/int8.c:   UNROLL := 8
$(obj)/int8.c:   $(src)/int.uc $(src)/unroll.awk FORCE
	$(call if_changed,unroll)

targets += int16.c
$(obj)/int16.c:  UNROLL := 16
$(obj)/int16.c:  $(src)/int.uc $(src)/unroll.awk FORCE
	$(call if_changed,unroll)

targets += int32.c
$(obj)/int32.c:  UNROLL := 32
$(obj)/int32.c:  $(src)/int.uc $(src)/unroll.awk FORCE
	$(call if_changed,unroll)

CFLAGS_altivec1.o += $(altivec_flags)
targets += altivec1.c
$(obj)/altivec1.c:   UNROLL := 1
$(obj)/altivec1.c:   $(src)/altivec.uc $(src)/unroll.awk FORCE
	$(call if_changed,unroll)

CFLAGS_altivec2.o += $(altivec_flags)
targets += altivec2.c
$(obj)/altivec2.c:   UNROLL := 2
$(obj)/altivec2.c:   $(src)/altivec.uc $(src)/unroll.awk FORCE
	$(call if_changed,unroll)

CFLAGS_altivec4.o += $(altivec_flags)
targets += altivec4.c
$(obj)/altivec4.c:   UNROLL := 4
$(obj)/altivec4.c:   $(src)/altivec.uc $(src)/unroll.awk FORCE
	$(call if_changed,unroll)

CFLAGS_altivec8.o += $(altivec_flags)
targets += altivec8.c
$(obj)/altivec8.c:   UNROLL := 8
$(obj)/altivec8.c:   $(src)/altivec.uc $(src)/unroll.awk FORCE
	$(call if_changed,unroll)

CFLAGS_neon1.o += $(NEON_FLAGS)
targets += neon1.c
$(obj)/neon1.c:   UNROLL := 1
$(obj)/neon1.c:   $(src)/neon.uc $(src)/unroll.awk FORCE
	$(call if_changed,unroll)

CFLAGS_neon2.o += $(NEON_FLAGS)
targets += neon2.c
$(obj)/neon2.c:   UNROLL := 2
$(obj)/neon2.c:   $(src)/neon.uc $(src)/unroll.awk FORCE
	$(call if_changed,unroll)

CFLAGS_neon4.o += $(NEON_FLAGS)
targets += neon4.c
$(obj)/neon4.c:   UNROLL := 4
$(obj)/neon4.c:   $(src)/neon.uc $(src)/unroll.awk FORCE
	$(call if_changed,unroll)

CFLAGS_neon8.o += $(NEON_FLAGS)
targets += neon8.c
$(obj)/neon8.c:   UNROLL := 8
$(obj)/neon8.c:   $(src)/neon.uc $(src)/unroll.awk FORCE
	$(call if_changed,unroll)

targets += s390vx8.c
$(obj)/s390vx8.c:   UNROLL := 8
$(obj)/s390vx8.c:   $(src)/s390vx.uc $(src)/unroll.awk FORCE
	$(call if_changed,unroll)

quiet_cmd_mktable = TABLE   $@
      cmd_mktable = $(obj)/mktables > $@ || ( rm -f $@ && exit 1 )

targets += tables.c
$(obj)/tables.c: $(obj)/mktables FORCE
	$(call if_changed,mktable)
