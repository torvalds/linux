# $FreeBSD$

.PATH:	${SRCTOP}/sys/dev/cpufreq		\
	${SRCTOP}/sys/${MACHINE_CPUARCH}/cpufreq

KMOD=	cpufreq
SRCS=	ichss.c
SRCS+=	bus_if.h cpufreq_if.h device_if.h pci_if.h

.if ${MACHINE} == "i386" || ${MACHINE} == "amd64"
.PATH:	${SRCTOP}/sys/x86/cpufreq

SRCS+=	acpi_if.h opt_acpi.h
SRCS+=	est.c hwpstate.c p4tcc.c powernow.c
.endif

.if ${MACHINE} == "i386"
SRCS+=	smist.c
.endif

.if ${MACHINE} == "powerpc"
.PATH:	${SRCTOP}/sys/powerpc/cpufreq
SRCS+= dfs.c
.endif

.include <bsd.kmod.mk>
