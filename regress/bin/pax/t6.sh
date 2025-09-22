# $OpenBSD: t6.sh,v 1.1 2006/07/21 22:59:05 ray Exp $
# Don't segfault if no file list is given.
#
OBJ=$2
cd ${OBJ}
cpio -o < /dev/null
