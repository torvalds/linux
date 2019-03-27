#
# $FreeBSD$
#

.include <src.opts.mk>

PACKAGE=	vi
SRCDIR=		${SRCTOP}/contrib/nvi
SUBDIR+=	catalog

WARNS?=	0	# some warn issues on 32 bit machines

VI=	nvi
EX=	nex
VIEW=	nview

PROG=	nvi

CFLAGS+=	-D__REGEX_PRIVATE

LINKS=	${BINDIR}/${VI} ${BINDIR}/${EX} ${BINDIR}/${VI} ${BINDIR}/${VIEW}
LINKS+= ${BINDIR}/${VI} ${BINDIR}/vi ${BINDIR}/${EX} ${BINDIR}/ex
LINKS+= ${BINDIR}/${VI} ${BINDIR}/view

MAN=	${SRCDIR}/docs/man/vi.1
MLINKS+=vi.1 ex.1 vi.1 view.1
MLINKS+=vi.1 nex.1 vi.1 nview.1 vi.1 nvi.1

.PATH:	${SRCDIR}/common
.PATH:	${SRCDIR}/ex
.PATH:	${SRCDIR}/cl
.PATH:	${SRCDIR}/vi
.PATH:	${SRCDIR}/regex

CFLAGS+=-I${.CURDIR} -I${SRCDIR} -I${SRCDIR}/regex

LIBADD=	util ncursesw
CFLAGS+=	-DUSE_WIDECHAR

.if ${MK_ICONV} == "yes" && !defined(RESCUE) && !defined(RELEASE_CRUNCH)
CFLAGS+=	-DUSE_ICONV
.endif

CLEANFILES+=${EX}

# Vi curses sources
SRCS+=	cl_funcs.c cl_main.c cl_read.c cl_screen.c cl_term.c

# General sources.
SRCS+=	conv.c cut.c delete.c encoding.c exf.c key.c line.c log.c main.c mark.c msg.c options.c \
	options_f.c put.c recover.c screen.c search.c seq.c util.c

# Ex source.
SRCS+=	ex.c ex_abbrev.c ex_append.c ex_args.c ex_argv.c ex_at.c ex_bang.c \
	ex_cd.c ex_cmd.c ex_cscope.c ex_delete.c ex_display.c \
	ex_edit.c ex_equal.c ex_file.c ex_filter.c ex_global.c \
	ex_init.c ex_join.c ex_map.c ex_mark.c ex_mkexrc.c ex_move.c \
	ex_open.c ex_preserve.c ex_print.c ex_put.c ex_quit.c \
	ex_read.c ex_screen.c ex_script.c ex_set.c ex_shell.c \
	ex_shift.c ex_source.c ex_stop.c ex_subst.c ex_tag.c \
	ex_txt.c ex_undo.c ex_usage.c ex_util.c ex_version.c ex_visual.c \
	ex_write.c ex_yank.c ex_z.c

# Vi source.
SRCS+=	getc.c v_at.c v_ch.c v_cmd.c v_delete.c v_ex.c v_increment.c v_init.c \
	v_itxt.c v_left.c v_mark.c v_match.c v_paragraph.c v_put.c v_redraw.c \
	v_replace.c v_right.c v_screen.c v_scroll.c v_search.c v_section.c \
	v_sentence.c v_status.c v_txt.c v_ulcase.c v_undo.c \
	v_util.c v_word.c v_xchar.c v_yank.c v_z.c v_zexit.c vi.c

# Vi screen source.
SRCS+=	vs_line.c vs_msg.c vs_refresh.c vs_relative.c vs_smap.c vs_split.c

# Wide char regex
SRCS+=	regcomp.c regerror.c regexec.c regfree.c

.include <bsd.prog.mk>
