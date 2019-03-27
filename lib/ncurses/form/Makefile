# $FreeBSD$

.include "${.CURDIR:H}/config.mk"

SRCDIR=	${NCURSES_DIR}/form

LIB=	form${LIB_SUFFIX}

.PATH: ${SRCDIR}
SRCS=	\
	ncurses_def.h \
	fld_arg.c \
	fld_attr.c \
	fld_current.c \
	fld_def.c \
	fld_dup.c \
	fld_ftchoice.c \
	fld_ftlink.c \
	fld_info.c \
	fld_just.c \
	fld_link.c \
	fld_max.c \
	fld_move.c \
	fld_newftyp.c \
	fld_opts.c \
	fld_pad.c \
	fld_page.c \
	fld_stat.c \
	fld_type.c \
	fld_user.c \
	frm_cursor.c \
	frm_data.c \
	frm_def.c \
	frm_driver.c \
	frm_hook.c \
	frm_opts.c \
	frm_page.c \
	frm_post.c \
	frm_req_name.c \
	frm_scale.c \
	frm_sub.c \
	frm_user.c \
	frm_win.c \
	fty_alnum.c \
	fty_alpha.c \
	fty_enum.c \
	fty_int.c \
	fty_ipv4.c \
	fty_num.c \
	fty_regex.c

CLEANFILES=	ncurses_def.h

CFLAGS+=	-I${SRCDIR}
CFLAGS+=	-I${NCURSES_DIR}/menu

LIBADD+=	ncurses${LIB_SUFFIX}

.if defined(ENABLE_WIDEC)
INCS=	form.h

.PATH: ${NCURSES_DIR}/man
MAN=	\
	form.3 \
	form_cursor.3 \
	form_data.3 \
	form_driver.3 \
	form_field.3 \
	form_field_attributes.3 \
	form_field_buffer.3 \
	form_field_info.3 \
	form_field_just.3 \
	form_field_new.3 \
	form_field_opts.3 \
	form_field_userptr.3 \
	form_field_validation.3 \
	form_fieldtype.3 \
	form_hook.3 \
	form_new.3 \
	form_new_page.3 \
	form_opts.3 \
	form_page.3 \
	form_post.3 \
	form_requestname.3 \
	form_userptr.3 \
	form_variables.3 \
	form_win.3

CLEANFILES+=	${MAN:M*.3}

MLINKS=	form_cursor.3 pos_form_cursor.3 \
	form_data.3 data_ahead.3 \
	form_data.3 data_behind.3 \
	form_field.3 field_count.3 \
	form_field.3 form_fields.3 \
	form_field.3 move_field.3 \
	form_field.3 set_form_fields.3 \
	form_field_attributes.3 field_back.3 \
	form_field_attributes.3 field_fore.3 \
	form_field_attributes.3 field_pad.3 \
	form_field_attributes.3 set_field_back.3 \
	form_field_attributes.3 set_field_fore.3 \
	form_field_attributes.3 set_field_pad.3 \
	form_field_buffer.3 field_buffer.3 \
	form_field_buffer.3 field_status.3 \
	form_field_buffer.3 set_field_buffer.3 \
	form_field_buffer.3 set_field_status.3 \
	form_field_buffer.3 set_max_field.3 \
	form_field_info.3 dynamic_fieldinfo.3 \
	form_field_info.3 field_info.3 \
	form_field_just.3 field_just.3 \
	form_field_just.3 set_field_just.3 \
	form_field_new.3 dup_field.3 \
	form_field_new.3 free_field.3 \
	form_field_new.3 link_field.3 \
	form_field_new.3 new_field.3 \
	form_field_opts.3 field_opts.3 \
	form_field_opts.3 field_opts_off.3 \
	form_field_opts.3 field_opts_on.3 \
	form_field_opts.3 set_field_opts.3 \
	form_field_userptr.3 field_userptr.3 \
	form_field_userptr.3 set_field_userptr.3 \
	form_field_validation.3 field_arg.3 \
	form_field_validation.3 field_type.3 \
	form_field_validation.3 set_field_type.3 \
	form_fieldtype.3 free_fieldtype.3 \
	form_fieldtype.3 link_fieldtype.3 \
	form_fieldtype.3 new_fieldtype.3 \
	form_fieldtype.3 set_fieldtype_arg.3 \
	form_fieldtype.3 set_fieldtype_choice.3 \
	form_hook.3 field_init.3 \
	form_hook.3 field_term.3 \
	form_hook.3 form_init.3 \
	form_hook.3 form_term.3 \
	form_hook.3 set_field_init.3 \
	form_hook.3 set_field_term.3 \
	form_hook.3 set_form_init.3 \
	form_hook.3 set_form_term.3 \
	form_new.3 free_form.3 \
	form_new.3 new_form.3 \
	form_new_page.3 new_page.3 \
	form_new_page.3 set_new_page.3 \
	form_opts.3 form_opts_off.3 \
	form_opts.3 form_opts_on.3 \
	form_opts.3 set_form_opts.3 \
	form_page.3 current_field.3 \
	form_page.3 field_index.3 \
	form_page.3 set_current_field.3 \
	form_page.3 set_form_page.3 \
	form_post.3 post_form.3 \
	form_post.3 unpost_form.3 \
	form_requestname.3 form_request_by_name.3 \
	form_requestname.3 form_request_name.3 \
	form_userptr.3 set_form_userptr.3 \
	form_win.3 form_sub.3 \
	form_win.3 scale_form.3 \
	form_win.3 set_form_sub.3 \
	form_win.3 set_form_win.3
.else
MAN=
.endif

.include <bsd.lib.mk>

# Keep the .SUFFIXES line after the include of bsd.lib.mk
.SUFFIXES: .3 .3x
.3x.3:
	cat ${.IMPSRC} > ${.TARGET}
