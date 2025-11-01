# SPDX-License-Identifier: GPL-2.0
subdir-ccflags-y += -I$(src)/../../common

snd-hda-cirrus-scodec-y :=	cirrus_scodec.o
snd-hda-cirrus-scodec-test-y :=	cirrus_scodec_test.o
snd-hda-scodec-cs35l41-y :=	cs35l41_hda.o cs35l41_hda_property.o
snd-hda-scodec-cs35l41-i2c-y :=	cs35l41_hda_i2c.o
snd-hda-scodec-cs35l41-spi-y :=	cs35l41_hda_spi.o
snd-hda-scodec-cs35l56-y :=	cs35l56_hda.o
snd-hda-scodec-cs35l56-i2c-y :=	cs35l56_hda_i2c.o
snd-hda-scodec-cs35l56-spi-y :=	cs35l56_hda_spi.o
snd-hda-scodec-component-y :=	hda_component.o
snd-hda-scodec-tas2781-y :=	tas2781_hda.o
snd-hda-scodec-tas2781-i2c-y :=	tas2781_hda_i2c.o
snd-hda-scodec-tas2781-spi-y :=	tas2781_hda_spi.o

obj-$(CONFIG_SND_HDA_CIRRUS_SCODEC) += snd-hda-cirrus-scodec.o
obj-$(CONFIG_SND_HDA_CIRRUS_SCODEC_KUNIT_TEST) += snd-hda-cirrus-scodec-test.o
obj-$(CONFIG_SND_HDA_SCODEC_CS35L41) += snd-hda-scodec-cs35l41.o
obj-$(CONFIG_SND_HDA_SCODEC_CS35L41_I2C) += snd-hda-scodec-cs35l41-i2c.o
obj-$(CONFIG_SND_HDA_SCODEC_CS35L41_SPI) += snd-hda-scodec-cs35l41-spi.o
obj-$(CONFIG_SND_HDA_SCODEC_CS35L56) += snd-hda-scodec-cs35l56.o
obj-$(CONFIG_SND_HDA_SCODEC_CS35L56_I2C) += snd-hda-scodec-cs35l56-i2c.o
obj-$(CONFIG_SND_HDA_SCODEC_CS35L56_SPI) += snd-hda-scodec-cs35l56-spi.o
obj-$(CONFIG_SND_HDA_SCODEC_COMPONENT) += snd-hda-scodec-component.o
obj-$(CONFIG_SND_HDA_SCODEC_TAS2781) += snd-hda-scodec-tas2781.o
obj-$(CONFIG_SND_HDA_SCODEC_TAS2781_I2C) += snd-hda-scodec-tas2781-i2c.o
obj-$(CONFIG_SND_HDA_SCODEC_TAS2781_SPI) += snd-hda-scodec-tas2781-spi.o
