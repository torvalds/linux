#
# Makefile for cs5535audio
#

snd-cs5535audio-objs := cs5535audio.o cs5535audio_pcm.o

ifdef CONFIG_PM
snd-cs5535audio-objs += cs5535audio_pm.o
endif

# Toplevel Module Dependency
obj-$(CONFIG_SND_CS5535AUDIO) += snd-cs5535audio.o
