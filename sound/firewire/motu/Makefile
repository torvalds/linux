# SPDX-License-Identifier: GPL-2.0
CFLAGS_amdtp-motu.o	:= -I$(src)

snd-firewire-motu-objs := motu.o amdtp-motu.o motu-transaction.o motu-stream.o \
			  motu-proc.o motu-pcm.o motu-midi.o motu-hwdep.o \
			  motu-protocol-v2.o motu-protocol-v3.o
obj-$(CONFIG_SND_FIREWIRE_MOTU) += snd-firewire-motu.o
