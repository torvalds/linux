# Makefile for Creative Labs EMU10K1 
#
# 12 Apr 2000 Rui Sousa

obj-$(CONFIG_SOUND_EMU10K1) += emu10k1.o

emu10k1-objs := audio.o cardmi.o cardmo.o cardwi.o cardwo.o ecard.o \
                efxmgr.o emuadxmg.o hwaccess.o irqmgr.o main.o midi.o \
                mixer.o passthrough.o recmgr.o timer.o voicemgr.o

ifdef DEBUG
    EXTRA_CFLAGS += -DEMU10K1_DEBUG
endif

ifdef CONFIG_MIDI_EMU10K1
    EXTRA_CFLAGS += -DEMU10K1_SEQUENCER
endif
