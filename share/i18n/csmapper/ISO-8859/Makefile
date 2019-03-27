# $FreeBSD$
# $NetBSD: Makefile.inc,v 1.4 2008/10/25 22:35:36 apb Exp $

CODE=	ISO-8859

mapper.dir.ISO-8859: ISO-8859.part
	(\
	    echo "# ISO-8859";\
	    printf '%-32s%-16s\n' ISO-8859-1/UCS mapper_none;\
	    printf '%-32s%-16s%s\n' UCS/ISO-8859-1 mapper_zone 0x00-0xFF;\
	    for i in ${PART}; do \
		printf '%-32s%-16s%s\n' ISO-8859-$$i'/UCS' mapper_std \
		    ISO-8859/ISO-8859-$$i'%UCS.mps';\
		printf '%-32s%-16s%s\n' UCS/ISO-8859-$$i mapper_std \
		    'ISO-8859/UCS%ISO-8859-'$$i'.mps';\
		printf '%-32s%-16s%s\n' ISO-8859-$$i':GR/ISO-8859-'$$i \
		    mapper_zone '0x00 - 0x7F : +0x80';\
		printf '%-32s%-16s%s\n' ISO-8859-$$i'/ISO-8859-'$$i':GR' \
		    mapper_zone '0x80 - 0xFF : -0x80';\
		printf '%-32s%-16s%s\n' ISO-8859-$$i':GR/UCS' mapper_serial \
		    ISO-8859-$$i':GR/ISO-8859-'$$i',ISO-8859-'$$i'/UCS';\
		printf '%-32s%-16s%s\n' UCS/ISO-8859-$$i':GR' mapper_serial \
		    UCS/ISO-8859-$$i',ISO-8859-'$$i'/ISO-8859-'$$i':GR';\
	    done;\
	    echo;\
	) > ${.TARGET}

charset.pivot.ISO-8859: ISO-8859.part
	(\
	    echo "# ISO-8859";\
	    for i in ${PART}; do \
		printf "%-32s%-32s%d\n" ISO-8859-$$i UCS 1;\
		printf "%-32s%-32s%d\n" UCS ISO-8859-$$i 1;\
		printf "%-32s%-32s%d\n" ISO-8859-$$i':GR' UCS 1;\
		printf "%-32s%-32s%d\n" UCS ISO-8859-$$i':GR' 1;\
	    done;\
	    echo;\
	) > ${.TARGET}

.include "../Makefile.part"
