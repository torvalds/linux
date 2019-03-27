#	@(#)Makefile	8.2 (Berkeley) 1/4/94
# $FreeBSD$
#
# Doing a "make install" builds /usr/include.

.include <src.opts.mk>

PACKAGE=runtime
TAGS+=	development
CLEANFILES= osreldate.h version
SUBDIR= arpa protocols rpcsvc rpc xlocale
SUBDIR_PARALLEL=
INCS=	a.out.h ar.h assert.h bitstring.h complex.h cpio.h _ctype.h ctype.h \
	db.h \
	dirent.h dlfcn.h elf.h elf-hints.h err.h fmtmsg.h fnmatch.h fstab.h \
	fts.h ftw.h getopt.h glob.h grp.h \
	ieeefp.h ifaddrs.h \
	inttypes.h iso646.h kenv.h langinfo.h libgen.h limits.h link.h \
	locale.h malloc.h malloc_np.h memory.h monetary.h mpool.h mqueue.h \
	ndbm.h netconfig.h \
	netdb.h nl_types.h nlist.h nss.h nsswitch.h paths.h \
	printf.h proc_service.h pthread.h \
	pthread_np.h pwd.h ranlib.h readpassphrase.h regex.h \
	res_update.h resolv.h runetype.h search.h semaphore.h setjmp.h \
	signal.h spawn.h stab.h stdalign.h stdbool.h stddef.h \
	stdnoreturn.h stdio.h stdlib.h string.h stringlist.h \
	strings.h sysexits.h tar.h termios.h tgmath.h \
	time.h timeconv.h timers.h ttyent.h \
	uchar.h ulimit.h unistd.h utime.h utmpx.h uuid.h varargs.h \
	wchar.h wctype.h wordexp.h xlocale.h

.PATH: ${SRCTOP}/contrib/libc-vis
INCS+=	vis.h

MHDRS=	float.h floatingpoint.h stdarg.h

PHDRS=	sched.h _semaphore.h

LHDRS=	aio.h errno.h fcntl.h linker_set.h poll.h stdatomic.h stdint.h \
	syslog.h ucontext.h

LDIRS=	bsm cam geom net net80211 netgraph netinet netinet6 \
	netipsec netsmb nfs nfsclient nfsserver sys vm

LSUBDIRS=	cam/ata cam/mmc cam/nvme cam/scsi \
	dev/acpica dev/agp dev/an dev/bktr dev/ciss dev/filemon dev/firewire \
	dev/hwpmc dev/hyperv \
	dev/ic dev/iicbus dev/io dev/mfi dev/mmc dev/nvme \
	dev/ofw dev/pbio dev/pci ${_dev_powermac_nvram} dev/ppbus dev/smbus \
	dev/speaker dev/tcp_log dev/veriexec dev/vkbd dev/wi \
	fs/devfs fs/fdescfs fs/msdosfs fs/nandfs fs/nfs fs/nullfs \
	fs/procfs fs/smbfs fs/udf fs/unionfs \
	geom/cache geom/concat geom/eli geom/gate geom/journal geom/label \
	geom/mirror geom/mountver geom/multipath geom/nop \
	geom/raid geom/raid3 geom/shsec geom/stripe geom/virstor \
	net/altq \
	netgraph/atm netgraph/netflow \
	netinet/cc \
	netinet/netdump \
	security/audit \
	security/mac_biba security/mac_bsdextended security/mac_lomac \
	security/mac_mls security/mac_partition \
	security/mac_veriexec \
	sys/disk \
	ufs/ffs ufs/ufs

LSUBSUBDIRS=	dev/mpt/mpilib

.if ${MK_BLUETOOTH} != "no"
LSUBSUBDIRS+=	netgraph/bluetooth/include
.endif

.if ${MK_CUSE} != "no"
LSUBDIRS+=	fs/cuse
.endif

.if ${MK_GSSAPI} != "no"
SUBDIR+=	gssapi
INCS+=		gssapi.h
.endif

.if ${MK_HESIOD} != "no"
INCS+=	hesiod.h
.endif

# Handle the #define aliases for libiconv
.if ${MK_ICONV} == "yes"
INCS+=		iconv.h
.endif

