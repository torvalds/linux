#	$FreeBSD$

BINDIR=	/usr/local/bin
MAN=

ATH_DEFAULT=	ath0

CFLAGS+=-DATH_DEFAULT='"${ATH_DEFAULT}"'
CFLAGS+=-I${.CURDIR}
CFLAGS+=-I${.CURDIR}/../common
CFLAGS+=-I${.CURDIR}/../../../../sys
CFLAGS+=-I${.CURDIR}/../../../../sys/dev/ath
CFLAGS+=-I${.CURDIR}/../../../../sys/dev/ath/ath_hal
CFLAGS+=-I${.CURDIR}/../../../../sys/contrib/dev/ath/ath_hal
CFLAGS+=-I${.OBJDIR}
