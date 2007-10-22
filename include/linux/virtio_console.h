#ifndef _LINUX_VIRTIO_CONSOLE_H
#define _LINUX_VIRTIO_CONSOLE_H
#include <linux/virtio_config.h>

/* The ID for virtio console */
#define VIRTIO_ID_CONSOLE	3

#ifdef __KERNEL__
int __init virtio_cons_early_init(int (*put_chars)(u32, const char *, int));
#endif /* __KERNEL__ */

#endif /* _LINUX_VIRTIO_CONSOLE_H */
