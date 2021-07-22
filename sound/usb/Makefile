# SPDX-License-Identifier: GPL-2.0
#
# Makefile for ALSA
#

snd-usb-audio-objs := 	card.o \
			clock.o \
			endpoint.o \
			format.o \
			helper.o \
			implicit.o \
			mixer.o \
			mixer_quirks.o \
			mixer_scarlett.o \
			mixer_scarlett_gen2.o \
			mixer_us16x08.o \
			mixer_s1810c.o \
			pcm.o \
			power.o \
			proc.o \
			quirks.o \
			stream.o \
			validate.o

snd-usb-audio-$(CONFIG_SND_USB_AUDIO_USE_MEDIA_CONTROLLER) += media.o

snd-usbmidi-lib-objs := midi.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_USB_AUDIO) += snd-usb-audio.o snd-usbmidi-lib.o

obj-$(CONFIG_SND_USB_UA101) += snd-usbmidi-lib.o
obj-$(CONFIG_SND_USB_USX2Y) += snd-usbmidi-lib.o
obj-$(CONFIG_SND_USB_US122L) += snd-usbmidi-lib.o

obj-$(CONFIG_SND) += misc/ usx2y/ caiaq/ 6fire/ hiface/ bcd2000/
obj-$(CONFIG_SND_USB_LINE6)	+= line6/
