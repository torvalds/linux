# $FreeBSD$

PACKAGE=	clibs
SHLIBDIR?=	/lib

.if !defined(ENABLE_WIDEC)
# Override any MAN= setting below..
MK_MAN=no
.endif

.include <src.opts.mk>

.include "${.CURDIR:H}/config.mk"

LIB=		ncurses${LIB_SUFFIX}
SHLIB_MAJOR=	8

NO_LINT=

NCURSES_MAJOR!=	egrep 'NCURSES_MAJOR[ 	]*=' ${NCURSES_DIR}/dist.mk | sed -e 's%^[^0-9]*%%'
NCURSES_MINOR!=	egrep 'NCURSES_MINOR[ 	]*=' ${NCURSES_DIR}/dist.mk | sed -e 's%^[^0-9]*%%'
NCURSES_PATCH!=	egrep 'NCURSES_PATCH[ 	]*=' ${NCURSES_DIR}/dist.mk | sed -e 's%^[^0-9]*%%'

# From autoconf (!)
.if defined(ENABLE_WIDEC)
NCURSES_CH_T=		cchar_t
NCURSES_OK_WCHAR_T=	1
NEED_WCHAR_H=		1
.else
NCURSES_CH_T=		chtype
NCURSES_OK_WCHAR_T=
NEED_WCHAR_H=		0
.endif
NCURSES_CONST=		const
NCURSES_EXT_COLORS=	0
NCURSES_EXT_FUNCS=	1
NCURSES_INLINE=		inline
NCURSES_LIBUTF8=	0
NCURSES_MBSTATE_T=	0
NCURSES_MOUSE_VERSION=	1
NCURSES_OPAQUE=		0
NCURSES_OSPEED=		short
NCURSES_SBOOL=		char
NCURSES_SIZE_T=		short
NCURSES_TPARM_VARARGS=	1
NCURSES_WCHAR_T=	0
NCURSES_WINT_T=		0
NCURSES_XNAMES=		1
BROKEN_LINKER=		0
BUILTIN_BOOL=		1
ENABLE_LP64=		0
ENABLE_OPAQUE=		NCURSES_OPAQUE
ENABLE_REENTRANT=	0
HAVE_TCGETATTR=		1
HAVE_TERMIOS_H=		1
HAVE_TERMIO_H=		0
HAVE_VSSCANF=		1
HEADER_STDBOOL=		1
# XXX amd64 1L and int
ONEUL=			1UL
TYPEOF_CHTYPE=		long
TYPEOF_MMASK_T=		long
TYPE_OF_BOOL=		unsigned char
USE_BIG_STRINGS=	1
USE_CXX_BOOL=		defined(__cplusplus)

GENSRCS=	\
	codes.c \
	expanded.c \
	fallback.c \
	lib_gen.c \
	lib_keyname.c \
	names.c \
	unctrl.c

GENHDRS=	\
	curses.h \
	hashsize.h \
	init_keytry.h \
	ncurses_def.h \
	nomacros.h \
	parametrized.h \
	term.h \
	termcap.h \
	unctrl.h

SRCS=	${GENHDRS} ${GENSRCS}

.PATH: ${NCURSES_DIR}/ncurses/base
SRCS+=	\
	define_key.c \
	key_defined.c \
	keybound.c \
	keyok.c \
	legacy_coding.c \
	lib_addch.c \
	lib_addstr.c \
	lib_beep.c \
	lib_bkgd.c \
	lib_box.c \
	lib_chgat.c \
	lib_clear.c \
	lib_clearok.c \
	lib_clrbot.c \
	lib_clreol.c \
	lib_color.c \
	lib_colorset.c \
	lib_delch.c \
	lib_delwin.c \
	lib_dft_fgbg.c \
	lib_echo.c \
	lib_endwin.c \
	lib_erase.c \
	lib_flash.c \
	lib_freeall.c \
	lib_getch.c \
	lib_getstr.c \
	lib_hline.c \
	lib_immedok.c \
	lib_inchstr.c \
	lib_initscr.c \
	lib_insch.c \
	lib_insdel.c \
	lib_insnstr.c \
	lib_instr.c \
	lib_isendwin.c \
	lib_leaveok.c \
	lib_mouse.c \
	lib_move.c \
	lib_mvwin.c \
	lib_newterm.c \
	lib_newwin.c \
	lib_nl.c \
	lib_overlay.c \
	lib_pad.c \
	lib_printw.c \
	lib_redrawln.c \
	lib_refresh.c \
	lib_restart.c \
	lib_scanw.c \
	lib_screen.c \
	lib_scroll.c \
	lib_scrollok.c \
	lib_scrreg.c \
	lib_set_term.c \
	lib_slk.c \
	lib_slkatr_set.c \
	lib_slkatrof.c \
	lib_slkatron.c \
	lib_slkatrset.c \
	lib_slkattr.c \
	lib_slkclear.c \
	lib_slkcolor.c \
	lib_slkinit.c \
	lib_slklab.c \
	lib_slkrefr.c \
	lib_slkset.c \
	lib_slktouch.c \
	lib_touch.c \
	lib_ungetch.c \
	lib_vline.c \
	lib_wattroff.c \
	lib_wattron.c \
	lib_winch.c \
	lib_window.c \
	nc_panel.c \
	resizeterm.c \
	safe_sprintf.c \
	tries.c \
	version.c \
	vsscanf.c \
	wresize.c

.PATH: ${NCURSES_DIR}/ncurses/tinfo
SRCS+=	\
	access.c \
	add_tries.c \
	alloc_entry.c \
	alloc_ttype.c \
	captoinfo.c \
	comp_captab.c \
	comp_error.c \
	comp_expand.c \
	comp_hash.c \
	comp_parse.c \
	comp_scan.c \
	db_iterator.c \
	doalloc.c \
	entries.c \
	free_ttype.c \
	getenv_num.c \
	hashed_db.c \
	home_terminfo.c \
	init_keytry.c \
	lib_acs.c \
	lib_baudrate.c \
	lib_cur_term.c \
	lib_data.c \
	lib_has_cap.c \
	lib_kernel.c \
	lib_longname.c \
	lib_napms.c \
	lib_options.c \
	lib_print.c \
	lib_raw.c \
	lib_setup.c \
	lib_termcap.c \
	lib_termname.c \
	lib_tgoto.c \
	lib_ti.c \
	lib_tparm.c \
	lib_tputs.c \
	lib_ttyflags.c \
	name_match.c \
	obsolete.c \
	parse_entry.c \
	read_entry.c \
	strings.c \
	trim_sgr0.c \
	write_entry.c

.PATH: ${NCURSES_DIR}/ncurses/tty
SRCS+=	\
	hardscroll.c \
	hashmap.c \
	lib_mvcur.c \
	lib_tstp.c \
	lib_twait.c \
	lib_vidattr.c \
	tty_update.c

