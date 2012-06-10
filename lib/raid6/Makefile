obj-$(CONFIG_RAID6_PQ)	+= raid6_pq.o

raid6_pq-y	+= algos.o recov.o recov_ssse3.o tables.o int1.o int2.o int4.o \
		   int8.o int16.o int32.o altivec1.o altivec2.o altivec4.o \
		   altivec8.o mmx.o sse1.o sse2.o
hostprogs-y	+= mktables

quiet_cmd_unroll = UNROLL  $@
      cmd_unroll = $(AWK) -f$(srctree)/$(src)/unroll.awk -vN=$(UNROLL) \
                   < $< > $@ || ( rm -f $@ && exit 1 )

ifeq ($(CONFIG_ALTIVEC),y)
altivec_flags := -maltivec -mabi=altivec
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

quiet_cmd_mktable = TABLE   $@
      cmd_mktable = $(obj)/mktables > $@ || ( rm -f $@ && exit 1 )

targets += tables.c
$(obj)/tables.c: $(obj)/mktables FORCE
	$(call if_changed,mktable)
