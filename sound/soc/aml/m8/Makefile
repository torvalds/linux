# AML Platform Support
snd-soc-aml-pcm-objs := aml_pcm.o
snd-soc-aml-i2s-objs := aml_i2s.o
#snd-soc-aml-platform-pcm2bt-objs := aml_platform_pcm2bt.o
snd-soc-aml-i2s-dai-objs := aml_i2s_dai.o
snd-soc-aml-pcm-dai-objs := aml_pcm_dai.o
snd-soc-aml-spdif-dai-objs  := aml_spdif_dai.o
snd-soc-aml-hw-objs  := aml_audio_hw.o
snd-soc-aml-hw-pcm2bt-objs  := aml_audio_hw_pcm2bt.o
#snd-soc-aml-platform-objs := aml_platform.o
obj-$(CONFIG_SND_AML_M8_SOC) += snd-soc-aml-pcm.o
obj-$(CONFIG_SND_AML_M8_SOC) += snd-soc-aml-i2s.o
obj-$(CONFIG_SND_AML_M8_SOC) += snd-soc-aml-i2s-dai.o
obj-$(CONFIG_SND_AML_M8_SOC) += snd-soc-aml-pcm-dai.o
obj-$(CONFIG_SND_AML_M8_SOC) += snd-soc-aml-hw.o
obj-$(CONFIG_SND_AML_M8_SOC) += aml_notify.o
#obj-$(CONFIG_SND_AML_M8_SOC) += snd-soc-aml-platform.o
#obj-$(CONFIG_SND_AML_M8_SOC) += snd-soc-aml-platform-pcm2bt.o
obj-$(CONFIG_SND_AML_M8_SOC) += snd-soc-aml-hw-pcm2bt.o
obj-$(CONFIG_SND_AML_M8_SOC) += snd-soc-aml-spdif-dai.o
#obj-$(CONFIG_SND_AML_M8_SOC) += aml_audio_codec_probe.o

#AML spdif codec support
snd-soc-aml-spdif-codec-objs := aml_spdif_codec.o
#obj-$(CONFIG_SND_SOC_AML_SPDIF_CODEC) += snd-soc-aml-spdif-codec.o
obj-$(CONFIG_SND_AML_M8_SOC) += snd-soc-aml-spdif-codec.o
#ODROID Machine support
snd-soc-odroid-hdmi-objs := odroid_hdmi.o
snd-soc-odroid-dac-objs := odroid_dac.o
obj-$(CONFIG_SND_AML_M8_SOC) += snd-soc-odroid-hdmi.o
obj-$(CONFIG_SND_ODROID_HIFI1) += snd-soc-odroid-dac.o

#AML M series with dummy codec support
snd-soc-aml-m-dummy-objs := aml_m_dummy.o
obj-$(CONFIG_SND_AML_M_DUMMY_CODEC) += snd-soc-aml-m-dummy.o