.if defined(ENABLE_WIDEC)
.PATH: ${NCURSES_DIR}/ncurses/widechar
SRCS+=	\
	charable.c \
	lib_add_wch.c \
	lib_box_set.c \
	lib_cchar.c \
	lib_erasewchar.c \
	lib_get_wch.c \
	lib_get_wstr.c \
	lib_hline_set.c \
	lib_in_wch.c \
	lib_in_wchnstr.c \
	lib_ins_wch.c \
	lib_inwstr.c \
	lib_key_name.c \
	lib_pecho_wchar.c \
	lib_slk_wset.c \
	lib_unget_wch.c \
	lib_vid_attr.c \
	lib_vline_set.c \
	lib_wacs.c \
	lib_wunctrl.c
.endif

.PATH: ${NCURSES_DIR}/ncurses/trace
SRCS+=	\
	lib_trace.c \
	visbuf.c

# Currently unused, for debugging libncurses itself.
DBGSRCS= \
	lib_traceatr.c \
	lib_tracebits.c \
	lib_tracechr.c \
	lib_tracedmp.c \
	lib_tracemse.c \
	trace_buf.c \
	trace_tries.c \
	trace_xnames.c \
	varargs.c

# From our old libtermcap.
# Used instead of the hideous read_termcap.c abomination.
SRCS+=	termcap.c

CLEANFILES=	${GENSRCS} ${GENHDRS} keys.list make_hash term.h.new \
		make_keys MKterm.h.awk comp_captab.c curses.head

CFLAGS+=	-DFREEBSD_NATIVE -DTERMIOS

# Installed
HEADERS=	curses.h term.h termcap.h unctrl.h
SRCHDRS=	ncurses_dll.h
CLEANFILES+=	ncurses_dll.h

.if defined(ENABLE_WIDEC)
INCS=		${HEADERS} ${SRCHDRS}
INCSLINKS=	curses.h ${INCLUDEDIR}/ncurses.h
.endif

.if ${MK_INSTALLLIB} != "no"
SYMLINKS+=	libncurses${LIB_SUFFIX}.a ${LIBDIR}/libcurses${LIB_SUFFIX}.a
SYMLINKS+=	libncurses${LIB_SUFFIX}.a ${LIBDIR}/libtermcap${LIB_SUFFIX}.a
SYMLINKS+=	libncurses${LIB_SUFFIX}.a ${LIBDIR}/libtermlib${LIB_SUFFIX}.a
SYMLINKS+=	libncurses${LIB_SUFFIX}.a ${LIBDIR}/libtinfo${LIB_SUFFIX}.a
.endif
.if !defined(NO_PIC)
# no need for major at all, it's an ld-time redirection only
SYMLINKS+=	libncurses${LIB_SUFFIX}.so ${LIBDIR}/libcurses${LIB_SUFFIX}.so
SYMLINKS+=	libncurses${LIB_SUFFIX}.so ${LIBDIR}/libtermcap${LIB_SUFFIX}.so
SYMLINKS+=	libncurses${LIB_SUFFIX}.so ${LIBDIR}/libtermlib${LIB_SUFFIX}.so
SYMLINKS+=	libncurses${LIB_SUFFIX}.so ${LIBDIR}/libtinfo${LIB_SUFFIX}.so
.endif
.if ${MK_PROFILE} != "no"
SYMLINKS+=	libncurses${LIB_SUFFIX}_p.a ${LIBDIR}/libcurses${LIB_SUFFIX}_p.a
SYMLINKS+=	libncurses${LIB_SUFFIX}_p.a ${LIBDIR}/libtermcap${LIB_SUFFIX}_p.a
SYMLINKS+=	libncurses${LIB_SUFFIX}_p.a ${LIBDIR}/libtermlib${LIB_SUFFIX}_p.a
SYMLINKS+=	libncurses${LIB_SUFFIX}_p.a ${LIBDIR}/libtinfo${LIB_SUFFIX}_p.a
.endif

.if ${.CURDIR:T} == "ncursesw"
.if defined(ENABLE_WIDEC)
DOCSDIR=	${SHAREDIR}/doc/ncurses
DOCS=		ncurses-intro.html hackguide.html
.endif

.if ${MK_HTML} != "no"
.PATH: ${NCURSES_DIR}/doc/html
FILESGROUPS=	DOCS
.endif
.endif

# Generated source
.ORDER: names.c codes.c

names.c: MKnames.awk
	${AWK} -f ${NCURSES_DIR}/ncurses/tinfo/MKnames.awk bigstrings=${USE_BIG_STRINGS} ${NCURSES_DIR}/include/Caps > names.c

codes.c: MKcodes.awk
	${AWK} -f ${NCURSES_DIR}/ncurses/tinfo/MKcodes.awk bigstrings=${USE_BIG_STRINGS} ${NCURSES_DIR}/include/Caps > codes.c

lib_gen.c: MKlib_gen.sh curses.h
	LC_ALL=C sh ${NCURSES_DIR}/ncurses/base/MKlib_gen.sh "${CC:N${CCACHE_BIN}} -E ${CFLAGS}" \
	    "${AWK}" generated < curses.h >$@

lib_keyname.c: keys.list MKkeyname.awk
	${AWK} -f ${NCURSES_DIR}/ncurses/base/MKkeyname.awk bigstrings=${USE_BIG_STRINGS} keys.list > lib_keyname.c

unctrl.c: MKunctrl.awk
	echo | ${AWK} -f ${NCURSES_DIR}/ncurses/base/MKunctrl.awk bigstrings=${USE_BIG_STRINGS} > unctrl.c

comp_captab.c: MKcaptab.sh MKcaptab.awk Caps make_hash
	env PATH=${BTOOLSPATH:U.}:${PATH} sh ${NCURSES_DIR}/ncurses/tinfo/MKcaptab.sh "${AWK}" \
	    ${USE_BIG_STRINGS} ${NCURSES_DIR}/ncurses/tinfo/MKcaptab.awk \
	    ${NCURSES_DIR}/include/Caps > comp_captab.c

expanded.c: MKexpanded.sh
	sh ${NCURSES_DIR}/ncurses/tty/MKexpanded.sh "${CC:N${CCACHE_BIN}} -E" ${CFLAGS} >expanded.c

fallback.c: MKfallback.sh
	sh -e ${NCURSES_DIR}/ncurses/tinfo/MKfallback.sh "" "" "" > ${.TARGET}.tmp && \
	    mv -f ${.TARGET}.tmp ${.TARGET}

# Generated headers
nomacros.h: MKlib_gen.sh curses.h
	LC_ALL=C sh ${NCURSES_DIR}/ncurses/base/MKlib_gen.sh "${CC:N${CCACHE_BIN}} -E ${CFLAGS}" \
	    "${AWK}" generated < curses.h | fgrep undef > $@

