# $FreeBSD$
# $NetBSD: Makefile.inc,v 1.3 2008/10/25 22:35:36 apb Exp $

CODE=	KOI
PARTFILE= ${.CURDIR}/KOI8.part

MAPS=	GOST19768-74%UCS.mps ISO-5427%UCS.mps KOI7%UCS.mps KOI8%UCS.mps \
	KOI8-C%UCS.mps KOI8-CyrillicOld%UCS.mps KOI8-E%UCS.mps KOI8-R%UCS.mps \
	KOI8-RU%UCS.mps KOI8-T%UCS.mps KOI8-U%UCS.mps UCS%GOST19768-74.mps \
	UCS%ISO-5427.mps UCS%KOI7.mps UCS%KOI8-C.mps UCS%KOI8-CyrillicOld.mps \
	UCS%KOI8-E.mps UCS%KOI8-R.mps UCS%KOI8-RU.mps UCS%KOI8-T.mps \
	UCS%KOI8-U.mps UCS%KOI8.mps

mapper.dir.KOI: KOI8.part mapper.dir.KOI.src
	( \
	    cat ${.CURDIR}/mapper.dir.KOI.src; \
	    for i in ${PART}; do \
		printf "%-31s %-31s %s\n" KOI$$i"EXT/UCS" \
			mapper_std KOI/KOI$$i"%UCS.mps"; \
		printf "%-31s %-31s %s\n" UCS/KOI$$i"EXT" \
			mapper_std KOI/UCS%KOI$$i".mps"; \
	    done; \
	    for i in ${PART}; do \
		printf "%-31s %-31s %s\n" KOI$$i"/UCS" \
			mapper_parallel GOST19768-74/UCS,KOI$$i"EXT/UCS"; \
		printf "%-31s %-31s %s\n" UCS/KOI$$i \
			mapper_parallel UCS/GOST19768-74,UCS/KOI$$i"EXT"; \
	    done; \
	    echo; \
	) > ${.TARGET}

charset.pivot.KOI: KOI8.part charset.pivot.KOI.src
	( \
	    cat ${.CURDIR}/charset.pivot.KOI.src; \
	    for i in ${PART}; do \
		printf "%-31s %-31s%d\n" KOI$$i UCS 1; \
		printf "%-31s %-31s%d\n" UCS KOI$$i 1; \
	    done; \
	    echo; \
	) > ${.TARGET}

.include "../Makefile.part"
