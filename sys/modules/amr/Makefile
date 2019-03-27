# $FreeBSD$

.PATH: ${SRCTOP}/sys/dev/amr

SUBDIR= amr_cam
.if ${MACHINE_CPUARCH} == "i386" || ${MACHINE_CPUARCH} == "amd64"
SUBDIR+= amr_linux
.endif

KMOD=	amr
SRCS=	amr.c amr_pci.c amr_disk.c device_if.h bus_if.h pci_if.h

# Enable a questionable optimisation for newer adapters
#CFLAGS+= -DAMR_QUARTZ_GOFASTER

# Debugging
#CFLAGS+= -DAMR_DEBUG=3

.include <bsd.kmod.mk>
