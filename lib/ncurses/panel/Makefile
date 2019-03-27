# $FreeBSD$

.include "${.CURDIR:H}/config.mk"

SRCDIR=	${NCURSES_DIR}/panel

LIB=	panel${LIB_SUFFIX}

.PATH: ${SRCDIR}
SRCS=	\
	ncurses_def.h \
	p_above.c \
	p_below.c \
	p_bottom.c \
	p_delete.c \
	p_hidden.c \
	p_hide.c \
	p_move.c \
	p_new.c \
	p_replace.c \
	p_show.c \
	p_top.c \
	p_update.c \
	p_user.c \
	p_win.c \
	panel.c

CLEANFILES=	ncurses_def.h

CFLAGS+=	-I${SRCDIR}

LIBADD+=	ncurses${LIB_SUFFIX}

.if defined(ENABLE_WIDEC)
INCS=	panel.h

# generate MAN
.PATH: ${NCURSES_DIR}/man
MAN=	\
	panel.3

CLEANFILES+=	${MAN:M*.3}

MLINKS=	panel.3 bottom_panel.3 \
	panel.3 del_panel.3 \
	panel.3 hide_panel.3 \
	panel.3 move_panel.3 \
	panel.3 new_panel.3 \
	panel.3 panel_above.3 \
	panel.3 panel_below.3 \
	panel.3 panel_hidden.3 \
	panel.3 panel_userptr.3 \
	panel.3 panel_window.3 \
	panel.3 replace_panel.3 \
	panel.3 set_panel_userptr.3 \
	panel.3 show_panel.3 \
	panel.3 top_panel.3 \
	panel.3 update_panels.3
.else
MAN=
.endif
	
.include <bsd.lib.mk>

# Keep the .SUFFIXES line after the include of bsd.lib.mk
.SUFFIXES: .3 .3x
.3x.3:
	cat ${.IMPSRC} > ${.TARGET}