init_keytry.h: keys.list make_keys
	${BTOOLSPATH:U.}/make_keys keys.list > init_keytry.h

hashsize.h: MKhashsize.sh Caps
	sh ${NCURSES_DIR}/include/MKhashsize.sh ${NCURSES_DIR}/include/Caps > $@

parametrized.h: MKparametrized.sh Caps
	AWK=${AWK} sh ${NCURSES_DIR}/include/MKparametrized.sh \
	    ${NCURSES_DIR}/include/Caps > $@

term.h: MKterm.h.awk edit_cfg.sh Caps
	${AWK} -f MKterm.h.awk ${NCURSES_DIR}/include/Caps > $@.new
	sh ${NCURSES_DIR}/include/edit_cfg.sh ${NCURSES_CFG_H} $@.new
	mv -f $@.new $@

curses.h: curses.head MKkey_defs.sh Caps
	cat curses.head > $@.new
	AWK=${AWK} _POSIX2_VERSION=199209 sh ${NCURSES_DIR}/include/MKkey_defs.sh \
	    ${NCURSES_DIR}/include/Caps >> $@.new
.if defined(ENABLE_WIDEC)
	cat ${NCURSES_DIR}/include/curses.wide >> $@.new
.endif
	cat ${NCURSES_DIR}/include/curses.tail >> $@.new
	mv -f $@.new $@

# Generated intermediate files
keys.list: MKkeys_list.sh Caps
	AWK=${AWK} sh ${NCURSES_DIR}/ncurses/tinfo/MKkeys_list.sh \
	    ${NCURSES_DIR}/include/Caps | LC_ALL=C sort > keys.list

# Build tools
build-tools: make_hash make_keys

make_keys: make_keys.c names.c ncurses_def.h ${HEADERS} ${BUILD_TOOLS_META}
	${CC:N${CCACHE_BIN}} -o $@ ${CFLAGS} ${NCURSES_DIR}/ncurses/tinfo/make_keys.c

make_hash: make_hash.c hashsize.h ncurses_def.h ${HEADERS} ${BUILD_TOOLS_META}
	${CC:N${CCACHE_BIN}} -o $@ ${CFLAGS} -DMAIN_PROGRAM \
		${NCURSES_DIR}/ncurses/tinfo/make_hash.c

# ./configure generated
MKterm.h.awk: MKterm.h.awk.in
	sed <${NCURSES_DIR}/include/MKterm.h.awk.in >$@ \
	    -e "/@BROKEN_LINKER@/s%%${BROKEN_LINKER}%" \
	    -e "s%@NCURSES_USE_DATABASE@%0%g" \
	    -e "s%@NCURSES_USE_TERMCAP@%1%g" \
	    -e "/@NCURSES_MAJOR@/s%%${NCURSES_MAJOR}%" \
	    -e "/@NCURSES_MINOR@/s%%${NCURSES_MINOR}%" \
	    -e "/@NCURSES_CONST@/s%%${NCURSES_CONST}%" \
	    -e "/@NCURSES_TPARM_VARARGS@/s%%${NCURSES_TPARM_VARARGS}%" \
	    -e "/@NCURSES_SBOOL@/s%%${NCURSES_SBOOL}%" \
	    -e "/@NCURSES_XNAMES@/s%%${NCURSES_XNAMES}%" \
	    -e "/@HAVE_TERMIOS_H@/s%%${HAVE_TERMIOS_H}%" \
	    -e "/@HAVE_TERMIO_H@/s%%${HAVE_TERMIO_H}%" \
	    -e "/@HAVE_TCGETATTR@/s%%${HAVE_TCGETATTR}%" \
	    -e "s%@cf_cv_enable_reentrant@%${ENABLE_REENTRANT}%g"

termcap.h: termcap.h.in
	sed <${NCURSES_DIR}/include/termcap.h.in >$@ \
	    -e "/@NCURSES_MAJOR@/s%%${NCURSES_MAJOR}%" \
	    -e "/@NCURSES_MINOR@/s%%${NCURSES_MINOR}%" \
	    -e "/@NCURSES_CONST@/s%%${NCURSES_CONST}%" \
	    -e "/@NCURSES_OSPEED@/s%%${NCURSES_OSPEED}%"

ncurses_dll.h: ncurses_dll.h.in
	sed <${NCURSES_DIR}/include/ncurses_dll.h.in >$@ \
	    -e "s%@NCURSES_WRAP_PREFIX@%_nc_%g"

curses.head: curses.h.in
	sed <${NCURSES_DIR}/include/curses.h.in >$@ \
	    -e "/@BROKEN_LINKER@/s%%${BROKEN_LINKER}%" \
	    -e "/@HAVE_VSSCANF@/s%%${HAVE_VSSCANF}%" \
	    -e "/@NCURSES_CH_T@/s%%${NCURSES_CH_T}%" \
	    -e "/@NCURSES_CONST@/s%%${NCURSES_CONST}%" \
	    -e "/@NCURSES_EXT_COLORS@/s%%${NCURSES_EXT_COLORS}%" \
	    -e "/@NCURSES_EXT_FUNCS@/s%%${NCURSES_EXT_FUNCS}%" \
	    -e "/@NCURSES_INLINE@/s%%${NCURSES_INLINE}%" \
	    -e "/@NCURSES_LIBUTF8@/s%%${NCURSES_LIBUTF8}%" \
	    -e "/@NCURSES_MAJOR@/s%%${NCURSES_MAJOR}%" \
	    -e "/@NCURSES_MBSTATE_T@/s%%${NCURSES_MBSTATE_T}%" \
	    -e "/@NCURSES_MINOR@/s%%${NCURSES_MINOR}%" \
	    -e "/@NCURSES_MOUSE_VERSION@/s%%${NCURSES_MOUSE_VERSION}%" \
	    -e "/@NCURSES_OK_WCHAR_T@/s%%${NCURSES_OK_WCHAR_T}%" \
	    -e "/@NCURSES_OPAQUE@/s%%${NCURSES_OPAQUE}%" \
	    -e "/@NCURSES_PATCH@/s%%${NCURSES_PATCH}%" \
	    -e "/@NCURSES_SIZE_T@/s%%${NCURSES_SIZE_T}%" \
	    -e "/@NCURSES_TPARM_VARARGS@/s%%${NCURSES_TPARM_VARARGS}%" \
	    -e "/@NCURSES_WCHAR_T@/s%%${NCURSES_WCHAR_T}%" \
	    -e "/@NCURSES_WCHAR_T@/s%%${NCURSES_WCHAR_T}%" \
	    -e "/@NCURSES_WINT_T@/s%%${NCURSES_WINT_T}%" \
	    -e "/@NEED_WCHAR_H@/s%%${NEED_WCHAR_H}%" \
	    -e "/@USE_CXX_BOOL@/s%%${USE_CXX_BOOL}%" \
	    -e "s%@NCURSES_SP_FUNCS@%0%g" \
	    -e "s%@NCURSES_INTEROP_FUNCS@%0%g" \
	    -e "s%@NCURSES_CCHARW_MAX@%5%g" \
	    -e "s%@NCURSES_TPARM_ARG@%long%g" \
	    -e "s%@cf_cv_1UL@%${ONEUL}%g" \
	    -e "s%@cf_cv_builtin_bool@%${BUILTIN_BOOL}%g" \
	    -e "s%@cf_cv_enable_lp64@%${ENABLE_LP64}%g" \
	    -e "s%@cf_cv_enable_opaque@%${ENABLE_OPAQUE}%g" \
	    -e "s%@cf_cv_enable_reentrant@%${ENABLE_REENTRANT}%g" \
	    -e "s%@cf_cv_header_stdbool_h@%${HEADER_STDBOOL}%g" \
	    -e "s%@cf_cv_type_of_bool@%${TYPE_OF_BOOL}%g" \
	    -e "s%@cf_cv_typeof_chtype@%${TYPEOF_CHTYPE}%g" \
	    -e "s%@cf_cv_typeof_mmask_t@%${TYPEOF_MMASK_T}%g" \
	    -e "s/ _WCHAR_T/ __wchar_t/g" \
	    -e "s/ _WINT_T/ __wint_t/g"

