#	$OpenBSD: Makefile.inc,v 1.29 2025/08/10 11:36:07 miod Exp $

# locale sources
.PATH: ${LIBCSRCDIR}/locale

SRCS+=	_def_messages.c _def_monetary.c _def_numeric.c _def_time.c \
	localeconv.c nl_langinfo.c nl_langinfo_l.c setlocale.c \
	duplocale.c freelocale.c newlocale.c uselocale.c \
	__mb_cur_max.c _CurrentRuneLocale.c _get_locname.c \
	isctype_l.c iswctype.c iswctype_l.c wctype.c \
	mblen.c mbrlen.c mbrtoc16.c mbrtoc32.c mbstowcs.c mbtowc.c \
	btowc.c c16rtomb.c c32rtomb.c multibyte_citrus.c wcscoll.c \
	wcscoll_l.c \
	wcstombs.c wctob.c wctomb.c wcstof.c wcstod.c wcstold.c wcstol.c \
	wcstoul.c wcstoll.c wcstoull.c wcstoimax.c wcstoumax.c \
	rune.c runetable.c ___runetype_mb.c \
	_wctrans.c wcsxfrm.c wcsxfrm_l.c

MAN+=	nl_langinfo.3 setlocale.3 newlocale.3 uselocale.3 localeconv.3 \
	iswalnum.3 towlower.3 \
	btowc.3 c16rtomb.3 MB_CUR_MAX.3 mblen.3 mbrlen.3 mbrtoc16.3 mbrtowc.3 \
	mbsinit.3 mbsrtowcs.3 \
	mbstowcs.3 mbtowc.3 wcrtomb.3 wcscoll.3 wcsrtombs.3 wcstod.3 \
	wcstol.3 wcstombs.3 wcsxfrm.3 wctob.3 wctomb.3 \
	wctype.3 iswctype.3 wctrans.3 towctrans.3 wcwidth.3

CFLAGS+=-I${LIBCSRCDIR}/citrus
