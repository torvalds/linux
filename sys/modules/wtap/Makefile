# $FreeBSD$


# Declare Name of kernel module
KMOD	=  wtap

# Enumerate Source files for kernel module
.PATH:	${SRCTOP}/sys/dev/wtap
SRCS	=  if_wtap_module.c if_wtap.c if_medium.c

.PATH:	${SRCTOP}/sys/dev/wtap/wtap_hal
SRCS	+= hal.c

.PATH:	${SRCTOP}/sys/dev/wtap/plugins
SRCS	+= visibility.c

SRCS	+= opt_global.h

.if !defined(KERNBUILDDIR)
CFLAGS+=        -include opt_global.h

opt_global.h:
	echo "#define VIMAGE 1" > ${.TARGET}
.endif

# Include kernel module makefile
.include <bsd.kmod.mk>
