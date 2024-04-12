# SPDX-License-Identifier: GPL-2.0
snd-hda-intel-objs := hda_intel.o
snd-hda-tegra-objs := hda_tegra.o

snd-hda-codec-y := hda_bind.o hda_codec.o hda_jack.o hda_auto_parser.o hda_sysfs.o
snd-hda-codec-y += hda_controller.o
snd-hda-codec-$(CONFIG_SND_PROC_FS) += hda_proc.o

snd-hda-codec-$(CONFIG_SND_HDA_HWDEP) += hda_hwdep.o
snd-hda-codec-$(CONFIG_SND_HDA_INPUT_BEEP) += hda_beep.o

# for trace-points
CFLAGS_hda_controller.o := -I$(src)
CFLAGS_hda_intel.o := -I$(src)

snd-hda-codec-generic-objs :=	hda_generic.o
snd-hda-codec-realtek-objs :=	patch_realtek.o
snd-hda-codec-cmedia-objs :=	patch_cmedia.o
snd-hda-codec-analog-objs :=	patch_analog.o
snd-hda-codec-idt-objs :=	patch_sigmatel.o
snd-hda-codec-si3054-objs :=	patch_si3054.o
snd-hda-codec-cirrus-objs :=	patch_cirrus.o
snd-hda-codec-cs8409-objs :=	patch_cs8409.o patch_cs8409-tables.o
snd-hda-codec-ca0110-objs :=	patch_ca0110.o
snd-hda-codec-ca0132-objs :=	patch_ca0132.o
snd-hda-codec-conexant-objs :=	patch_conexant.o
snd-hda-codec-via-objs :=	patch_via.o
snd-hda-codec-hdmi-objs :=	patch_hdmi.o hda_eld.o

# side codecs
snd-hda-cirrus-scodec-objs :=		cirrus_scodec.o
snd-hda-cirrus-scodec-test-objs :=	cirrus_scodec_test.o
snd-hda-scodec-cs35l41-objs :=		cs35l41_hda.o cs35l41_hda_property.o
snd-hda-scodec-cs35l41-i2c-objs :=	cs35l41_hda_i2c.o
snd-hda-scodec-cs35l41-spi-objs :=	cs35l41_hda_spi.o
snd-hda-scodec-cs35l56-objs :=		cs35l56_hda.o
snd-hda-scodec-cs35l56-i2c-objs :=	cs35l56_hda_i2c.o
snd-hda-scodec-cs35l56-spi-objs :=	cs35l56_hda_spi.o
snd-hda-cs-dsp-ctls-objs :=		hda_cs_dsp_ctl.o
snd-hda-scodec-component-objs :=	hda_component.o
snd-hda-scodec-tas2781-i2c-objs :=	tas2781_hda_i2c.o

# common driver
obj-$(CONFIG_SND_HDA) := snd-hda-codec.o

# codec drivers
obj-$(CONFIG_SND_HDA_GENERIC) += snd-hda-codec-generic.o
obj-$(CONFIG_SND_HDA_CODEC_REALTEK) += snd-hda-codec-realtek.o
obj-$(CONFIG_SND_HDA_CODEC_CMEDIA) += snd-hda-codec-cmedia.o
obj-$(CONFIG_SND_HDA_CODEC_ANALOG) += snd-hda-codec-analog.o
obj-$(CONFIG_SND_HDA_CODEC_SIGMATEL) += snd-hda-codec-idt.o
obj-$(CONFIG_SND_HDA_CODEC_SI3054) += snd-hda-codec-si3054.o
obj-$(CONFIG_SND_HDA_CODEC_CIRRUS) += snd-hda-codec-cirrus.o
obj-$(CONFIG_SND_HDA_CODEC_CS8409) += snd-hda-codec-cs8409.o
obj-$(CONFIG_SND_HDA_CODEC_CA0110) += snd-hda-codec-ca0110.o
obj-$(CONFIG_SND_HDA_CODEC_CA0132) += snd-hda-codec-ca0132.o
obj-$(CONFIG_SND_HDA_CODEC_CONEXANT) += snd-hda-codec-conexant.o
obj-$(CONFIG_SND_HDA_CODEC_VIA) += snd-hda-codec-via.o
obj-$(CONFIG_SND_HDA_CODEC_HDMI) += snd-hda-codec-hdmi.o

# side codecs
obj-$(CONFIG_SND_HDA_CIRRUS_SCODEC) += snd-hda-cirrus-scodec.o
obj-$(CONFIG_SND_HDA_CIRRUS_SCODEC_KUNIT_TEST) += snd-hda-cirrus-scodec-test.o
obj-$(CONFIG_SND_HDA_SCODEC_CS35L41) += snd-hda-scodec-cs35l41.o
obj-$(CONFIG_SND_HDA_SCODEC_CS35L41_I2C) += snd-hda-scodec-cs35l41-i2c.o
obj-$(CONFIG_SND_HDA_SCODEC_CS35L41_SPI) += snd-hda-scodec-cs35l41-spi.o
obj-$(CONFIG_SND_HDA_SCODEC_CS35L56) += snd-hda-scodec-cs35l56.o
obj-$(CONFIG_SND_HDA_SCODEC_CS35L56_I2C) += snd-hda-scodec-cs35l56-i2c.o
obj-$(CONFIG_SND_HDA_SCODEC_CS35L56_SPI) += snd-hda-scodec-cs35l56-spi.o
obj-$(CONFIG_SND_HDA_CS_DSP_CONTROLS) += snd-hda-cs-dsp-ctls.o
obj-$(CONFIG_SND_HDA_SCODEC_COMPONENT) += snd-hda-scodec-component.o
obj-$(CONFIG_SND_HDA_SCODEC_TAS2781_I2C) += snd-hda-scodec-tas2781-i2c.o

# this must be the last entry after codec drivers;
# otherwise the codec patches won't be hooked before the PCI probe
# when built in kernel
obj-$(CONFIG_SND_HDA_INTEL) += snd-hda-intel.o
obj-$(CONFIG_SND_HDA_TEGRA) += snd-hda-tegra.o
