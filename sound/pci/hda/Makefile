snd-hda-intel-objs := hda_intel.o
# since snd-hda-intel is the only driver using hda-codec,
# merge it into a single module although it was originally
# designed to be individual modules
snd-hda-intel-objs += hda_codec.o \
	hda_generic.o \
	patch_realtek.o \
	patch_cmedia.o \
	patch_analog.o \
	patch_sigmatel.o \
	patch_si3054.o \
	patch_atihdmi.o \
	patch_conexant.o \
	patch_via.o
ifdef CONFIG_PROC_FS
snd-hda-intel-objs += hda_proc.o
endif

obj-$(CONFIG_SND_HDA_INTEL) += snd-hda-intel.o
