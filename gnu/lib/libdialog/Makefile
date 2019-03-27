# $FreeBSD$

DIALOG=		${SRCTOP}/contrib/dialog

LIB=		dialog
SHLIB_MAJOR=	8
SRCS=		argv.c arrows.c buildlist.c buttons.c calendar.c checklist.c \
		columns.c dlg_keys.c editbox.c fselect.c formbox.c guage.c \
		help.c inputbox.c inputstr.c menubox.c mixedform.c \
		mixedgauge.c mouse.c mousewget.c msgbox.c pause.c prgbox.c \
		progressbox.c rangebox.c rc.c tailbox.c textbox.c timebox.c \
		trace.c treeview.c ttysize.c ui_getc.c util.c version.c yesno.c
INCS=		dialog.h dlg_colors.h dlg_config.h dlg_keys.h
MAN=		dialog.3

LIBADD=		ncursesw m

CFLAGS+= 	-I${.CURDIR} -I${DIALOG} -D_XOPEN_SOURCE_EXTENDED -DGCC_UNUSED=__unused
.PATH:		${DIALOG}
WARNS?=		1

.include <bsd.lib.mk>
