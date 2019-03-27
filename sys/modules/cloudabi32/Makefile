# $FreeBSD$

SYSDIR?=${SRCTOP}/sys

.PATH: ${SYSDIR}/compat/cloudabi32
.PATH: ${SYSDIR}/${MACHINE_CPUARCH}/cloudabi32
.PATH: ${SYSDIR}/${MACHINE}/cloudabi32

KMOD=	cloudabi32
SRCS=	cloudabi32_fd.c cloudabi32_module.c cloudabi32_poll.c \
	cloudabi32_sock.c cloudabi32_syscalls.c cloudabi32_sysent.c \
	cloudabi32_sysvec.c cloudabi32_thread.c

OBJS=	cloudabi32_vdso_blob.o
CLEANFILES=cloudabi32_vdso.o

.if ${MACHINE_CPUARCH} == "aarch64"
VDSO_SRCS=${SYSDIR}/contrib/cloudabi/cloudabi_vdso_armv6_on_64bit.S
OUTPUT_TARGET=elf64-littleaarch64
BINARY_ARCHITECTURE=aarch64
.elif ${MACHINE_CPUARCH} == "amd64"
VDSO_SRCS=${SYSDIR}/contrib/cloudabi/cloudabi_vdso_i686_on_64bit.S
OUTPUT_TARGET=elf64-x86-64-freebsd
BINARY_ARCHITECTURE=i386
.elif ${MACHINE_ARCH:Marmv[67]*} != ""
VDSO_SRCS=${SYSDIR}/contrib/cloudabi/cloudabi_vdso_armv6.S
OUTPUT_TARGET=elf32-littlearm
BINARY_ARCHITECTURE=arm
.elif ${MACHINE_CPUARCH} == "i386"
VDSO_SRCS=${SYSDIR}/contrib/cloudabi/cloudabi_vdso_i686.S
OUTPUT_TARGET=elf32-i386-freebsd
BINARY_ARCHITECTURE=i386
.endif

cloudabi32_vdso.o: ${VDSO_SRCS}
	${CC} -x assembler-with-cpp -m32 -shared -nostdinc -nostdlib \
	    -Wl,-T${SYSDIR}/compat/cloudabi/cloudabi_vdso.lds \
	    ${VDSO_SRCS} -o ${.TARGET}

cloudabi32_vdso_blob.o: cloudabi32_vdso.o
	${OBJCOPY} --input-target binary \
	    --output-target ${OUTPUT_TARGET} \
	    --binary-architecture ${BINARY_ARCHITECTURE} \
	    cloudabi32_vdso.o ${.TARGET}

.include <bsd.kmod.mk>
