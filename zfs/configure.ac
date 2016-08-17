/*
 * This file is part of the ZFS Linux port.
 *
 * Copyright (c) 2009 Lawrence Livermore National Security, LLC.
 * Produced at Lawrence Livermore National Laboratory
 * Written by:
 *         Brian Behlendorf <behlendorf1@llnl.gov>,
 *         Herb Wartens <wartens2@llnl.gov>,
 *         Jim Garlick <garlick@llnl.gov>
 * LLNL-CODE-403049
 *
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

AC_INIT(m4_esyscmd(grep Name META | cut -d ':' -f 2 | tr -d ' \n'),
	m4_esyscmd(grep Version META | cut -d ':' -f 2 | tr -d ' \n'))
AC_LANG(C)
ZFS_AC_META
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_MACRO_DIR([config])
AC_CANONICAL_SYSTEM
AM_MAINTAINER_MODE
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])
AM_INIT_AUTOMAKE
AC_CONFIG_HEADERS([zfs_config.h], [
	(mv zfs_config.h zfs_config.h.tmp &&
	awk -f ${ac_srcdir}/config/config.awk zfs_config.h.tmp >zfs_config.h &&
	rm zfs_config.h.tmp) || exit 1])

AC_PROG_INSTALL
AC_PROG_CC
AC_PROG_LIBTOOL
AM_PROG_AS

ZFS_AC_LICENSE
ZFS_AC_PACKAGE
ZFS_AC_CONFIG
ZFS_AC_DEBUG
ZFS_AC_DEBUG_DMU_TX

AC_CONFIG_FILES([ 
	Makefile
	module/Makefile
	module/avl/Makefile
	module/nvpair/Makefile
	module/unicode/Makefile
	module/zcommon/Makefile
	module/zfs/Makefile
	module/zpios/Makefile
	include/Makefile
	include/linux/Makefile
	include/sys/Makefile
	include/sys/fs/Makefile
	include/sys/fm/Makefile
	include/sys/fm/fs/Makefile
	zfs.release
])

AC_OUTPUT
