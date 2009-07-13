obj-$(CONFIG_RAID6_PQ)	+= raid6_pq.o

raid6_pq-y	+= raid6algos.o raid6recov.o raid6tables.o \
		   raid6int1.o raid6int2.o raid6int4.o \
		   raid6int8.o raid6int16.o raid6int32.o \
		   raid6altivec1.o raid6altivec2.o raid6altivec4.o \
		   raid6altivec8.o \
		   raid6mmx.o raid6sse1.o raid6sse2.o
hostprogs-y	+= mktables

quiet_cmd_unroll = UNROLL  $@
      cmd_unroll = $(PERL) $(srctree)/$(src)/unroll.pl $(UNROLL) \
                   < $< > $@ || ( rm -f $@ && exit 1 )

ifeq ($(CONFIG_ALTIVEC),y)
altivec_flags := -maltivec -mabi=altivec
endif

targets += raid6int1.c
$(obj)/raid6int1.c:   UNROLL := 1
$(obj)/raid6int1.c:   $(src)/raid6int.uc $(src)/unroll.pl FORCE
	$(call if_changed,unroll)

targets += raid6int2.c
$(obj)/raid6int2.c:   UNROLL := 2
$(obj)/raid6int2.c:   $(src)/raid6int.uc $(src)/unroll.pl FORCE
	$(call if_changed,unroll)

targets += raid6int4.c
$(obj)/raid6int4.c:   UNROLL := 4
$(obj)/raid6int4.c:   $(src)/raid6int.uc $(src)/unroll.pl FORCE
	$(call if_changed,unroll)

targets += raid6int8.c
$(obj)/raid6int8.c:   UNROLL := 8
$(obj)/raid6int8.c:   $(src)/raid6int.uc $(src)/unroll.pl FORCE
	$(call if_changed,unroll)

targets += raid6int16.c
$(obj)/raid6int16.c:  UNROLL := 16
$(obj)/raid6int16.c:  $(src)/raid6int.uc $(src)/unroll.pl FORCE
	$(call if_changed,unroll)

targets += raid6int32.c
$(obj)/raid6int32.c:  UNROLL := 32
$(obj)/raid6int32.c:  $(src)/raid6int.uc $(src)/unroll.pl FORCE
	$(call if_changed,unroll)

CFLAGS_raid6altivec1.o += $(altivec_flags)
targets += raid6altivec1.c
$(obj)/raid6altivec1.c:   UNROLL := 1
$(obj)/raid6altivec1.c:   $(src)/raid6altivec.uc $(src)/unroll.pl FORCE
	$(call if_changed,unroll)

CFLAGS_raid6altivec2.o += $(altivec_flags)
targets += raid6altivec2.c
$(obj)/raid6altivec2.c:   UNROLL := 2
$(obj)/raid6altivec2.c:   $(src)/raid6altivec.uc $(src)/unroll.pl FORCE
	$(call if_changed,unroll)

CFLAGS_raid6altivec4.o += $(altivec_flags)
targets += raid6altivec4.c
$(obj)/raid6altivec4.c:   UNROLL := 4
$(obj)/raid6altivec4.c:   $(src)/raid6altivec.uc $(src)/unroll.pl FORCE
	$(call if_changed,unroll)

CFLAGS_raid6altivec8.o += $(altivec_flags)
targets += raid6altivec8.c
$(obj)/raid6altivec8.c:   UNROLL := 8
$(obj)/raid6altivec8.c:   $(src)/raid6altivec.uc $(src)/unroll.pl FORCE
	$(call if_changed,unroll)

quiet_cmd_mktable = TABLE   $@
      cmd_mktable = $(obj)/mktables > $@ || ( rm -f $@ && exit 1 )

targets += raid6tables.c
$(obj)/raid6tables.c: $(obj)/mktables FORCE
	$(call if_changed,mktable)
