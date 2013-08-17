/*
 * Synopsys DesignWare Multimedia Card Interface driver
 *  (Based on NXP driver for lpc 31xx)
 *
 * Copyright (C) 2009 NXP Semiconductors
 * Copyright (C) 2009, 2010 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef LINUX_MMC_DW_MMC_H
#define LINUX_MMC_DW_MMC_H

#include <linux/scatterlist.h>
#include <linux/mmc/core.h>
#include <linux/pm_qos.h>

#define MAX_MCI_SLOTS	2

enum dw_mci_state {
	STATE_IDLE = 0,
	STATE_SENDING_CMD,
	STATE_SENDING_DATA,
	STATE_DATA_BUSY,
	STATE_SENDING_STOP,
	STATE_DATA_ERROR,
};

enum {
	EVENT_CMD_COMPLETE = 0,
	EVENT_XFER_COMPLETE,
	EVENT_DATA_COMPLETE,
	EVENT_DATA_ERROR,
	EVENT_XFER_ERROR
};

struct mmc_data;

/**
 * struct dw_mci - MMC controller state shared between all slots
 * @lock: Spinlock protecting the queue and associated data.
 * @regs: Pointer to MMIO registers.
 * @sg: Scatterlist entry currently being processed by PIO code, if any.
 * @sg_miter: PIO mapping scatterlist iterator.
 * @cur_slot: The slot which is currently using the controller.
 * @mrq: The request currently being processed on @cur_slot,
 *	or NULL if the controller is idle.
 * @cmd: The command currently being sent to the card, or NULL.
 * @data: The data currently being transferred, or NULL if no data
 *	transfer is in progress.
 * @use_dma: Whether DMA channel is initialized or not.
 * @using_dma: Whether DMA is in use for the current transfer.
 * @sg_dma: Bus address of DMA buffer.
 * @sg_cpu: Virtual address of DMA buffer.
 * @dma_ops: Pointer to platform-specific DMA callbacks.
 * @cmd_status: Snapshot of SR taken upon completion of the current
 *	command. Only valid when EVENT_CMD_COMPLETE is pending.
 * @data_status: Snapshot of SR taken upon completion of the current
 *	data transfer. Only valid when EVENT_DATA_COMPLETE or
 *	EVENT_DATA_ERROR is pending.
 * @stop_cmdr: Value to be loaded into CMDR when the stop command is
 *	to be sent.
 * @dir_status: Direction of current transfer.
 * @tasklet: Tasklet running the request state machine.
 * @card_tasklet: Tasklet handling card detect.
 * @pending_events: Bitmask of events flagged by the interrupt handler
 *	to be processed by the tasklet.
 * @completed_events: Bitmask of events which the state machine has
 *	processed.
 * @state: Tasklet state.
 * @queue: List of slots waiting for access to the controller.
 * @bus_hz: The rate of @mck in Hz. This forms the basis for MMC bus
 *	rate and timeout calculations.
 * @current_speed: Configured rate of the controller.
 * @num_slots: Number of slots available.
 * @verid: Denote Version ID.
 * @data_offset: Set the offset of DATA register according to VERID.
 * @dev: Device associated with the MMC controller.
 * @pdata: Platform data associated with the MMC controller.
 * @slot: Slots sharing this MMC controller.
 * @fifo_depth: depth of FIFO.
 * @data_shift: log2 of FIFO item size.
 * @part_buf_start: Start index in part_buf.
 * @part_buf_count: Bytes of partial data in part_buf.
 * @part_buf: Simple buffer for partial fifo reads/writes.
 * @push_data: Pointer to FIFO push function.
 * @pull_data: Pointer to FIFO pull function.
 * @quirks: Set of quirks that apply to specific versions of the IP.
 * @irq_flags: The flags to be passed to request_irq.
 * @irq: The irq value to be passed to request_irq.
 *
 * Locking
 * =======
 *
 * @lock is a softirq-safe spinlock protecting @queue as well as
 * @cur_slot, @mrq and @state. These must always be updated
 * at the same time while holding @lock.
 *
 * The @mrq field of struct dw_mci_slot is also protected by @lock,
 * and must always be written at the same time as the slot is added to
 * @queue.
 *
 * @pending_events and @completed_events are accessed using atomic bit
 * operations, so they don't need any locking.
 *
 * None of the fields touched by the interrupt handler need any
 * locking. However, ordering is important: Before EVENT_DATA_ERROR or
 * EVENT_DATA_COMPLETE is set in @pending_events, all data-related
 * interrupts must be disabled and @data_status updated with a
 * snapshot of SR. Similarly, before EVENT_CMD_COMPLETE is set, the
 * CMDRDY interrupt must be disabled and @cmd_status updated with a
 * snapshot of SR, and before EVENT_XFER_COMPLETE can be set, the
 * bytes_xfered field of @data must be written. This is ensured by
 * using barriers.
 */
