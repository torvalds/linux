# $FreeBSD$

.include <src.opts.mk>

CONFDIR=	/etc/rc.d
CONFGROUPS=	CONFS
#PACKAGE=rc

CONFS=	DAEMON \
	FILESYSTEMS \
	LOGIN \
	NETWORKING \
	SERVERS \
	abi \
	addswap \
	adjkerntz \
	archdep \
	bgfsck \
	${_blacklistd} \
	${_bluetooth} \
	bridge \
	${_bthidd} \
	cfumass \
	cleanvar \
	cleartmp \
	cron \
	ctld \
	ddb \
	defaultroute \
	devd \
	devfs \
	devmatch \
	dhclient \
	dmesg \
	dumpon \
	fsck \
	gbde \
	geli \
	geli2 \
	gptboot \
	growfs \
	gssd \
	${_hcsecd} \
	hostid \
	hostid_save \
	hostname \
	iovctl \
	ip6addrctl \
	ipsec \
	${_kadmind} \
	${_kdc} \
	${_kfd} \
	kld \
	kldxref \
	${_kpasswdd} \
	ldconfig \
	local \
	localpkg \
	lockd \
	mixer \
	motd \
	mountcritlocal \
	mountcritremote \
	mountlate \
	mdconfig \
	mdconfig2 \
	mountd \
	msgs \
	natd \
	netif \
	netoptions \
	netwait \
	newsyslog \
	nfsclient \
	nfscbd \
	nfsd \
	nfsuserd \
	nisdomain \
	${_nscd} \
	nsswitch \
	ntpdate \
	${_opensm} \
	pf \
	pflog \
	pfsync \
	ppp \
	pppoed \
	pwcheck \
	quota \
	random \
	rarpd \
	rctl \
	resolv \
	root \
	route6d \
	routing \
	rpcbind \
	rtadvd \
	rtsold \
	rwho \
	savecore \
	securelevel \
	serial \
	sppp \
	statd \
	static_arp \
	static_ndp \
	stf \
	swap \
	swaplate \
	sysctl \
	syslogd \
	tmp \
	${_ubthidhci} \
	ugidfw \
	${_utx} \
	var \
	watchdogd

.if ${MK_NIS} != "no"
CONFS+=	ypbind \
	ypldap \
	yppasswdd \
	ypserv \
	ypset \
	ypupdated \
	ypxfrd
.endif

.if ${MK_ACCT} != "no"
CONFGROUPS+=	ACCT
ACCT+=		accounting
ACCTPACKAGE=	acct
.endif

.if ${MK_ACPI} != "no"
CONFGROUPS+=	ACPI
ACPI=		power_profile
ACPIPACKAGE=	acpi
.endif

.if ${MK_ACPI} != "no" || ${MK_APM} != "no"
CONFS+=		powerd
.endif

.if ${MK_AMD} != "no"
CONFGROUPS+=	AMD
AMD+=		amd
AMDPACKAGE=	amd
.endif

.if ${MK_APM} != "no"
CONFGROUPS+=	APM
APM+=		apm
.if ${MACHINE} == "i386"
APM+=		apmd
.endif
APMPACKAGE=	apm
.endif

.if ${MK_AUDIT} != "no"
CONFGROUPS+=	AUDIT
AUDIT+=		auditd
AUDIT+=		auditdistd
.endif

.if ${MK_AUTOFS} != "no"
CONFS+=		automount
CONFS+=		automountd
CONFS+=		autounmountd
.endif

.if ${MK_BLACKLIST} != "no"
_blacklistd+=	blacklistd
.endif

.if ${MK_BLUETOOTH} != "no"
_bluetooth=	bluetooth
_bthidd=	bthidd
_hcsecd=	hcsecd
CONFS+=		rfcomm_pppd_server
CONFS+=		sdpd
_ubthidhci=	ubthidhci
.endif

.if ${MK_BOOTPARAMD} != "no"
CONFS+=		bootparams
.endif

.if ${MK_BSNMP} != "no"
CONFGROUPS+=	BSNMP
BSNMP+=		bsnmpd
BSNMPPACKAGE=	bsnmp
.endif

.if ${MK_CCD} != "no"
CONFS+=		ccd
.endif

.if ${MK_FTP} != "no"
CONFS+=		ftpd
.endif

.if ${MK_HAST} != "no"
CONFGROUPS+=	HAST
HAST=		hastd
HASTPACKAGE=	hast
.endif

.if ${MK_INETD} != "no"
CONFS+=		inetd
.endif

.if ${MK_IPFILTER} != "no"
CONFS+=	ipfilter \
	ipfs \
	ipmon \
	ipnat \
	ippool
.endif

.if ${MK_IPFW} != "no"
CONFS+=		ipfw
.if ${MK_NETGRAPH} != "no"
CONFS+=		ipfw_netflow
.endif
.endif

.if ${MK_ISCSI} != "no"
CONFS+=		iscsictl
CONFS+=		iscsid
.endif

.if ${MK_JAIL} != "no"
CONFGROUPS+=	JAIL
JAIL+=		jail
JAILPACKAGE=	jail
.endif

.if ${MK_LEGACY_CONSOLE} != "no"
CONFS+=		moused
CONFS+=		syscons
.endif

.if ${MK_LPR} != "no"
CONFS+=		lpd
.endif

.if ${MK_KERBEROS} != "no"
CONFS+=		ipropd_master
CONFS+=		ipropd_slave
_kadmind=	kadmind
_kdc=		kdc
_kfd=		kfd
_kpasswdd=	kpasswdd

DIRS+=	VAR_HEMIDAL
VAR_HEMIDAL=	/var/heimdal
VAR_HEMIDAL_MODE=	700
.endif

.if ${MK_MAIL} != "no"
CONFS+=		othermta
.endif

.if ${MK_NS_CACHING} != "no"
_nscd=		nscd
.endif

.if ${MK_NTP} != "no"
CONFS+=		ntpd
.endif

.if ${MK_OFED} != "no"
_opensm=	opensm
.endif

.if ${MK_OPENSSL} != "no"
CONFS+=		keyserv
.endif

.if ${MK_OPENSSH} != "no"
CONFGROUPS+=	SSH
SSH=		sshd
SSHPACKAGE=	ssh
.endif

.if ${MK_PF} != "no"
CONFS+=		ftp-proxy
.endif

.if ${MK_ROUTED} != "no"
CONFS+=		routed
.endif

.if ${MK_SENDMAIL} != "no"
CONFGROUPS+=	SMRCD
SMRCD=		sendmail
SMRCDPACKAGE=	sendmail
.endif

.if ${MK_UNBOUND} != "no"
CONFGROUPS+=	UNBOUND
UNBOUND+=	local_unbound
UNBOUNDPACKAGE=	unbound
.endif

.if ${MK_UTMPX} != "no"
_utx=		utx
.endif

.if ${MK_VI} != "no"
CONFGROUPS+=	VI
VI+=		virecover
VIPACKAGE=	vi
.endif

.if ${MK_WIRELESS} != "no"
CONFS+=		hostapd
CONFS+=		wpa_supplicant
.endif

.if ${MK_ZFS} != "no"
CONFGROUPS+=	ZFS
ZFS+=		zfs
ZFS+=		zfsbe
ZFS+=		zfsd
ZFS+=		zvol
ZFSPACKAGE=	zfs
DIRS+=	ETC_ZFS
ETC_ZFS=	/etc/zfs
ETC_ZFSPACKAGE=	zfs
.endif

.for fg in ${CONFGROUPS}
${fg}MODE?=	${BINMODE}
.endfor

.include <bsd.prog.mk>
