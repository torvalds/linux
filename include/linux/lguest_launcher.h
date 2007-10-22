#ifndef _ASM_LGUEST_USER
#define _ASM_LGUEST_USER
/* Everything the "lguest" userspace program needs to know. */
#include <linux/types.h>
/* They can register up to 32 arrays of lguest_dma. */
#define LGUEST_MAX_DMA		32
/* At most we can dma 16 lguest_dma in one op. */
#define LGUEST_MAX_DMA_SECTIONS	16

/* How many devices?  Assume each one wants up to two dma arrays per device. */
#define LGUEST_MAX_DEVICES (LGUEST_MAX_DMA/2)

/* Where the Host expects the Guest to SEND_DMA console output to. */
#define LGUEST_CONSOLE_DMA_KEY 0

/*D:010
 * Drivers
 *
 * The Guest needs devices to do anything useful.  Since we don't let it touch
 * real devices (think of the damage it could do!) we provide virtual devices.
 * We could emulate a PCI bus with various devices on it, but that is a fairly
 * complex burden for the Host and suboptimal for the Guest, so we have our own
 * "lguest" bus and simple drivers.
 *
 * Devices are described by a simplified ID, a status byte, and some "config"
 * bytes which describe this device's configuration.  This is placed by the
 * Launcher just above the top of physical memory:
 */
struct lguest_device_desc {
	/* The device type: console, network, disk etc.  Type 0 terminates. */
	__u8 type;
	/* The number of bytes of the config array. */
	__u8 config_len;
	/* A status byte, written by the Guest. */
	__u8 status;
	__u8 config[0];
};

/*D:135 This is how we expect the device configuration field for a virtqueue
 * (type VIRTIO_CONFIG_F_VIRTQUEUE) to be laid out: */
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
	LHREQ_INITIALIZE, /* + pfnlimit, pgdir, start, pageoffset */
	LHREQ_GETDMA, /* No longer used */
	LHREQ_IRQ, /* + irq */
	LHREQ_BREAK, /* + on/off flag (on blocks until someone does off) */
};
#endif /* _ASM_LGUEST_USER */
