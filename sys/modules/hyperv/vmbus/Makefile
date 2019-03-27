# $FreeBSD$

.PATH:	${SRCTOP}/sys/dev/hyperv/vmbus \
	${SRCTOP}/sys/dev/hyperv/vmbus/${MACHINE_CPUARCH}

KMOD=	hv_vmbus
SRCS=	hyperv.c \
	hyperv_busdma.c \
	hyperv_machdep.c \
	vmbus.c \
	vmbus_br.c \
	vmbus_chan.c \
	vmbus_et.c \
	vmbus_if.c \
	vmbus_res.c \
	vmbus_vector.S \
	vmbus_xact.c
SRCS+=	acpi_if.h bus_if.h device_if.h opt_acpi.h pci_if.h pcib_if.h vmbus_if.h

# XXX: for assym.inc
SRCS+=  opt_kstack_pages.h opt_nfs.h opt_hwpmc_hooks.h
.if ${MACHINE_CPUARCH} == "i386"
SRCS+=	opt_apic.h
.endif

DPSRCS=	assym.inc

vmbus_vector.o:
	${CC} -c -x assembler-with-cpp -DLOCORE ${CFLAGS} \
            ${.IMPSRC} -o ${.TARGET}

CFLAGS+= -I${SRCTOP}/sys/dev/hyperv/include \
	 -I${SRCTOP}/sys/dev/hyperv/vmbus

.include <bsd.kmod.mk>
