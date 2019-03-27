# $FreeBSD$

.PATH: ${SRCTOP}/sys/compat/linux ${SRCTOP}/sys/${MACHINE}/linux

VDSO=	linux_vdso

KMOD=	linux64
SRCS=	linux_fork.c linux_dummy.c linux_file.c linux_event.c \
	linux_futex.c linux_getcwd.c linux_ioctl.c linux_ipc.c \
	linux_machdep.c linux_misc.c linux_ptrace.c linux_signal.c \
	linux_socket.c linux_stats.c linux_sysctl.c linux_sysent.c \
	linux_sysvec.c linux_time.c linux_vdso.c linux_timer.c \
	opt_compat.h opt_inet6.h opt_posix.h opt_usb.h \
	vnode_if.h device_if.h bus_if.h \
	linux_support.s
DPSRCS=	assym.inc linux_genassym.c

# XXX: for assym.inc
SRCS+=  opt_kstack_pages.h opt_nfs.h opt_hwpmc_hooks.h
.if ${MACHINE_CPUARCH} == "i386"
SRCS+=	opt_apic.h
.endif

CLEANFILES=	linux_assym.h linux_genassym.o linux_locore.o

OBJS=	${VDSO}.so

linux_assym.h: linux_genassym.o
	sh ${SYSDIR}/kern/genassym.sh linux_genassym.o > ${.TARGET}

linux_locore.o: linux_locore.s linux_assym.h
	${CC} -x assembler-with-cpp -DLOCORE -shared -mcmodel=small	\
	-pipe -I. -I${SYSDIR} -Werror -Wall -fno-common -fPIC -nostdinc	\
	-Wl,-T${SRCTOP}/sys/${MACHINE}/linux/${VDSO}.lds.s	\
	-Wl,-soname=${VDSO}.so.1,-warn-common -nostdlib			\
	    ${.IMPSRC} -o ${.TARGET}

.if ${MACHINE_CPUARCH} == "aarch64"
OBJCOPY_TARGET=--output-target elf64-littleaarch64 --binary-architecture aarch64
.elif ${MACHINE_CPUARCH} == "amd64"
OBJCOPY_TARGET=--output-target elf64-x86-64 --binary-architecture i386:x86-64
.else
.error ${MACHINE_CPUARCH} not yet supported by linux64
.endif
${VDSO}.so: linux_locore.o
	${OBJCOPY} --input-target binary ${OBJCOPY_TARGET} -S -g \
	    linux_locore.o ${.TARGET}
	strip -N _binary_linux_locore_o_size ${.TARGET}

linux_support.o: assym.inc linux_assym.h
	${CC} -c -x assembler-with-cpp -DLOCORE ${CFLAGS} \
	    ${.IMPSRC} -o ${.TARGET}

linux_genassym.o: offset.inc
	${CC} -c ${CFLAGS:N-flto:N-fno-common} ${.IMPSRC}

.if !defined(KERNBUILDDIR)
.if defined(DEBUG)
CFLAGS+=-DDEBUG
.endif
.if defined(KTR)
CFLAGS+=-DKTR
.endif
.endif

.include <bsd.kmod.mk>
