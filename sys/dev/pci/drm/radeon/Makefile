#	$OpenBSD: Makefile,v 1.2 2020/06/08 04:48:15 jsg Exp $

AWK=	awk

radeon_devlist.h: ../include/drm/drm_pciids.h devlist.awk
	/bin/rm -f radeon_devlist.h
	${AWK} -f devlist.awk ../include/drm/drm_pciids.h > radeon_devlist.h
