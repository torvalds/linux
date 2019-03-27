# $FreeBSD$
#
# Doing a make install builds /usr/share/examples

.include <src.opts.mk>

PACKAGE=examples
FILESDIR=	${SHAREDIR}/examples

LDIRS=	BSD_daemon \
	FreeBSD_version \
	IPv6 \
	bootforth \
	csh \
	diskless \
	drivers \
	etc \
	find_interface \
	indent \
	ipfw \
	jails \
	kld \
	libvgl \
	mdoc \
	netgraph \
	perfmon \
	ppi \
	ppp \
	printing \
	ses \
	scsi_target \
	sunrpc \
	ypldap


SE_DIRS+=	BSD_daemon
SE_BSD_DAEMON= \
	FreeBSD.pfa \
	README \
	beastie.eps \
	beastie.fig \
	eps.patch \
	poster.sh

.if ${MACHINE_CPUARCH} == "amd64"
.if ${MK_BHYVE} != "no"
LDIRS+=		bhyve
SE_DIRS+=	bhyve
SE_BHYVE=	vmrun.sh
PACKAGE_bhyve/vmrun.sh=	bhyve
.endif
.endif

SE_DIRS+=	FreeBSD_version
SE_FREEBSD_VERSION= \
	FreeBSD_version.c \
	Makefile \
	README

SE_DIRS+=	IPv6
SE_IPV6=	USAGE

SE_DIRS+=	bootforth
SE_BOOTFORTH= \
	README \
	boot.4th \
	frames.4th \
	loader.rc \
	menu.4th \
	menuconf.4th \
	screen.4th

SE_DIRS+=	csh
SE_CSH=	dot.cshrc

SE_DIRS+=	diskless
SE_DISKLESS= \
	ME \
	README.BOOTP \
	README.TEMPLATING \
	clone_root

SE_DIRS+=	drivers
SE_DRIVERS= \
	README \
	make_device_driver.sh \
	make_pseudo_driver.sh

SE_DIRS+=	etc
SE_ETC= \
	README.examples \
	bsd-style-copyright \
	make.conf

SE_DIRS+=	find_interface
SE_FIND_INTERFACE= \
	Makefile \
	README \
	find_interface.c

SE_DIRS+=	indent
SE_INDENT=	indent.pro

.if ${MK_IPFILTER} != "no"
SUBDIR+=	ipfilter
.endif

SE_DIRS+=	ipfw
SE_IPFW=	change_rules.sh

SE_DIRS+=	jails
SE_JAILS= \
	README \
	VIMAGE \
	jail.xxx.conf \
	jib \
	jng \
	rc.conf.jails \
	rcjail.xxx.conf

SE_DIRS+=	kld
SE_KLD=	Makefile

SE_DIRS+=	kld/cdev
SE_KLD_CDEV= \
	Makefile \
	README \

SE_DIRS+=	kld/cdev/module
SE_KLD_CDEV_MODULE= \
	Makefile \
	cdev.c \
	cdev.h \
	cdevmod.c

SE_DIRS+=	kld/cdev/test
SE_KLD_CDEV_TEST= \
	Makefile \
	testcdev.c

SE_DIRS+=	kld/dyn_sysctl
SE_KLD_DYN_SYSCTL= \
	Makefile \
	README \
	dyn_sysctl.c

SE_DIRS+=	kld/firmware
SE_KLD_FIRMWARE= \
	Makefile \
	README

SE_DIRS+=	kld/firmware/fwconsumer
SE_KLD_FIRMWARE_FWCONSUMER= \
	Makefile \
	fw_consumer.c

SE_DIRS+=	kld/firmware/fwimage
SE_KLD_FIRMWARE_FWIMAGE= \
	Makefile \
	firmware.img.uu

SE_DIRS+=	kld/khelp
SE_KLD_KHELP= \
	Makefile \
	README \
	h_example.c

SE_DIRS+=	kld/syscall
SE_KLD_SYSCALL=	Makefile

SE_DIRS+=	kld/syscall/module
SE_KLD_SYSCALL_MODULE= \
	Makefile \
	syscall.c

SE_DIRS+=	kld/syscall/test
SE_KLD_SYSCALL_TEST= \
	Makefile \
	call.c

SE_DIRS+=	libvgl
SE_LIBVGL=	\
	Makefile \
	demo.c

SE_DIRS+=	mdoc
SE_MDOC= \
	POSIX-copyright \
	deshallify.sh \
	example.1 \
	example.3 \
	example.4 \
	example.9

SE_DIRS+=	netgraph
SE_NETGRAPH= \
	ether.bridge \
	frame_relay \
	ngctl \
	raw \
	udp.tunnel \
	virtual.chain \
	virtual.lan \

SE_DIRS+=	perfmon
SE_PERFMON= \
	Makefile \
	README \
	perfmon.c \

