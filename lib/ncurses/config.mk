# $FreeBSD$

# This Makefile is shared by libncurses, libform, libmenu, libpanel.

NCURSES_DIR=	${SRCTOP}/contrib/ncurses

.if defined(ENABLE_WIDEC)
LIB_SUFFIX=	w
CFLAGS+=	-D_XOPEN_SOURCE_EXTENDED -DENABLE_WIDEC
NCURSES_CFG_H=	${.CURDIR:H}/ncurses/ncurses_cfg.h
.else
LIB_SUFFIX=
NCURSES_CFG_H=	${.CURDIR}/ncurses_cfg.h
.endif

CFLAGS+=	-I.
.if exists(${.OBJDIR:H}/ncurses${LIB_SUFFIX})
CFLAGS+=	-I${.OBJDIR:H}/ncurses${LIB_SUFFIX}
.endif
CFLAGS+=	-I${.CURDIR:H}/ncurses${LIB_SUFFIX}

# for ${NCURSES_CFG_H}
CFLAGS+=	-I${.CURDIR:H}/ncurses

CFLAGS+=	-I${NCURSES_DIR}/include
CFLAGS+=	-I${NCURSES_DIR}/ncurses

CFLAGS+=	-Wall

CFLAGS+=	-DNDEBUG

CFLAGS+=	-DHAVE_CONFIG_H

# everyone needs this
.PATH:		${NCURSES_DIR}/include

# tools and directories
AWK?=		awk
TERMINFODIR?=	${SHAREDIR}/misc

# Generate headers
ncurses_def.h:	MKncurses_def.sh ncurses_defs
	AWK=${AWK} sh ${NCURSES_DIR}/include/MKncurses_def.sh \
	    ${NCURSES_DIR}/include/ncurses_defs > ncurses_def.h

# Manual pages filter
MANFILTER=	sed -e 's%@TERMINFO@%${TERMINFODIR}/terminfo%g' \
		    -e 's%@DATADIR@%/usr/share%g' \
		    -e 's%@NCURSES_OSPEED@%${NCURSES_OSPEED}%g' \
		    -e 's%@NCURSES_MAJOR@%${NCURSES_MAJOR}%g' \
		    -e 's%@NCURSES_MINOR@%${NCURSES_MINOR}%g' \
		    -e 's%@NCURSES_PATCH@%${NCURSES_PATCH}%g' \
		    -e 's%@TIC@%tic%g' \
		    -e 's%@INFOCMP@%infocmp%g'
