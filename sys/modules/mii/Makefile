# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/mii

KMOD=	miibus
SRCS=	acphy.c amphy.c atphy.c axphy.c bmtphy.c brgphy.c bus_if.h
SRCS+=	ciphy.c device_if.h
SRCS+=	e1000phy.c gentbi.c icsphy.c ip1000phy.c jmphy.c lxtphy.c
SRCS+=	miibus_if.c miibus_if.h mii.c miidevs.h mii_bitbang.c mii_physubr.c
SRCS+=	mlphy.c nsgphy.c nsphy.c nsphyter.c pci_if.h pnaphy.c qsphy.c
SRCS+=	rdcphy.c rgephy.c rlphy.c smcphy.c
SRCS+=	smscphy.c tdkphy.c tlphy.c truephy.c
SRCS+=	ukphy.c ukphy_subr.c
SRCS+=	xmphy.c

EXPORT_SYMS=	mii_attach	\
		mii_bitbang_readreg \
		mii_bitbang_sync \
		mii_bitbang_writereg \
		mii_mediachg	\
		mii_pollstat	\
		mii_tick

.include <bsd.kmod.mk>
