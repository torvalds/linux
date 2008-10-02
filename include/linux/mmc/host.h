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

#include <linux/leds.h>

#include <linux/mmc/core.h>

struct mmc_ios {
	unsigned int	clock;			/* clock rate */
	unsigned short	vdd;

/* vdd stores the bit number of the selected voltage range from below. */

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
	/*
	 * Avoid calling these three functions too often or in a "fast path",
	 * since underlaying controller might implement them in an expensive
	 * and/or slow way.
	 *
	 * Also note that these functions might sleep, so don't call them
	 * in the atomic contexts!
	 *
	 * Return values for the get_ro callback should be:
	 *   0 for a read/write card
	 *   1 for a read-only card
	 *   -ENOSYS when not supported (equal to NULL callback)
	 *   or a negative errno value when something bad happened
	 *
	 * Return values for the get_ro callback should be:
	 *   0 for a absent card
	 *   1 for a present card
	 *   -ENOSYS when not supported (equal to NULL callback)
	 *   or a negative errno value when something bad happened
	 */
	void	(*set_ios)(struct mmc_host *host, struct mmc_ios *ios);
	int	(*get_ro)(struct mmc_host *host);
	int	(*get_cd)(struct mmc_host *host);

	void	(*enable_sdio_irq)(struct mmc_host *host, int enable);
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

#define MMC_VDD_165_195		0x00000080	/* VDD voltage 1.65 - 1.95 */
#define MMC_VDD_20_21		0x00000100	/* VDD voltage 2.0 ~ 2.1 */
#define MMC_VDD_21_22		0x00000200	/* VDD voltage 2.1 ~ 2.2 */
#define MMC_VDD_22_23		0x00000400	/* VDD voltage 2.2 ~ 2.3 */
#define MMC_VDD_23_24		0x00000800	/* VDD voltage 2.3 ~ 2.4 */
#define MMC_VDD_24_25		0x00001000	/* VDD voltage 2.4 ~ 2.5 */
#define MMC_VDD_25_26		0x00002000	/* VDD voltage 2.5 ~ 2.6 */
#define MMC_VDD_26_27		0x00004000	/* VDD voltage 2.6 ~ 2.7 */
#define MMC_VDD_27_28		0x00008000	/* VDD voltage 2.7 ~ 2.8 */
#define MMC_VDD_28_29		0x00010000	/* VDD voltage 2.8 ~ 2.9 */
#define MMC_VDD_29_30		0x00020000	/* VDD voltage 2.9 ~ 3.0 */
#define MMC_VDD_30_31		0x00040000	/* VDD voltage 3.0 ~ 3.1 */
#define MMC_VDD_31_32		0x00080000	/* VDD voltage 3.1 ~ 3.2 */
#define MMC_VDD_32_33		0x00100000	/* VDD voltage 3.2 ~ 3.3 */
#define MMC_VDD_33_34		0x00200000	/* VDD voltage 3.3 ~ 3.4 */
#define MMC_VDD_34_35		0x00400000	/* VDD voltage 3.4 ~ 3.5 */
#define MMC_VDD_35_36		0x00800000	/* VDD voltage 3.5 ~ 3.6 */

	unsigned long		caps;		/* Host capabilities */

#define MMC_CAP_4_BIT_DATA	(1 << 0)	/* Can the host do 4 bit transfers */
#define MMC_CAP_MMC_HIGHSPEED	(1 << 1)	/* Can do MMC high-speed timing */
#define MMC_CAP_SD_HIGHSPEED	(1 << 2)	/* Can do SD high-speed timing */
#define MMC_CAP_SDIO_IRQ	(1 << 3)	/* Can signal pending SDIO IRQs */
#define MMC_CAP_SPI		(1 << 4)	/* Talks only SPI protocols */
#define MMC_CAP_NEEDS_POLL	(1 << 5)	/* Needs polling for card-detection */

	/* host specific block data */
	unsigned int		max_seg_size;	/* see blk_queue_max_segment_size */
	unsigned short		max_hw_segs;	/* see blk_queue_max_hw_segments */
	unsigned short		max_phys_segs;	/* see blk_queue_max_phys_segments */
	unsigned short		unused;
	unsigned int		max_req_size;	/* maximum number of bytes in one req */
	unsigned int		max_blk_size;	/* maximum size of one mmc block */
	unsigned int		max_blk_count;	/* maximum number of blocks in one req */

	/* private data */
	spinlock_t		lock;		/* lock for claim and bus ops */

	struct mmc_ios		ios;		/* current io bus settings */
	u32			ocr;		/* the current OCR setting */

	/* group bitfields together to minimize padding */
	unsigned int		use_spi_crc:1;
	unsigned int		claimed:1;	/* host exclusively claimed */
	unsigned int		bus_dead:1;	/* bus has been released */
#ifdef CONFIG_MMC_DEBUG
	unsigned int		removed:1;	/* host is being removed */
#endif

	struct mmc_card		*card;		/* device attached to this host */

	wait_queue_head_t	wq;

	struct delayed_work	detect;

	const struct mmc_bus_ops *bus_ops;	/* current bus driver */
	unsigned int		bus_refs;	/* reference counter */

	unsigned int		sdio_irqs;
	struct task_struct	*sdio_irq_thread;
	atomic_t		sdio_irq_thread_abort;

#ifdef CONFIG_LEDS_TRIGGERS
	struct led_trigger	*led;		/* activity led */
#endif

	struct dentry		*debugfs_root;

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

#define mmc_host_is_spi(host)	((host)->caps & MMC_CAP_SPI)

#define mmc_dev(x)	((x)->parent)
#define mmc_classdev(x)	(&(x)->class_dev)
#define mmc_hostname(x)	((x)->class_dev.bus_id)

extern int mmc_suspend_host(struct mmc_host *, pm_message_t);
extern int mmc_resume_host(struct mmc_host *);

extern void mmc_detect_change(struct mmc_host *, unsigned long delay);
extern void mmc_request_done(struct mmc_host *, struct mmc_request *);

static inline void mmc_signal_sdio_irq(struct mmc_host *host)
{
	host->ops->enable_sdio_irq(host, 0);
	wake_up_process(host->sdio_irq_thread);
}

#endif

