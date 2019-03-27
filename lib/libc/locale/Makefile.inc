#	from @(#)Makefile.inc	8.1 (Berkeley) 6/4/93
# $FreeBSD$

# locale sources
.PATH: ${LIBC_SRCTOP}/${LIBC_ARCH}/locale ${LIBC_SRCTOP}/locale

SRCS+=	ascii.c big5.c btowc.c collate.c collcmp.c euc.c fix_grouping.c \
	gb18030.c gb2312.c gbk.c ctype.c isctype.c iswctype.c \
	ldpart.c lmessages.c lmonetary.c lnumeric.c localeconv.c mblen.c \
	mbrlen.c \
	mbrtowc.c mbsinit.c mbsnrtowcs.c \
	mbsrtowcs.c mbtowc.c mbstowcs.c \
	mskanji.c nextwctype.c nl_langinfo.c nomacros.c none.c rpmatch.c \
	rune.c \
	runetype.c setlocale.c setrunelocale.c \
	table.c \
	tolower.c toupper.c utf8.c wcrtomb.c wcsnrtombs.c \
	wcsrtombs.c wcsftime.c \
	wcstof.c wcstod.c \
	wcstoimax.c wcstol.c wcstold.c wcstoll.c \
	wcstombs.c \
	wcstoul.c wcstoull.c wcstoumax.c wctob.c wctomb.c wctrans.c wctype.c \
	wcwidth.c\
	xlocale.c

.if ${MK_ICONV} != "no"
SRCS+=	c16rtomb_iconv.c c32rtomb_iconv.c mbrtoc16_iconv.c mbrtoc32_iconv.c
.else
SRCS+=	c16rtomb.c c32rtomb.c mbrtoc16.c mbrtoc32.c
.endif

SYM_MAPS+=${LIBC_SRCTOP}/locale/Symbol.map

MAN+=	btowc.3 \
	ctype_l.3 \
	ctype.3 digittoint.3 isalnum.3 isalpha.3 isascii.3 isblank.3 iscntrl.3 \
	isdigit.3 isgraph.3 isideogram.3 islower.3 isphonogram.3 isprint.3 \
	ispunct.3 isrune.3 isspace.3 isspecial.3 \
	isupper.3 iswalnum.3 iswalnum_l.3 isxdigit.3 \
	localeconv.3 mblen.3 mbrlen.3 \
	mbrtowc.3 \
	mbsinit.3 \
	mbsrtowcs.3 mbstowcs.3 mbtowc.3 multibyte.3 \
	nextwctype.3 nl_langinfo.3 rpmatch.3 \
	setlocale.3 toascii.3 tolower.3 toupper.3 towlower.3 towupper.3 \
	wcsftime.3 \
	wcrtomb.3 \
	wcsrtombs.3 wcstod.3 wcstol.3 wcstombs.3 wctomb.3 \
	wctrans.3 wctype.3 wcwidth.3 \
	duplocale.3 freelocale.3 newlocale.3 querylocale.3 uselocale.3 xlocale.3

MAN+=	big5.5 euc.5 gb18030.5 gb2312.5 gbk.5 mskanji.5 utf8.5

MLINKS+=btowc.3 wctob.3
MLINKS+=isdigit.3 isnumber.3
MLINKS+=isgraph.3 isgraph_l.3
MLINKS+=islower.3 islower_l.3
MLINKS+=ispunct.3 ispunct_l.3
MLINKS+=isspace.3 isspace_l.3
MLINKS+=nl_langinfo.3 nl_langinfo_l.3
MLINKS+=iswalnum.3 iswalpha.3 iswalnum.3 iswascii.3 iswalnum.3 iswblank.3 \
	iswalnum.3 iswcntrl.3 iswalnum.3 iswdigit.3 iswalnum.3 iswgraph.3 \
	iswalnum.3 iswhexnumber.3 \
	iswalnum.3 iswideogram.3 iswalnum.3 iswlower.3 iswalnum.3 iswnumber.3 \
	iswalnum.3 iswphonogram.3 iswalnum.3 iswprint.3 iswalnum.3 iswpunct.3 \
	iswalnum.3 iswrune.3 iswalnum.3 iswspace.3 iswalnum.3 iswspecial.3 \
	iswalnum.3 iswupper.3 iswalnum.3 iswxdigit.3
MLINKS+=iswalnum_l.3 iswalpha_l.3 iswalnum_l.3 iswcntrl_l.3 \
	iswalnum_l.3 iswctype_l.3 iswalnum_l.3 iswdigit_l.3 \
	iswalnum_l.3 iswgraph_l.3 iswalnum_l.3 iswlower_l.3 \
	iswalnum_l.3 iswprint_l.3 iswalnum_l.3 iswpunct_l.3 \
	iswalnum_l.3 iswspace_l.3 iswalnum_l.3 iswupper_l.3 \
	iswalnum_l.3 iswxdigit_l.3 iswalnum_l.3 towlower_l.3 \
	iswalnum_l.3 towupper_l.3 iswalnum_l.3 wctype_l.3 \
	iswalnum_l.3 iswblank_l.3 iswalnum_l.3 iswhexnumber_l.3 \
	iswalnum_l.3 iswideogram_l.3 iswalnum_l.3 iswnumber_l.3 \
	iswalnum_l.3 iswphonogram_l.3 iswalnum_l.3 iswrune_l.3 \
	iswalnum_l.3 iswspecial_l.3 iswalnum_l.3 nextwctype_l.3 \
	iswalnum_l.3 towctrans_l.3 iswalnum_l.3 wctrans_l.3
MLINKS+=isxdigit.3 ishexnumber.3
MLINKS+=localeconv.3 localeconv_l.3
MLINKS+=mbrtowc.3 mbrtoc16.3 mbrtowc.3 mbrtoc32.3
MLINKS+=mbsrtowcs.3 mbsnrtowcs.3
MLINKS+=wcrtomb.3 c16rtomb.3 wcrtomb.3 c32rtomb.3
MLINKS+=wcsrtombs.3 wcsnrtombs.3
MLINKS+=wcstod.3 wcstof.3 wcstod.3 wcstold.3
MLINKS+=wcstol.3 wcstoul.3 wcstol.3 wcstoll.3 wcstol.3 wcstoull.3 \
	wcstol.3 wcstoimax.3 wcstol.3 wcstoumax.3
MLINKS+=wctrans.3 towctrans.3
MLINKS+=wctype.3 iswctype.3