.if ${MK_USB} != "no"
LSUBDIRS+=	dev/usb
.endif

.if ${MACHINE_ARCH} == "powerpc" || ${MACHINE_ARCH} == "powerpc64"
_dev_powermac_nvram=	dev/powermac_nvram
.endif

# Define SHARED to indicate whether you want symbolic links to the system
# source (``symlinks''), or a separate copy (``copies'').  ``symlinks'' is
# probably only useful for developers and should be avoided if you do not
# wish to tie your /usr/include and /usr/src together.
#SHARED=	symlinks
SHARED?=	copies

INCS+=	osreldate.h

SYSDIR=			${SRCTOP}/sys
NEWVERS_SH=		${SYSDIR}/conf/newvers.sh
PARAM_H=		${SYSDIR}/sys/param.h
MK_OSRELDATE_SH=	${.CURDIR}/mk-osreldate.sh

SYMLINKS+= ${LIBDIR:C,[^/]+,..,g:C,^/,,}${INCLUDEDIR} ${LIBDIR}/include

osreldate.h: ${NEWVERS_SH} ${PARAM_H} ${MK_OSRELDATE_SH}
	env NEWVERS_SH=${NEWVERS_SH} PARAMFILE=${PARAM_H} SYSDIR=${SYSDIR} \
	    sh ${MK_OSRELDATE_SH}

.for i in ${LHDRS}
INCSLINKS+=	sys/$i ${INCLUDEDIR}/$i
.endfor
.for i in ${MHDRS}
INCSLINKS+=	machine/$i ${INCLUDEDIR}/$i
.endfor
.for i in ${PHDRS}
INCSLINKS+=	sys/$i ${INCLUDEDIR}/$i
.endfor

.if ${MACHINE} != ${MACHINE_CPUARCH}
_MARCHS=	${MACHINE_CPUARCH}
.endif
.if ${MACHINE_CPUARCH} == "i386" || ${MACHINE_CPUARCH} == "amd64"
_MARCHS+=	x86
.endif

META_TARGETS+=	compat
stage_includes: ${SHARED}
SDESTDIR=	${SYSROOT:U${DESTDIR}}

# Take care of stale directory-level symlinks.
compat:
.for i in ${LDIRS} ${LSUBDIRS} machine ${_MARCHS} crypto
	if [ -L ${SDESTDIR}${INCLUDEDIR}/$i ]; then \
		rm -f ${SDESTDIR}${INCLUDEDIR}/$i; \
	fi
.endfor
	mtree -deU ${NO_ROOT:D-W} ${MTREE_FOLLOWS_SYMLINKS} \
	    -f ${SRCTOP}/etc/mtree/BSD.include.dist \
	    -p ${SDESTDIR}${INCLUDEDIR} > /dev/null

copies: .PHONY .META
.for i in ${LDIRS} ${LSUBDIRS} ${LSUBSUBDIRS} crypto machine machine/pc \
	${_MARCHS}
	if [ -d ${SDESTDIR}${INCLUDEDIR}/$i ]; then \
		cd ${SDESTDIR}${INCLUDEDIR}/$i; \
		for h in *.h; do \
			if [ -L $$h ]; then rm -f $$h; fi; \
		done; \
	fi
