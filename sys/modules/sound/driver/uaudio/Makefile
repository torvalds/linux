# $FreeBSD$

.PATH:	${SRCTOP}/sys/dev/sound/usb

KMOD=	snd_uaudio
SRCS=	bus_if.h device_if.h usb_if.h vnode_if.h
SRCS+=	opt_usb.h opt_bus.h feeder_if.h channel_if.h usbdevs.h
SRCS+=	uaudio_pcm.c uaudio.c

.include <bsd.kmod.mk>
