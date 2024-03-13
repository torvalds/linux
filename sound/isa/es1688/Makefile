# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@perex.cz>
#

snd-es1688-lib-objs := es1688_lib.o
snd-es1688-objs := es1688.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_ES1688) += snd-es1688.o snd-es1688-lib.o
obj-$(CONFIG_SND_GUSEXTREME) += snd-es1688-lib.o