.endfor
.for i in ${LDIRS} ${LSUBDIRS:Ndev/agp:Ndev/acpica:Ndev/bktr:Ndev/evdev:Ndev/hyperv:Ndev/nand:Ndev/pci:Ndev/veriexec} ${LSUBSUBDIRS}
	cd ${SRCTOP}/sys; \
	${INSTALL} -C ${TAG_ARGS} -o ${BINOWN} -g ${BINGRP} -m 444 $i/*.h \
	    ${SDESTDIR}${INCLUDEDIR}/$i
.endfor
	cd ${SRCTOP}/sys/dev/acpica; \
	${INSTALL} -C ${TAG_ARGS} -o ${BINOWN} -g ${BINGRP} -m 444 acpiio.h \
	    ${SDESTDIR}${INCLUDEDIR}/dev/acpica; \
	${INSTALL} -C ${TAG_ARGS} -o ${BINOWN} -g ${BINGRP} -m 444 acpi_hpet.h \
	    ${SDESTDIR}${INCLUDEDIR}/dev/acpica
	cd ${SRCTOP}/sys/dev/agp; \
	${INSTALL} -C ${TAG_ARGS} -o ${BINOWN} -g ${BINGRP} -m 444 agpreg.h \
	    ${SDESTDIR}${INCLUDEDIR}/dev/agp
	cd ${SRCTOP}/sys/dev/bktr; \
	${INSTALL} -C ${TAG_ARGS} -o ${BINOWN} -g ${BINGRP} -m 444 ioctl_*.h \
	    ${SDESTDIR}${INCLUDEDIR}/dev/bktr
.if ${MK_NAND} != "no"
	cd ${SRCTOP}/sys/dev/nand; \
	${INSTALL} -C ${TAG_ARGS} -o ${BINOWN} -g ${BINGRP} -m 444 nandsim.h \
	    ${SDESTDIR}${INCLUDEDIR}/dev/nand; \
	${INSTALL} -C ${TAG_ARGS} -o ${BINOWN} -g ${BINGRP} -m 444 nand_dev.h \
	    ${SDESTDIR}${INCLUDEDIR}/dev/nand
.endif
	cd ${SRCTOP}/sys/dev/evdev; \
	${INSTALL} -C -o ${BINOWN} -g ${BINGRP} -m 444 input.h \
	    ${SDESTDIR}${INCLUDEDIR}/dev/evdev; \
	${INSTALL} -C -o ${BINOWN} -g ${BINGRP} -m 444 input-event-codes.h \
	    ${SDESTDIR}${INCLUDEDIR}/dev/evdev; \
	${INSTALL} -C -o ${BINOWN} -g ${BINGRP} -m 444 uinput.h \
	    ${SDESTDIR}${INCLUDEDIR}/dev/evdev
	cd ${SRCTOP}/sys/dev/hyperv/include; \
	${INSTALL} -C ${TAG_ARGS} -o ${BINOWN} -g ${BINGRP} -m 444 hyperv.h \
	    ${SDESTDIR}${INCLUDEDIR}/dev/hyperv
	cd ${SRCTOP}/sys/dev/hyperv/utilities; \
	${INSTALL} -C ${TAG_ARGS} -o ${BINOWN} -g ${BINGRP} -m 444 hv_snapshot.h \
	    ${SDESTDIR}${INCLUDEDIR}/dev/hyperv
	cd ${SRCTOP}/sys/dev/pci; \
	${INSTALL} -C ${TAG_ARGS} -o ${BINOWN} -g ${BINGRP} -m 444 pcireg.h \
	    ${SDESTDIR}${INCLUDEDIR}/dev/pci
	cd ${SRCTOP}/sys/dev/veriexec; \
	${INSTALL} -C -o ${BINOWN} -g ${BINGRP} -m 444 veriexec_ioctl.h \
	    ${SDESTDIR}${INCLUDEDIR}/dev/veriexec
	cd ${SRCTOP}/sys/fs/cd9660/; \
	${INSTALL} -C ${TAG_ARGS} -o ${BINOWN} -g ${BINGRP} -m 444 *.h \
	    ${SDESTDIR}${INCLUDEDIR}/isofs/cd9660
.if ${MK_IPFILTER} != "no"
	cd ${SRCTOP}/sys/contrib/ipfilter/netinet; \
	${INSTALL} -C ${TAG_ARGS} -o ${BINOWN} -g ${BINGRP} -m 444 *.h \
	    ${SDESTDIR}${INCLUDEDIR}/netinet
.endif
.if ${MK_PF} != "no"
	cd ${SRCTOP}/sys/netpfil/pf; \
	${INSTALL} -C ${TAG_ARGS} -o ${BINOWN} -g ${BINGRP} -m 444 *.h \
	    ${SDESTDIR}${INCLUDEDIR}/netpfil/pf
.endif
	cd ${SRCTOP}/sys/crypto; \
	${INSTALL} -C ${TAG_ARGS} -o ${BINOWN} -g ${BINGRP} -m 444 rijndael/rijndael.h \
	    ${SDESTDIR}${INCLUDEDIR}/crypto
	cd ${SRCTOP}/sys/opencrypto; \
	${INSTALL} -C ${TAG_ARGS} -o ${BINOWN} -g ${BINGRP} -m 444 *.h \
	    ${SDESTDIR}${INCLUDEDIR}/crypto
	cd ${SRCTOP}/sys/${MACHINE}/include; \
	${INSTALL} -C ${TAG_ARGS} -o ${BINOWN} -g ${BINGRP} -m 444 *.h \
	    ${SDESTDIR}${INCLUDEDIR}/machine
.if exists(${SRCTOP}/sys/${MACHINE}/include/pc)
	cd ${SRCTOP}/sys/${MACHINE}/include/pc; \
	${INSTALL} -C ${TAG_ARGS} -o ${BINOWN} -g ${BINGRP} -m 444 *.h \
	    ${SDESTDIR}${INCLUDEDIR}/machine/pc
.endif
.for _MARCH in ${_MARCHS}
.if exists(${SRCTOP}/sys/${_MARCH}/include)
	${INSTALL} -d ${TAG_ARGS} -o ${BINOWN} -g ${BINGRP} -m 755 \
	    ${SDESTDIR}${INCLUDEDIR}/${_MARCH}; \
	cd ${SRCTOP}/sys/${_MARCH}/include; \
	${INSTALL} -C ${TAG_ARGS} -o ${BINOWN} -g ${BINGRP} -m 444 *.h \
	    ${SDESTDIR}${INCLUDEDIR}/${_MARCH}
.if exists(${SRCTOP}/sys/${_MARCH}/include/pc)
	${INSTALL} -d ${TAG_ARGS} -o ${BINOWN} -g ${BINGRP} -m 755 \
	    ${SDESTDIR}${INCLUDEDIR}/${_MARCH}/pc; \
	cd ${SRCTOP}/sys/${_MARCH}/include/pc; \
	${INSTALL} -C ${TAG_ARGS} -o ${BINOWN} -g ${BINGRP} -m 444 *.h \
	    ${SDESTDIR}${INCLUDEDIR}/${_MARCH}/pc
.endif
.endif
.endfor
	cd ${SRCTOP}/sys/rpc; \
	${INSTALL} -C ${TAG_ARGS} -o ${BINOWN} -g ${BINGRP} -m 444 types.h \
	    ${SDESTDIR}${INCLUDEDIR}/rpc
	cd ${SRCTOP}/sys/teken; \
	${INSTALL} -C ${TAG_ARGS} -o ${BINOWN} -g ${BINGRP} -m 444 teken.h \
	    ${SDESTDIR}${INCLUDEDIR}/teken
.if ${MK_CDDL} != "no"
	cd ${SRCTOP}/sys/cddl/contrib/opensolaris/uts/common/sys; \
	${INSTALL} -C ${TAG_ARGS} -o ${BINOWN} -g ${BINGRP} -m 444 nvpair.h \
	    ${SDESTDIR}${INCLUDEDIR}/sys
.endif
.if ${MK_MLX5TOOL} != "no"
	cd ${SRCTOP}/sys/dev/mlx5; \
	${INSTALL} -C ${TAG_ARGS} -o ${BINOWN} -g ${BINGRP} -m 444 mlx5io.h \
	    ${SDESTDIR}${INCLUDEDIR}/dev/mlx5
.endif

symlinks: .PHONY .META
	@${ECHO} "Setting up symlinks to kernel source tree..."
.for i in ${LDIRS}
	cd ${SRCTOP}/sys/$i; \
	for h in *.h; do \
		${INSTALL_SYMLINK} ${TAG_ARGS} ../../../sys/$i/$$h ${SDESTDIR}${INCLUDEDIR}/$i; \
	done
.endfor
.for i in ${LSUBDIRS:Ndev/agp:Ndev/acpica:Ndev/bktr:Ndev/evdev:Ndev/hyperv:Ndev/nand:Ndev/pci:Ndev/veriexec}
	cd ${SRCTOP}/sys/$i; \
	for h in *.h; do \
		${INSTALL_SYMLINK} ${TAG_ARGS} ../../../../sys/$i/$$h ${SDESTDIR}${INCLUDEDIR}/$i; \
	done
.endfor
	cd ${SRCTOP}/sys/dev/acpica; \
	for h in acpiio.h acpi_hpet.h; do \
		${INSTALL_SYMLINK} ${TAG_ARGS} ../../../../sys/dev/acpica/$$h \
		    ${SDESTDIR}${INCLUDEDIR}/dev/acpica; \
	done
	cd ${SRCTOP}/sys/dev/agp; \
	for h in agpreg.h; do \
		${INSTALL_SYMLINK} ${TAG_ARGS} ../../../../sys/dev/agp/$$h \
		    ${SDESTDIR}${INCLUDEDIR}/dev/agp; \
	done
	cd ${SRCTOP}/sys/dev/bktr; \
	for h in ioctl_*.h; do \
		${INSTALL_SYMLINK} ${TAG_ARGS} ../../../../sys/dev/bktr/$$h \
		    ${SDESTDIR}${INCLUDEDIR}/dev/bktr; \
	done
.if ${MK_NAND} != "no"
	cd ${SRCTOP}/sys/dev/nand; \
	for h in nandsim.h nand_dev.h; do \
		${INSTALL_SYMLINK} ${TAG_ARGS} ../../../../sys/dev/nand/$$h \
		    ${SDESTDIR}${INCLUDEDIR}/dev/nand; \
	done
.endif
	cd ${SRCTOP}/sys/dev/evdev; \
	for h in input.h input-event-codes.h uinput.h; do \
		ln -fs ../../../../sys/dev/evdev/$$h \
		    ${SDESTDIR}${INCLUDEDIR}/dev/evdev; \
	done
	cd ${SRCTOP}/sys/dev/hyperv/include; \
	for h in hyperv.h; do \
		${INSTALL_SYMLINK} ${TAG_ARGS} ../../../../sys/dev/hyperv/include/$$h \
		    ${SDESTDIR}${INCLUDEDIR}/dev/hyperv; \
	done
	cd ${SRCTOP}/sys/dev/hyperv/utilities; \
	for h in hv_snapshot.h; do \
		${INSTALL_SYMLINK} ${TAG_ARGS} ../../../../sys/dev/hyperv/utilities/$$h \
		    ${SDESTDIR}${INCLUDEDIR}/dev/hyperv; \
	done
	cd ${SRCTOP}/sys/dev/pci; \
	for h in pcireg.h; do \
		${INSTALL_SYMLINK} ${TAG_ARGS} ../../../../sys/dev/pci/$$h \
		    ${SDESTDIR}${INCLUDEDIR}/dev/pci; \
	done
	cd ${SRCTOP}/sys/dev/veriexec; \
	for h in veriexec_ioctl.h; do \
		ln -fs ../../../../sys/dev/veriexec/$$h \
		    ${SDESTDIR}${INCLUDEDIR}/dev/veriexec; \
	done
.for i in ${LSUBSUBDIRS}
	cd ${SRCTOP}/sys/$i; \
	for h in *.h; do \
		${INSTALL_SYMLINK} ${TAG_ARGS} ../../../../../sys/$i/$$h ${SDESTDIR}${INCLUDEDIR}/$i; \
	done
.endfor
.if ${MK_IPFILTER} != "no"
	cd ${SRCTOP}/sys/contrib/ipfilter/netinet; \
	for h in *.h; do \
		${INSTALL_SYMLINK} ${TAG_ARGS} ../../../sys/contrib/ipfilter/netinet/$$h \
		    ${SDESTDIR}${INCLUDEDIR}/netinet; \
	done
.endif
.if ${MK_PF} != "no"
	cd ${SRCTOP}/sys/netpfil/pf; \
	for h in *.h; do \
		${INSTALL_SYMLINK} ${TAG_ARGS} ../../../../sys/netpfil/pf/$$h \
		    ${SDESTDIR}${INCLUDEDIR}/netpfil/pf; \
	done
.endif
	cd ${SRCTOP}/sys/crypto; \
	for h in rijndael/rijndael.h; do \
		${INSTALL_SYMLINK} ${TAG_ARGS} ../../../sys/crypto/$$h \
		    ${SDESTDIR}${INCLUDEDIR}/crypto; \
	done
	cd ${SRCTOP}/sys/opencrypto; \
	for h in *.h; do \
		${INSTALL_SYMLINK} ${TAG_ARGS} ../../../sys/opencrypto/$$h \
		    ${SDESTDIR}${INCLUDEDIR}/crypto; \
	done
	cd ${SRCTOP}/sys/${MACHINE}/include; \
	for h in *.h; do \
		${INSTALL_SYMLINK} ${TAG_ARGS} ../../../sys/${MACHINE}/include/$$h \
		    ${SDESTDIR}${INCLUDEDIR}/machine; \
	done
.if exists(${SRCTOP}/sys/${MACHINE}/include/pc)
	cd ${SRCTOP}/sys/${MACHINE}/include/pc; \
	for h in *.h; do \
		${INSTALL_SYMLINK} ${TAG_ARGS} ../../../../sys/${MACHINE}/include/pc/$$h \
		    ${SDESTDIR}${INCLUDEDIR}/machine/pc; \
	done
.endif
.for _MARCH in ${_MARCHS}
.if exists(${SRCTOP}/sys/${_MARCH}/include)
	${INSTALL} -d ${TAG_ARGS} -o ${BINOWN} -g ${BINGRP} -m 755 \
	    ${SDESTDIR}${INCLUDEDIR}/${_MARCH}; \
	cd ${SRCTOP}/sys/${_MARCH}/include; \
	for h in *.h; do \
		${INSTALL_SYMLINK} ${TAG_ARGS} ../../../sys/${_MARCH}/include/$$h \
		    ${SDESTDIR}${INCLUDEDIR}/${_MARCH}; \
	done
.if exists(${SRCTOP}/sys/${_MARCH}/include/pc)
	${INSTALL} -d ${TAG_ARGS} -o ${BINOWN} -g ${BINGRP} -m 755 \
	    ${SDESTDIR}${INCLUDEDIR}/${_MARCH}/pc; \
	cd ${SRCTOP}/sys/${_MARCH}/include/pc; \
	for h in *.h; do \
		${INSTALL_SYMLINK} ${TAG_ARGS} ../../../../sys/${_MARCH}/include/pc/$$h \
		    ${SDESTDIR}${INCLUDEDIR}/${_MARCH}/pc; \
	done
.endif
.endif
.endfor
	cd ${SRCTOP}/sys/fs/cd9660; \
	for h in *.h; do \
		${INSTALL_SYMLINK} ${TAG_ARGS} ../../../../sys/fs/cd9660/$$h \
		    ${SDESTDIR}${INCLUDEDIR}/isofs/cd9660; \
	done
	cd ${SRCTOP}/sys/rpc; \
	for h in types.h; do \
		${INSTALL_SYMLINK} ${TAG_ARGS} ../../../sys/rpc/$$h \
		    ${SDESTDIR}${INCLUDEDIR}/rpc; \
	done
.if ${MK_CDDL} != "no"
	${INSTALL_SYMLINK} ${TAG_ARGS} \
	    ../../../sys/cddl/contrib/opensolaris/uts/common/sys/nvpair.h \
	    ${SDESTDIR}${INCLUDEDIR}/sys
.endif
.if ${MK_MLX5TOOL} != "no"
	${INSTALL_SYMLINK} ${TAG_ARGS} ../../../../sys/dev/mlx5/mlx5io.h \
	    ${SDESTDIR}${INCLUDEDIR}/dev/mlx5
.endif

.include <bsd.prog.mk>

installincludes: ${SHARED}
${SHARED}: compat

.if ${MACHINE} == "host" && !defined(_SKIP_BUILD)
# we're here because we are building a sysroot...
# we need MACHINE et al set correctly
HOST_MACHINE!= uname -m
HOST_MACHINE_ARCH!= uname -p
MACHINE:= ${HOST_MACHINE}
MACHINE_ARCH:= ${HOST_MACHINE_ARCH}
.endif
