#	@(#)Makefile	8.5 (Berkeley) 3/31/94
# $FreeBSD$

.include <src.opts.mk>

SUBDIR=adjkerntz \
	camcontrol \
	clri \
	comcontrol \
	conscontrol \
	ddb \
	devfs \
	devmatch \
	dhclient \
	dmesg \
	dump \
	dumpfs \
	dumpon \
	etherswitchcfg \
	ffsinfo \
	fsck \
	fsck_ffs \
	fsck_msdosfs \
	fsdb \
	fsirand \
	gbde \
	geom \
	ggate \
	growfs \
	gvinum \
	ifconfig \
	init \
	kldconfig \
	kldload \
	kldstat \
	kldunload \
	ldconfig \
	md5 \
	mdconfig \
	mdmfs \
	mknod \
	mksnap_ffs \
	mount \
	mount_cd9660 \
	mount_fusefs \
	mount_msdosfs \
	mount_nfs \
	mount_nullfs \
	mount_udf \
	mount_unionfs \
	newfs \
	newfs_msdos \
	nfsiod \
	nos-tun \
	pfilctl \
	ping \
	rcorder \
	reboot \
	recoverdisk \
	resolvconf \
	restore \
	route \
	savecore \
	setkey \
	shutdown \
	spppcontrol \
	swapon \
	sysctl \
	tunefs \
	umount

SUBDIR.${MK_CCD}+=	ccdconfig
SUBDIR.${MK_CXX}+=	devd
SUBDIR.${MK_HAST}+=	hastctl
SUBDIR.${MK_HAST}+=	hastd
SUBDIR.${MK_INET6}+=	ping6
SUBDIR.${MK_INET6}+=	rtsol
SUBDIR.${MK_IPFILTER}+=	ipf
SUBDIR.${MK_IPFW}+=	ipfw
SUBDIR.${MK_IPFW}+=	natd
SUBDIR.${MK_ISCSI}+=	iscontrol
SUBDIR.${MK_NAND}+=	nandfs
SUBDIR.${MK_NAND}+=	newfs_nandfs
SUBDIR.${MK_NVME}+=	nvmecontrol
SUBDIR.${MK_OPENSSL}+=	decryptcore
SUBDIR.${MK_PF}+=	pfctl
SUBDIR.${MK_PF}+=	pflogd
SUBDIR.${MK_QUOTAS}+=	quotacheck
SUBDIR.${MK_ROUTED}+=	routed
SUBDIR.${MK_VERIEXEC}+=	veriexec
SUBDIR.${MK_ZFS}+=	bectl
SUBDIR.${MK_ZFS}+=	zfsbootcfg

SUBDIR.${MK_TESTS}+=	tests

.include <bsd.arch.inc.mk>

SUBDIR_PARALLEL=

.include <bsd.subdir.mk>
