# SPDX-License-Identifier: GPL-2.0
CFLAGS_amdtp-motu.o	:= -I$(src)

snd-firewire-motu-y := motu.o amdtp-motu.o motu-transaction.o motu-stream.o \
			  motu-proc.o motu-pcm.o motu-midi.o motu-hwdep.o \
			  motu-protocol-v2.o motu-protocol-v3.o \
			  motu-protocol-v1.o motu-register-dsp-message-parser.o \
			  motu-command-dsp-message-parser.o
obj-$(CONFIG_SND_FIREWIRE_MOTU) += snd-firewire-motu.o
