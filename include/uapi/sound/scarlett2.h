/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *   Focusrite Scarlett 2 Protocol Driver for ALSA
 *   (including Scarlett 2nd Gen, 3rd Gen, 4th Gen, Clarett USB, and
 *   Clarett+ series products)
 *
 *   Copyright (c) 2023 by Geoffrey D. Bennett <g at b4.vu>
 */
#ifndef __UAPI_SOUND_SCARLETT2_H
#define __UAPI_SOUND_SCARLETT2_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define SCARLETT2_HWDEP_MAJOR 1
#define SCARLETT2_HWDEP_MINOR 0
#define SCARLETT2_HWDEP_SUBMINOR 0

#define SCARLETT2_HWDEP_VERSION \
	((SCARLETT2_HWDEP_MAJOR << 16) | \
	 (SCARLETT2_HWDEP_MINOR << 8) | \
	  SCARLETT2_HWDEP_SUBMINOR)

#define SCARLETT2_HWDEP_VERSION_MAJOR(v) (((v) >> 16) & 0xFF)
#define SCARLETT2_HWDEP_VERSION_MINOR(v) (((v) >> 8) & 0xFF)
#define SCARLETT2_HWDEP_VERSION_SUBMINOR(v) ((v) & 0xFF)

/* Get protocol version */
#define SCARLETT2_IOCTL_PVERSION _IOR('S', 0x60, int)

/* Reboot */
#define SCARLETT2_IOCTL_REBOOT _IO('S', 0x61)

/* Select flash segment */
#define SCARLETT2_SEGMENT_ID_SETTINGS 0
#define SCARLETT2_SEGMENT_ID_FIRMWARE 1
#define SCARLETT2_SEGMENT_ID_COUNT 2

#define SCARLETT2_IOCTL_SELECT_FLASH_SEGMENT _IOW('S', 0x62, int)

/* Erase selected flash segment */
#define SCARLETT2_IOCTL_ERASE_FLASH_SEGMENT _IO('S', 0x63)

/* Get selected flash segment erase progress
 * 1 through to num_blocks, or 255 for complete
 */
struct scarlett2_flash_segment_erase_progress {
	unsigned char progress;
	unsigned char num_blocks;
};
#define SCARLETT2_IOCTL_GET_ERASE_PROGRESS \
	_IOR('S', 0x64, struct scarlett2_flash_segment_erase_progress)

#endif /* __UAPI_SOUND_SCARLETT2_H */
