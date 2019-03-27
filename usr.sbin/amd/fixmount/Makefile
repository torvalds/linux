# ex:ts=8
#
# Makefile for amd
# 	This file is under a "BSD" copyright (c) by David O'Brien 1998
#
# $FreeBSD$

.PATH:	${SRCTOP}/contrib/amd/fixmount \
	${SRCTOP}/contrib/amd/conf/checkmount

PROG=	fixmount
MAN=	fixmount.8
SRCS=	fixmount.c

#	These would be links created by the GNU-style configure
SRCS+=	checkmount_bsd44.c

LIBADD+=	amu rpcsvc

.include <bsd.prog.mk>
