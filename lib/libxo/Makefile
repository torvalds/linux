# $FreeBSD$

PACKAGE=lib${LIB}
SHLIBDIR?=      /lib

.include <src.opts.mk>

LIBXOSRC=	${SRCTOP}/contrib/libxo

.PATH:	${LIBXOSRC}/libxo

LIB=	xo
SHLIB_MAJOR=0

SRCS=	libxo.c xo_encoder.c xo_syslog.c

CFLAGS+=-I${LIBXOSRC}/libxo -I${.CURDIR}
CFLAGS+=-DXO_ENCODERDIR=\"/usr/lib/libxo/encoder\"

INCS=	xo.h xo_encoder.h
INCSDIR=${INCLUDEDIR}/libxo

LIBADD=	util

WARNS?= 5

MAN+=	libxo.3
MAN+=	xo_attr.3 \
	xo_create.3 \
	xo_emit.3 \
	xo_emit_err.3 \
	xo_err.3 \
	xo_error.3 \
	xo_finish.3 \
	xo_flush.3 \
	xo_message.3 \
	xo_no_setlocale.3 \
	xo_open_container.3 \
	xo_open_list.3 \
	xo_open_marker.3 \
	xo_parse_args.3 \
	xo_set_allocator.3 \
	xo_set_flags.3 \
	xo_set_info.3 \
	xo_set_options.3 \
	xo_set_style.3 \
	xo_set_syslog_enterprise_id.3 \
	xo_set_version.3 \
	xo_set_writer.3 \
	xo_syslog.3

MAN+=	xo_format.5

MAN+=	xo_options.7

MLINKS=	xo_attr.3 xo_attr_h.3 \
	xo_attr.3 xo_attr_hv.3 \
	xo_create.3 xo_create_to_file.3 \
	xo_create.3 xo_destroy.3 \
	xo_emit.3 xo_emit_h.3 \
	xo_emit.3 xo_emit_hv.3 \
	xo_emit_err.3 xo_emit_errc.3 \
	xo_emit_err.3 xo_emit_errx.3 \
	xo_emit_err.3 xo_emit_warn.3 \
	xo_emit_err.3 xo_emit_warnx.3 \
	xo_emit_err.3 xo_emit_warn_c.3 \
	xo_emit_err.3 xo_emit_warn_hc.3 \
	xo_err.3 xo_errc.3 \
	xo_err.3 xo_errx.3 \
	xo_err.3 xo_warn.3 \
	xo_err.3 xo_warnx.3 \
	xo_err.3 xo_warn_c.3 \
	xo_err.3 xo_warn_hc.3 \
	xo_finish.3 xo_finish_h.3 \
	xo_flush.3 xo_flush_h.3 \
	xo_message.3 xo_message_c.3 \
	xo_message.3 xo_message_hc.3 \
	xo_message.3 xo_message_hcv.3 \
	xo_open_container.3 xo_open_container_h.3 \
	xo_open_container.3 xo_open_container_hd.3 \
	xo_open_container.3 xo_open_container_d.3 \
	xo_open_container.3 xo_close_container.3 \
	xo_open_container.3 xo_close_container_h.3 \
	xo_open_container.3 xo_close_container_hd.3 \
	xo_open_container.3 xo_close_container_d.3 \
	xo_open_list.3 xo_open_list_h.3 \
	xo_open_list.3 xo_open_list_hd.3 \
	xo_open_list.3 xo_open_list_d.3 \
	xo_open_list.3 xo_open_instance.3 \
	xo_open_list.3 xo_open_instance_h.3 \
	xo_open_list.3 xo_open_instance_hd.3 \
	xo_open_list.3 xo_open_instance_d.3 \
	xo_open_list.3 xo_close_instance.3 \
	xo_open_list.3 xo_close_instance_h.3 \
	xo_open_list.3 xo_close_instance_hd.3 \
	xo_open_list.3 xo_close_instance_d.3 \
	xo_open_list.3 xo_close_list.3 \
	xo_open_list.3 xo_close_list_h.3 \
	xo_open_list.3 xo_close_list_hd.3 \
	xo_open_list.3 xo_close_list_d.3 \
	xo_open_marker.3 xo_open_marker_h.3 \
	xo_open_marker.3 xo_close_marker.3 \
	xo_open_marker.3 xo_close_marker_h.3 \
	xo_parse_args.3 xo_set_program.3 \
	xo_set_flags.3 xo_clear_flags.3 \
	xo_set_style.3 xo_set_style_name.3 \
	xo_set_version.3 xo_set_version_h.3 \
	xo_syslog.3 xo_close_log.3 \
	xo_syslog.3 xo_open_log.3 \
	xo_syslog.3 xo_set_logmask.3 \
	xo_syslog.3 xo_vsyslog.3

HAS_TESTS=
SUBDIR.${MK_TESTS}+= tests

.include <bsd.lib.mk>
