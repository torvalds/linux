# $FreeBSD$

BZ2DIR=	${SRCTOP}/contrib/bzip2
.PATH: ${BZ2DIR}

PROG=	bzip2
CFLAGS+= -D_FILE_OFFSET_BITS=64

WARNS?=	3

LIBADD=	bz2

LINKS=	${BINDIR}/bzip2 ${BINDIR}/bunzip2
LINKS+=	${BINDIR}/bzip2 ${BINDIR}/bzcat
MLINKS= bzip2.1 bunzip2.1 bzip2.1 bzcat.1

REFFILES=	sample1.ref sample2.ref sample3.ref 
DREFFILES=	sample1.bz2 sample2.bz2 sample3.bz2 
TESTFILES=	${REFFILES} ${DREFFILES}

CLEANFILES+=	${TESTFILES} \
		sample1.rb2 sample2.rb2 sample3.rb2 \
		sample1.tst sample2.tst sample3.tst

.for f in ${REFFILES}
${f}:	${f}.gz.uu
	uudecode -p ${BZ2DIR}/${f}.gz.uu | gunzip > ${f}
.endfor
.for f in ${DREFFILES}
${f}:	${f}.uu
	uudecode ${BZ2DIR}/${f}.uu
.endfor

test:  bzip2 ${TESTFILES}
	@cat ${BZ2DIR}/words1
	./bzip2 -1  < sample1.ref > sample1.rb2
	./bzip2 -2  < sample2.ref > sample2.rb2
	./bzip2 -3  < sample3.ref > sample3.rb2
	./bzip2 -d  < sample1.bz2 > sample1.tst
	./bzip2 -d  < sample2.bz2 > sample2.tst
	./bzip2 -ds < sample3.bz2 > sample3.tst
	cmp sample1.bz2 sample1.rb2 
	cmp sample2.bz2 sample2.rb2
	cmp sample3.bz2 sample3.rb2
	cmp sample1.tst sample1.ref
	cmp sample2.tst sample2.ref
	cmp sample3.tst sample3.ref
	@cat ${BZ2DIR}/words3

.include <bsd.prog.mk>
