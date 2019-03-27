/*-
 * Copyright (c) 2008 Semihalf, Rafal Jaworowski
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stand.h>
#include "bootstrap.h"
#include "libuboot.h"

#if defined(LOADER_NET_SUPPORT)
#include "dev_net.h"
#endif

/* Make sure we have an explicit reference to exit so libsa's panic pulls in the MD exit */
void (*exitfn)(int) = exit;

struct devsw *devsw[] = {
#if defined(LOADER_DISK_SUPPORT) || defined(LOADER_CD9660_SUPPORT)
	&uboot_storage,
#endif
#if defined(LOADER_NET_SUPPORT)
	&netdev,
#endif
	NULL
};

struct fs_ops *file_system[] = {
#if defined(LOADER_MSDOS_SUPPORT)
	&dosfs_fsops,
#endif
#if defined(LOADER_UFS_SUPPORT)
	&ufs_fsops,
#endif
#if defined(LOADER_CD9660_SUPPORT)
	&cd9660_fsops,
#endif
#if defined(LOADER_EXT2FS_SUPPORT)
	&ext2fs_fsops,
#endif
#if defined(LOADER_NANDFS_SUPPORT)
	&nandfs_fsops,
#endif
#if defined(LOADER_NFS_SUPPORT)
	&nfs_fsops,
#endif
#if defined(LOADER_TFTP_SUPPORT)
	&tftp_fsops,
#endif
#if defined(LOADER_GZIP_SUPPORT)
	&gzipfs_fsops,
#endif
#if defined(LOADER_BZIP2_SUPPORT)
	&bzipfs_fsops,
#endif
	NULL
};

struct netif_driver *netif_drivers[] = {
#if defined(LOADER_NET_SUPPORT)
	&uboot_net,
#endif
	NULL,
};

struct file_format *file_formats[] = {
	&uboot_elf,
	NULL
};

extern struct console uboot_console;

struct console *consoles[] = {
	&uboot_console,
	NULL
};

void
abort(void)
{
 
	printf("error: loader abort\n");
	while (1);
	__unreachable();
}

void
longjmperror(void)
{
 
	printf("error: loader longjmp error\n");
	while (1);
	__unreachable();
}

int debug = 1;
