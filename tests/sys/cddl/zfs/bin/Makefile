# $FreeBSD$

.include <src.opts.mk>

PACKAGE=	tests
MAN=
BINDIR=		${TESTSBASE}/sys/cddl/zfs/bin
SCRIPTSDIR=	${TESTSBASE}/sys/cddl/zfs/bin

SCRIPTS+=	bsddisks.ksh
SCRIPTS+=	dircmp.ksh
SCRIPTS+=	dumpadm.ksh
SCRIPTS+=	ff.ksh
SCRIPTS+=	fmadm.ksh
SCRIPTS+=	fmdump.ksh
SCRIPTS+=	format.ksh
SCRIPTS+=	groupadd.ksh
SCRIPTS+=	groupdel.ksh
SCRIPTS+=	groupmod.ksh
SCRIPTS+=	groupshow.ksh
SCRIPTS+=	svcs.ksh
SCRIPTS+=	swap.ksh
SCRIPTS+=	testenv.ksh
SCRIPTS+=	useradd.ksh
SCRIPTS+=	userdel.ksh
SCRIPTS+=	usermod.ksh
SCRIPTS+=	zfs.ksh
SCRIPTS+=	zfs_crypto.ksh
SCRIPTS+=	zfs_version.ksh
SCRIPTS+=	zlogin.ksh
SCRIPTS+=	zoneadm.ksh
SCRIPTS+=	zonecfg.ksh
SCRIPTS+=	zpool.ksh
SCRIPTS+=	zpool_bsd.ksh
SCRIPTS+=	zpool_smi.ksh
SCRIPTS+=	zpool_version.ksh

PROGS+=		chg_usr_exec
# Not ported to FreeBSD
# PROGRS+=	devname2devid
PROGS+=		dir_rd_update
PROGS+=		file_check
PROGS+=		file_trunc
PROGS+=		file_write
PROGS+=		largest_file
PROGS+=		mkfile
PROGS+=		mktree
PROGS+=		mmapwrite
PROGS+=		randfree_file
PROGS+=		readmmap
PROGS+=		rename_dir
PROGS+=		rm_lnkcnt_zero_file

.for p in ${PROGS}
SRCS.$p=	$p.c
.endfor

LIBADD.mmapwrite+=	pthread
LIBADD.rm_lnkcnt_zero_file+=	pthread

.include <bsd.progs.mk>
