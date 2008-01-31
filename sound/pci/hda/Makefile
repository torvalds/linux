snd-hda-intel-y := hda_intel.o
# since snd-hda-intel is the only driver using hda-codec,
# merge it into a single module although it was originally
# designed to be individual modules
snd-hda-intel-y += hda_codec.o vmaster.o
snd-hda-intel-$(CONFIG_PROC_FS) += hda_proc.o
snd-hda-intel-$(CONFIG_SND_HDA_HWDEP) += hda_hwdep.o
snd-hda-intel-$(CONFIG_SND_HDA_GENERIC) += hda_generic.o
snd-hda-intel-$(CONFIG_SND_HDA_CODEC_REALTEK) += patch_realtek.o
snd-hda-intel-$(CONFIG_SND_HDA_CODEC_CMEDIA) += patch_cmedia.o
snd-hda-intel-$(CONFIG_SND_HDA_CODEC_ANALOG) += patch_analog.o
snd-hda-intel-$(CONFIG_SND_HDA_CODEC_SIGMATEL) += patch_sigmatel.o
snd-hda-intel-$(CONFIG_SND_HDA_CODEC_SI3054) += patch_si3054.o
snd-hda-intel-$(CONFIG_SND_HDA_CODEC_ATIHDMI) += patch_atihdmi.o
snd-hda-intel-$(CONFIG_SND_HDA_CODEC_CONEXANT) += patch_conexant.o
snd-hda-intel-$(CONFIG_SND_HDA_CODEC_VIA) += patch_via.o

obj-$(CONFIG_SND_HDA_INTEL) += snd-hda-intel.o
