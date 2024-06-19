# SPDX-License-Identifier: GPL-2.0-only
snd-q6dsp-common-y := q6dsp-common.o q6dsp-lpass-ports.o q6dsp-lpass-clocks.o
snd-q6apm-y := q6apm.o audioreach.o topology.o

obj-$(CONFIG_SND_SOC_QDSP6_COMMON) += snd-q6dsp-common.o
obj-$(CONFIG_SND_SOC_QDSP6_CORE) += q6core.o
obj-$(CONFIG_SND_SOC_QDSP6_AFE) += q6afe.o
obj-$(CONFIG_SND_SOC_QDSP6_AFE_DAI) += q6afe-dai.o
obj-$(CONFIG_SND_SOC_QDSP6_AFE_CLOCKS) += q6afe-clocks.o
obj-$(CONFIG_SND_SOC_QDSP6_ADM) += q6adm.o
obj-$(CONFIG_SND_SOC_QDSP6_ROUTING) += q6routing.o
obj-$(CONFIG_SND_SOC_QDSP6_ASM) += q6asm.o
obj-$(CONFIG_SND_SOC_QDSP6_ASM_DAI) += q6asm-dai.o

obj-$(CONFIG_SND_SOC_QDSP6_APM) += snd-q6apm.o
obj-$(CONFIG_SND_SOC_QDSP6_APM_DAI) += q6apm-dai.o
obj-$(CONFIG_SND_SOC_QDSP6_APM_LPASS_DAI) += q6apm-lpass-dais.o
obj-$(CONFIG_SND_SOC_QDSP6_PRM) += q6prm.o
obj-$(CONFIG_SND_SOC_QDSP6_PRM_LPASS_CLOCKS) += q6prm-clocks.o
