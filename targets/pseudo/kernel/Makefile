# $FreeBSD$

# Build the kernel ${KERNCONF}
KERNCONF?= ${KERNEL:UGENERIC}

TARGET?= ${MACHINE}
# keep this compatible with peoples expectations...
KERN_OBJDIR= ${OBJTOP}/sys/${KERNCONF}
KERN_CONFDIR= ${SRCTOP}/sys/${TARGET}/conf

CONFIG= ${STAGE_HOST_OBJTOP}/usr/sbin/config

${KERNCONF}.config: .MAKE ${META_DEPS}
	mkdir -p ${KERN_OBJDIR:H}
	(cd ${KERN_CONFDIR} && \
	${CONFIG} ${CONFIGARGS} -d ${KERN_OBJDIR} ${KERNCONF})

${KERNCONF}.build: .MAKE ${KERNCONF}.config
	(cd ${KERN_OBJDIR} && ${.MAKE})

.if ${.MAKE.LEVEL} > 0
all: ${KERNCONF}.build
.endif

UPDATE_DEPENDFILE= no

.include <bsd.prog.mk>
