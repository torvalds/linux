# $OpenBSD: Makefile,v 1.18 2025/02/21 18:16:45 sthen Exp $

LIB=	form
SRCS=	fld_arg.c fld_attr.c fld_current.c fld_def.c fld_dup.c \
	fld_ftchoice.c fld_ftlink.c fld_info.c fld_just.c fld_link.c \
	fld_max.c fld_move.c fld_newftyp.c fld_opts.c fld_pad.c fld_page.c \
	fld_stat.c fld_type.c fld_user.c frm_cursor.c frm_data.c frm_def.c \
	frm_driver.c frm_hook.c frm_opts.c frm_page.c frm_post.c \
	frm_req_name.c frm_scale.c frm_sub.c frm_user.c frm_win.c \
	fty_alnum.c fty_alpha.c fty_enum.c fty_generic.c fty_int.c \
	fty_ipv4.c fty_num.c fty_regex.c
HDRS=	form.h
CFLAGS+=-I${.CURDIR}/../libcurses -I${.CURDIR}/../libmenu -D_XOPEN_SOURCE_EXTENDED -DNDEBUG
MAN=	form.3 form_cursor.3 form_data.3 form_driver.3 form_field.3 \
	form_field_attributes.3 form_field_buffer.3 form_field_info.3 \
	form_field_just.3 form_field_new.3 form_field_opts.3 \
	form_field_userptr.3 form_field_validation.3 form_fieldtype.3 \
	form_hook.3 form_new.3 form_new_page.3 form_opts.3 form_page.3 \
	form_post.3 form_requestname.3 form_userptr.3 form_variables.3 \
	form_win.3
PC_FILES= form.pc
CLEANFILES+= ${PC_FILES}

includes:
	@cd ${.CURDIR}; for i in ${HDRS}; do \
	  cmp -s $$i ${DESTDIR}/usr/include/$$i || \
	  ${INSTALL} ${INSTALL_COPY} -m 444 -o $(BINOWN) -g $(BINGRP) $$i \
	  ${DESTDIR}/usr/include; done

all: ${PC_FILES}

${PC_FILES}: ${.CURDIR}/../libcurses/curses.h ${.CURDIR}/../libcurses/ncurses_cfg.h
	/bin/sh ${.CURDIR}/generate_pkgconfig.sh -c ${.CURDIR} -o ${.OBJDIR}

.include <bsd.own.mk>

afterinstall:
.for p in ${PC_FILES}
	${INSTALL} ${INSTALL_COPY} -o root -g ${SHAREGRP} \
	    -m ${SHAREMODE} ${.OBJDIR}/$p ${DESTDIR}/usr/lib/pkgconfig/
.endfor
	-cd ${DESTDIR}${LIBDIR}; \
	for i in ${_LIBS}; do \
	    ln -f $$i `echo $$i | sed 's/form/formw/'`; \
	done

.include <bsd.lib.mk>
