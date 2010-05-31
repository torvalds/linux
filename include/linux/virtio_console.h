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
#define VIRTIO_CONSOLE_F_MULTIPORT 1	/* Does host provide multiple ports? */

#define VIRTIO_CONSOLE_BAD_ID		(~(u32)0)

struct virtio_console_config {
	/* colums of the screens */
	__u16 cols;
	/* rows of the screens */
	__u16 rows;
	/* max. number of ports this device can hold */
	__u32 max_nr_ports;
} __attribute__((packed));

/*
 * A message that's passed between the Host and the Guest for a
 * particular port.
 */
struct virtio_console_control {
	__u32 id;		/* Port number */
	__u16 event;		/* The kind of control event (see below) */
	__u16 value;		/* Extra information for the key */
};

/* Some events for control messages */
#define VIRTIO_CONSOLE_DEVICE_READY	0
#define VIRTIO_CONSOLE_PORT_ADD		1
#define VIRTIO_CONSOLE_PORT_REMOVE	2
#define VIRTIO_CONSOLE_PORT_READY	3
#define VIRTIO_CONSOLE_CONSOLE_PORT	4
#define VIRTIO_CONSOLE_RESIZE		5
#define VIRTIO_CONSOLE_PORT_OPEN	6
#define VIRTIO_CONSOLE_PORT_NAME	7

#ifdef __KERNEL__
int __init virtio_cons_early_init(int (*put_chars)(u32, const char *, int));
#endif /* __KERNEL__ */

#endif /* _LINUX_VIRTIO_CONSOLE_H */
