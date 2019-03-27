# $FreeBSD$
#	@(#)Make.tags.inc	8.1 (Berkeley) 6/11/93

SYS?=	${.CURDIR}/..

# Common files for "make tags", included by the Makefile for each
# architecture.

# Put the /sys/sys include files at the end so that subroutine definitions
# win when there is a struct tag with the same name (e.g., vmmeter).  The
# better solution would be for ctags to generate "struct vmmeter" tags.

COMM=	${SYS}/sys/vnode.h \
	${SYS}/dev/alc/*.[ch] \
	${SYS}/dev/en/*.[ch] \
	${SYS}/dev/iicbus/*.[ch] \
	${SYS}/dev/isp/*.[ch] \
	${SYS}/dev/ppbus/*.[ch] \
	${SYS}/dev/smbus/*.[ch] \
	${SYS}/dev/vx/*.[ch] \
	${SYS}/fs/autofs/*.[ch] \
	${SYS}/fs/cd9660/*.[ch] \
	${SYS}/fs/cuse/*.[ch] \
	${SYS}/fs/deadfs/*.[ch] \
	${SYS}/fs/devfs/*.[ch] \
	${SYS}/fs/ext2fs/*.[ch] \
	${SYS}/fs/fdescfs/*.[ch] \
	${SYS}/fs/fifofs/*.[ch] \
	${SYS}/fs/fuse/*.[ch] \
	${SYS}/fs/msdosfs/*.[ch] \
	${SYS}/fs/nandfs/*.[ch] \
	${SYS}/fs/nfs/*.[ch] \
	${SYS}/fs/nfsclient/*.[ch] \
	${SYS}/fs/nfsserver/*.[ch] \
	${SYS}/fs/nullfs/*.[ch] \
	${SYS}/fs/procfs/*.[ch] \
	${SYS}/fs/pseudofs/*.[ch] \
	${SYS}/fs/smbfs/*.[ch] \
	${SYS}/fs/tmpfs/*.[ch] \
	${SYS}/fs/udf/*.[ch] \
	${SYS}/fs/unionfs/*.[ch] \
	${SYS}/geom/*.[ch] \
	${SYS}/kern/*.[ch] \
	${SYS}/net/*.[ch] \
	${SYS}/netinet/*.[ch] \
	${SYS}/netinet6/*.[ch] \
	${SYS}/netipsec/*.[ch] \
	${SYS}/netnatm/*.[ch] \
	${SYS}/ddb/*.[ch] \
	${SYS}/ufs/ffs/*.[ch] \
	${SYS}/ufs/ufs/*.[ch] \
	${SYS}/vm/*.[ch] \
	${SYS}/sys/*.[ch]

COMMDIR1= ${SYS}/conf \
	${SYS}/geom \
	${SYS}/kern \
	${SYS}/net \
	${SYS}/netinet \
	${SYS}/netinet6 \
	${SYS}/netipsec \
	${SYS}/netnatm \
	${SYS}/ddb \
	${SYS}/vm \
	${SYS}/sys

COMMDIR2= ${SYS}/dev/alc \
	${SYS}/dev/en \
	${SYS}/dev/iicbus \
	${SYS}/dev/isp \
	${SYS}/dev/md \
	${SYS}/dev/ppbus \
	${SYS}/dev/smbus \
	${SYS}/dev/vx \
	${SYS}/fs/autofs \
	${SYS}/fs/cd9660 \
	${SYS}/fs/cuse \
	${SYS}/fs/deadfs \
	${SYS}/fs/devfs \
	${SYS}/fs/ext2fs \
	${SYS}/fs/fdescfs \
	${SYS}/fs/fifofs \
	${SYS}/fs/fuse \
	${SYS}/fs/msdosfs \
	${SYS}/fs/nandfs \
	${SYS}/fs/nfs \
	${SYS}/fs/nfsclient \
	${SYS}/fs/nfsserver \
	${SYS}/fs/nullfs \
	${SYS}/fs/procfs \
	${SYS}/fs/pseudofs \
	${SYS}/fs/smbfs \
	${SYS}/fs/tmpfs \
	${SYS}/fs/udf \
	${SYS}/fs/unionfs \
	${SYS}/ufs/ffs \
	${SYS}/ufs/ufs
