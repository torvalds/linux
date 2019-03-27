# $FreeBSD$

.include "${.CURDIR:H}/config.mk"

SRCDIR=	${NCURSES_DIR}/menu

LIB=	menu${LIB_SUFFIX}

.PATH: ${SRCDIR}
SRCS=	\
	ncurses_def.h \
	m_attribs.c \
	m_cursor.c \
	m_driver.c \
	m_format.c \
	m_global.c \
	m_hook.c \
	m_item_cur.c \
	m_item_nam.c \
	m_item_new.c \
	m_item_opt.c \
	m_item_top.c \
	m_item_use.c \
	m_item_val.c \
	m_item_vis.c \
	m_items.c \
	m_new.c \
	m_opts.c \
	m_pad.c \
	m_pattern.c \
	m_post.c \
	m_req_name.c \
	m_scale.c \
	m_spacing.c \
	m_sub.c \
	m_userptr.c \
	m_win.c

CLEANFILES=	ncurses_def.h

CFLAGS+=	-I${SRCDIR}

LIBADD+=	ncurses${LIB_SUFFIX}

.if defined(ENABLE_WIDEC)
INCS=	menu.h eti.h

.PATH: ${NCURSES_DIR}/man
MAN=	\
	menu.3 \
	menu_attributes.3 \
	menu_cursor.3 \
	menu_driver.3 \
	menu_format.3 \
	menu_hook.3 \
	menu_items.3 \
	menu_mark.3 \
	menu_new.3 \
	menu_opts.3 \
	menu_pattern.3 \
	menu_post.3 \
	menu_requestname.3 \
	menu_spacing.3 \
	menu_userptr.3 \
	menu_win.3 \
	mitem_current.3 \
	mitem_name.3 \
	mitem_new.3 \
	mitem_opts.3 \
	mitem_userptr.3 \
	mitem_value.3 \
	mitem_visible.3

CLEANFILES+=	${MAN:M*.3}

MLINKS=	menu_attributes.3 menu_back.3 \
	menu_attributes.3 menu_fore.3 \
	menu_attributes.3 menu_grey.3 \
	menu_attributes.3 menu_pad.3 \
	menu_attributes.3 set_menu_back.3 \
	menu_attributes.3 set_menu_fore.3 \
	menu_attributes.3 set_menu_grey.3 \
	menu_attributes.3 set_menu_pad.3 \
	menu_cursor.3 pos_menu_cursor.3 \
	menu_format.3 set_menu_format.3 \
	menu_hook.3 item_init.3 \
	menu_hook.3 item_term.3 \
	menu_hook.3 menu_init.3 \
	menu_hook.3 menu_term.3 \
	menu_hook.3 set_item_init.3 \
	menu_hook.3 set_item_term.3 \
	menu_hook.3 set_menu_init.3 \
	menu_hook.3 set_menu_term.3 \
	menu_items.3 item_count.3 \
	menu_items.3 set_menu_items.3 \
	menu_mark.3 set_menu_mark.3 \
	menu_new.3 free_menu.3 \
	menu_new.3 new_menu.3 \
	menu_opts.3 menu_opts_off.3 \
	menu_opts.3 menu_opts_on.3 \
	menu_opts.3 set_menu_opts.3 \
	menu_pattern.3 set_menu_pattern.3 \
	menu_post.3 post_menu.3 \
	menu_post.3 unpost_menu.3 \
	menu_requestname.3 menu_request_by_name.3 \
	menu_requestname.3 menu_request_name.3 \
	menu_spacing.3 set_menu_spacing.3 \
	menu_userptr.3 set_menu_userptr.3 \
	menu_win.3 menu_sub.3 \
	menu_win.3 scale_menu.3 \
	menu_win.3 set_menu_sub.3 \
	menu_win.3 set_menu_win.3 \
	mitem_current.3 current_item.3 \
	mitem_current.3 item_index.3 \
	mitem_current.3 set_current_item.3 \
	mitem_current.3 set_top_row.3 \
	mitem_current.3 top_row.3 \
	mitem_name.3 item_description.3 \
	mitem_name.3 item_name.3 \
	mitem_new.3 free_item.3 \
	mitem_new.3 new_item.3 \
	mitem_opts.3 item_opts.3 \
	mitem_opts.3 item_opts_off.3 \
	mitem_opts.3 item_opts_on.3 \
	mitem_opts.3 set_item_opts.3 \
	mitem_userptr.3 item_userptr.3 \
	mitem_userptr.3 set_item_userptr.3 \
	mitem_value.3 item_value.3 \
	mitem_value.3 set_item_value.3 \
	mitem_visible.3 item_visible.3
.else
MAN=
.endif

.include <bsd.lib.mk>

# Keep the .SUFFIXES line after the include of bsd.lib.mk
.SUFFIXES: .3 .3x
.3x.3:
	cat ${.IMPSRC} > ${.TARGET}
