# $FreeBSD$

.PATH: ${.PARSEDIR:H:H}/security/mac_veriexec

KMOD = mac_veriexec
SRCS = \
	bus_if.h \
	device_if.h \
	vnode_if.h
SRCS += \
	opt_capsicum.h \
	opt_global.h \
	opt_mac.h \
	opt_veriexec.h
SRCS += \
	mac_veriexec.c \
	veriexec_fingerprint.c \
	veriexec_metadata.c

EXPORT_SYMS+= ve_mutex \
	mac_veriexec_in_state \
	mac_veriexec_get_executable_flags

.if defined(KERNBUILDDIR)
MKDEP=		-include ${KERNBUILDDIR}/opt_global.h
.else
CFLAGS+=	-include opt_global.h
MKDEP=		-include opt_global.h
opt_mac.h:
	echo "#define MAC_DEBUG 1" >> ${.TARGET}
opt_global.h:
	echo "#define MAC 1" > ${.TARGET}
.endif

.ifndef WITHOUT_VERIEXEC_DEBUG
CFLAGS+= -DVERIFIED_EXEC_DEBUG
.endif

.include <bsd.kmod.mk>

