# $FreeBSD$

# Common flags to build FICL related files

.if ${MACHINE_CPUARCH} == "amd64" && ${DO32:U0} == 1
FICL_CPUARCH=	i386
.elif ${MACHINE_ARCH:Mmips64*} != ""
FICL_CPUARCH=	mips64
.else
FICL_CPUARCH=	${MACHINE_CPUARCH}
.endif

.if ${MACHINE_CPUARCH} == "amd64" && ${DO32:U0} == 0
CFLAGS+=	-fPIC
.endif

CFLAGS+=	-I${FICLSRC} -I${FICLSRC}/${FICL_CPUARCH} -I${LDRSRC}
CFLAGS+=	-DBF_DICTSIZE=15000
