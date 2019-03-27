#	@(#)Makefile	8.1 (Berkeley) 6/5/93
#	$FreeBSD$

.include <src.opts.mk>

PACKAGE=runtime-manuals

#MISSING: dump.5 plot.5
MAN=	acct.5 \
	ar.5 \
	a.out.5 \
	${_boot.config.5} \
	cd9660.5 \
	core.5 \
	devfs.5 \
	devfs.conf.5 \
	devfs.rules.5 \
	device.hints.5 \
	dir.5 \
	disktab.5 \
	elf.5 \
	ethers.5 \
	eui64.5 \
	ext2fs.5 \
	fbtab.5 \
	fdescfs.5 \
	forward.5 \
	fs.5 \
	fstab.5 \
	group.5 \
	hosts.5 \
	hosts.equiv.5 \
	hosts.lpd.5 \
	intro.5 \
	libmap.conf.5 \
	link.5 \
	linprocfs.5 \
	linsysfs.5 \
	mailer.conf.5 \
	make.conf.5 \
	moduli.5 \
	motd.5 \
	mount.conf.5 \
	mqueuefs.5 \
	msdosfs.5 \
	networks.5 \
	nsmb.conf.5 \
	nsswitch.conf.5 \
	nullfs.5 \
	passwd.5 \
	pbm.5 \
	periodic.conf.5 \
	phones.5 \
	portindex.5 \
	portsnap.conf.5 \
	procfs.5 \
	protocols.5 \
	quota.user.5 \
	rc.conf.5 \
	rctl.conf.5 \
	regdomain.5 \
	remote.5 \
	resolver.5 \
	services.5 \
	shells.5 \
	src.conf.5 \
	stab.5 \
	style.Makefile.5 \
	style.mdoc.5 \
	sysctl.conf.5 \
	tmpfs.5

MLINKS=	dir.5 dirent.5
MLINKS+=ext2fs.5 ext4fs.5
MLINKS+=fs.5 inode.5
MLINKS+=hosts.equiv.5 rhosts.5
MLINKS+=msdosfs.5 msdos.5
MLINKS+=passwd.5 master.passwd.5
MLINKS+=portindex.5 INDEX.5
MLINKS+=quota.user.5 quota.group.5
MLINKS+=rc.conf.5 rc.conf.local.5
MLINKS+=resolver.5 resolv.conf.5
MLINKS+=src.conf.5 src-env.conf.5

.if ${MK_AUTOFS} != "no"
MAN+=	autofs.5
.endif

.if ${MK_BLUETOOTH} != "no"
MAN+=	bluetooth.device.conf.5 \
	bluetooth.hosts.5 \
	bluetooth.protocols.5
.endif

.if ${MK_FREEBSD_UPDATE} != "no"
MAN+=	freebsd-update.conf.5
.endif

.if ${MK_HESIOD} != "no"
MAN+=	hesiod.conf.5 
.endif

.if ${MK_NAND} != "no"
MAN+=	nandfs.5
.endif

.if ${MK_PF} != "no"
MAN+=	pf.conf.5 \
	pf.os.5
.endif

.if ${MACHINE_CPUARCH} == "amd64" || ${MACHINE_CPUARCH} == "i386"
_boot.config.5=	boot.config.5
.endif

.include <bsd.prog.mk>
