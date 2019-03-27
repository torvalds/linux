#$FreeBSD$
#	@(#)Makefile	8.1 (Berkeley) 6/2/93

.include <src.opts.mk>

PACKAGE=rescue
MAN=
MK_SSP=	no
NO_SHARED=	yes

PROG=	rescue
BINDIR?=/rescue

# Shell scripts need #! line to be edited from /bin/sh to /rescue/sh
SCRIPTS= nextboot_FIXED
SCRIPTSNAME_nextboot_FIXED= nextboot
nextboot_FIXED: ../../sbin/reboot/nextboot.sh
	sed '1s/\/bin\//\/rescue\//' ${.ALLSRC} > ${.TARGET}
CLEANFILES+= nextboot_FIXED

SCRIPTS+= dhclient_FIXED
SCRIPTSNAME_dhclient_FIXED= dhclient-script
dhclient_FIXED: ../../sbin/dhclient/dhclient-script
	sed '1s/\/bin\//\/rescue\//' ${.ALLSRC} > ${.TARGET}
CLEANFILES+= dhclient_FIXED

# The help which used to be here is now in mk/bsd.crunchgen.mk

# Define Makefile variable RESCUE
CRUNCH_BUILDOPTS+= -DRESCUE
# Define compile-time RESCUE symbol when compiling components
CRUNCH_BUILDOPTS+= CRUNCH_CFLAGS=-DRESCUE

# An experiment that failed: try overriding bsd.lib.mk and bsd.prog.mk
# rather than incorporating rescue-specific logic into standard files.
#MAKEFLAGS= -m ${.CURDIR} ${.MAKEFLAGS}

