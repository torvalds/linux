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
 * Devices are described by an array of LGUEST_MAX_DEVICES of these structs,
 * placed by the Launcher just above the top of physical memory:
 */
struct lguest_device_desc {
	/* The device type: console, network, disk etc. */
	__u16 type;
#define LGUEST_DEVICE_T_CONSOLE	1
#define LGUEST_DEVICE_T_NET	2
#define LGUEST_DEVICE_T_BLOCK	3

	/* The specific features of this device: these depends on device type
	 * except for LGUEST_DEVICE_F_RANDOMNESS. */
	__u16 features;
#define LGUEST_NET_F_NOCSUM		0x4000 /* Don't bother checksumming */
#define LGUEST_DEVICE_F_RANDOMNESS	0x8000 /* IRQ is fairly random */

	/* This is how the Guest reports status of the device: the Host can set
	 * LGUEST_DEVICE_S_REMOVED to indicate removal, but the rest are only
	 * ever manipulated by the Guest, and only ever set. */
	__u16 status;
/* 256 and above are device specific. */
#define LGUEST_DEVICE_S_ACKNOWLEDGE	1 /* We have seen device. */
#define LGUEST_DEVICE_S_DRIVER		2 /* We have found a driver */
#define LGUEST_DEVICE_S_DRIVER_OK	4 /* Driver says OK! */
#define LGUEST_DEVICE_S_REMOVED		8 /* Device has gone away. */
#define LGUEST_DEVICE_S_REMOVED_ACK	16 /* Driver has been told. */
#define LGUEST_DEVICE_S_FAILED		128 /* Something actually failed */

	/* Each device exists somewhere in Guest physical memory, over some
	 * number of pages. */
	__u16 num_pages;
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
