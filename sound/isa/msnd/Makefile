# SPDX-License-Identifier: GPL-2.0

snd-msnd-lib-y := msnd.o msnd_pinnacle_mixer.o
snd-msnd-pinnacle-y := msnd_pinnacle.o
snd-msnd-classic-y := msnd_classic.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_MSND_PINNACLE) += snd-msnd-pinnacle.o snd-msnd-lib.o
obj-$(CONFIG_SND_MSND_CLASSIC) += snd-msnd-classic.o snd-msnd-lib.o

