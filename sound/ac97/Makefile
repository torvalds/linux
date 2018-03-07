#
# make for AC97 bus drivers
#

obj-$(CONFIG_AC97_BUS_NEW)	+= ac97.o

ac97-y				+= bus.o codec.o
ac97-$(CONFIG_AC97_BUS_COMPAT)	+= snd_ac97_compat.o
