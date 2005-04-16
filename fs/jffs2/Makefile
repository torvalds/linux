#
# Makefile for the Linux Journalling Flash File System v2 (JFFS2)
#
# $Id: Makefile.common,v 1.7 2004/11/03 12:57:38 jwboyer Exp $
#

obj-$(CONFIG_JFFS2_FS) += jffs2.o

jffs2-y	:= compr.o dir.o file.o ioctl.o nodelist.o malloc.o
jffs2-y	+= read.o nodemgmt.o readinode.o write.o scan.o gc.o
jffs2-y	+= symlink.o build.o erase.o background.o fs.o writev.o
jffs2-y	+= super.o

jffs2-$(CONFIG_JFFS2_FS_NAND)	+= wbuf.o
jffs2-$(CONFIG_JFFS2_FS_NOR_ECC) += wbuf.o
jffs2-$(CONFIG_JFFS2_RUBIN)	+= compr_rubin.o
jffs2-$(CONFIG_JFFS2_RTIME)	+= compr_rtime.o
jffs2-$(CONFIG_JFFS2_ZLIB)	+= compr_zlib.o
