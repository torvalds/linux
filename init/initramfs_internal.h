// SPDX-License-Identifier: GPL-2.0
#ifndef __INITRAMFS_INTERNAL_H__
#define __INITRAMFS_INTERNAL_H__

char *unpack_to_rootfs(char *buf, unsigned long len);
#define CPIO_HDRLEN 110

#endif
