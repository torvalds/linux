# $Id: Makefile.def,v 1.18 2007/03/22 11:20:15 gmm Exp $
# $FreeBSD$

HPTRR=  ${SRCTOP}/sys/dev/hptrr
.PATH: ${HPTRR}
KMOD = hptrr
SRCS = opt_scsi.h opt_cam.h bus_if.h device_if.h pci_if.h os_bsd.h
SRCS+= hptrr_os_bsd.c hptrr_osm_bsd.c hptrr_config.c
OBJS = hptrr_lib.o

hptrr_lib.o:
	uudecode -p < ${HPTRR}/$(MACHINE_CPUARCH)-elf.hptrr_lib.o.uu > hptrr_lib.o

.include <bsd.kmod.mk>