struct dw_mci {
	spinlock_t		lock;
	void __iomem		*regs;

	struct scatterlist	*sg;
	struct sg_mapping_iter	sg_miter;

	struct dw_mci_slot	*cur_slot;
	struct mmc_request	*mrq;
	struct mmc_command	*cmd;
	struct mmc_data		*data;
	struct mmc_command	stop;
	bool			stop_snd;
	struct clk		*hclk;
	struct clk		*cclk;
	atomic_t		cclk_cnt;
	atomic_t		hclk_cnt;
	spinlock_t		cclk_lock;
	struct workqueue_struct	*card_workqueue;

	/* DMA interface members*/
	int			use_dma;
	int			using_dma;

	dma_addr_t		sg_dma;
	void			*sg_cpu;
	struct dw_mci_dma_ops	*dma_ops;
#ifdef CONFIG_MMC_DW_IDMAC
	unsigned int		ring_size;
#else
	struct dw_mci_dma_data	*dma_data;
#endif
	unsigned int		desc_sz;
	unsigned int		align_size;

	struct pm_qos_request	pm_qos_int;
	struct delayed_work	qos_work;

	u32			cmd_status;
	u32			data_status;
	u32			stop_cmdr;
	u32			dir_status;
	struct tasklet_struct	tasklet;
	struct work_struct	card_work;
	unsigned long		pending_events;
	unsigned long		completed_events;
	enum dw_mci_state	state;
	struct list_head	queue;

	u32			bus_hz;
	u32			current_speed;
	u32			num_slots;
	u32			fifoth_val;
	u32			cd_rd_thr;
	u16			verid;
	u16			data_offset;
	u32			bytcnt;
	struct device		dev;
	struct dw_mci_board	*pdata;
	struct dw_mci_slot	*slot[MAX_MCI_SLOTS];

	/* FIFO push and pull */
	int			fifo_depth;
	int			data_shift;
	u8			part_buf_start;
	u8			part_buf_count;
	union {
		u16		part_buf16;
		u32		part_buf32;
		u64		part_buf;
	};
	void (*push_data)(struct dw_mci *host, void *buf, int cnt);
	void (*pull_data)(struct dw_mci *host, void *buf, int cnt);

	/* Workaround flags */
	u32			quirks;

	/* S/W reset timer */
	struct timer_list       timer;

	struct delayed_work	tp_mon;
	u32			transferred_cnt;
	u32			cmd_cnt;
	u32			sync_pre_cnt;
	unsigned long		pm_qos_time;
	int			pm_qos_step;
	struct pm_qos_request	pm_qos_mif;
	struct pm_qos_request	pm_qos_cpu;

	struct regulator	*vmmc;	/* Power regulator */
	struct regulator	*vqmmc;
	unsigned long		irq_flags; /* IRQ flags */
	int			irq;

	struct mmc_queue_req    *mqrq;	/* for mmc trace */
};

/* DMA ops for Internal/External DMAC interface */
struct dw_mci_dma_ops {
	/* DMA Ops */
	int (*init)(struct dw_mci *host);
	void (*start)(struct dw_mci *host, unsigned int sg_len);
	void (*complete)(struct dw_mci *host);
	void (*stop)(struct dw_mci *host);
	void (*reset)(struct dw_mci *host);
	void (*cleanup)(struct dw_mci *host);
	void (*exit)(struct dw_mci *host);
};

/* IP Quirks/flags. */
/* DTO fix for command transmission with IDMAC configured */
#define DW_MCI_QUIRK_IDMAC_DTO			BIT(0)
/* delay needed between retries on some 2.11a implementations */
#define DW_MCI_QUIRK_RETRY_DELAY		BIT(1)
/* High Speed Capable - Supports HS cards (up to 50MHz) */
#define DW_MCI_QUIRK_HIGHSPEED			BIT(2)
/* Unreliable card detection */
#define DW_MCI_QUIRK_BROKEN_CARD_DETECTION	BIT(3)
/* No detect end bit during read */
#define DW_MCI_QUIRK_NO_DETECT_EBIT             BIT(4)
/* Hardware reset using power off/on of card */
#define DW_MMC_QUIRK_HW_RESET_PW 		BIT(5)
/* No use voltage switch interrupt */
#define DW_MMC_QUIRK_NO_VOLSW_INT		BIT(6)

