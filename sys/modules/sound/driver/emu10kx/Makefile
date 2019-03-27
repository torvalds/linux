# $FreeBSD$
.PATH: ${SRCTOP}/sys/dev/sound/pci

KMOD=	snd_emu10kx

SRCS=	device_if.h bus_if.h pci_if.h
SRCS+=	isa_if.h channel_if.h ac97_if.h mixer_if.h mpufoi_if.h
SRCS+=	vnode_if.h
# Master, PCM and MIDI devices
SRCS+=	emu10kx.c
SRCS+=	emu10kx-pcm.c
SRCS+=	emu10kx-midi.c

.include <bsd.kmod.mk>