unctrl.h: unctrl.h.in
	sed <${NCURSES_DIR}/include/$@.in >$@ \
	    -e "s%@NCURSES_SP_FUNCS@%0%g" \
	    -e "/@NCURSES_MAJOR@/s%%${NCURSES_MAJOR}%" \
	    -e "/@NCURSES_MINOR@/s%%${NCURSES_MINOR}%"

# MAN page gunk
terminfo.5: MKterminfo.sh terminfo.head Caps
	sh ${NCURSES_DIR}/man/MKterminfo.sh ${NCURSES_DIR}/man/terminfo.head \
	    ${NCURSES_DIR}/include/Caps ${NCURSES_DIR}/man/terminfo.tail >$@

CLEANFILES+=	terminfo.5

.if defined(ENABLE_WIDEC)
.PATH: ${NCURSES_DIR}/man
MAN=	\
	curs_addch.3 \
	curs_addchstr.3 \
	curs_addstr.3 \
	curs_attr.3 \
	curs_beep.3 \
	curs_bkgd.3 \
	curs_bkgrnd.3 \
	curs_border.3 \
	curs_border_set.3 \
	curs_clear.3 \
	curs_color.3 \
	curs_delch.3 \
	curs_deleteln.3 \
	curs_extend.3 \
	curs_getcchar.3 \
	curs_getch.3 \
	curs_getstr.3 \
	curs_getyx.3 \
	curs_inch.3 \
	curs_inchstr.3 \
	curs_initscr.3 \
	curs_inopts.3 \
	curs_insch.3 \
	curs_insstr.3 \
	curs_instr.3 \
	curs_inwstr.3 \
	curs_kernel.3 \
	curs_legacy.3 \
	curs_memleaks.3 \
	curs_mouse.3 \
	curs_move.3 \
	curs_opaque.3 \
	curs_outopts.3 \
	curs_overlay.3 \
	curs_pad.3 \
	curs_print.3 \
	curs_refresh.3 \
	curs_scr_dump.3 \
	curs_scroll.3 \
	curs_slk.3 \
	curs_sp_funcs.3 \
	curs_termattrs.3 \
	curs_termcap.3 \
	curs_terminfo.3 \
	curs_threads.3 \
	curs_touch.3 \
	curs_trace.3 \
	curs_util.3 \
	curs_variables.3 \
	curs_window.3 \
	default_colors.3 \
	define_key.3 \
	key_defined.3 \
	keybound.3 \
	keyok.3 \
	legacy_coding.3 \
	ncurses.3 \
	resizeterm.3 \
	term_variables.3 \
	wresize.3

MAN+=	\
	curs_add_wch.3 \
	curs_add_wchstr.3 \
	curs_addwstr.3 \
	curs_get_wch.3 \
	curs_get_wstr.3 \
	curs_in_wch.3 \
	curs_in_wchstr.3 \
	curs_ins_wch.3 \
	curs_ins_wstr.3 \
	curs_printw.3 \
	curs_scanw.3

MAN+=	term.5 terminfo.5
MAN+=	term.7
.endif

CLEANFILES+=	${MAN:M*.3}

