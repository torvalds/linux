#ifndef _ASM_LGUEST_USER
#define _ASM_LGUEST_USER
/* Everything the "lguest" userspace program needs to know. */
/* They can register up to 32 arrays of lguest_dma. */
#define LGUEST_MAX_DMA		32
/* At most we can dma 16 lguest_dma in one op. */
#define LGUEST_MAX_DMA_SECTIONS	16

/* How many devices?  Assume each one wants up to two dma arrays per device. */
#define LGUEST_MAX_DEVICES (LGUEST_MAX_DMA/2)

struct lguest_dma
{
	/* 0 if free to be used, filled by hypervisor. */
 	u32 used_len;
	unsigned long addr[LGUEST_MAX_DMA_SECTIONS];
	u16 len[LGUEST_MAX_DMA_SECTIONS];
};

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

/* There is a shared page of these. */
struct lguest_net
{
	/* Simply the mac address (with multicast bit meaning promisc). */
	unsigned char mac[6];
};

/* Where the Host expects the Guest to SEND_DMA console output to. */
#define LGUEST_CONSOLE_DMA_KEY 0

/* We have a page of these descriptors in the lguest_device page. */
struct lguest_device_desc {
	u16 type;
#define LGUEST_DEVICE_T_CONSOLE	1
#define LGUEST_DEVICE_T_NET	2
#define LGUEST_DEVICE_T_BLOCK	3

	u16 features;
#define LGUEST_NET_F_NOCSUM		0x4000 /* Don't bother checksumming */
#define LGUEST_DEVICE_F_RANDOMNESS	0x8000 /* IRQ is fairly random */

	u16 status;
/* 256 and above are device specific. */
#define LGUEST_DEVICE_S_ACKNOWLEDGE	1 /* We have seen device. */
#define LGUEST_DEVICE_S_DRIVER		2 /* We have found a driver */
#define LGUEST_DEVICE_S_DRIVER_OK	4 /* Driver says OK! */
#define LGUEST_DEVICE_S_REMOVED		8 /* Device has gone away. */
#define LGUEST_DEVICE_S_REMOVED_ACK	16 /* Driver has been told. */
#define LGUEST_DEVICE_S_FAILED		128 /* Something actually failed */

	u16 num_pages;
	u32 pfn;
};

/* Write command first word is a request. */
enum lguest_req
{
	LHREQ_INITIALIZE, /* + pfnlimit, pgdir, start, pageoffset */
	LHREQ_GETDMA, /* + addr (returns &lguest_dma, irq in ->used_len) */
	LHREQ_IRQ, /* + irq */
	LHREQ_BREAK, /* + on/off flag (on blocks until someone does off) */
};
#endif /* _ASM_LGUEST_USER */
