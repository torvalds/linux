/* SPDX-License-Identifier: GPL-2.0 */
#ifndef UDLFB_H
#define UDLFB_H

/*
 * TODO: Propose standard fb.h ioctl for reporting damage,
 * using _IOWR() and one of the existing area structs from fb.h
 * Consider these ioctls deprecated, but they're still used by the
 * DisplayLink X server as yet - need both to be modified in tandem
 * when new ioctl(s) are ready.
 */
#define DLFB_IOCTL_RETURN_EDID	 0xAD
#define DLFB_IOCTL_REPORT_DAMAGE 0xAA
struct dloarea {
	int x, y;
	int w, h;
	int x2, y2;
};

struct urb_node {
	struct list_head entry;
	struct dlfb_data *dlfb;
	struct urb *urb;
};

struct urb_list {
	struct list_head list;
	spinlock_t lock;
	struct semaphore limit_sem;
	int available;
	int count;
	size_t size;
};

struct dlfb_data {
	struct usb_device *udev;
	struct fb_info *info;
	struct urb_list urbs;
	char *backing_buffer;
	int fb_count;
	bool virtualized; /* true when physical usb device not present */
	atomic_t usb_active; /* 0 = update virtual buffer, but no usb traffic */
	atomic_t lost_pixels; /* 1 = a render op failed. Need screen refresh */
	char *edid; /* null until we read edid from hw or get from sysfs */
	size_t edid_size;
	int sku_pixel_limit;
	int base16;
	int base8;
	u32 pseudo_palette[256];
	int blank_mode; /*one of FB_BLANK_ */
	int damage_x;
	int damage_y;
	int damage_x2;
	int damage_y2;
	spinlock_t damage_lock;
	struct work_struct damage_work;
	struct fb_ops ops;
	/* blit-only rendering path metrics, exposed through sysfs */
	atomic_t bytes_rendered; /* raw pixel-bytes driver asked to render */
	atomic_t bytes_identical; /* saved effort with backbuffer comparison */
	atomic_t bytes_sent; /* to usb, after compression including overhead */
	atomic_t cpu_kcycles_used; /* transpired during pixel processing */
	struct fb_var_screeninfo current_mode;
	struct list_head deferred_free;
};

#define NR_USB_REQUEST_I2C_SUB_IO 0x02
#define NR_USB_REQUEST_CHANNEL 0x12

/* -BULK_SIZE as per usb-skeleton. Can we get full page and avoid overhead? */
#define BULK_SIZE 512
#define MAX_TRANSFER (PAGE_SIZE*16 - BULK_SIZE)
#define WRITES_IN_FLIGHT (4)

#define MAX_VENDOR_DESCRIPTOR_SIZE 256

#define GET_URB_TIMEOUT	HZ
#define FREE_URB_TIMEOUT (HZ*2)

#define BPP                     2
#define MAX_CMD_PIXELS		255

#define RLX_HEADER_BYTES	7
#define MIN_RLX_PIX_BYTES       4
#define MIN_RLX_CMD_BYTES	(RLX_HEADER_BYTES + MIN_RLX_PIX_BYTES)

#define RLE_HEADER_BYTES	6
#define MIN_RLE_PIX_BYTES	3
#define MIN_RLE_CMD_BYTES	(RLE_HEADER_BYTES + MIN_RLE_PIX_BYTES)

#define RAW_HEADER_BYTES	6
#define MIN_RAW_PIX_BYTES	2
#define MIN_RAW_CMD_BYTES	(RAW_HEADER_BYTES + MIN_RAW_PIX_BYTES)

#define DL_DEFIO_WRITE_DELAY    msecs_to_jiffies(HZ <= 300 ? 4 : 10) /* optimal value for 720p video */
#define DL_DEFIO_WRITE_DISABLE  (HZ*60) /* "disable" with long delay */

/* remove these once align.h patch is taken into kernel */
#define DL_ALIGN_UP(x, a) ALIGN(x, a)
#define DL_ALIGN_DOWN(x, a) ALIGN_DOWN(x, a)

#endif
