/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (c) 2022, Google LLC
 */

#ifndef _UAPI_LINUX_LOOP_LOADPIN_H
#define _UAPI_LINUX_LOOP_LOADPIN_H

#define LOADPIN_IOC_MAGIC	'L'

/**
 * LOADPIN_IOC_SET_TRUSTED_VERITY_DIGESTS - Set up the root digests of verity devices
 *                                          that loadpin should trust.
 *
 * Takes a file descriptor from which to read the root digests of trusted verity devices. The file
 * is expected to contain a list of digests in ASCII format, with one line per digest. The ioctl
 * must be issued on the securityfs attribute 'loadpin/dm-verity' (which can be typically found
 * under /sys/kernel/security/loadpin/dm-verity).
 */
#define LOADPIN_IOC_SET_TRUSTED_VERITY_DIGESTS _IOW(LOADPIN_IOC_MAGIC, 0x00, unsigned int)

#endif /* _UAPI_LINUX_LOOP_LOADPIN_H */
