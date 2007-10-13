#ifndef _ASM_LGUEST_USER
#define _ASM_LGUEST_USER
/* Everything the "lguest" userspace program needs to know. */
/* They can register up to 32 arrays of lguest_dma. */
#define LGUEST_MAX_DMA		32
/* At most we can dma 16 lguest_dma in one op. */
#define LGUEST_MAX_DMA_SECTIONS	16

/* How many devices?  Assume each one wants up to two dma arrays per device. */
#define LGUEST_MAX_DEVICES (LGUEST_MAX_DMA/2)

/*D:200
 * Lguest I/O
 *
 * The lguest I/O mechanism is the only way Guests can talk to devices.  There
 * are two hypercalls involved: SEND_DMA for output and BIND_DMA for input.  In
 * each case, "struct lguest_dma" describes the buffer: this contains 16
 * addr/len pairs, and if there are fewer buffer elements the len array is
 * terminated with a 0.
 *
 * I/O is organized by keys: BIND_DMA attaches buffers to a particular key, and
 * SEND_DMA transfers to buffers bound to particular key.  By convention, keys
 * correspond to a physical address within the device's page.  This means that
 * devices will never accidentally end up with the same keys, and allows the
 * Host use The Futex Trick (as we'll see later in our journey).
 *
 * SEND_DMA simply indicates a key to send to, and the physical address of the
 * "struct lguest_dma" to send.  The Host will write the number of bytes
 * transferred into the "struct lguest_dma"'s used_len member.
 *
 * BIND_DMA indicates a key to bind to, a pointer to an array of "struct
 * lguest_dma"s ready for receiving, the size of that array, and an interrupt
 * to trigger when data is received.  The Host will only allow transfers into
 * buffers with a used_len of zero: it then sets used_len to the number of
 * bytes transferred and triggers the interrupt for the Guest to process the
 * new input. */
struct lguest_dma
{
	/* 0 if free to be used, filled by the Host. */
 	u32 used_len;
	unsigned long addr[LGUEST_MAX_DMA_SECTIONS];
	u16 len[LGUEST_MAX_DMA_SECTIONS];
};
/*:*/

/*D:460 This is the layout of a block device memory page.  The Launcher sets up
 * the num_sectors initially to tell the Guest the size of the disk.  The Guest
 * puts the type, sector and length of the request in the first three fields,
 * then DMAs to the Host.  The Host processes the request, sets up the result,
 * then DMAs back to the Guest. */
struct lguest_block_page
{
	/* 0 is a read, 1 is a write. */
	int type;
	u32 sector; 	/* Offset in device = sector * 512. */
	u32 bytes;	/* Length expected to be read/written in bytes */
	/* 0 = pending, 1 = done, 2 = done, error */
	int result;
	u32 num_sectors; /* Disk length = num_sectors * 512 */
};

/*D:520 The network device is basically a memory page where all the Guests on
 * the network publish their MAC (ethernet) addresses: it's an array of "struct
 * lguest_net": */
struct lguest_net
{
	/* Simply the mac address (with multicast bit meaning promisc). */
	unsigned char mac[6];
};
/*:*/

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
	u16 type;
#define LGUEST_DEVICE_T_CONSOLE	1
#define LGUEST_DEVICE_T_NET	2
#define LGUEST_DEVICE_T_BLOCK	3

	/* The specific features of this device: these depends on device type
	 * except for LGUEST_DEVICE_F_RANDOMNESS. */
	u16 features;
#define LGUEST_NET_F_NOCSUM		0x4000 /* Don't bother checksumming */
#define LGUEST_DEVICE_F_RANDOMNESS	0x8000 /* IRQ is fairly random */

	/* This is how the Guest reports status of the device: the Host can set
	 * LGUEST_DEVICE_S_REMOVED to indicate removal, but the rest are only
	 * ever manipulated by the Guest, and only ever set. */
	u16 status;
/* 256 and above are device specific. */
#define LGUEST_DEVICE_S_ACKNOWLEDGE	1 /* We have seen device. */
#define LGUEST_DEVICE_S_DRIVER		2 /* We have found a driver */
#define LGUEST_DEVICE_S_DRIVER_OK	4 /* Driver says OK! */
#define LGUEST_DEVICE_S_REMOVED		8 /* Device has gone away. */
#define LGUEST_DEVICE_S_REMOVED_ACK	16 /* Driver has been told. */
#define LGUEST_DEVICE_S_FAILED		128 /* Something actually failed */

	/* Each device exists somewhere in Guest physical memory, over some
	 * number of pages. */
	u16 num_pages;
	u32 pfn;
};
/*:*/

/* Write command first word is a request. */
enum lguest_req
{
	LHREQ_INITIALIZE, /* + pfnlimit, pgdir, start, pageoffset */
	LHREQ_GETDMA, /* + addr (returns &lguest_dma, irq in ->used_len) */
	LHREQ_IRQ, /* + irq */
	LHREQ_BREAK, /* + on/off flag (on blocks until someone does off) */
};
#endif /* _ASM_LGUEST_USER */
