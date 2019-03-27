# $FreeBSD$

SYSDIR?=${SRCTOP}/sys

.PATH: ${SYSDIR}/compat/cloudabi64
.PATH: ${SYSDIR}/${MACHINE_CPUARCH}/cloudabi64
.PATH: ${SYSDIR}/${MACHINE}/cloudabi64

KMOD=	cloudabi64
SRCS=	cloudabi64_fd.c cloudabi64_module.c cloudabi64_poll.c \
	cloudabi64_sock.c cloudabi64_syscalls.c cloudabi64_sysent.c \
	cloudabi64_sysvec.c cloudabi64_thread.c

OBJS=	cloudabi64_vdso_blob.o
CLEANFILES=cloudabi64_vdso.o

.if ${MACHINE_CPUARCH} == "aarch64"
VDSO_SRCS=${SYSDIR}/contrib/cloudabi/cloudabi_vdso_aarch64.S
OUTPUT_TARGET=elf64-littleaarch64
BINARY_ARCHITECTURE=aarch64
.elif ${MACHINE_CPUARCH} == "amd64"
VDSO_SRCS=${SYSDIR}/contrib/cloudabi/cloudabi_vdso_x86_64.S
OUTPUT_TARGET=elf64-x86-64-freebsd
BINARY_ARCHITECTURE=i386
.endif

cloudabi64_vdso.o: ${VDSO_SRCS}
	${CC} -x assembler-with-cpp -shared -nostdinc -nostdlib \
	    -Wl,-T${SYSDIR}/compat/cloudabi/cloudabi_vdso.lds \
	    ${VDSO_SRCS} -o ${.TARGET}

cloudabi64_vdso_blob.o: cloudabi64_vdso.o
	${OBJCOPY} --input-target binary \
	    --output-target ${OUTPUT_TARGET} \
	    --binary-architecture ${BINARY_ARCHITECTURE} \
	    cloudabi64_vdso.o ${.TARGET}

.include <bsd.kmod.mk>
