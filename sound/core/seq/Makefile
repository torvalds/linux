#
# Makefile for ALSA
# Copyright (c) 1999 by Jaroslav Kysela <perex@perex.cz>
#

ifeq ($(CONFIG_SND_SEQUENCER_OSS),y)
  obj-$(CONFIG_SND_SEQUENCER) += oss/
endif

snd-seq-device-objs := seq_device.o
snd-seq-objs := seq.o seq_lock.o seq_clientmgr.o seq_memory.o seq_queue.o \
                seq_fifo.o seq_prioq.o seq_timer.o \
                seq_system.o seq_ports.o seq_info.o
snd-seq-midi-objs := seq_midi.o
snd-seq-midi-emul-objs := seq_midi_emul.o
snd-seq-midi-event-objs := seq_midi_event.o
snd-seq-dummy-objs := seq_dummy.o
snd-seq-virmidi-objs := seq_virmidi.o

#
# this function returns:
#   "m" - CONFIG_SND_SEQUENCER is m
#   <empty string> - CONFIG_SND_SEQUENCER is undefined
#   otherwise parameter #1 value
#
sequencer = $(if $(subst y,,$(CONFIG_SND_SEQUENCER)),$(if $(1),m),$(if $(CONFIG_SND_SEQUENCER),$(1)))

obj-$(CONFIG_SND_SEQUENCER) += snd-seq.o snd-seq-device.o
ifeq ($(CONFIG_SND_SEQUENCER_OSS),y)
obj-$(CONFIG_SND_SEQUENCER) += snd-seq-midi-event.o
endif
obj-$(CONFIG_SND_SEQ_DUMMY) += snd-seq-dummy.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_VIRMIDI) += snd-seq-virmidi.o snd-seq-midi-event.o
obj-$(call sequencer,$(CONFIG_SND_RAWMIDI)) += snd-seq-midi.o snd-seq-midi-event.o
obj-$(call sequencer,$(CONFIG_SND_OPL3_LIB)) += snd-seq-midi-event.o snd-seq-midi-emul.o
obj-$(call sequencer,$(CONFIG_SND_OPL4_LIB)) += snd-seq-midi-event.o snd-seq-midi-emul.o
obj-$(call sequencer,$(CONFIG_SND_SBAWE)) += snd-seq-midi-emul.o snd-seq-virmidi.o
obj-$(call sequencer,$(CONFIG_SND_EMU10K1)) += snd-seq-midi-emul.o snd-seq-virmidi.o