MLINKS=	ncurses.3 curses.3 \
	curs_addch.3 addch.3 \
	curs_addch.3 echochar.3 \
	curs_addch.3 mvaddch.3 \
	curs_addch.3 mvwaddch.3 \
	curs_addch.3 waddch.3 \
	curs_addch.3 wechochar.3 \
	curs_addchstr.3 addchnstr.3 \
	curs_addchstr.3 addchstr.3 \
	curs_addchstr.3 mvaddchnstr.3 \
	curs_addchstr.3 mvaddchstr.3 \
	curs_addchstr.3 mvwaddchnstr.3 \
	curs_addchstr.3 mvwaddchstr.3 \
	curs_addchstr.3 waddchnstr.3 \
	curs_addchstr.3 waddchstr.3 \
	curs_addstr.3 addnstr.3 \
	curs_addstr.3 addstr.3 \
	curs_addstr.3 mvaddnstr.3 \
	curs_addstr.3 mvaddstr.3 \
	curs_addstr.3 mvwaddnstr.3 \
	curs_addstr.3 mvwaddstr.3 \
	curs_addstr.3 waddnstr.3 \
	curs_addstr.3 waddstr.3 \
	curs_attr.3 PAIR_NUMBER.3 \
	curs_attr.3 attr_get.3 \
	curs_attr.3 attr_off.3 \
	curs_attr.3 attr_on.3 \
	curs_attr.3 attr_set.3 \
	curs_attr.3 attroff.3 \
	curs_attr.3 attron.3 \
	curs_attr.3 attrset.3 \
	curs_attr.3 chgat.3 \
	curs_attr.3 color_set.3 \
	curs_attr.3 mvchgat.3 \
	curs_attr.3 mvwchgat.3 \
	curs_attr.3 standend.3 \
	curs_attr.3 standout.3 \
	curs_attr.3 wattr_get.3 \
	curs_attr.3 wattr_off.3 \
	curs_attr.3 wattr_on.3 \
	curs_attr.3 wattr_set.3 \
	curs_attr.3 wattroff.3 \
	curs_attr.3 wattron.3 \
	curs_attr.3 wattrset.3 \
	curs_attr.3 wchgat.3 \
	curs_attr.3 wcolor_set.3 \
	curs_attr.3 wstandend.3 \
	curs_attr.3 wstandout.3 \
	curs_beep.3 beep.3 \
	curs_beep.3 flash.3 \
	curs_bkgd.3 bkgd.3 \
	curs_bkgd.3 bkgdset.3 \
	curs_bkgd.3 getbkgd.3 \
	curs_bkgd.3 wbkgd.3 \
	curs_bkgd.3 wbkgdset.3 \
	curs_bkgrnd.3 bkgrnd.3 \
	curs_bkgrnd.3 bkgrndset.3 \
	curs_bkgrnd.3 getbkgrnd.3 \
	curs_bkgrnd.3 wbkgrnd.3 \
	curs_bkgrnd.3 wbkgrndset.3 \
	curs_bkgrnd.3 wgetbkgrnd.3 \
	curs_border.3 border.3 \
	curs_border.3 box.3 \
	curs_border.3 hline.3 \
	curs_border.3 mvhline.3 \
	curs_border.3 mvvline.3 \
	curs_border.3 mvwhline.3 \
	curs_border.3 mvwvline.3 \
	curs_border.3 vline.3 \
	curs_border.3 wborder.3 \
	curs_border.3 whline.3 \
	curs_border.3 wvline.3 \
	curs_border_set.3 border_set.3 \
	curs_border_set.3 box_set.3 \
	curs_border_set.3 hline_set.3 \
	curs_border_set.3 mvhline_set.3 \
	curs_border_set.3 mvvline_set.3 \
	curs_border_set.3 mvwhline_set.3 \
	curs_border_set.3 mvwvline_set.3 \
	curs_border_set.3 vline_set.3 \
	curs_border_set.3 wborder_set.3 \
	curs_border_set.3 whline_set.3 \
	curs_border_set.3 wvline_set.3 \
	curs_clear.3 clear.3 \
	curs_clear.3 clrtobot.3 \
	curs_clear.3 clrtoeol.3 \
	curs_clear.3 erase.3 \
	curs_clear.3 wclear.3 \
	curs_clear.3 wclrtobot.3 \
	curs_clear.3 wclrtoeol.3 \
	curs_clear.3 werase.3 \
	curs_color.3 COLOR_PAIR.3 \
	curs_color.3 can_change_color.3 \
	curs_color.3 color_content.3 \
	curs_color.3 has_colors.3 \
	curs_color.3 init_color.3 \
	curs_color.3 init_pair.3 \
	curs_color.3 pair_content.3 \
	curs_color.3 start_color.3 \
	curs_delch.3 delch.3 \
	curs_delch.3 mvdelch.3 \
	curs_delch.3 mvwdelch.3 \
	curs_delch.3 wdelch.3 \
	curs_deleteln.3 deleteln.3 \
	curs_deleteln.3 insdelln.3 \
	curs_deleteln.3 insertln.3 \
	curs_deleteln.3 wdeleteln.3 \
	curs_deleteln.3 winsdelln.3 \
	curs_deleteln.3 winsertln.3 \
	curs_extend.3 curses_version.3 \
	curs_extend.3 use_extended_names.3 \
	curs_getcchar.3 getcchar.3 \
	curs_getcchar.3 setcchar.3 \
	curs_getch.3 getch.3 \
	curs_getch.3 has_key.3 \
	curs_getch.3 mvgetch.3 \
	curs_getch.3 mvwgetch.3 \
	curs_getch.3 ungetch.3 \
	curs_getch.3 wgetch.3 \
	curs_getstr.3 getnstr.3 \
	curs_getstr.3 getstr.3 \
	curs_getstr.3 mvgetnstr.3 \
	curs_getstr.3 mvgetstr.3 \
	curs_getstr.3 mvwgetnstr.3 \
	curs_getstr.3 mvwgetstr.3 \
	curs_getstr.3 wgetnstr.3 \
	curs_getstr.3 wgetstr.3 \
	curs_getyx.3 getbegyx.3 \
	curs_getyx.3 getmaxyx.3 \
	curs_getyx.3 getparyx.3 \
	curs_getyx.3 getyx.3 \
	curs_inch.3 inch.3 \
	curs_inch.3 mvinch.3 \
	curs_inch.3 mvwinch.3 \
	curs_inch.3 winch.3 \
	curs_inchstr.3 inchnstr.3 \
	curs_inchstr.3 inchstr.3 \
	curs_inchstr.3 mvinchnstr.3 \
	curs_inchstr.3 mvinchstr.3 \
	curs_inchstr.3 mvwinchnstr.3 \
	curs_inchstr.3 mvwinchstr.3 \
	curs_inchstr.3 winchnstr.3 \
	curs_inchstr.3 winchstr.3 \
	curs_initscr.3 delscreen.3 \
	curs_initscr.3 endwin.3 \
	curs_initscr.3 initscr.3 \
	curs_initscr.3 isendwin.3 \
	curs_initscr.3 newterm.3 \
	curs_initscr.3 set_term.3 \
	curs_inopts.3 cbreak.3 \
	curs_inopts.3 echo.3 \
	curs_inopts.3 halfdelay.3 \
	curs_inopts.3 intrflush.3 \
	curs_inopts.3 keypad.3 \
	curs_inopts.3 meta.3 \
	curs_inopts.3 nocbreak.3 \
	curs_inopts.3 nodelay.3 \
	curs_inopts.3 noecho.3 \
	curs_inopts.3 noqiflush.3 \
	curs_inopts.3 noraw.3 \
	curs_inopts.3 notimeout.3 \
	curs_inopts.3 qiflush.3 \
	curs_inopts.3 raw.3 \
	curs_inopts.3 timeout.3 \
	curs_inopts.3 typeahead.3 \
	curs_inopts.3 wtimeout.3 \
	curs_insch.3 insch.3 \
	curs_insch.3 mvinsch.3 \
	curs_insch.3 mvwinsch.3 \
	curs_insch.3 winsch.3 \
	curs_insstr.3 insnstr.3 \
	curs_insstr.3 insstr.3 \
	curs_insstr.3 mvinsnstr.3 \
	curs_insstr.3 mvinsstr.3 \
	curs_insstr.3 mvwinsnstr.3 \
	curs_insstr.3 mvwinsstr.3 \
	curs_insstr.3 winsnstr.3 \
	curs_insstr.3 winsstr.3 \
	curs_instr.3 innstr.3 \
	curs_instr.3 instr.3 \
	curs_instr.3 mvinnstr.3 \
	curs_instr.3 mvinstr.3 \
	curs_instr.3 mvwinnstr.3 \
	curs_instr.3 mvwinstr.3 \
	curs_instr.3 winnstr.3 \
	curs_instr.3 winstr.3 \
	curs_kernel.3 curs_set.3 \
	curs_kernel.3 def_prog_mode.3 \
	curs_kernel.3 def_shell_mode.3 \
	curs_kernel.3 getsyx.3 \
	curs_kernel.3 napms.3 \
	curs_kernel.3 reset_prog_mode.3 \
	curs_kernel.3 reset_shell_mode.3 \
	curs_kernel.3 resetty.3 \
	curs_kernel.3 ripoffline.3 \
	curs_kernel.3 savetty.3 \
	curs_kernel.3 setsyx.3 \
	curs_legacy.3 getbegx.3 \
	curs_legacy.3 getbegy.3 \
	curs_legacy.3 getcurx.3 \
	curs_legacy.3 getcury.3 \
	curs_legacy.3 getmaxx.3 \
	curs_legacy.3 getmaxy.3 \
	curs_legacy.3 getparx.3 \
	curs_legacy.3 getpary.3 \
	curs_memleaks.3 _nc_free_and_exit.3 \
	curs_memleaks.3 _nc_freeall.3 \
	curs_mouse.3 getmouse.3 \
	curs_mouse.3 mouse_trafo.3 \
	curs_mouse.3 mouseinterval.3 \
	curs_mouse.3 mousemask.3 \
	curs_mouse.3 ungetmouse.3 \
	curs_mouse.3 wenclose.3 \
	curs_mouse.3 wmouse_trafo.3 \
	curs_move.3 move.3 \
	curs_move.3 wmove.3 \
	curs_opaque.3 is_cleared.3 \
	curs_opaque.3 is_idcok.3 \
	curs_opaque.3 is_idlok.3 \
	curs_opaque.3 is_immedok.3 \
	curs_opaque.3 is_keypad.3 \
	curs_opaque.3 is_leaveok.3 \
	curs_opaque.3 is_nodelay.3 \
	curs_opaque.3 is_notimeout.3 \
	curs_opaque.3 is_scrollok.3 \
	curs_opaque.3 is_syncok.3 \
	curs_opaque.3 is_timeout.3 \
	curs_opaque.3 wgetparent.3 \
	curs_opaque.3 wgetscrreg.3 \
	curs_outopts.3 clearok.3 \
	curs_outopts.3 idcok.3 \
	curs_outopts.3 idlok.3 \
	curs_outopts.3 immedok.3 \
	curs_outopts.3 leaveok.3 \
	curs_outopts.3 nl.3 \
	curs_outopts.3 nonl.3 \
	curs_outopts.3 scrollok.3 \
	curs_outopts.3 setscrreg.3 \
	curs_outopts.3 wsetscrreg.3 \
	curs_overlay.3 copywin.3 \
	curs_overlay.3 overlay.3 \
	curs_overlay.3 overwrite.3 \
	curs_pad.3 newpad.3 \
	curs_pad.3 pecho_wchar.3 \
	curs_pad.3 pechochar.3 \
	curs_pad.3 pnoutrefresh.3 \
	curs_pad.3 prefresh.3 \
	curs_pad.3 subpad.3 \
	curs_print.3 mcprint.3 \
	curs_refresh.3 doupdate.3 \
	curs_refresh.3 redrawwin.3 \
	curs_refresh.3 refresh.3 \
	curs_refresh.3 wnoutrefresh.3 \
	curs_refresh.3 wredrawln.3 \
	curs_refresh.3 wrefresh.3 \
	curs_scr_dump.3 scr_dump.3 \
	curs_scr_dump.3 scr_init.3 \
	curs_scr_dump.3 scr_restore.3 \
	curs_scr_dump.3 scr_set.3 \
	curs_scroll.3 scrl.3 \
	curs_scroll.3 scroll.3 \
	curs_scroll.3 wscrl.3 \
	curs_slk.3 slk_attr.3 \
	curs_slk.3 slk_attr_off.3 \
	curs_slk.3 slk_attr_on.3 \
	curs_slk.3 slk_attr_set.3 \
	curs_slk.3 slk_attroff.3 \
	curs_slk.3 slk_attron.3 \
	curs_slk.3 slk_attrset.3 \
	curs_slk.3 slk_clear.3 \
	curs_slk.3 slk_color.3 \
	curs_slk.3 slk_init.3 \
	curs_slk.3 slk_label.3 \
	curs_slk.3 slk_noutrefresh.3 \
	curs_slk.3 slk_refresh.3 \
	curs_slk.3 slk_restore.3 \
	curs_slk.3 slk_set.3 \
	curs_slk.3 slk_touch.3 \
	curs_sp_funcs.3 sp_funcs.3 \
	curs_sp_funcs.3 assume_default_colors_sp.3 \
	curs_sp_funcs.3 baudrate_sp.3 \
	curs_sp_funcs.3 beep_sp.3 \
	curs_sp_funcs.3 can_change_color_sp.3 \
	curs_sp_funcs.3 cbreak_sp.3 \
	curs_sp_funcs.3 ceiling_panel.3 \
	curs_sp_funcs.3 color_content_sp.3 \
	curs_sp_funcs.3 curs_set_sp.3 \
	curs_sp_funcs.3 def_prog_mode_sp.3 \
	curs_sp_funcs.3 def_shell_mode_sp.3 \
	curs_sp_funcs.3 define_key_sp.3 \
	curs_sp_funcs.3 del_curterm_sp.3 \
	curs_sp_funcs.3 delay_output_sp.3 \
	curs_sp_funcs.3 doupdate_sp.3 \
	curs_sp_funcs.3 echo_sp.3 \
	curs_sp_funcs.3 endwin_sp.3 \
	curs_sp_funcs.3 erasechar_sp.3 \
	curs_sp_funcs.3 filter_sp.3 \
	curs_sp_funcs.3 flash_sp.3 \
	curs_sp_funcs.3 flushinp_sp.3 \
	curs_sp_funcs.3 get_escdelay_sp.3 \
	curs_sp_funcs.3 getmouse_sp.3 \
	curs_sp_funcs.3 getwin_sp.3 \
	curs_sp_funcs.3 ground_panel.3 \
	curs_sp_funcs.3 halfdelay_sp.3 \
	curs_sp_funcs.3 has_colors_sp.3 \
	curs_sp_funcs.3 has_ic_sp.3 \
	curs_sp_funcs.3 has_il_sp.3 \
	curs_sp_funcs.3 has_key_sp.3 \
	curs_sp_funcs.3 has_mouse_sp.3 \
	curs_sp_funcs.3 init_color_sp.3 \
	curs_sp_funcs.3 init_pair_sp.3 \
	curs_sp_funcs.3 intrflush_sp.3 \
	curs_sp_funcs.3 is_term_resized_sp.3 \
	curs_sp_funcs.3 isendwin_sp.3 \
	curs_sp_funcs.3 key_defined_sp.3 \
	curs_sp_funcs.3 keybound_sp.3 \
	curs_sp_funcs.3 keyname_sp.3 \
	curs_sp_funcs.3 keyok_sp.3 \
	curs_sp_funcs.3 killchar_sp.3 \
	curs_sp_funcs.3 mcprint_sp.3 \
	curs_sp_funcs.3 mouseinterval_sp.3 \
	curs_sp_funcs.3 mousemask_sp.3 \
	curs_sp_funcs.3 mvcur_sp.3 \
	curs_sp_funcs.3 napms_sp.3 \
	curs_sp_funcs.3 new_form_sp.3 \
	curs_sp_funcs.3 new_menu_sp.3 \
	curs_sp_funcs.3 new_prescr.3 \
	curs_sp_funcs.3 newpad_sp.3 \
	curs_sp_funcs.3 newterm_sp.3 \
	curs_sp_funcs.3 newwin_sp.3 \
	curs_sp_funcs.3 nl_sp.3 \
	curs_sp_funcs.3 nocbreak_sp.3 \
	curs_sp_funcs.3 noecho_sp.3 \
	curs_sp_funcs.3 nofilter_sp.3 \
	curs_sp_funcs.3 nonl_sp.3 \
	curs_sp_funcs.3 noqiflush_sp.3 \
	curs_sp_funcs.3 noraw_sp.3 \
	curs_sp_funcs.3 pair_content_sp.3 \
	curs_sp_funcs.3 putp_sp.3 \
	curs_sp_funcs.3 qiflush_sp.3 \
	curs_sp_funcs.3 raw_sp.3 \
	curs_sp_funcs.3 reset_prog_mode_sp.3 \
	curs_sp_funcs.3 reset_shell_mode_sp.3 \
	curs_sp_funcs.3 resetty_sp.3 \
	curs_sp_funcs.3 resize_term_sp.3 \
	curs_sp_funcs.3 resizeterm_sp.3 \
	curs_sp_funcs.3 restartterm_sp.3 \
	curs_sp_funcs.3 ripoffline_sp.3 \
	curs_sp_funcs.3 savetty_sp.3 \
	curs_sp_funcs.3 scr_init_sp.3 \
	curs_sp_funcs.3 scr_restore_sp.3 \
	curs_sp_funcs.3 scr_set_sp.3 \
	curs_sp_funcs.3 set_curterm_sp.3 \
	curs_sp_funcs.3 set_escdelay_sp.3 \
	curs_sp_funcs.3 set_tabsize_sp.3 \
	curs_sp_funcs.3 slk_attr_set_sp.3 \
	curs_sp_funcs.3 slk_attr_sp.3 \
	curs_sp_funcs.3 slk_attroff_sp.3 \
	curs_sp_funcs.3 slk_attron_sp.3 \
	curs_sp_funcs.3 slk_attrset_sp.3 \
	curs_sp_funcs.3 slk_clear_sp.3 \
	curs_sp_funcs.3 slk_color_sp.3 \
	curs_sp_funcs.3 slk_init_sp.3 \
	curs_sp_funcs.3 slk_label_sp.3 \
	curs_sp_funcs.3 slk_noutrefresh_sp.3 \
	curs_sp_funcs.3 slk_refresh_sp.3 \
	curs_sp_funcs.3 slk_restore_sp.3 \
	curs_sp_funcs.3 slk_set_sp.3 \
	curs_sp_funcs.3 slk_touch_sp.3 \
	curs_sp_funcs.3 start_color_sp.3 \
	curs_sp_funcs.3 term_attrs_sp.3 \
	curs_sp_funcs.3 termattrs_sp.3 \
	curs_sp_funcs.3 termname_sp.3 \
	curs_sp_funcs.3 tgetent_sp.3 \
	curs_sp_funcs.3 tgetflag_sp.3 \
	curs_sp_funcs.3 tgetnum_sp.3 \
	curs_sp_funcs.3 tgetstr_sp.3 \
	curs_sp_funcs.3 tigetflag_sp.3 \
	curs_sp_funcs.3 tigetnum_sp.3 \
	curs_sp_funcs.3 tigetstr_sp.3 \
	curs_sp_funcs.3 tputs_sp.3 \
	curs_sp_funcs.3 typeahead_sp.3 \
	curs_sp_funcs.3 unctrl_sp.3 \
	curs_sp_funcs.3 unget_wch_sp.3 \
	curs_sp_funcs.3 ungetch_sp.3 \
	curs_sp_funcs.3 ungetmouse_sp.3 \
	curs_sp_funcs.3 update_panels_sp.3 \
	curs_sp_funcs.3 use_default_colors_sp.3 \
	curs_sp_funcs.3 use_env_sp.3 \
	curs_sp_funcs.3 use_legacy_coding_sp.3 \
	curs_sp_funcs.3 vid_attr_sp.3 \
	curs_sp_funcs.3 vid_puts_sp.3 \
	curs_sp_funcs.3 vidattr_sp.3 \
	curs_sp_funcs.3 vidputs_sp.3 \
	curs_sp_funcs.3 wunctrl_sp.3 \
	curs_termattrs.3 baudrate.3 \
	curs_termattrs.3 erasechar.3 \
	curs_termattrs.3 erasewchar.3 \
	curs_termattrs.3 has_ic.3 \
	curs_termattrs.3 has_il.3 \
	curs_termattrs.3 killchar.3 \
	curs_termattrs.3 killwchar.3 \
	curs_termattrs.3 longname.3 \
	curs_termattrs.3 term_attrs.3 \
	curs_termattrs.3 termattrs.3 \
	curs_termattrs.3 termname.3 \
	curs_termcap.3 termcap.3 \
	curs_termcap.3 tgetent.3 \
	curs_termcap.3 tgetflag.3 \
	curs_termcap.3 tgetnum.3 \
	curs_termcap.3 tgetstr.3 \
	curs_termcap.3 tgoto.3 \
	curs_termcap.3 tputs.3 \
	curs_terminfo.3 del_curterm.3 \
	curs_terminfo.3 mvcur.3 \
	curs_terminfo.3 putp.3 \
	curs_terminfo.3 restartterm.3 \
	curs_terminfo.3 set_curterm.3 \
	curs_terminfo.3 setterm.3 \
	curs_terminfo.3 setupterm.3 \
	curs_terminfo.3 tigetflag.3 \
	curs_terminfo.3 tigetnum.3 \
	curs_terminfo.3 tigetstr.3 \
	curs_terminfo.3 tparm.3 \
	curs_terminfo.3 vid_attr.3 \
	curs_terminfo.3 vid_puts.3 \
	curs_terminfo.3 vidattr.3 \
	curs_terminfo.3 vidputs.3 \
	curs_threads.3 set_escdelay.3 \
	curs_threads.3 set_tabsize.3 \
	curs_threads.3 use_screen.3 \
	curs_threads.3 use_window.3 \
	curs_touch.3 is_linetouched.3 \
	curs_touch.3 is_wintouched.3 \
	curs_touch.3 touchline.3 \
	curs_touch.3 touchwin.3 \
	curs_touch.3 untouchwin.3 \
	curs_touch.3 wtouchln.3 \
	curs_trace.3 _nc_tracebits.3 \
	curs_trace.3 _traceattr.3 \
	curs_trace.3 _traceattr2.3 \
	curs_trace.3 _tracechar.3 \
	curs_trace.3 _tracechtype.3 \
	curs_trace.3 _tracechtype2.3 \
	curs_trace.3 _tracedump.3 \
	curs_trace.3 _tracef.3 \
	curs_trace.3 _tracemouse.3 \
	curs_trace.3 trace.3 \
	curs_util.3 delay_output.3 \
	curs_util.3 filter.3 \
	curs_util.3 flushinp.3 \
	curs_util.3 getwin.3 \
	curs_util.3 key_name.3 \
	curs_util.3 keyname.3 \
	curs_util.3 nofilter.3 \
	curs_util.3 putwin.3 \
	curs_util.3 unctrl.3 \
	curs_util.3 use_env.3 \
	curs_util.3 wunctrl.3 \
	curs_window.3 delwin.3 \
	curs_window.3 derwin.3 \
	curs_window.3 dupwin.3 \
	curs_window.3 mvderwin.3 \
	curs_window.3 mvwin.3 \
	curs_window.3 newwin.3 \
	curs_window.3 subwin.3 \
	curs_window.3 syncok.3 \
	curs_window.3 wcursyncup.3 \
	curs_window.3 wsyncdown.3 \
	curs_window.3 wsyncup.3 \
	default_colors.3 assume_default_colors.3 \
	default_colors.3 use_default_colors.3 \
	legacy_coding.3 use_legacy_coding.3 \
	resizeterm.3 is_term_resized.3 \
	resizeterm.3 resize_term.3

