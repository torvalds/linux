# SPDX-License-Identifier: GPL-2.0
snd-hda-intel-y := intel.o
snd-hda-tegra-y := tegra.o
snd-hda-acpi-y := acpi.o

subdir-ccflags-y += -I$(src)/../common

# for trace-points
CFLAGS_intel.o := -I$(src)

obj-$(CONFIG_SND_HDA_INTEL) += snd-hda-intel.o
obj-$(CONFIG_SND_HDA_TEGRA) += snd-hda-tegra.o
obj-$(CONFIG_SND_HDA_ACPI) += snd-hda-acpi.o
