dnl $FreeBSD$
dnl $OpenBSD: src/regress/usr.bin/m4/translit.m4,v 1.1 2010/03/23 20:11:52 espie Exp $
dnl first one should match, not second one
translit(`onk*', `**', `p_')
