# $FreeBSD$

.PATH: ${SRCTOP}/contrib/ntp/libparse

LIB=	parse
INTERNALLIB=

SRCS=	binio.c	clk_computime.c	clk_dcf7000.c	clk_hopf6021.c \
	clk_meinberg.c	clk_rawdcf.c	clk_rcc8000.c	clk_schmid.c \
	clk_sel240x.c	clk_trimtaip.c	clk_trimtsip.c	clk_varitext.c \
	clk_wharton.c	data_mbg.c	gpstolfp.c	ieee754io.c \
	info_trimble.c	mfp_mul.c	parse.c	parse_conf.c \
	trim_info.c

CFLAGS+= -I${SRCTOP}/contrib/ntp/include \
	-I${SRCTOP}/contrib/ntp/lib/isc/unix/include \
	-I${SRCTOP}/contrib/ntp/lib/isc/include -I${.CURDIR:H}

.include <bsd.lib.mk>
