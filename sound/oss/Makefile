# Makefile for the Linux sound card driver
#
# 18 Apr 1998, Michael Elizabeth Chastain, <mailto:mec@shout.net>
# Rewritten to use lists instead of if-statements.

# Each configuration option enables a list of files.

obj-$(CONFIG_SOUND_OSS)		+= sound.o
obj-$(CONFIG_SOUND_CS4232)	+= cs4232.o ad1848.o 

# Please leave it as is, cause the link order is significant !

obj-$(CONFIG_SOUND_SH_DAC_AUDIO)	+= sh_dac_audio.o
obj-$(CONFIG_SOUND_HAL2)	+= hal2.o
obj-$(CONFIG_SOUND_AEDSP16)	+= aedsp16.o
obj-$(CONFIG_SOUND_PSS)		+= pss.o ad1848.o mpu401.o
obj-$(CONFIG_SOUND_TRIX)	+= trix.o ad1848.o sb_lib.o uart401.o
obj-$(CONFIG_SOUND_SSCAPE)	+= sscape.o ad1848.o mpu401.o
obj-$(CONFIG_SOUND_CS4232)	+= cs4232.o uart401.o
obj-$(CONFIG_SOUND_MSS)		+= ad1848.o
obj-$(CONFIG_SOUND_PAS)		+= pas2.o sb.o sb_lib.o uart401.o
obj-$(CONFIG_SOUND_SB)		+= sb.o sb_lib.o uart401.o
obj-$(CONFIG_SOUND_KAHLUA)	+= kahlua.o
obj-$(CONFIG_SOUND_MPU401)	+= mpu401.o
obj-$(CONFIG_SOUND_UART6850)	+= uart6850.o
obj-$(CONFIG_SOUND_YM3812)	+= opl3.o
obj-$(CONFIG_SOUND_VMIDI)	+= v_midi.o
obj-$(CONFIG_SOUND_VIDC)	+= vidc_mod.o
obj-$(CONFIG_SOUND_WAVEARTIST)	+= waveartist.o

obj-$(CONFIG_SOUND_VIA82CXXX)	+= via82cxxx_audio.o ac97_codec.o
ifeq ($(CONFIG_MIDI_VIA82CXXX),y)
  obj-$(CONFIG_SOUND_VIA82CXXX) += sound.o uart401.o
endif
obj-$(CONFIG_SOUND_MSNDCLAS)	+= msnd.o msnd_classic.o
obj-$(CONFIG_SOUND_MSNDPIN)	+= msnd.o msnd_pinnacle.o
obj-$(CONFIG_SOUND_VWSND)	+= vwsnd.o
obj-$(CONFIG_SOUND_ICH)		+= i810_audio.o ac97_codec.o
obj-$(CONFIG_SOUND_ES1371)	+= es1371.o ac97_codec.o
obj-$(CONFIG_SOUND_VRC5477)	+= nec_vrc5477.o ac97_codec.o
obj-$(CONFIG_SOUND_AU1550_AC97)	+= au1550_ac97.o ac97_codec.o
obj-$(CONFIG_SOUND_TRIDENT)	+= trident.o ac97_codec.o
obj-$(CONFIG_SOUND_BCM_CS4297A)	+= swarm_cs4297a.o
obj-$(CONFIG_SOUND_BT878)	+= btaudio.o

obj-$(CONFIG_SOUND_WM97XX)	+= ac97_plugin_wm97xx.o

obj-$(CONFIG_DMASOUND)		+= dmasound/

# Declare multi-part drivers.

sound-objs	:= 							\
    dev_table.o soundcard.o 		\
    audio.o dmabuf.o					\
    midi_synth.o midibuf.o					\
    sequencer.o sound_timer.o sys_timer.o

pas2-objs	:= pas2_card.o pas2_midi.o pas2_mixer.o pas2_pcm.o
sb-objs		:= sb_card.o
sb_lib-objs	:= sb_common.o sb_audio.o sb_midi.o sb_mixer.o sb_ess.o
vidc_mod-objs	:= vidc.o vidc_fill.o

hostprogs-y	:= bin2hex hex2hex

# Files generated that shall be removed upon make clean
clean-files := msndperm.c msndinit.c pndsperm.c pndspini.c \
               pss_boot.h trix_boot.h

# Firmware files that need translation
#
# The translated files are protected by a file that keeps track
# of what name was used to build them.  If the name changes, they
# will be forced to be remade.
#

# Turtle Beach MultiSound

ifeq ($(CONFIG_MSNDCLAS_HAVE_BOOT),y)
    $(obj)/msnd_classic.o: $(obj)/msndperm.c $(obj)/msndinit.c

    $(obj)/msndperm.c: $(patsubst "%", %, $(CONFIG_MSNDCLAS_PERM_FILE)) $(obj)/bin2hex
	$(obj)/bin2hex msndperm < $< > $@

    $(obj)/msndinit.c: $(patsubst "%", %, $(CONFIG_MSNDCLAS_INIT_FILE)) $(obj)/bin2hex
	$(obj)/bin2hex msndinit < $< > $@
endif

ifeq ($(CONFIG_MSNDPIN_HAVE_BOOT),y)
    $(obj)/msnd_pinnacle.o: $(obj)/pndsperm.c $(obj)/pndspini.c

    $(obj)/pndsperm.c: $(patsubst "%", %, $(CONFIG_MSNDPIN_PERM_FILE)) $(obj)/bin2hex
	$(obj)/bin2hex pndsperm < $< > $@

    $(obj)/pndspini.c: $(patsubst "%", %, $(CONFIG_MSNDPIN_INIT_FILE)) $(obj)/bin2hex
	$(obj)/bin2hex pndspini < $< > $@
endif

# PSS (ECHO-ADI2111)

$(obj)/pss.o: $(obj)/pss_boot.h

ifeq ($(CONFIG_PSS_HAVE_BOOT),y)
    $(obj)/pss_boot.h: $(patsubst "%", %, $(CONFIG_PSS_BOOT_FILE)) $(obj)/bin2hex
	$(obj)/bin2hex pss_synth < $< > $@
else
    $(obj)/pss_boot.h:
	(							\
	    echo 'static unsigned char * pss_synth = NULL;';	\
	    echo 'static int pss_synthLen = 0;';		\
	) > $@
endif

# MediaTrix AudioTrix Pro

$(obj)/trix.o: $(obj)/trix_boot.h

ifeq ($(CONFIG_TRIX_HAVE_BOOT),y)
    $(obj)/trix_boot.h: $(patsubst "%", %, $(CONFIG_TRIX_BOOT_FILE)) $(obj)/hex2hex
	$(obj)/hex2hex -i trix_boot < $< > $@
else
    $(obj)/trix_boot.h:
	(							\
	    echo 'static unsigned char * trix_boot = NULL;';	\
	    echo 'static int trix_boot_len = 0;';		\
	) > $@
endif
