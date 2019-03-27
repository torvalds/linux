# $FreeBSD$
# $NetBSD: Makefile.inc,v 1.6 2008/10/25 22:35:36 apb Exp $

CODE=	CP
TABLENAME= ${CODE}%%PART%%%UCS
RTABLENAME= UCS%${CODE}%%PART%%

mapper.dir.CP: CP.part mapper.dir.CP.src
	(\
	    echo "# CP";\
	    for i in ${PART}; do \
	        f=`echo $$i | sed 's/:/@/'`; \
		printf '%-32s%-16s%s\n' CP$$i'/UCS' \
		    mapper_std CP/CP$$f'%UCS.mps';\
		printf '%-32s%-16s%s\n' UCS/CP$$i \
		    mapper_std CP/UCS%CP$$f'.mps';\
	    done;\
	    cat ${.CURDIR}/mapper.dir.CP.src;\
	    echo;\
	) > ${.TARGET}

charset.pivot.CP: CP.part charset.pivot.CP.src
	(\
	    echo "# CP";\
	    for i in ${PART}; do \
		printf "%-32s%-32s%d\n" CP$$i UCS 1;\
		printf "%-32s%-32s%d\n" UCS CP$$i 1;\
	    done;\
	    cat ${.CURDIR}/charset.pivot.CP.src;\
	    echo;\
	) > ${.TARGET}

.include "../Makefile.part"
