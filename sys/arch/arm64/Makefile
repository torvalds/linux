#	$OpenBSD: Makefile,v 1.2 2017/01/22 03:27:31 tb Exp $

.include <bsd.own.mk>    # for KEEPKERNELS

S=	${.CURDIR}/../..
KFILE=	GENERIC
.if exists(conf/GENERIC.MP)
KFILE=	GENERIC.MP
.endif
TDIRS=	${_arch} include
TAGS=	${.CURDIR}/tags

NOPROG=
NOMAN=
NOOBJ=
SUBDIR=	stand
.if !defined(KEEPKERNELS) || !(make(clean) || make(cleandir))
SUBDIR+=compile
.endif

# config the fattest kernel we can find into a temporary dir
# to create a Makefile.  Then use make to pull some variables
# out and push them into the sub-shell to expand the paths,
# and finally run ctags.
tags::
	TDIR=`mktemp -d /tmp/_tagXXXXXXXXXX` || exit 1; \
	eval "S=${S}" && \
	config -s ${S} -b $${TDIR} ${.CURDIR}/conf/${KFILE} && \
	eval "_arch=\"`make -V _arch -f $${TDIR}/Makefile`\"" && \
	eval "_mach=\"`make -V _mach -f $${TDIR}/Makefile`\"" && \
	eval "_machdir=\$S/arch/$${_mach}" && \
	eval "_archdir=\$S/arch/$${_arch}" && \
	eval "HFILES=\"`find $S \( -path $S/'arch' -o -path $S/stand -o -path $S/lib/libsa -o -path $S'/lib/libkern/arch' \) -prune -o -name '*.h'; find $${_machdir} $${_archdir} $S/lib/libkern/arch/$${_mach} \( -name boot -o -name stand \) -prune -o -name '*.h'`\"" && \
	eval "SFILES=\"`make -V SFILES -f $${TDIR}/Makefile`\"" && \
	eval "CFILES=\"`make -V CFILES -f $${TDIR}/Makefile`\"" && \
	eval "AFILES=\"`make -V AFILES -f $${TDIR}/Makefile`\"" && \
	ctags -wd -f ${TAGS} $${CFILES} $${HFILES} && \
	egrep "^[_A-Z]*ENTRY[_A-Z]*\(.*\)" $${SFILES} $${AFILES} | \
	    sed "s;\\([^:]*\\):\\([^(]*\\)(\\([^, )]*\\)\\(.*\\);\\3	\\1	/^\\2(\\3\\4$$/;" \
	    >> ${TAGS} && \
	sort -o ${TAGS} ${TAGS} && \
	rm -rf $${TDIR}

links:
	-for i in conf ${TDIRS}; do \
	    (cd $$i && rm -f tags; ln -s tags tags); done

obj:	_SUBDIRUSE

.include <bsd.prog.mk>
