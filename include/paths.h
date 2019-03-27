/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)paths.h	8.1 (Berkeley) 6/2/93
 * $FreeBSD$
 */

#ifndef _PATHS_H_
#define	_PATHS_H_

#include <sys/cdefs.h>

/* Default search path. */
#define	_PATH_DEFPATH	"/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin"
/* All standard utilities path. */
#define	_PATH_STDPATH	"/usr/bin:/bin:/usr/sbin:/sbin"
/* Locate system binaries. */
#define	_PATH_SYSPATH	"/sbin:/usr/sbin"

#define	_PATH_BSHELL	"/bin/sh"
#define	_PATH_CAPABILITY	"/etc/capability"
#define	_PATH_CAPABILITY_DB	"/etc/capability.db"
#define	_PATH_CONSOLE	"/dev/console"
#define	_PATH_CP	"/bin/cp"
#define	_PATH_CSHELL	"/bin/csh"
#define	_PATH_CSMAPPER	"/usr/share/i18n/csmapper"
#define	_PATH_DEFTAPE	"/dev/sa0"
#define	_PATH_DEVGPIOC	"/dev/gpioc"
#define	_PATH_DEVNULL	"/dev/null"
#define	_PATH_DEVZERO	"/dev/zero"
#define	_PATH_DRUM	"/dev/drum"
#define	_PATH_ESDB	"/usr/share/i18n/esdb"
#define	_PATH_ETC	"/etc"
#define	_PATH_FIRMWARE	"/usr/share/firmware"
#define	_PATH_FTPUSERS	"/etc/ftpusers"
#define	_PATH_FWMEM	"/dev/fwmem"
#define	_PATH_GBDE	"/sbin/gbde"
#define	_PATH_GELI	"/sbin/geli"
#define	_PATH_HALT	"/sbin/halt"
#ifdef COMPAT_32BIT
#define	_PATH_I18NMODULE	"/usr/lib32/i18n"
#else
#define	_PATH_I18NMODULE	"/usr/lib/i18n"
#endif
#define	_PATH_IFCONFIG	"/sbin/ifconfig"
#define	_PATH_KMEM	"/dev/kmem"
#define	_PATH_LIBMAP_CONF	"/etc/libmap.conf"
#define	_PATH_LOCALE	"/usr/share/locale"
#define	_PATH_LOGIN	"/usr/bin/login"
#define	_PATH_MAILDIR	"/var/mail"
#define	_PATH_MAN	"/usr/share/man"
#define	_PATH_MDCONFIG	"/sbin/mdconfig"
#define	_PATH_MEM	"/dev/mem"
#define	_PATH_MKSNAP_FFS	"/sbin/mksnap_ffs"
#define	_PATH_MOUNT	"/sbin/mount"
#define	_PATH_NEWFS	"/sbin/newfs"
#define	_PATH_NOLOGIN	"/var/run/nologin"
#define	_PATH_RCP	"/bin/rcp"
#define	_PATH_REBOOT	"/sbin/reboot"
#define	_PATH_RLOGIN	"/usr/bin/rlogin"
#define	_PATH_RM	"/bin/rm"
#define	_PATH_RSH	"/usr/bin/rsh"
#define	_PATH_SENDMAIL	"/usr/sbin/sendmail"
#define	_PATH_SHELLS	"/etc/shells"
#define	_PATH_TTY	"/dev/tty"
#define	_PATH_UNIX	"don't use _PATH_UNIX"
#define	_PATH_UFSSUSPEND	"/dev/ufssuspend"
#define	_PATH_VI	"/usr/bin/vi"
#define	_PATH_WALL	"/usr/bin/wall"

/* Provide trailing slash, since mostly used for building pathnames. */
#define	_PATH_DEV	"/dev/"
#define	_PATH_TMP	"/tmp/"
#define	_PATH_VARDB	"/var/db/"
#define	_PATH_VARRUN	"/var/run/"
#define	_PATH_VARTMP	"/var/tmp/"
#define	_PATH_DEVVMM	"/dev/vmm/"
#define	_PATH_YP	"/var/yp/"
#define	_PATH_UUCPLOCK	"/var/spool/lock/"

/* How to get the correct name of the kernel. */
__BEGIN_DECLS
const char *getbootfile(void);
__END_DECLS

#ifdef RESCUE
#undef	_PATH_DEFPATH
#define	_PATH_DEFPATH	"/rescue:/sbin:/bin:/usr/sbin:/usr/bin:/usr/local/sbin:/usr/local/bin"
#undef	_PATH_STDPATH
#define	_PATH_STDPATH	"/rescue:/usr/bin:/bin:/usr/sbin:/sbin"
#undef	_PATH_SYSPATH
#define	_PATH_SYSPATH	"/rescue:/sbin:/usr/sbin"
#undef	_PATH_BSHELL
#define	_PATH_BSHELL	"/rescue/sh"
#undef	_PATH_CP
#define	_PATH_CP	"/rescue/cp"
#undef	_PATH_CSHELL
#define	_PATH_CSHELL	"/rescue/csh"
#undef	_PATH_HALT
#define	_PATH_HALT	"/rescue/halt"
#undef	_PATH_IFCONFIG
#define	_PATH_IFCONFIG	"/rescue/ifconfig"
#undef	_PATH_MDCONFIG
#define	_PATH_MDCONFIG	"/rescue/mdconfig"
#undef	_PATH_MOUNT
#define	_PATH_MOUNT	"/rescue/mount"
#undef	_PATH_NEWFS
#define	_PATH_NEWFS	"/rescue/newfs"
#undef	_PATH_RCP
#define	_PATH_RCP	"/rescue/rcp"
#undef	_PATH_REBOOT
#define	_PATH_REBOOT	"/rescue/reboot"
#undef	_PATH_RM
#define	_PATH_RM	"/rescue/rm"
#undef	_PATH_VI
#define	_PATH_VI	"/rescue/vi"
#undef	_PATH_WALL
#define	_PATH_WALL	"/rescue/wall"
#endif /* RESCUE */

#endif /* !_PATHS_H_ */
