/*
 *  linux/include/linux/mmc/host.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Host driver specific definitions.
 */
#ifndef LINUX_MMC_HOST_H
#define LINUX_MMC_HOST_H

#include <linux/mmc/mmc.h>

struct mmc_ios {
	unsigned int	clock;			/* clock rate */
	unsigned short	vdd;

#define	MMC_VDD_150	0
#define	MMC_VDD_155	1
#define	MMC_VDD_160	2
#define	MMC_VDD_165	3
#define	MMC_VDD_170	4
#define	MMC_VDD_180	5
#define	MMC_VDD_190	6
#define	MMC_VDD_200	7
#define	MMC_VDD_210	8
#define	MMC_VDD_220	9
#define	MMC_VDD_230	10
#define	MMC_VDD_240	11
#define	MMC_VDD_250	12
#define	MMC_VDD_260	13
#define	MMC_VDD_270	14
#define	MMC_VDD_280	15
#define	MMC_VDD_290	16
#define	MMC_VDD_300	17
#define	MMC_VDD_310	18
#define	MMC_VDD_320	19
#define	MMC_VDD_330	20
#define	MMC_VDD_340	21
#define	MMC_VDD_350	22
#define	MMC_VDD_360	23

	unsigned char	bus_mode;		/* command output mode */

#define MMC_BUSMODE_OPENDRAIN	1
#define MMC_BUSMODE_PUSHPULL	2

	unsigned char	chip_select;		/* SPI chip select */

#define MMC_CS_DONTCARE		0
#define MMC_CS_HIGH		1
#define MMC_CS_LOW		2

	unsigned char	power_mode;		/* power supply mode */

#define MMC_POWER_OFF		0
#define MMC_POWER_UP		1
#define MMC_POWER_ON		2

	unsigned char	bus_width;		/* data bus width */

#define MMC_BUS_WIDTH_1		0
#define MMC_BUS_WIDTH_4		2

	unsigned char	timing;			/* timing specification used */

#define MMC_TIMING_LEGACY	0
#define MMC_TIMING_MMC_HS	1
#define MMC_TIMING_SD_HS	2
};

struct mmc_host_ops {
	void	(*request)(struct mmc_host *host, struct mmc_request *req);
	void	(*set_ios)(struct mmc_host *host, struct mmc_ios *ios);
	int	(*get_ro)(struct mmc_host *host);
};

struct mmc_card;
struct device;

struct mmc_host {
	struct device		*parent;
	struct device		class_dev;
	int			index;
	const struct mmc_host_ops *ops;
	unsigned int		f_min;
	unsigned int		f_max;
	u32			ocr_avail;

	unsigned long		caps;		/* Host capabilities */

#define MMC_CAP_4_BIT_DATA	(1 << 0)	/* Can the host do 4 bit transfers */
#define MMC_CAP_MULTIWRITE	(1 << 1)	/* Can accurately report bytes sent to card on error */
#define MMC_CAP_BYTEBLOCK	(1 << 2)	/* Can do non-log2 block sizes */
#define MMC_CAP_MMC_HIGHSPEED	(1 << 3)	/* Can do MMC high-speed timing */
#define MMC_CAP_SD_HIGHSPEED	(1 << 4)	/* Can do SD high-speed timing */

	/* host specific block data */
	unsigned int		max_seg_size;	/* see blk_queue_max_segment_size */
	unsigned short		max_hw_segs;	/* see blk_queue_max_hw_segments */
	unsigned short		max_phys_segs;	/* see blk_queue_max_phys_segments */
	unsigned short		unused;
	unsigned int		max_req_size;	/* maximum number of bytes in one req */
	unsigned int		max_blk_size;	/* maximum size of one mmc block */
	unsigned int		max_blk_count;	/* maximum number of blocks in one req */

	/* private data */
	struct mmc_ios		ios;		/* current io bus settings */
	u32			ocr;		/* the current OCR setting */

	unsigned int		mode;		/* current card mode of host */
#define MMC_MODE_MMC		0
#define MMC_MODE_SD		1

	struct list_head	cards;		/* devices attached to this host */

	wait_queue_head_t	wq;
	spinlock_t		lock;		/* claimed lock */
	unsigned int		claimed:1;	/* host exclusively claimed */

	struct mmc_card		*card_selected;	/* the selected MMC card */

	struct delayed_work	detect;

	unsigned long		private[0] ____cacheline_aligned;
};

extern struct mmc_host *mmc_alloc_host(int extra, struct device *);
extern int mmc_add_host(struct mmc_host *);
extern void mmc_remove_host(struct mmc_host *);
extern void mmc_free_host(struct mmc_host *);

static inline void *mmc_priv(struct mmc_host *host)
{
	return (void *)host->private;
}

#define mmc_dev(x)	((x)->parent)
#define mmc_classdev(x)	(&(x)->class_dev)
#define mmc_hostname(x)	((x)->class_dev.bus_id)

extern int mmc_suspend_host(struct mmc_host *, pm_message_t);
extern int mmc_resume_host(struct mmc_host *);

extern void mmc_detect_change(struct mmc_host *, unsigned long delay);
extern void mmc_request_done(struct mmc_host *, struct mmc_request *);

#endif

