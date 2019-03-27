# $FreeBSD$

.include <src.opts.mk>

.include "../Makefile.inc"

.PATH.c:${WPA_SUPPLICANT_DISTDIR} \
	${WPA_DISTDIR}/src/eap_peer \
	${WPA_DISTDIR}/src/drivers

PROG=	wpa_supplicant

SRCS=	base64.c bitfield.c blacklist.c bss.c cli.c common.c \
	config.c config_file.c \
	ctrl_iface.c ctrl_iface_common.c ctrl_iface_unix.c \
	dh_groups.c driver_bsd.c driver_common.c \
	driver_ndis.c driver_wired.c driver_wired_common.c drivers.c \
	eap_register.c eap_wsc.c eap_wsc_common.c eloop.c \
	events.c gas.c gas_query.c hs20_supplicant.c \
	http_client.c http_server.c \
	httpread.c hw_features_common.c \
	ieee802_11_common.c interworking.c l2_packet_freebsd.c main.c \
	notify.c offchannel.c op_classes.c os_unix.c pmksa_cache.c preauth.c \
	rrm.c scan.c upnp_xml.c uuid.c \
	wmm_ac.c wpa.c wpa_common.c wpa_ctrl.c \
	wpa_debug.c wpa_ft.c wpa_ie.c wpa_supplicant.c wpabuf.c wpas_glue.c \
	wps.c wps_attr_build.c wps_attr_parse.c wps_attr_process.c \
	wps_common.c wps_dev_attr.c wps_enrollee.c wps_registrar.c \
	wps_supplicant.c wps_upnp.c wps_upnp_ap.c wps_upnp_event.c \
	wps_upnp_ssdp.c wps_upnp_web.c \
	Packet32.c

MAN=	wpa_supplicant.8 wpa_supplicant.conf.5

.if ${MK_EXAMPLES} != "no"
FILESDIR= ${SHAREDIR}/examples/etc
.PATH:	${WPA_SUPPLICANT_DISTDIR}
FILES=	wpa_supplicant.conf
.endif

CFLAGS+=-DCONFIG_BACKEND_FILE \
	-DCONFIG_DEBUG_SYSLOG \
	-DCONFIG_DRIVER_BSD \
	-DCONFIG_DRIVER_NDIS \
	-DCONFIG_DRIVER_WIRED \
	-DCONFIG_GAS \
	-DCONFIG_HS20 \
	-DCONFIG_IEEE80211R \
	-DCONFIG_INTERWORKING \
	-DCONFIG_PEERKEY \
	-DCONFIG_PRIVSEP \
	-DCONFIG_SMARTCARD \
	-DCONFIG_TERMINATE_ONLASTIF \
	-DCONFIG_TLS=openssl \
	-DCONFIG_WPS \
	-DCONFIG_WPS2 \
	-DCONFIG_WPS_UPNP \
	-DPKCS12_FUNCS
#CFLAGS+= -g
LIBADD=	pcap util

# User customizations to the wpa_supplicant build environment
CFLAGS+=${WPA_SUPPLICANT_CFLAGS}
#DPADD+=${WPA_SUPPLICANT_DPADD}
LDADD+=${WPA_SUPPLICANT_LDADD}
#LDFLAGS+=${WPA_SUPPLICANT_LDFLAGS}

.if ${MK_WPA_SUPPLICANT_EAPOL} != "no"
CFLAGS+=-DEAP_GTC \
	-DEAP_LEAP \
	-DEAP_MD5 \
	-DEAP_MSCHAPv2 \
	-DEAP_OTP \
	-DEAP_PEAP \
	-DEAP_PSK \
	-DEAP_TLS \
	-DEAP_TTLS \
	-DIEEE8021X_EAPOL
SRCS+=	chap.c \
	eap.c \
	eap_common.c \
	eap_gtc.c \
	eap_leap.c \
	eap_md5.c \
	eap_methods.c \
	eap_mschapv2.c \
	eap_otp.c \
	eap_peap.c \
	eap_peap_common.c \
	eap_psk.c \
	eap_psk_common.c \
	eap_tls.c \
	eap_tls_common.c \
	eap_ttls.c \
	eapol_supp_sm.c \
	ms_funcs.c \
	mschapv2.c
TLS_FUNCS=y
NEED_AES_EAX=y
NEED_AES_ENCBLOCK=y
NEED_AES_OMAC1=y
.endif

.if !empty(CFLAGS:M*-DEAP_AKA)
SRCS+=	eap_aka.c
NEED_SIM_COMMON=y
NEED_AES_CBC=y
.endif

.if !empty(CFLAGS:M*-DEAP_SIM)
SRCS+=	eap_sim.c
NEED_SIM_COMMON=y
NEED_AES_CBC=y
.endif

.if defined(NEED_SIM_COMMON)
SRCS+=	eap_sim_common.c
NEED_FIPS186_2_PRF=y
.endif

# PC/SC interface for smartcards (USIM, GSM SIM)
# GSM/UMTS authentication algorithm (for EAP-SIM/EAP-AKA)
# NB: requires devel/pcsc-lite
#
# WPA_SUPPLICANT_CFLAGS=-DEAP_AKA -DPCSC_FUNCS -I/usr/local/include/PCSC
# WPA_SUPPLICANT_LDADD=-L/usr/local/lib
#
.if !empty(CFLAGS:M*-DPCSC_FUNCS)
SRCS+=	pcsc_funcs.c
LIBADD+=	pcslite pthread
.endif

.if !empty(CFLAGS:M*-DEAP_GPSK)
CFLAGS+=-DEAP_GPSK_SHA256
SRCS+=	eap_gpsk.c \
	eap_gpsk_common.c
NEED_AES_OMAC1=y
.endif

.if !empty(CFLAGS:M*-DEAP_PAX)
SRCS+=	eap_pax.c \
	eap_pax_common.c
.endif

.if !empty(CFLAGS:M*-DEAP_SAKE)
SRCS+=	eap_sake.c \
	eap_sake_common.c
.endif

.include "../Makefile.crypto"

.include <bsd.prog.mk>