# Hackery:  'librescue' exists merely as a tool for appropriately
# recompiling specific library entries.  We _know_ they're needed, and
# regular archive searching creates ugly library ordering problems.
# Easiest fix: tell the linker to include them into the executable
# first, so they are guaranteed to override the regular lib entries.
# Note that if 'librescue' hasn't been compiled, we'll just get the
# regular lib entries from libc and friends.
CRUNCH_LIBS+= ${.OBJDIR}/../librescue/*.o

###################################################################
# Programs from stock /bin
#
# WARNING: Changing this list may require adjusting
# /usr/include/paths.h as well!  You were warned!
#
CRUNCH_SRCDIRS+= bin
CRUNCH_PROGS_bin= cat chflags chio chmod cp date dd df echo 	\
	 ed expr getfacl hostname kenv kill ln ls mkdir mv	\
	 pkill ps pwd realpath rm rmdir setfacl sh sleep stty	\
	 sync test
CRUNCH_LIBS+= -lcrypt -ledit -ljail -lkvm -lelf -ll -ltermcapw -lutil -lxo
CRUNCH_BUILDTOOLS+= bin/sh

# Additional options for specific programs
CRUNCH_ALIAS_test= [
CRUNCH_ALIAS_sh= -sh
# The -sh alias shouldn't appear in /rescue as a hard link
CRUNCH_SUPPRESS_LINK_-sh= 1
CRUNCH_ALIAS_ln= link
CRUNCH_ALIAS_rm= unlink
CRUNCH_ALIAS_ed= red
CRUNCH_ALIAS_pkill= pgrep

.if ${MK_TCSH} != "no"
CRUNCH_PROGS_bin+= csh
CRUNCH_ALIAS_csh= -csh tcsh -tcsh
CRUNCH_BUILDTOOLS+= bin/csh
CRUNCH_SUPPRESS_LINK_-csh= 1
CRUNCH_SUPPRESS_LINK_-tcsh= 1
.endif

###################################################################
# Programs from standard /sbin
#
# WARNING: Changing this list may require adjusting
# /usr/include/paths.h as well!  You were warned!
#
# Note that mdmfs have their own private 'pathnames.h'
# headers in addition to the standard 'paths.h' header.
#
CRUNCH_SRCDIRS+= sbin
CRUNCH_PROGS_sbin= 						\
	camcontrol clri devfs dmesg dump			\
	dumpfs dumpon fsck fsck_ffs fsck_msdosfs fsdb		\
	fsirand gbde geom ifconfig init 			\
	kldconfig kldload kldstat kldunload ldconfig 		\
	md5 mdconfig mdmfs mknod mount mount_cd9660		\
	mount_msdosfs mount_nfs mount_nullfs			\
	mount_udf mount_unionfs newfs				\
	newfs_msdos nos-tun ping reboot				\
	restore rcorder route savecore		 		\
	shutdown spppcontrol swapon sysctl tunefs umount

.if ${MK_CCD} != "no"
CRUNCH_PROGS_sbin+= ccdconfig
.endif

.if ${MK_INET6_SUPPORT} != "no"
CRUNCH_PROGS_sbin+= ping6
CRUNCH_PROGS_sbin+= rtsol
.endif

.if ${MK_IPFILTER} != "no"
CRUNCH_PROGS_sbin+= ipf
CRUNCH_LIBS_ipf+=	${LIBIPF}
.endif

.if ${MK_ROUTED} != "no"
CRUNCH_PROGS_sbin+= routed rtquery
.endif

.if ${MK_ZFS} != "no"
CRUNCH_PROGS_sbin+= bectl
CRUNCH_PROGS_sbin+= zfs
CRUNCH_PROGS_sbin+= zpool
CRUNCH_PROGS_usr.sbin+= zdb
.endif

# crunchgen does not like C++ programs; this should be fixed someday
# CRUNCH_PROGS+= devd

CRUNCH_LIBS+= -l80211 -lalias -lcam -lncursesw -ldevstat -lipsec -llzma
.if ${MK_ZFS} != "no"
CRUNCH_LIBS+= -lavl -lzpool -lzfs_core -lzfs -lnvpair -lpthread -luutil -lumem
CRUNCH_LIBS+= -lbe
.else
# liblzma needs pthread
CRUNCH_LIBS+= -lpthread
.endif
CRUNCH_LIBS+= -lgeom -lbsdxml -lkiconv
.if ${MK_OPENSSL} == "no"
CRUNCH_LIBS+= -lmd
.endif
CRUNCH_LIBS+= -lmt -lsbuf -lufs -lz

.if ${MACHINE_CPUARCH} == "i386"
CRUNCH_PROGS_sbin+= bsdlabel sconfig fdisk
CRUNCH_ALIAS_bsdlabel= disklabel
#CRUNCH_PROGS+= mount_smbfs
#CRUNCH_LIBS+= -lsmb
.endif

.if ${MACHINE_CPUARCH} == "sparc64"
CRUNCH_PROGS_sbin+= bsdlabel sunlabel
.endif

.if ${MACHINE_CPUARCH} == "amd64"
CRUNCH_PROGS_sbin+= bsdlabel fdisk
CRUNCH_ALIAS_bsdlabel= disklabel
.endif

CRUNCH_SRCDIR_rtquery= ${SRCTOP}/sbin/routed/rtquery
CRUNCH_SRCDIR_ipf= ${SRCTOP}/sbin/ipf/ipf
.if ${MK_ZFS} != "no"
CRUNCH_SRCDIR_zfs= ${SRCTOP}/cddl/sbin/zfs
CRUNCH_SRCDIR_zpool= ${SRCTOP}/cddl/sbin/zpool
CRUNCH_SRCDIR_zdb= ${SRCTOP}/cddl/usr.sbin/zdb
.endif
CRUNCH_ALIAS_reboot= fastboot halt fasthalt
CRUNCH_ALIAS_restore= rrestore
CRUNCH_ALIAS_dump= rdump
CRUNCH_ALIAS_fsck_ffs= fsck_4.2bsd fsck_ufs
CRUNCH_ALIAS_geom= glabel gpart
CRUNCH_ALIAS_shutdown= poweroff

# dhclient has historically been troublesome...
CRUNCH_PROGS_sbin+= dhclient
CRUNCH_BUILDOPTS_dhclient= -DRELEASE_CRUNCH -Dlint

##################################################################
# Programs from stock /usr/bin
#
CRUNCH_SRCDIRS+= usr.bin

CRUNCH_PROGS_usr.bin= head mt sed tail tee

CRUNCH_PROGS_usr.bin+= gzip
CRUNCH_ALIAS_gzip= gunzip gzcat zcat

CRUNCH_PROGS_usr.bin+= bzip2
CRUNCH_ALIAS_bzip2= bunzip2 bzcat
CRUNCH_LIBS+= -lbz2

CRUNCH_PROGS_usr.bin+= less
CRUNCH_ALIAS_less= more

CRUNCH_PROGS_usr.bin+= xz
CRUNCH_ALIAS_xz= unxz lzma unlzma xzcat lzcat

CRUNCH_PROGS_usr.bin+= zstd
CRUNCH_ALIAS_zstd= unzstd zstdcat zstdmt
CRUNCH_LIBS+=	${LDADD_zstd}

CRUNCH_PROGS_usr.bin+= tar
CRUNCH_LIBS+= -larchive
.if ${MK_OPENSSL} != "no"
CRUNCH_LIBS+= -lcrypto
.endif
CRUNCH_LIBS+= -lmd

.if ${MK_NETCAT} != "no"
CRUNCH_PROGS_usr.bin+=	nc
.endif

.if ${MK_VI} != "no"
CRUNCH_PROGS_usr.bin+= vi
CRUNCH_ALIAS_vi= ex
.endif

CRUNCH_PROGS_usr.bin+= id
CRUNCH_ALIAS_id= groups whoami

##################################################################
# Programs from stock /usr/sbin
#
CRUNCH_SRCDIRS+= usr.sbin

CRUNCH_PROGS_usr.sbin+= chroot

CRUNCH_PROGS_usr.sbin+= chown
CRUNCH_ALIAS_chown= chgrp
##################################################################
CRUNCH_LIBS+= -lm

.if ${MK_ISCSI} != "no"
CRUNCH_PROGS_usr.bin+=	iscsictl
CRUNCH_PROGS_usr.sbin+=	iscsid
.endif

.include <bsd.crunchgen.mk>
.include <bsd.prog.mk>
