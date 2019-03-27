#
# $FreeBSD$
#

.include <src.opts.mk>
CFLAGS+=-I${SRCTOP}/sys
.PATH:  ${SRCTOP}/sys/cam/ctl

PROG=	bhyve
PACKAGE=	bhyve

DEBUG_FLAGS= -g -O0

MAN=	bhyve.8

BHYVE_SYSDIR?=${SRCTOP}

SRCS=	\
	atkbdc.c		\
	acpi.c			\
	bhyvegc.c		\
	bhyverun.c		\
	block_if.c		\
	bootrom.c		\
	console.c		\
	consport.c		\
	ctl_util.c		\
	ctl_scsi_all.c		\
	dbgport.c		\
	fwctl.c			\
	gdb.c			\
	inout.c			\
	ioapic.c		\
	mem.c			\
	mevent.c		\
	mptbl.c			\
	pci_ahci.c		\
	pci_e82545.c		\
	pci_emul.c		\
	pci_fbuf.c		\
	pci_hostbridge.c	\
	pci_irq.c		\
	pci_lpc.c		\
	pci_nvme.c		\
	pci_passthru.c		\
	pci_virtio_block.c	\
	pci_virtio_console.c	\
	pci_virtio_net.c	\
	pci_virtio_rnd.c	\
	pci_virtio_scsi.c	\
	pci_uart.c		\
	pci_xhci.c		\
	pm.c			\
	post.c			\
	ps2kbd.c		\
	ps2mouse.c		\
	rfb.c			\
	rtc.c			\
	smbiostbl.c		\
	sockstream.c		\
	task_switch.c		\
	uart_emul.c		\
	usb_emul.c		\
	usb_mouse.c		\
	virtio.c		\
	vga.c			\
	xmsr.c			\
	spinup_ap.c		\
	iov.c

.PATH:  ${BHYVE_SYSDIR}/sys/amd64/vmm
SRCS+=	vmm_instruction_emul.c

LIBADD=	vmmapi md pthread z util sbuf cam

.if ${MK_INET_SUPPORT} != "no"
CFLAGS+=-DINET
.endif
.if ${MK_INET6_SUPPORT} != "no"
CFLAGS+=-DINET6
.endif
.if ${MK_OPENSSL} == "no"
CFLAGS+=-DNO_OPENSSL
.else
LIBADD+=	crypto
.endif

CFLAGS+= -I${BHYVE_SYSDIR}/sys/dev/e1000
CFLAGS+= -I${BHYVE_SYSDIR}/sys/dev/mii
CFLAGS+= -I${BHYVE_SYSDIR}/sys/dev/usb/controller

.ifdef GDB_LOG
CFLAGS+=-DGDB_LOG
.endif

WARNS?=	2

.include <bsd.prog.mk>