.if ${MK_PF} != "no"
SE_DIRS+=	pf
SE_PF= \
	ackpri \
	faq-example1 \
	faq-example2 \
	faq-example3 \
	pf.conf \
	queue1 \
	queue2 \
	queue3 \
	queue4 \
	spamd
.endif

SE_DIRS+=	ppi
SE_PPI= \
	Makefile \
	ppilcd.c

SE_DIRS+=	ppp
SE_PPP= \
	chap-auth \
	login-auth \
	ppp.conf.sample \
	ppp.conf.span-isp \
	ppp.conf.span-isp.working \
	ppp.linkdown.sample \
	ppp.linkdown.span-isp \
	ppp.linkdown.span-isp.working \
	ppp.linkup.sample \
	ppp.linkup.span-isp \
	ppp.linkup.span-isp.working \
	ppp.secret.sample \
	ppp.secret.span-isp \
	ppp.secret.span-isp.working

SE_DIRS+=	printing
SE_PRINTING= \
	diablo-if-net \
	hpdf \
	hpif \
	hpof \
	hprf \
	hpvf \
	if-simple \
	if-simpleX \
	ifhp \
	make-ps-header \
	netprint \
	psdf \
	psdfX \
	psif \
	pstf \
	pstfX

SE_DIRS+=	ses
SE_SES= \
	Makefile \
	Makefile.inc

SE_DIRS+=	ses/getencstat
SE_SES_GETENCSTAT= \
	Makefile \
	getencstat.0

SE_DIRS+=	ses/sesd
SE_SES_SESD= \
	Makefile \
	sesd.0

SE_DIRS+=	ses/setencstat
SE_SES_SETENCSTAT= \
	Makefile \
	setencstat.0

SE_DIRS+=	ses/setobjstat
SE_SES_SETOBJSTAT= \
	Makefile \
	setobjstat.0

SE_DIRS+=	ses/srcs
SE_SES_SRCS= \
	chpmon.c \
	eltsub.c \
	eltsub.h \
	getencstat.c \
	getnobj.c \
	getobjmap.c \
	getobjstat.c \
	inienc.c \
	sesd.c \
	setencstat.c \
	setobjstat.c

SE_DIRS+=	scsi_target
SE_SCSI_TARGET= \
	Makefile \
	scsi_target.c \
	scsi_target.h \
	scsi_target.8 \
	scsi_cmds.c

SE_DIRS+=	sunrpc
SE_SUNRPC=	Makefile

SE_DIRS+=	sunrpc/dir
SE_SUNRPC_DIR= \
	Makefile \
	dir.x \
	dir_proc.c \
	rls.c

SE_DIRS+=	sunrpc/msg
SE_SUNRPC_MSG= \
	Makefile \
	msg.x \
	msg_proc.c \
	printmsg.c \
	rprintmsg.c

SE_DIRS+=	sunrpc/sort
SE_SUNRPC_SORT= \
	Makefile \
	rsort.c \
	sort.x \
	sort_proc.c

.if ${MK_EFI} != "no"
LDIRS+=		uefisign
SE_DIRS+=	uefisign
SE_UEFISIGN=	uefikeys
.endif

SE_DIRS+=	ypldap
SE_YPLDAP=	ypldap.conf

.if ${MK_HAST} != "no"
LDIRS+=		hast
SE_DIRS+=	hast
SE_HAST=	ucarp.sh \
	ucarp_down.sh \
	ucarp_up.sh \
	vip-down.sh \
	vip-up.sh
.endif

.if ${MK_USB} != "no"
LDIRS+=		libusb20
SE_DIRS+=	libusb20
SE_LIBUSB20= \
	Makefile \
	README \
	util.c \
	util.h \
	bulk.c \
	control.c
.endif


# Setup the FILES_GROUPS for all DIRS variables above.
# The variables are prefixed by 'SE_' to prevent variable collision in
# other parts of the system
.for d in ${SE_DIRS}
.for f in ${SE_${d:tu:C/\//_/g}}
SER_${d:tu:C/\//_/g}+=	${d}/${f}
.endfor
FILESGROUPS+=	SER_${d:tu:C/\//_/g}
SER_${d:tu:C/\//_/g}DIR+=	${SHAREDIR}/examples/${d}
.endfor

BINDIR= ${SHAREDIR}/examples

beforeinstall: copies etc-examples
META_TARGETS+=	copies
.ORDER: etc-examples

copies:
.for i in ${LDIRS}
	if [ -L ${DESTDIR}${BINDIR}/$i ]; then \
		rm -f ${DESTDIR}${BINDIR}/$i; \
	fi
.endfor

etc-examples:
	${_+_}(cd ${SRCTOP}/etc; ${MAKE} etc-examples)

SUBDIR+=	smbfs

HAS_TESTS=
SUBDIR.${MK_TESTS}+= tests

SUBDIR_PARALLEL=

.include <bsd.prog.mk>
