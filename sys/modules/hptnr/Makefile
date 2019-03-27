# $FreeBSD$
HPTNR=	${SRCTOP}/sys/dev/hptnr
.PATH:	${HPTNR}

KMOD=	hptnr
SRCS=	bus_if.h device_if.h pci_if.h
SRCS+=	opt_cam.h opt_scsi.h
SRCS+=	os_bsd.h hptnr_os_bsd.c hptnr_osm_bsd.c hptnr_config.c
OBJS+=	hptnr_lib.o

hptnr_lib.o:
	uudecode -p < ${HPTNR}/${MACHINE_ARCH}-elf.hptnr_lib.o.uu > ${.TARGET}

.include <bsd.kmod.mk>
