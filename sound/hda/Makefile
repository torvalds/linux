# SPDX-License-Identifier: GPL-2.0
obj-y += core/
obj-$(CONFIG_SND_HDA) += common/
obj-$(CONFIG_SND_HDA) += codecs/
# this must be the last entry after codec drivers;
# otherwise the codec drivers won't be hooked before the PCI probe
# when built in kernel
obj-$(CONFIG_SND_HDA) += controllers/
