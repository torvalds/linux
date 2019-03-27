# $FreeBSD$

.PATH: ${SRCTOP}/sys/net80211

KMOD=	wlan
SRCS=	ieee80211.c ieee80211_action.c ieee80211_ageq.c \
	ieee80211_crypto.c ieee80211_crypto_none.c ieee80211_dfs.c \
	ieee80211_freebsd.c ieee80211_input.c ieee80211_ioctl.c \
	ieee80211_mesh.c ieee80211_node.c ieee80211_output.c ieee80211_phy.c \
	ieee80211_power.c ieee80211_proto.c ieee80211_scan.c ieee80211_scan_sw.c \
	ieee80211_scan_sta.c ieee80211_radiotap.c ieee80211_ratectl.c \
	ieee80211_ratectl_none.c ieee80211_regdomain.c \
	ieee80211_ht.c ieee80211_hwmp.c ieee80211_adhoc.c ieee80211_hostap.c \
	ieee80211_monitor.c ieee80211_sta.c ieee80211_wds.c ieee80211_ddb.c \
	ieee80211_tdma.c ieee80211_superg.c ieee80211_vht.c
SRCS+=	bus_if.h device_if.h opt_ddb.h opt_inet.h opt_inet6.h \
	opt_tdma.h opt_wlan.h

.include <bsd.kmod.mk>

CWARNFLAGS.ieee80211_adhoc.c=	-Wno-unused-function
CWARNFLAGS.ieee80211_crypto.c=	-Wno-unused-function
CWARNFLAGS.ieee80211_hostap.c=	-Wno-unused-function
CWARNFLAGS.ieee80211_mesh.c=	-Wno-unused-function
CWARNFLAGS.ieee80211_sta.c=	-Wno-unused-function
