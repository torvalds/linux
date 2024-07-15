# SPDX-License-Identifier: GPL-2.0
#
# Makefile for ALSA
# Copyright (c) 1999 by Jaroslav Kysela <perex@perex.cz>
#

snd-seq-y := seq.o seq_lock.o seq_clientmgr.o seq_memory.o seq_queue.o \
                seq_fifo.o seq_prioq.o seq_timer.o \
                seq_system.o seq_ports.o
snd-seq-$(CONFIG_SND_PROC_FS) += seq_info.o
snd-seq-$(CONFIG_SND_SEQ_UMP) += seq_ump_convert.o
snd-seq-midi-y := seq_midi.o
snd-seq-midi-emul-y := seq_midi_emul.o
snd-seq-midi-event-y := seq_midi_event.o
snd-seq-dummy-y := seq_dummy.o
snd-seq-virmidi-y := seq_virmidi.o
snd-seq-ump-client-y := seq_ump_client.o

obj-$(CONFIG_SND_SEQUENCER) += snd-seq.o
obj-$(CONFIG_SND_SEQUENCER_OSS) += oss/

obj-$(CONFIG_SND_SEQ_DUMMY) += snd-seq-dummy.o
obj-$(CONFIG_SND_SEQ_MIDI) += snd-seq-midi.o
obj-$(CONFIG_SND_SEQ_UMP_CLIENT) += snd-seq-ump-client.o
obj-$(CONFIG_SND_SEQ_MIDI_EMUL) += snd-seq-midi-emul.o
obj-$(CONFIG_SND_SEQ_MIDI_EVENT) += snd-seq-midi-event.o
obj-$(CONFIG_SND_SEQ_VIRMIDI) += snd-seq-virmidi.o
