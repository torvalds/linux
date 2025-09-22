#	$OpenBSD: Makefile,v 1.31 2024/09/11 15:42:52 bluhm Exp $

.if ${MACHINE} == "amd64" || ${MACHINE} == "arm64"

PROG=		vmd
SRCS=		vmd.c control.c log.c priv.c proc.c config.c vmm.c vm.c
SRCS+=		pci.c virtio.c dhcp.c packet.c parse.y atomicio.c
SRCS+=		vioscsi.c vioraw.c vioqcow2.c vm_agentx.c vioblk.c
SRCS+=		vionet.c

.if ${MACHINE} == "amd64"
SRCS+=		i8253.c i8259.c fw_cfg.c loadfile_elf.c mc146818.c ns8250.c
SRCS+=		x86_vm.c x86_mmio.c
SRCS+=		psp.c sev.c
.endif # amd64
.if ${MACHINE} == "arm64"
SRCS+=		arm64_vm.c
.endif # arm64

CFLAGS+=	-Wall -I${.CURDIR}
CFLAGS+=	-Wstrict-prototypes -Wmissing-prototypes
CFLAGS+=	-Wmissing-declarations
CFLAGS+=	-Wshadow -Wpointer-arith -Wcast-qual
CFLAGS+=	-Wsign-compare

LDADD+=		-lutil -lpthread -levent -lz -lagentx
DPADD+=		${LIBUTIL} ${LIBPTHREAD} ${LIBEVENT} ${LIBZ} ${LIBAGENTX}

YFLAGS=

.else

NOPROG= yes

.endif # amd64 or arm64

MAN=		vmd.8 vm.conf.5

.include <bsd.prog.mk>
