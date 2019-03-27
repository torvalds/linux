#	From: @(#)Makefile	5.20 (Berkeley) 6/12/93
# $FreeBSD$

.include <src.opts.mk>

SUBDIR=	adduser \
	arp \
	binmiscctl \
	camdd \
	cdcontrol \
	chkgrp \
	chown \
	chroot \
	ckdist \
	clear_locks \
	crashinfo \
	cron \
	ctladm \
	ctld \
	daemon \
	dconschat \
	devctl \
	devinfo \
	diskinfo \
	dumpcis \
	etcupdate \
	extattr \
	extattrctl \
	fifolog \
	fstyp \
	fwcontrol \
	getfmac \
	getpmac \
	gstat \
	i2c \
	ifmcstat \
	iostat \
	iovctl \
	kldxref \
	mailwrapper \
	makefs \
	memcontrol \
	mergemaster \
	mfiutil \
	mixer \
	mlxcontrol \
	mountd \
	mount_smbfs \
	mpsutil \
	mptutil \
	mtest \
	newsyslog \
	nfscbd \
	nfsd \
	nfsdumpstate \
	nfsrevoke \
	nfsuserd \
	nmtree \
	nologin \
	pciconf \
	periodic \
	pnfsdscopymr \
	pnfsdsfile \
	pnfsdskill \
	powerd \
	prometheus_sysctl_exporter \
	pstat \
	pw \
	pwd_mkdb \
	pwm \
	quot \
	rarpd \
	rmt \
	rpcbind \
	rpc.lockd \
	rpc.statd \
	rpc.umntall \
	rtprio \
	rwhod \
	service \
	services_mkdb \
	sesutil \
	setfib \
	setfmac \
	setpmac \
	smbmsg \
	snapinfo \
	spi \
	spray \
	syslogd \
	sysrc \
	tcpdrop \
	tcpdump \
	traceroute \
	trim \
	trpt \
	tzsetup \
	ugidfw \
	vigr \
	vipw \
	wake \
	watch \
	watchdogd \
	zic \
	zonectl

# NB: keep these sorted by MK_* knobs

