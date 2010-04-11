#ifndef _LINUX_VIRTIO_CONSOLE_H
#define _LINUX_VIRTIO_CONSOLE_H
#include <linux/types.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>
/*
 * This header, excluding the #ifdef __KERNEL__ part, is BSD licensed so
 * anyone can use the definitions to implement compatible drivers/servers.
 *
 * Copyright (C) Red Hat, Inc., 2009, 2010
 */

/* Feature bits */
#define VIRTIO_CONSOLE_F_SIZE	0	/* Does host provide console size? */

struct virtio_console_config {
	/* colums of the screens */
	__u16 cols;
	/* rows of the screens */
	__u16 rows;
} __attribute__((packed));

#ifdef __KERNEL__
int __init virtio_cons_early_init(int (*put_chars)(u32, const char *, int));
#endif /* __KERNEL__ */

#endif /* _LINUX_VIRTIO_CONSOLE_H */
