#
# Makefile for RR182x FreeBSD driver
# Copyright (c)  2004-2005 HighPoint Technologies, Inc. All rights reserved
#
# $FreeBSD$

HPTMV= ${SRCTOP}/sys/dev/hptmv
.PATH: ${HPTMV}

KMOD= hptmv
SRCS= opt_scsi.h opt_cam.h
SRCS+= bus_if.h device_if.h pci_if.h
SRCS+= mv.c entry.c ioctl.c hptproc.c gui_lib.c
OBJS+= hptmvraid.o

.if $(MACHINE_CPUARCH) == "amd64"
HPTMV_RAID_O = amd64-elf.raid.o.uu
.else
HPTMV_RAID_O = i386-elf.raid.o.uu
.endif

hptmvraid.o:	${HPTMV}/$(HPTMV_RAID_O)
	uudecode -p < ${HPTMV}/$(HPTMV_RAID_O) > ${.TARGET}

#
# Debug Options:
#
#   -DDEBUG: include debug code
#   -DDEBUG_LEVEL=x:
#		0 - No debug output message
#		1 - print only error messages
#		2 - print error and warning
#		3 - print all messages (info, warning, error)
#
#DEBUGOPT = -O2

.if defined(DEBUG) && $(DEBUG) == 1
DEBUGOPT += -DDEBUG
.if defined(DEBUG_LEVEL)
DEBUGOPT += -DDEBUG_LEVEL=$(DEBUG_LEVEL)
.else
DEBUGOPT += -DDEBUG_LEVEL=1
.endif
.endif

.if defined(FOR_DEMO) && $(FOR_DEMO) == 1
DEBUGOPT += -DFOR_DEMO
.endif

CFLAGS = ${DEBUGOPT}

.include <bsd.kmod.mk>
