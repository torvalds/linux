#ifndef _LINUX_LGUEST_LAUNCHER
#define _LINUX_LGUEST_LAUNCHER
/* Everything the "lguest" userspace program needs to know. */
#include <linux/types.h>

/*D:010
 * Drivers
 *
 * The Guest needs devices to do anything useful.  Since we don't let it touch
 * real devices (think of the damage it could do!) we provide virtual devices.
 * We could emulate a PCI bus with various devices on it, but that is a fairly
 * complex burden for the Host and suboptimal for the Guest, so we have our own
 * simple lguest bus and we use "virtio" drivers.  These drivers need a set of
 * routines from us which will actually do the virtual I/O, but they handle all
 * the net/block/console stuff themselves.  This means that if we want to add
 * a new device, we simply need to write a new virtio driver and create support
 * for it in the Launcher: this code won't need to change.
 *
 * Virtio devices are also used by kvm, so we can simply reuse their optimized
 * device drivers.  And one day when everyone uses virtio, my plan will be
 * complete.  Bwahahahah!
 *
 * Devices are described by a simplified ID, a status byte, and some "config"
 * bytes which describe this device's configuration.  This is placed by the
 * Launcher just above the top of physical memory:
 */
struct lguest_device_desc {
	/* The device type: console, network, disk etc.  Type 0 terminates. */
	__u8 type;
	/* The number of virtqueues (first in config array) */
	__u8 num_vq;
	/* The number of bytes of feature bits.  Multiply by 2: one for host
	 * features and one for Guest acknowledgements. */
	__u8 feature_len;
	/* The number of bytes of the config array after virtqueues. */
	__u8 config_len;
	/* A status byte, written by the Guest. */
	__u8 status;
	__u8 config[0];
};

/*D:135 This is how we expect the device configuration field for a virtqueue
 * to be laid out in config space. */
struct lguest_vqconfig {
	/* The number of entries in the virtio_ring */
	__u16 num;
	/* The interrupt we get when something happens. */
	__u16 irq;
	/* The page number of the virtio ring for this device. */
	__u32 pfn;
};
/*:*/

/* Write command first word is a request. */
enum lguest_req
{
	LHREQ_INITIALIZE, /* + base, pfnlimit, start */
	LHREQ_GETDMA, /* No longer used */
	LHREQ_IRQ, /* + irq */
	LHREQ_BREAK, /* + on/off flag (on blocks until someone does off) */
};

/* The alignment to use between consumer and producer parts of vring.
 * x86 pagesize for historical reasons. */
#define LGUEST_VRING_ALIGN	4096
#endif /* _LINUX_LGUEST_LAUNCHER */
