#	From: @(#)Makefile	8.16 (Berkeley) 8/15/94
# $FreeBSD$

VOLUME=		usd/13.viref
EXTRA=		ex.cmd.roff ref.so set.opt.roff vi.cmd.roff
MACROS=		-me
CLEANFILES=	vi.ref-patched index
TRFLAGS=	-U		# this is to hide warnings only
USE_SOELIM=
USE_TBL=

vi.ref-patched: vi.ref
	sed -e 's:^\.so index.so$$:&.\\*[.T]:' ${.ALLSRC} > ${.TARGET}

PRINTERDEVICE?=	ascii
.for _dev in ${PRINTERDEVICE}
SRCS+=		vi.ref-${_dev}
EXTRA+=		index.so.${_dev}
CLEANFILES+=	index.so.${_dev} vi.ref-${_dev}

vi.ref-${_dev}: index.so.${_dev}
	sed -e 's:^\.so index\.so\.\\\*\[\.T\]$$:${_dev}:' vi.ref-patched > ${.TARGET}

# Build index.so as a side-effect of building the paper.
index.so.${_dev}: vi.ref-patched ${EXTRA:Nindex.so.${_dev}}
	sed -e 's:^\.so index\.so\.\\\*\[\.T\]$$::' vi.ref-patched | \
	    ${ROFF.${_dev}} -U -z
	sed -e 's/MINUSSIGN/-/' \
	    -e 's/DOUBLEQUOTE/""/' \
	    -e "s/SQUOTE/'/" \
	    -e 's/ /__SPACE/g' < index | \
	sort -u '-t	' -k 1,1 -k 2n | awk -f ${.CURDIR}/merge.awk | \
	sed -e 's/__SPACE/ /g' \
	    -e "s/^\\(['\\.]\\)/\\\\\&\\1/" > ${.TARGET}
.endfor

.include <bsd.doc.mk>
