# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for ALSA
# Copyright (c) 2001 by Jaroslav Kysela <perex@perex.cz>
#

snd-mpu401-objs := mpu401.o
snd-mpu401-uart-objs := mpu401_uart.o

obj-$(CONFIG_SND_MPU401_UART) += snd-mpu401-uart.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_MPU401) += snd-mpu401.o
