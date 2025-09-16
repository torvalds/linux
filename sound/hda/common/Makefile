# SPDX-License-Identifier: GPL-2.0
snd-hda-codec-y := bind.o codec.o jack.o auto_parser.o sysfs.o
snd-hda-codec-y += controller.o
snd-hda-codec-$(CONFIG_SND_PROC_FS) += proc.o

snd-hda-codec-$(CONFIG_SND_HDA_HWDEP) += hwdep.o
snd-hda-codec-$(CONFIG_SND_HDA_INPUT_BEEP) += beep.o

# for trace-points
CFLAGS_controller.o := -I$(src)

# common driver
obj-$(CONFIG_SND_HDA) := snd-hda-codec.o
