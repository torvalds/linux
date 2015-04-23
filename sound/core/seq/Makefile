#
# Makefile for ALSA
# Copyright (c) 1999 by Jaroslav Kysela <perex@perex.cz>
#

snd-seq-device-objs := seq_device.o
snd-seq-objs := seq.o seq_lock.o seq_clientmgr.o seq_memory.o seq_queue.o \
                seq_fifo.o seq_prioq.o seq_timer.o \
                seq_system.o seq_ports.o
snd-seq-$(CONFIG_PROC_FS) += seq_info.o
snd-seq-midi-objs := seq_midi.o
snd-seq-midi-emul-objs := seq_midi_emul.o
snd-seq-midi-event-objs := seq_midi_event.o
snd-seq-dummy-objs := seq_dummy.o
snd-seq-virmidi-objs := seq_virmidi.o

obj-$(CONFIG_SND_SEQUENCER) += snd-seq.o snd-seq-device.o
ifeq ($(CONFIG_SND_SEQUENCER_OSS),y)
  obj-$(CONFIG_SND_SEQUENCER) += snd-seq-midi-event.o
  obj-$(CONFIG_SND_SEQUENCER) += oss/
endif
obj-$(CONFIG_SND_SEQ_DUMMY) += snd-seq-dummy.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_VIRMIDI) += snd-seq-virmidi.o snd-seq-midi-event.o
obj-$(CONFIG_SND_RAWMIDI_SEQ) += snd-seq-midi.o snd-seq-midi-event.o
obj-$(CONFIG_SND_OPL3_LIB_SEQ) += snd-seq-midi-event.o snd-seq-midi-emul.o
obj-$(CONFIG_SND_OPL4_LIB_SEQ) += snd-seq-midi-event.o snd-seq-midi-emul.o
obj-$(CONFIG_SND_SBAWE_SEQ) += snd-seq-midi-emul.o snd-seq-virmidi.o
obj-$(CONFIG_SND_EMU10K1_SEQ) += snd-seq-midi-emul.o snd-seq-virmidi.o
