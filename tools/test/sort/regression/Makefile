# $FreeBSD$

TESTFILE= test01 test02 test03 test04 test05 test06 test07 test08 \
	test09 test10 test11 test12 test13 test14 test15 test16 test17 \
	test_nums

OPTIONS= r f fr fs fu i ir b bs bd bu g gs gu gr n nr M Mr u nu ns

#SORT?=	/usr/local/bin/bsdsort
GNUSORT?=~/coreutils/bin/sort
SORT?=	../sort
CMP?=	${.CURDIR}/cmp.sh

check:
	mkdir -p output
.for f in ${TESTFILE}
	@LANG=C;${SORT} data/${f} >output/${f}
	@#LANG=C;${GNUSORT} data/${f} >ref/${f}	
	@${CMP} ref/${f} output/${f}
.endfor
.for opt in ${OPTIONS}
.for f in ${TESTFILE}
	@LANG=C;${SORT} -${opt} data/${f} >output/${f}${opt}
	@#LANG=C;${GNUSORT} -${opt} data/${f} >ref/${f}${opt}
	@${CMP} ref/${f}${opt} output/${f}${opt}
.endfor
.endfor

CLEANDIRS+= output

.include <bsd.prog.mk>
