# $FreeBSD$

SUBDIR=	tablegen refgen	posix gnu

ENCODING= ASCII ISO8859-1 ISO8859-2 ISO8859-3 ISO8859-4	ISO8859-5 ISO8859-6 \
	ISO8859-7 ISO8859-8 ISO8859-9 ISO8859-10 ISO8859-11 ISO8859-13 \
	ISO8859-14 ISO8859-15 ISO8859-16 ARMSCII-8 BIG5 BIG5-HKSCS \
	CP1131 CP1251 CP866 CP949 GB18030 GB2312 GBK VISCII KOI8-R KOI8-U \
	PT154 SHIFT_JIS EUC-CN EUC-JP EUC-KR

.if defined(EXTRA_CHECK)
ENCODING+= ATARIST BIG5-2003 C99 CP1046 CP1124 CP1125 CP1129 CP1131 CP1133 \
	CP1161 CP1162 CP1163 CP1250 CP1251 CP1252 CP1253 CP1254 CP1255 \
	CP1256 CP1257 CP1258 CP437 CP737 CP775 CP850 CP852 CP853 CP855 \
	CP856 CP857 CP858 CP860 CP861 CP862 CP863 CP864 CP865 CP866 CP869 \
	CP874 CP922 CP932 CP936 CP943 CP949 CP950 DEC-HANYU EUC-TW \
	EUC-JIS-2004 GB18030 GBK GEORGIAN-ACADEMY GEORGIAN-PS HP-ROMAN8 \
	HZ ISO-2022-CN ISO-2022-CN-EXT ISO-2022-JP ISO-2022-JP-1 \
	ISO-2022-JP-2 ISO-2022-JP-2004 ISO-2022-KR ISO-IR-14 ISO-IR-57 \
	ISO-IR-165 JAVA JIS_X0201 JIS_X0208-1990 JOHAB KOI8-RU KOI8-T \
	KZ-1048 MULELAO-1 NEXTSTEP PT154 RISCOS-LATIN1 TCVN5712-1 \
	TDS565
#
# MAC encodings are known to be outdated in GNU so it's no good
# to ensure compatibility blindly here
#
#ENCODING+= MACARABIC MACCENTRALEUROPE MACCROATIAN MACCYRILLIC MACGREEK \
#	MACICELAND MACROMANIA MACROMAN MACTHAI MACTURKISH MACUKRAINE \
#	MACHEBREW
.endif

GEN_FWD?= ${.CURDIR}/tablegen/tablegen -t
GEN_REV?= ${.CURDIR}/tablegen/tablegen -tr
REF_FWD?= ${.CURDIR}/refgen/refgen -t
REF_REV?= ${.CURDIR}/refgen/refgen -tr
CMP?=	${.CURDIR}/tablegen/cmp.sh

make-ref: refgen
	mkdir -p ref
.for enc in ${ENCODING}
	@echo "Generating ${enc} --> UTF-32 ..."
	-@${REF_FWD} ${enc} >ref/${enc}
	@echo "Generating UTF-32 --> ${enc} ..."
	-@${REF_REV} ${enc} >ref/${enc}-rev
.endfor

check: tablegen
	mkdir -p output
.for enc in ${ENCODING}
	@echo "Checking ${enc} --> UTF-32 ..."
	-@${GEN_FWD} ${enc} >output/${enc}
	@${CMP} ref/${enc} output/${enc}
	@echo "Checking UTF-32 --> ${enc} ..."
	-@${GEN_REV} ${enc} >output/${enc}-rev
	@${CMP} ref/${enc}-rev output/${enc}-rev
.endfor

perftest: refgen tablegen
.for enc in ${ENCODING}
	@echo "Checking ${enc} --> UTF-32 ..."
	@echo "GNU runtime:"
	@/usr/bin/time -h ${REF_FWD} ${enc} >/dev/null
	@echo "BSD runtime:"
	@/usr/bin/time -h ${GEN_FWD} ${enc} >/dev/null
	@echo "Checking UTF-32 --> ${enc} ..."
	@echo "GNU runtime:"
	@/usr/bin/time -h ${REF_REV} ${enc} >/dev/null
	@echo "BSD runtime:"
	@/usr/bin/time -h ${GEN_REV} ${enc} >/dev/null
.endfor

posixtest: posix
	@${.CURDIR}/posix/posix

gnutest: gnu
	@${.CURDIR}/gnu/gnu

test-everything: check perftest posixtest gnutest

CLEANDIRS+= output

.include <bsd.prog.mk>
