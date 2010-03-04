#
# Makefile for ALSA
#

snd-usb-audio-objs := usbaudio.o usbmixer.o
snd-usb-lib-objs := usbmidi.o

# Toplevel Module Dependency
obj-$(CONFIG_SND_USB_AUDIO) += snd-usb-audio.o snd-usb-lib.o
obj-$(CONFIG_SND_USB_UA101) += snd-usb-lib.o
obj-$(CONFIG_SND_USB_USX2Y) += snd-usb-lib.o
obj-$(CONFIG_SND_USB_US122L) += snd-usb-lib.o

obj-$(CONFIG_SND) += misc/ usx2y/ caiaq/
