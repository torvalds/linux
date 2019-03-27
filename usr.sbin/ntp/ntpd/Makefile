# $FreeBSD$

MAN=

.include <src.opts.mk>

.PATH:	${SRCTOP}/contrib/ntp/ntpd \
	${.OBJDIR}

DIRS=	ETC_NTP
ETC_NTP=	/etc/ntp
ETC_NTP_MODE=	0700
CONFS=	ntp.conf
FILES=	leap-seconds
FILESDIR=	ETC_NTP
FILESMODE=	644
PROG=	ntpd

SRCS=	cmd_args.c ntp_config.c ntp_control.c ntp_crypto.c ntp_filegen.c \
	ntp_io.c ntp_leapsec.c ntp_loopfilter.c ntp_monitor.c ntp_parser.c \
	ntp_peer.c ntp_proto.c ntp_refclock.c ntp_request.c ntp_restrict.c \
	ntp_scanner.c ntp_signd.c ntp_timer.c ntp_util.c ntpd-opts.c ntpd.c \
	rc_cmdlength.c \
	refclock_acts.c refclock_arbiter.c refclock_arc.c refclock_as2201.c \
	refclock_atom.c refclock_bancomm.c refclock_chronolog.c \
	refclock_chu.c refclock_conf.c refclock_datum.c refclock_dumbclock.c \
	refclock_fg.c refclock_gpsdjson.c refclock_gpsvme.c refclock_heath.c \
	refclock_hopfpci.c refclock_hopfser.c refclock_hpgps.c \
	refclock_irig.c refclock_jjy.c refclock_jupiter.c refclock_leitch.c \
	refclock_local.c refclock_nmea.c refclock_neoclock4x.c \
	refclock_oncore.c refclock_palisade.c \
	refclock_parse.c refclock_pcf.c refclock_pst.c refclock_ripencc.c \
	refclock_shm.c refclock_tpro.c refclock_true.c refclock_tsyncpci.c \
	refclock_tt560.c refclock_ulink.c refclock_wwv.c refclock_wwvb.c \
	refclock_zyfer.c version.c

CFLAGS+= -I${SRCTOP}/contrib/ntp/ntpd \
	-I${SRCTOP}/contrib/ntp/include \
	-I${SRCTOP}/contrib/ntp/lib/isc/include \
	-I${SRCTOP}/contrib/ntp/lib/isc/pthreads/include \
	-I${SRCTOP}/contrib/ntp/lib/isc/unix/include \
	-I${SRCTOP}/contrib/ntp/sntp/libopts \
	-I${.CURDIR:H} \
	-I${.CURDIR}

LIBADD=	parse ntp m opts pthread

.if ${MK_OPENSSL} != "no"
LIBADD+=	crypto
.else
LIBADD+=	md
.endif

CLEANFILES+= .version version.c

version.c:
	sh -e ${.CURDIR:H}/scripts/mkver ntpd

.include <bsd.prog.mk>
