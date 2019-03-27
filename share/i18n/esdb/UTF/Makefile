# $FreeBSD$
# $NetBSD: Makefile.inc,v 1.5 2008/10/25 22:27:36 apb Exp $

CODE=	UTF
NO_PREPROC= yes

UTF-16-mod= UTF1632
UTF-16-var= utf16
UTF-16BE-mod= UTF1632
UTF-16BE-var= utf16,big,force
UTF-16LE-mod= UTF1632
UTF-16LE-var= utf16,little,force
UTF-16-INTERNAL-mod= UTF1632
UTF-16-INTERNAL-var= utf16,internal,force
UTF-16-SWAPPED-mod= UTF1632
UTF-16-SWAPPED-var= utf16,swapped,force
UTF-32-mod= UTF1632
UTF-32-var= utf32
UTF-32BE-mod= UTF1632
UTF-32BE-var= utf32,big,force
UTF-32LE-mod= UTF1632
UTF-32LE-var= utf32,little,force
UTF-32-INTERNAL-mod= UTF1632
UTF-32-INTERNAL-var= utf32,internal,force
UTF-32-SWAPPED-mod= UTF1632
UTF-32-SWAPPED-var= utf32,swapped,force
UTF-8-mod= UTF8
UTF-8-var= utf8
UTF-7-mod= UTF7
UTF-7-var= utf7

.include "../Makefile.part"

.for i in ${PART}
${EPREFIX}${i}.src: ${CODE}.src
	sed -e 's/UTF-x/UTF-${i}/' \
		-e 's/UTF-mod/${UTF-${i}-mod}/' \
		-e 's/UTF-var/${UTF-${i}-var}/' \
		${.ALLSRC} > ${.TARGET}
	@echo ${.TARGET} >>.tmpfiles
.endfor