.if defined(ENABLE_WIDEC)
MLINKS+=curs_add_wch.3 add_wch.3 \
	curs_add_wch.3 echo_wchar.3 \
	curs_add_wch.3 mvadd_wch.3 \
	curs_add_wch.3 mvwadd_wch.3 \
	curs_add_wch.3 wadd_wch.3 \
	curs_add_wch.3 wecho_wchar.3 \
	curs_add_wchstr.3 add_wchnstr.3 \
	curs_add_wchstr.3 add_wchstr.3 \
	curs_add_wchstr.3 mvadd_wchnstr.3 \
	curs_add_wchstr.3 mvadd_wchstr.3 \
	curs_add_wchstr.3 mvwadd_wchnstr.3 \
	curs_add_wchstr.3 mvwadd_wchstr.3 \
	curs_add_wchstr.3 wadd_wchnstr.3 \
	curs_add_wchstr.3 wadd_wchstr.3 \
	curs_addwstr.3 addnwstr.3 \
	curs_addwstr.3 addwstr.3 \
	curs_addwstr.3 mvaddnwstr.3 \
	curs_addwstr.3 mvaddwstr.3 \
	curs_addwstr.3 mvwaddnwstr.3 \
	curs_addwstr.3 mvwaddwstr.3 \
	curs_addwstr.3 waddnwstr.3 \
	curs_addwstr.3 waddwstr.3 \
	curs_get_wch.3 get_wch.3 \
	curs_get_wch.3 mvget_wch.3 \
	curs_get_wch.3 mvwget_wch.3 \
	curs_get_wch.3 unget_wch.3 \
	curs_get_wch.3 wget_wch.3 \
	curs_get_wstr.3 get_wstr.3 \
	curs_get_wstr.3 getn_wstr.3 \
	curs_get_wstr.3 mvget_wstr.3 \
	curs_get_wstr.3 mvgetn_wstr.3 \
	curs_get_wstr.3 mvwget_wstr.3 \
	curs_get_wstr.3 mvwgetn_wstr.3 \
	curs_get_wstr.3 wget_wstr.3 \
	curs_get_wstr.3 wgetn_wstr.3 \
	curs_in_wch.3 in_wch.3 \
	curs_in_wch.3 mvin_wch.3 \
	curs_in_wch.3 mvwin_wch.3 \
	curs_in_wch.3 win_wch.3 \
	curs_in_wchstr.3 in_wchnstr.3 \
	curs_in_wchstr.3 in_wchstr.3 \
	curs_in_wchstr.3 mvin_wchnstr.3 \
	curs_in_wchstr.3 mvin_wchstr.3 \
	curs_in_wchstr.3 mvwin_wchnstr.3 \
	curs_in_wchstr.3 mvwin_wchstr.3 \
	curs_in_wchstr.3 win_wchnstr.3 \
	curs_in_wchstr.3 win_wchstr.3 \
	curs_ins_wch.3 ins_wch.3 \
	curs_ins_wch.3 mvins_wch.3 \
	curs_ins_wch.3 mvwins_wch.3 \
	curs_ins_wch.3 wins_wch.3 \
	curs_ins_wstr.3 ins_nwstr.3 \
	curs_ins_wstr.3 ins_wstr.3 \
	curs_ins_wstr.3 mvins_nwstr.3 \
	curs_ins_wstr.3 mvins_wstr.3 \
	curs_ins_wstr.3 mvwins_nwstr.3 \
	curs_ins_wstr.3 mvwins_wstr.3 \
	curs_ins_wstr.3 wins_nwstr.3 \
	curs_ins_wstr.3 wins_wstr.3 \
	curs_inwstr.3 innwstr.3 \
	curs_inwstr.3 inwstr.3 \
	curs_inwstr.3 mvinnwstr.3 \
	curs_inwstr.3 mvinwstr.3 \
	curs_inwstr.3 mvwinnwstr.3 \
	curs_inwstr.3 mvwinwstr.3 \
	curs_inwstr.3 winnwstr.3 \
	curs_inwstr.3 winwstr.3 \
	curs_printw.3 mvprintw.3 \
	curs_printw.3 mvwprintw.3 \
	curs_printw.3 printw.3 \
	curs_printw.3 vw_printw.3 \
	curs_printw.3 vwprintw.3 \
	curs_printw.3 wprintw.3 \
	curs_scanw.3 mvscanw.3 \
	curs_scanw.3 mvwscanw.3 \
	curs_scanw.3 scanw.3 \
	curs_scanw.3 vw_scanw.3 \
	curs_scanw.3 vwscanw.3 \
	curs_scanw.3 wscanw.3
.endif


.include <bsd.lib.mk>

# Keep the .SUFFIXES line after the include of bsd.lib.mk
.SUFFIXES: .3 .3x
.3x.3:
	cat ${.IMPSRC} > ${.TARGET}
