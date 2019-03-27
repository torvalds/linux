# $FreeBSD$
# $NetBSD: Makefile.inc,v 1.2 2008/10/25 22:27:36 apb Exp $

CODE=	GEORGIAN
SED_EXP= -e 's/GEORGIANx/GEORGIAN-%%PART%%/' \
		-e "s/GEORGIANy/GEORGIAN-$$(echo %%PART%% | sed 's|-|:|')/"

.include "../Makefile.part"
