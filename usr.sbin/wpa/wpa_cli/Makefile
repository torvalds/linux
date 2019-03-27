# $FreeBSD$

.include <src.opts.mk>

.include "../Makefile.inc"

.PATH.c:${WPA_SUPPLICANT_DISTDIR} \
	${WPA_DISTDIR}/wpa_supplicant \
	${WPA_DISTDIR}/src/eap_peer \
	${WPA_DISTDIR}/src/drivers

PROG=	wpa_cli
SRCS=	base64.c bitfield.c blacklist.c bss.c cli.c common.c config.c \
	config_file.c \
	ctrl_iface.c ctrl_iface_common.c ctrl_iface_unix.c \
	drivers.c driver_common.c \
	eap_register.c \
	edit.c eloop.c events.c hw_features_common.c \
	ieee802_11_common.c l2_packet_freebsd.c notify.c \
	op_classes.c \
	os_unix.c rrm.c scan.c wmm_ac.c \
	wpa.c wpa_cli.c \
	wpa_ctrl.c wpa_common.c \
	wpa_debug.c wpa_ie.c wpa_supplicant.c wpabuf.c wpas_glue.c

MAN=	wpa_cli.8

CFLAGS+= -DCONFIG_CTRL_IFACE
CFLAGS+= -DCONFIG_CTRL_IFACE_UNIX
CFLAGS+= -DCONFIG_TLS=openssl
# enable use of d_type to identify unix domain sockets
CFLAGS+= -D_DIRENT_HAVE_D_TYPE

CFLAGS+= -DCONFIG_WPA_CLI_EDIT=y
LIBADD+=	pcap util

TLS_FUNCS=y

.include "../Makefile.crypto"

.include <bsd.prog.mk>
