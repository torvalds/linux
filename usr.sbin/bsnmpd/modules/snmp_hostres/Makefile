#
# Copyright (c) 2005-2006 The FreeBSD Project
# All rights reserved.
# Author: Victor Cruceru <soc-victor@freebsd.org>
#
# Redistribution of this software and documentation and use in source and
# binary forms, with or without modification, are permitted provided that
# the following conditions are met:
#
# 1. Redistributions of source code or documentation must retain the above
#    copyright notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$
#

LPRSRC=	${SRCTOP}/usr.sbin/lpr/common_source
.PATH: ${LPRSRC}

MOD=	hostres
SRCS=	hostres_begemot.c		\
	hostres_device_tbl.c		\
	hostres_diskstorage_tbl.c	\
	hostres_fs_tbl.c		\
	hostres_network_tbl.c		\
	hostres_partition_tbl.c		\
	hostres_printer_tbl.c		\
	hostres_processor_tbl.c		\
	hostres_scalars.c		\
	hostres_snmp.c			\
	hostres_storage_tbl.c		\
	hostres_swinstalled_tbl.c	\
	hostres_swrun_tbl.c		\
	printcap.c

#Not having NDEBUG defined will enable assertions and a lot of output on stderr
CFLAGS+= -DNDEBUG -I${LPRSRC}
XSYM=	host hrStorageOther hrStorageRam hrStorageVirtualMemory \
	hrStorageFixedDisk hrStorageRemovableDisk hrStorageFloppyDisk \
	hrStorageCompactDisc hrStorageRamDisk hrStorageFlashMemory \
	hrStorageNetworkDisk hrDeviceOther hrDeviceUnknown \
	hrDeviceProcessor hrDeviceNetwork hrDevicePrinter \
	hrDeviceDiskStorage hrDeviceVideo hrDeviceAudio \
	hrDeviceCoprocessor hrDeviceKeyboard hrDeviceModem \
	hrDeviceParallelPort hrDevicePointing \
	hrDeviceSerialPort hrDeviceTape hrDeviceClock \
	hrDeviceVolatileMemory hrDeviceNonVolatileMemory \
	hrFSOther hrFSUnknown hrFSBerkeleyFFS hrFSSys5FS hrFSFat\
	hrFSHPFS hrFSHFS hrFSMFS hrFSNTFS hrFSVNode hrFSJournaled \
	hrFSiso9660 hrFSRockRidge hrFSNFS hrFSNetware hrFSAFS hrFSDFS \
	hrFSAppleshare hrFSRFS hrFSDGCFS hrFSBFS hrFSFAT32 hrFSLinuxExt2

MAN=	snmp_hostres.3

DEFS=	${MOD}_tree.def
BMIBS=	BEGEMOT-HOSTRES-MIB.txt

LIBADD=	kvm devinfo m geom memstat

.include <bsd.snmpmod.mk>

printcap.pico: printcap.c
	${CC} ${PICFLAG} -DPIC ${CFLAGS:C/^-W.*//} -c ${.IMPSRC} -o ${.TARGET}
