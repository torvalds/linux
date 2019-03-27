# $FreeBSD$

.if ${MACHINE_CPUARCH} == "amd64"
SFX= 32
CFLAGS+=-DCOMPAT_FREEBSD32 -DCOMPAT_LINUX32
.endif

.PATH: ${SRCTOP}/sys/compat/linux ${SRCTOP}/sys/${MACHINE_CPUARCH}/linux${SFX}

VDSO=	linux${SFX}_vdso

KMOD=	linux
SRCS=	linux_fork.c linux${SFX}_dummy.c linux_file.c linux_event.c \
	linux_futex.c linux_getcwd.c linux_ioctl.c linux_ipc.c \
	linux${SFX}_machdep.c linux_misc.c linux_signal.c \
	linux_socket.c linux_stats.c linux_sysctl.c linux${SFX}_sysent.c \
	linux${SFX}_sysvec.c linux_uid16.c linux_time.c \
	linux_timer.c linux_vdso.c \
	opt_inet6.h opt_compat.h opt_posix.h opt_usb.h vnode_if.h \
	device_if.h bus_if.h
.if ${MACHINE_CPUARCH} == "amd64"
SRCS+=	linux${SFX}_support.s
.else
SRCS+=	linux_copyout.c
.endif
DPSRCS=	assym.inc linux${SFX}_genassym.c

# XXX: for assym.inc
SRCS+=  opt_kstack_pages.h opt_nfs.h opt_hwpmc_hooks.h
.if ${MACHINE_CPUARCH} == "i386"
SRCS+=	opt_apic.h
.endif

OBJS=	${VDSO}.so

.if ${MACHINE_CPUARCH} == "i386"
SRCS+=	linux_ptrace.c imgact_linux.c linux_util.c linux_mib.c linux_mmap.c \
	linux_emul.c linux_errno.c opt_cpu.h linux.c
.endif

.if ${MACHINE_CPUARCH} == "i386"
EXPORT_SYMS=
EXPORT_SYMS+=	linux_emul_path
EXPORT_SYMS+=	linux_get_osname
EXPORT_SYMS+=	linux_get_osrelease
EXPORT_SYMS+=	linux_ioctl_register_handler
EXPORT_SYMS+=	linux_ioctl_unregister_handler
.endif

CLEANFILES=	linux${SFX}_assym.h linux${SFX}_genassym.o linux${SFX}_locore.o

linux${SFX}_assym.h: linux${SFX}_genassym.o
	sh ${SYSDIR}/kern/genassym.sh linux${SFX}_genassym.o > ${.TARGET}

linux${SFX}_locore.o: linux${SFX}_assym.h assym.inc
	${CC} -x assembler-with-cpp -DLOCORE -m32 -shared -s			\
	-pipe -I. -I${SYSDIR} -Werror -Wall -fno-common -nostdinc -nostdlib	\
	-fno-omit-frame-pointer -fPIC						\
	-Wl,-T${SRCTOP}/sys/${MACHINE_CPUARCH}/linux${SFX}/${VDSO}.lds.s	\
	-Wl,-soname=${VDSO}.so.1,--eh-frame-hdr,-warn-common		\
	    ${.IMPSRC} -o ${.TARGET}

.if ${MACHINE_CPUARCH} == "amd64"
linux${SFX}_support.o: linux${SFX}_assym.h assym.inc
	${CC} -c -x assembler-with-cpp -DLOCORE ${CFLAGS} \
	    ${.IMPSRC} -o ${.TARGET}

${VDSO}.so: linux${SFX}_locore.o
	${OBJCOPY} --input-target binary --output-target elf64-x86-64-freebsd			\
	    --binary-architecture i386 linux${SFX}_locore.o ${.TARGET}
	strip -N _binary_linux${SFX}_locore_o_size ${.TARGET}
.else
${VDSO}.so: linux${SFX}_locore.o
	${OBJCOPY} --input-target binary --output-target elf32-i386-freebsd			\
	    --binary-architecture i386 linux${SFX}_locore.o ${.TARGET}
	strip -N _binary_linux_locore_o_size ${.TARGET}
.endif

linux${SFX}_genassym.o: offset.inc
	${CC} -c ${CFLAGS:N-flto:N-fno-common} ${.IMPSRC}

.if !defined(KERNBUILDDIR)
.if defined(KTR)
CFLAGS+=	-DKTR
.endif
.if defined(DEBUG)
CFLAGS+=	-DDEBUG
.endif
.endif

.include <bsd.kmod.mk>