enum dw_mci_cd_types {
	DW_MCI_CD_INTERNAL,	/* use mmc internal CD line */
	DW_MCI_CD_EXTERNAL,	/* use external callback */
	DW_MCI_CD_GPIO,		/* use external gpio pin for CD line */
	DW_MCI_CD_NONE,		/* no CD line, use polling to detect card */
	DW_MCI_CD_PERMANENT,	/* no CD line, card permanently wired to host */
};

struct dma_pdata;

struct block_settings {
	unsigned short	max_segs;	/* see blk_queue_max_segments */
	unsigned int	max_blk_size;	/* maximum size of one mmc block */
	unsigned int	max_blk_count;	/* maximum number of blocks in one req*/
	unsigned int	max_req_size;	/* maximum number of bytes in one req*/
	unsigned int	max_seg_size;	/* see blk_queue_max_segment_size */
};

struct dw_mci_clk {
	u32	cclkin;
	u32	sclkin;
};

struct dw_mci_mon_table {
	u32	range;
	s32	mif_lock_value;
	s32	cpu_lock_value;
};

/* Board platform data */
struct dw_mci_board {
	u32 num_slots;

	u32 quirks; /* Workaround / Quirk flags */
	unsigned int bus_hz; /* Bus speed */

	unsigned int caps;	/* Capabilities */
	unsigned int caps2;	/* More capabilities */
	unsigned int pm_caps;	/* supported pm features */

	/*
	 * Override fifo depth. If 0, autodetect it from the FIFOTH register,
	 * but note that this may not be reliable after a bootloader has used
	 * it.
	 */
	unsigned int fifo_depth;

	/* delay in mS before detecting cards after interrupt */
	u32 detect_delay_ms;

	char *hclk_name;
	char *cclk_name;

	int (*init)(u32 slot_id, irq_handler_t , void *);
	int (*get_ro)(u32 slot_id);
	int (*get_cd)(u32 slot_id);
	int (*get_ocr)(u32 slot_id);
	int (*get_bus_wd)(u32 slot_id);
	void (*cfg_gpio)(int width);
	void (*hw_reset)(u32 slot_id);
	void (*set_io_timing)(void *data, unsigned char timing);
	void (*save_drv_st)(void *data, u32 slot_id);
	void (*restore_drv_st)(void *data, u32 slot_id);
	void (*tuning_drv_st)(void *data, u32 slot_id);

	/* Phase Shift Value */
	unsigned int sdr_timing;
	unsigned int ddr_timing;
	unsigned int ddr200_timing;
	u8 clk_drv;
	u8 clk_smpl;
	bool tuned;
	bool only_once_tune;
	struct drv_strength {
		unsigned int pin;
		unsigned int val;
	} __drv_st;

	/* cd_type: Type of Card Detection method (see cd_types enum above) */
	enum dw_mci_cd_types cd_type;

	/* Number of descriptors */
	unsigned int desc_sz;

	/* ext_cd_cleanup: Cleanup external card detect subsystem.
	* ext_cd_init: Initialize external card detect subsystem.
	*	notify_func argument is a callback to the dwmci driver
	*	that triggers the card detection event. Callback arguments:
	*	dev is pointer to platform device of the host controller,
	*	state is new state of the card (0 - removed, 1 - inserted).
	*/

	int (*ext_cd_init)(void (*notify_func)
		(struct platform_device *, int state));
	int (*ext_cd_cleanup)(void (*notify_func)
		(struct platform_device *, int state));

	/*
	 * Enable power to selected slot and set voltage to desired level.
	 * Voltage levels are specified using MMC_VDD_xxx defines defined
	 * in linux/mmc/host.h file.
	 */
	void (*setpower)(u32 slot_id, u32 volt);
	void (*exit)(u32 slot_id);
	void (*select_slot)(u32 slot_id);

	struct dw_mci_dma_ops *dma_ops;
	struct dma_pdata *data;
	struct block_settings *blk_settings;
	struct dw_mci_clk *clk_tbl;
	struct dw_mci_mon_table *tp_mon_tbl;
	bool ttp_enabled;
	unsigned long ttp_timeout;
};

#endif /* LINUX_MMC_DW_MMC_H */