SUBDIR.${MK_ACCT}+=	accton
SUBDIR.${MK_ACCT}+=	sa
SUBDIR.${MK_AMD}+=	amd
SUBDIR.${MK_AUDIT}+=	audit
SUBDIR.${MK_AUDIT}+=	auditd
.if ${MK_OPENSSL} != "no"
SUBDIR.${MK_AUDIT}+=	auditdistd
.endif
SUBDIR.${MK_AUDIT}+=	auditreduce
SUBDIR.${MK_AUDIT}+=	praudit
SUBDIR.${MK_AUTHPF}+=	authpf
SUBDIR.${MK_AUTOFS}+=	autofs
SUBDIR.${MK_BLACKLIST}+=	blacklistctl
SUBDIR.${MK_BLACKLIST}+=	blacklistd
SUBDIR.${MK_BLUETOOTH}+=	bluetooth
SUBDIR.${MK_BOOTPARAMD}+=	bootparamd
SUBDIR.${MK_BSDINSTALL}+=	bsdinstall
SUBDIR.${MK_BSNMP}+=	bsnmpd
SUBDIR.${MK_CXGBETOOL}+=	cxgbetool
SUBDIR.${MK_DIALOG}+=	bsdconfig
SUBDIR.${MK_EFI}+=	efivar efidp efibootmgr
.if ${MK_OPENSSL} != "no"
SUBDIR.${MK_EFI}+=	uefisign
.endif
SUBDIR.${MK_FLOPPY}+=	fdcontrol
SUBDIR.${MK_FLOPPY}+=	fdformat
SUBDIR.${MK_FLOPPY}+=	fdread
SUBDIR.${MK_FLOPPY}+=	fdwrite
SUBDIR.${MK_FMTREE}+=	fmtree
SUBDIR.${MK_FREEBSD_UPDATE}+=	freebsd-update
SUBDIR.${MK_GSSAPI}+=	gssd
SUBDIR.${MK_GPIO}+=	gpioctl
SUBDIR.${MK_INET6}+=	ip6addrctl
SUBDIR.${MK_INET6}+=	mld6query
SUBDIR.${MK_INET6}+=	ndp
SUBDIR.${MK_INET6}+=	rip6query
SUBDIR.${MK_INET6}+=	route6d
SUBDIR.${MK_INET6}+=	rrenumd
SUBDIR.${MK_INET6}+=	rtadvctl
SUBDIR.${MK_INET6}+=	rtadvd
SUBDIR.${MK_INET6}+=	rtsold
SUBDIR.${MK_INET6}+=	traceroute6
SUBDIR.${MK_INETD}+=	inetd
SUBDIR.${MK_IPFW}+=	ipfwpcap
SUBDIR.${MK_ISCSI}+=	iscsid
SUBDIR.${MK_JAIL}+=	jail
SUBDIR.${MK_JAIL}+=	jexec
SUBDIR.${MK_JAIL}+=	jls
# XXX MK_SYSCONS
SUBDIR.${MK_LEGACY_CONSOLE}+=	kbdcontrol
SUBDIR.${MK_LEGACY_CONSOLE}+=	kbdmap
SUBDIR.${MK_LEGACY_CONSOLE}+=	moused
SUBDIR.${MK_LEGACY_CONSOLE}+=	vidcontrol
.if ${MK_LIBTHR} != "no" || ${MK_LIBPTHREAD} != "no"
SUBDIR.${MK_PPP}+=	pppctl
SUBDIR.${MK_NS_CACHING}+=	nscd
.endif
SUBDIR.${MK_LPR}+=	lpr
SUBDIR.${MK_MAN_UTILS}+=	manctl
SUBDIR.${MK_MLX5TOOL}+=	mlx5tool
SUBDIR.${MK_NAND}+=	nandsim
SUBDIR.${MK_NAND}+=	nandtool
SUBDIR.${MK_NETGRAPH}+=	flowctl
SUBDIR.${MK_NETGRAPH}+=	ngctl
SUBDIR.${MK_NETGRAPH}+=	nghook
SUBDIR.${MK_NIS}+=	rpc.yppasswdd
SUBDIR.${MK_NIS}+=	rpc.ypupdated
SUBDIR.${MK_NIS}+=	rpc.ypxfrd
SUBDIR.${MK_NIS}+=	ypbind
SUBDIR.${MK_NIS}+=	ypldap
SUBDIR.${MK_NIS}+=	yp_mkdb
SUBDIR.${MK_NIS}+=	yppoll
SUBDIR.${MK_NIS}+=	yppush
SUBDIR.${MK_NIS}+=	ypserv
SUBDIR.${MK_NIS}+=	ypset
SUBDIR.${MK_NTP}+=	ntp
SUBDIR.${MK_OPENSSL}+=	keyserv
SUBDIR.${MK_PC_SYSINSTALL}+=	pc-sysinstall
SUBDIR.${MK_PF}+=	ftp-proxy
SUBDIR.${MK_PKGBOOTSTRAP}+=	pkg
.if ${COMPILER_FEATURES:Mc++11}
SUBDIR.${MK_PMC}+=	pmc
.endif
SUBDIR.${MK_PMC}+=	pmcannotate pmccontrol pmcstat pmcstudy
SUBDIR.${MK_PORTSNAP}+=	portsnap
SUBDIR.${MK_PPP}+=	ppp
SUBDIR.${MK_QUOTAS}+=	edquota
SUBDIR.${MK_QUOTAS}+=	quotaon
SUBDIR.${MK_QUOTAS}+=	repquota
SUBDIR.${MK_SENDMAIL}+=	editmap
SUBDIR.${MK_SENDMAIL}+=	mailstats
SUBDIR.${MK_SENDMAIL}+=	makemap
SUBDIR.${MK_SENDMAIL}+=	praliases
SUBDIR.${MK_SENDMAIL}+=	sendmail
SUBDIR.${MK_TCP_WRAPPERS}+=	tcpdchk
SUBDIR.${MK_TCP_WRAPPERS}+=	tcpdmatch
SUBDIR.${MK_TOOLCHAIN}+=	config
SUBDIR.${MK_TOOLCHAIN}+=	crunch
SUBDIR.${MK_UNBOUND}+=	unbound
SUBDIR.${MK_USB}+=	uathload
SUBDIR.${MK_USB}+=	uhsoctl
SUBDIR.${MK_USB}+=	usbconfig
SUBDIR.${MK_USB}+=	usbdump
SUBDIR.${MK_UTMPX}+=	ac
SUBDIR.${MK_UTMPX}+=	lastlogin
SUBDIR.${MK_UTMPX}+=	utx
SUBDIR.${MK_WIRELESS}+=	ancontrol
SUBDIR.${MK_WIRELESS}+=	wlandebug
SUBDIR.${MK_WIRELESS}+=	wpa

SUBDIR.${MK_TESTS}+=	tests

.include <bsd.arch.inc.mk>

SUBDIR_PARALLEL=

.include <bsd.subdir.mk>
