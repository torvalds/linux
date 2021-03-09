/*
 * include/linux/initramfs.h
 *
 * Copyright (C) 2015, Google
 * Rom Lemarchand <romlem@android.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef _LINUX_INITRAMFS_H
#define _LINUX_INITRAMFS_H

#include <linux/kconfig.h>

#if IS_BUILTIN(CONFIG_BLK_DEV_INITRD)

int __init default_rootfs(void);

#endif

#if defined(CONFIG_ROCKCHIP_HW_DECOMPRESS)
void __init wait_initrd_hw_decom_done(void);
#endif

#if defined(CONFIG_ROCKCHIP_THUNDER_BOOT_CRYPTO)
int __init rk_tb_crypto_sha256_wait_compare_done(void);
#endif

#endif /* _LINUX_INITRAMFS_H */
