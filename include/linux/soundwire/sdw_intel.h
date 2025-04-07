/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/* Copyright(c) 2015-17 Intel Corporation. */

#ifndef __SDW_INTEL_H
#define __SDW_INTEL_H

#include <linux/acpi.h>
#include <linux/irqreturn.h>
#include <linux/soundwire/sdw.h>

/*********************************************************************
 * cAVS and ACE1.x definitions
 *********************************************************************/

#define SDW_SHIM_BASE			0x2C000
#define SDW_ALH_BASE			0x2C800
#define SDW_SHIM_BASE_ACE		0x38000
#define SDW_ALH_BASE_ACE		0x24000
#define SDW_LINK_BASE			0x30000
#define SDW_LINK_SIZE			0x10000

/* Intel SHIM Registers Definition */
/* LCAP */
#define SDW_SHIM_LCAP			0x0
#define SDW_SHIM_LCAP_LCOUNT_MASK	GENMASK(2, 0)
#define SDW_SHIM_LCAP_MLCS_MASK		BIT(8)

/* LCTL */
#define SDW_SHIM_LCTL			0x4

#define SDW_SHIM_LCTL_SPA		BIT(0)
#define SDW_SHIM_LCTL_SPA_MASK		GENMASK(3, 0)
#define SDW_SHIM_LCTL_CPA		BIT(8)
#define SDW_SHIM_LCTL_CPA_MASK		GENMASK(11, 8)
#define SDW_SHIM_LCTL_MLCS_MASK		GENMASK(29, 27)
#define SDW_SHIM_MLCS_XTAL_CLK		0x0
#define SDW_SHIM_MLCS_CARDINAL_CLK	0x1
#define SDW_SHIM_MLCS_AUDIO_PLL_CLK	0x2

/* SYNC */
#define SDW_SHIM_SYNC			0xC

#define SDW_SHIM_SYNC_SYNCPRD_VAL_24		(24000 / SDW_CADENCE_GSYNC_KHZ - 1)
#define SDW_SHIM_SYNC_SYNCPRD_VAL_24_576	(24576 / SDW_CADENCE_GSYNC_KHZ - 1)
#define SDW_SHIM_SYNC_SYNCPRD_VAL_38_4		(38400 / SDW_CADENCE_GSYNC_KHZ - 1)
#define SDW_SHIM_SYNC_SYNCPRD_VAL_96		(96000 / SDW_CADENCE_GSYNC_KHZ - 1)
#define SDW_SHIM_SYNC_SYNCPRD		GENMASK(14, 0)
#define SDW_SHIM_SYNC_SYNCCPU		BIT(15)
#define SDW_SHIM_SYNC_CMDSYNC_MASK	GENMASK(19, 16)
#define SDW_SHIM_SYNC_CMDSYNC		BIT(16)
#define SDW_SHIM_SYNC_SYNCGO		BIT(24)

/* Control stream capabililities and channel mask */
#define SDW_SHIM_CTLSCAP(x)		(0x010 + 0x60 * (x))
#define SDW_SHIM_CTLS0CM(x)		(0x012 + 0x60 * (x))
#define SDW_SHIM_CTLS1CM(x)		(0x014 + 0x60 * (x))
#define SDW_SHIM_CTLS2CM(x)		(0x016 + 0x60 * (x))
#define SDW_SHIM_CTLS3CM(x)		(0x018 + 0x60 * (x))

/* PCM Stream capabilities */
#define SDW_SHIM_PCMSCAP(x)		(0x020 + 0x60 * (x))

#define SDW_SHIM_PCMSCAP_ISS		GENMASK(3, 0)
#define SDW_SHIM_PCMSCAP_OSS		GENMASK(7, 4)
#define SDW_SHIM_PCMSCAP_BSS		GENMASK(12, 8)

/* PCM Stream Channel Map */
#define SDW_SHIM_PCMSYCHM(x, y)		(0x022 + (0x60 * (x)) + (0x2 * (y)))

/* PCM Stream Channel Count */
#define SDW_SHIM_PCMSYCHC(x, y)		(0x042 + (0x60 * (x)) + (0x2 * (y)))

#define SDW_SHIM_PCMSYCM_LCHN		GENMASK(3, 0)
#define SDW_SHIM_PCMSYCM_HCHN		GENMASK(7, 4)
#define SDW_SHIM_PCMSYCM_STREAM		GENMASK(13, 8)
#define SDW_SHIM_PCMSYCM_DIR		BIT(15)

/* IO control */
#define SDW_SHIM_IOCTL(x)		(0x06C + 0x60 * (x))

#define SDW_SHIM_IOCTL_MIF		BIT(0)
#define SDW_SHIM_IOCTL_CO		BIT(1)
#define SDW_SHIM_IOCTL_COE		BIT(2)
#define SDW_SHIM_IOCTL_DO		BIT(3)
#define SDW_SHIM_IOCTL_DOE		BIT(4)
#define SDW_SHIM_IOCTL_BKE		BIT(5)
#define SDW_SHIM_IOCTL_WPDD		BIT(6)
#define SDW_SHIM_IOCTL_CIBD		BIT(8)
#define SDW_SHIM_IOCTL_DIBD		BIT(9)

/* Wake Enable*/
#define SDW_SHIM_WAKEEN			0x190

#define SDW_SHIM_WAKEEN_ENABLE		BIT(0)

/* Wake Status */
#define SDW_SHIM_WAKESTS		0x192

#define SDW_SHIM_WAKESTS_STATUS		BIT(0)

/* AC Timing control */
#define SDW_SHIM_CTMCTL(x)		(0x06E + 0x60 * (x))

#define SDW_SHIM_CTMCTL_DACTQE		BIT(0)
#define SDW_SHIM_CTMCTL_DODS		BIT(1)
#define SDW_SHIM_CTMCTL_DOAIS		GENMASK(4, 3)

/* Intel ALH Register definitions */
#define SDW_ALH_STRMZCFG(x)		(0x000 + (0x4 * (x)))
#define SDW_ALH_NUM_STREAMS		64

#define SDW_ALH_STRMZCFG_DMAT_VAL	0x3
#define SDW_ALH_STRMZCFG_DMAT		GENMASK(7, 0)
#define SDW_ALH_STRMZCFG_CHN		GENMASK(19, 16)

/*********************************************************************
 * ACE2.x definitions for SHIM registers - only accessible when the
 * HDAudio extended link LCTL.SPA/CPA = 1.
 *********************************************************************/
/* x variable is link index */
#define SDW_SHIM2_GENERIC_BASE(x)	(0x00030000 + 0x8000 * (x))
#define SDW_IP_BASE(x)			(0x00030100 + 0x8000 * (x))
#define SDW_SHIM2_VS_BASE(x)		(0x00036000 + 0x8000 * (x))

/* SHIM2 Generic Registers */
/* Read-only capabilities */
#define SDW_SHIM2_LECAP			0x00
#define SDW_SHIM2_LECAP_HDS		BIT(0)		/* unset -> Host mode */
#define SDW_SHIM2_LECAP_MLC		GENMASK(3, 1)	/* Number of Lanes */

/* PCM Stream capabilities */
#define SDW_SHIM2_PCMSCAP		0x10
#define SDW_SHIM2_PCMSCAP_ISS		GENMASK(3, 0)	/* Input-only streams */
#define SDW_SHIM2_PCMSCAP_OSS		GENMASK(7, 4)	/* Output-only streams */
#define SDW_SHIM2_PCMSCAP_BSS		GENMASK(12, 8)	/* Bidirectional streams */

/* Read-only PCM Stream Channel Count, y variable is stream */
#define SDW_SHIM2_PCMSYCHC(y)		(0x14 + (0x4 * (y)))
#define SDW_SHIM2_PCMSYCHC_CS		GENMASK(3, 0)	/* Channels Supported */

/* PCM Stream Channel Map */
#define SDW_SHIM2_PCMSYCHM(y)		(0x16 + (0x4 * (y)))
#define SDW_SHIM2_PCMSYCHM_LCHAN	GENMASK(3, 0)	/* Lowest channel used by the FIFO port */
#define SDW_SHIM2_PCMSYCHM_HCHAN	GENMASK(7, 4)	/* Lowest channel used by the FIFO port */
#define SDW_SHIM2_PCMSYCHM_STRM		GENMASK(13, 8)	/* HDaudio stream tag */
#define SDW_SHIM2_PCMSYCHM_DIR		BIT(15)		/* HDaudio stream direction */

/* SHIM2 vendor-specific registers */
#define SDW_SHIM2_INTEL_VS_LVSCTL	0x04
#define SDW_SHIM2_INTEL_VS_LVSCTL_FCG	BIT(26)
#define SDW_SHIM2_INTEL_VS_LVSCTL_MLCS	GENMASK(29, 27)
#define SDW_SHIM2_INTEL_VS_LVSCTL_DCGD	BIT(30)
#define SDW_SHIM2_INTEL_VS_LVSCTL_ICGD	BIT(31)

#define SDW_SHIM2_MLCS_XTAL_CLK		0x0
#define SDW_SHIM2_MLCS_CARDINAL_CLK	0x1
#define SDW_SHIM2_MLCS_AUDIO_PLL_CLK	0x2
#define SDW_SHIM2_MLCS_MCLK_INPUT_CLK	0x3
#define SDW_SHIM2_MLCS_WOV_RING_OSC_CLK 0x4

#define SDW_SHIM2_INTEL_VS_WAKEEN	0x08
#define SDW_SHIM2_INTEL_VS_WAKEEN_PWE	BIT(0)

#define SDW_SHIM2_INTEL_VS_WAKESTS	0x0A
#define SDW_SHIM2_INTEL_VS_WAKEEN_PWS	BIT(0)

#define SDW_SHIM2_INTEL_VS_IOCTL	0x0C
#define SDW_SHIM2_INTEL_VS_IOCTL_MIF	BIT(0)
#define SDW_SHIM2_INTEL_VS_IOCTL_CO	BIT(1)
#define SDW_SHIM2_INTEL_VS_IOCTL_COE	BIT(2)
#define SDW_SHIM2_INTEL_VS_IOCTL_DO	BIT(3)
#define SDW_SHIM2_INTEL_VS_IOCTL_DOE	BIT(4)
#define SDW_SHIM2_INTEL_VS_IOCTL_BKE	BIT(5)
#define SDW_SHIM2_INTEL_VS_IOCTL_WPDD	BIT(6)
#define SDW_SHIM2_INTEL_VS_IOCTL_ODC	BIT(7)
#define SDW_SHIM2_INTEL_VS_IOCTL_CIBD	BIT(8)
#define SDW_SHIM2_INTEL_VS_IOCTL_DIBD	BIT(9)
#define SDW_SHIM2_INTEL_VS_IOCTL_HAMIFD	BIT(10)

#define SDW_SHIM2_INTEL_VS_ACTMCTL	0x0E
#define SDW_SHIM2_INTEL_VS_ACTMCTL_DACTQE	BIT(0)
#define SDW_SHIM2_INTEL_VS_ACTMCTL_DODS		BIT(1)
#define SDW_SHIM2_INTEL_VS_ACTMCTL_DODSE	BIT(2)
#define SDW_SHIM2_INTEL_VS_ACTMCTL_DOAIS	GENMASK(4, 3)
#define SDW_SHIM2_INTEL_VS_ACTMCTL_DOAISE	BIT(5)
#define SDW_SHIM3_INTEL_VS_ACTMCTL_CLSS		BIT(6)
#define SDW_SHIM3_INTEL_VS_ACTMCTL_CLDS		GENMASK(11, 7)
#define SDW_SHIM3_INTEL_VS_ACTMCTL_DODSE2	GENMASK(13, 12)
#define SDW_SHIM3_INTEL_VS_ACTMCTL_DOAISE2	BIT(14)
#define SDW_SHIM3_INTEL_VS_ACTMCTL_CLDE		BIT(15)

/**
 * struct sdw_intel_stream_params_data: configuration passed during
 * the @params_stream callback, e.g. for interaction with DSP
 * firmware.
 */
struct sdw_intel_stream_params_data {
	struct snd_pcm_substream *substream;
	struct snd_soc_dai *dai;
	struct snd_pcm_hw_params *hw_params;
	int link_id;
	int alh_stream_id;
};

/**
 * struct sdw_intel_stream_free_data: configuration passed during
 * the @free_stream callback, e.g. for interaction with DSP
 * firmware.
 */
struct sdw_intel_stream_free_data {
	struct snd_pcm_substream *substream;
	struct snd_soc_dai *dai;
	int link_id;
};

/**
 * struct sdw_intel_ops: Intel audio driver callback ops
 *
 */
struct sdw_intel_ops {
	int (*params_stream)(struct device *dev,
			     struct sdw_intel_stream_params_data *params_data);
	int (*free_stream)(struct device *dev,
			   struct sdw_intel_stream_free_data *free_data);
	int (*trigger)(struct snd_pcm_substream *substream, int cmd, struct snd_soc_dai *dai);
};

/**
 * struct sdw_intel_acpi_info - Soundwire Intel information found in ACPI tables
 * @handle: ACPI controller handle
 * @count: link count found with "sdw-master-count" or "sdw-manager-list" property
 * @link_mask: bit-wise mask listing links enabled by BIOS menu
 *
 * this structure could be expanded to e.g. provide all the _ADR
 * information in case the link_mask is not sufficient to identify
 * platform capabilities.
 */
struct sdw_intel_acpi_info {
	acpi_handle handle;
	int count;
	u32 link_mask;
};

struct sdw_intel_link_dev;

/* Intel clock-stop/pm_runtime quirk definitions */

/*
 * Force the clock to remain on during pm_runtime suspend. This might
 * be needed if Slave devices do not have an alternate clock source or
 * if the latency requirements are very strict.
 */
#define SDW_INTEL_CLK_STOP_NOT_ALLOWED		BIT(0)

/*
 * Stop the bus during pm_runtime suspend. If set, a complete bus
 * reset and re-enumeration will be performed when the bus
 * restarts. This mode shall not be used if Slave devices can generate
 * in-band wakes.
 */
#define SDW_INTEL_CLK_STOP_TEARDOWN		BIT(1)

/*
 * Stop the bus during pm_suspend if Slaves are not wake capable
 * (e.g. speaker amplifiers). The clock-stop mode is typically
 * slightly higher power than when the IP is completely powered-off.
 */
#define SDW_INTEL_CLK_STOP_WAKE_CAPABLE_ONLY	BIT(2)

/*
 * Require a bus reset (and complete re-enumeration) when exiting
 * clock stop modes. This may be needed if the controller power was
 * turned off and all context lost. This quirk shall not be used if a
 * Slave device needs to remain enumerated and keep its context,
 * e.g. to provide the reasons for the wake, report acoustic events or
 * pass a history buffer.
 */
#define SDW_INTEL_CLK_STOP_BUS_RESET		BIT(3)

struct hdac_bus;

/**
 * struct sdw_intel_ctx - context allocated by the controller
 * driver probe
 * @count: link count
 * @mmio_base: mmio base of SoundWire registers, only used to check
 * hardware capabilities after all power dependencies are settled.
 * @link_mask: bit-wise mask listing SoundWire links reported by the
 * Controller
 * @handle: ACPI parent handle
 * @ldev: information for each link (controller-specific and kept
 * opaque here)
 * @link_list: list to handle interrupts across all links
 * @shim_lock: mutex to handle concurrent rmw access to shared SHIM registers.
 * @shim_mask: flags to track initialization of SHIM shared registers
 * @shim_base: sdw shim base.
 * @alh_base: sdw alh base.
 * @peripherals: array representing Peripherals exposed across all enabled links
 */
struct sdw_intel_ctx {
	int count;
	void __iomem *mmio_base;
	u32 link_mask;
	acpi_handle handle;
	struct sdw_intel_link_dev **ldev;
	struct list_head link_list;
	struct mutex shim_lock; /* lock for access to shared SHIM registers */
	u32 shim_mask;
	u32 shim_base;
	u32 alh_base;
	struct sdw_peripherals *peripherals;
};

/**
 * struct sdw_intel_res - Soundwire Intel global resource structure,
 * typically populated by the DSP driver
 *
 * @hw_ops: abstraction for platform ops
 * @count: link count
 * @mmio_base: mmio base of SoundWire registers
 * @irq: interrupt number
 * @handle: ACPI parent handle
 * @parent: parent device
 * @ops: callback ops
 * @dev: device implementing hwparams and free callbacks
 * @link_mask: bit-wise mask listing links selected by the DSP driver
 * This mask may be a subset of the one reported by the controller since
 * machine-specific quirks are handled in the DSP driver.
 * @clock_stop_quirks: mask array of possible behaviors requested by the
 * DSP driver. The quirks are common for all links for now.
 * @shim_base: sdw shim base.
 * @alh_base: sdw alh base.
 * @ext: extended HDaudio link support
 * @hbus: hdac_bus pointer, needed for power management
 * @eml_lock: mutex protecting shared registers in the HDaudio multi-link
 * space
 */
struct sdw_intel_res {
	const struct sdw_intel_hw_ops *hw_ops;
	int count;
	void __iomem *mmio_base;
	int irq;
	acpi_handle handle;
	struct device *parent;
	const struct sdw_intel_ops *ops;
	struct device *dev;
	u32 link_mask;
	u32 clock_stop_quirks;
	u32 shim_base;
	u32 alh_base;
	bool ext;
	struct hdac_bus *hbus;
	struct mutex *eml_lock;
};

/*
 * On Intel platforms, the SoundWire IP has dependencies on power
 * rails shared with the DSP, and the initialization steps are split
 * in three. First an ACPI scan to check what the firmware describes
 * in DSDT tables, then an allocation step (with no hardware
 * configuration but with all the relevant devices created) and last
 * the actual hardware configuration. The final stage is a global
 * interrupt enable which is controlled by the DSP driver. Splitting
 * these phases helps simplify the boot flow and make early decisions
 * on e.g. which machine driver to select (I2S mode, HDaudio or
 * SoundWire).
 */
int sdw_intel_acpi_scan(acpi_handle *parent_handle,
			struct sdw_intel_acpi_info *info);

void sdw_intel_process_wakeen_event(struct sdw_intel_ctx *ctx);

struct sdw_intel_ctx *
sdw_intel_probe(struct sdw_intel_res *res);

int sdw_intel_startup(struct sdw_intel_ctx *ctx);

void sdw_intel_exit(struct sdw_intel_ctx *ctx);

irqreturn_t sdw_intel_thread(int irq, void *dev_id);

#define SDW_INTEL_QUIRK_MASK_BUS_DISABLE      BIT(1)

struct sdw_intel;

/* struct intel_sdw_hw_ops - SoundWire ops for Intel platforms.
 * @debugfs_init: initialize all debugfs capabilities
 * @debugfs_exit: close and cleanup debugfs capabilities
 * @get_link_count: fetch link count from hardware registers
 * @register_dai: read all PDI information and register DAIs
 * @check_clock_stop: throw error message if clock is not stopped.
 * @start_bus: normal start
 * @start_bus_after_reset: start after reset
 * @start_bus_after_clock_stop: start after mode0 clock stop
 * @stop_bus: stop all bus
 * @link_power_up: power-up using chip-specific helpers
 * @link_power_down: power-down with chip-specific helpers
 * @shim_check_wake: check if a wake was received
 * @shim_wake: enable/disable in-band wake management
 * @pre_bank_switch: helper for bus management
 * @post_bank_switch: helper for bus management
 * @sync_arm: helper for multi-link synchronization
 * @sync_go_unlocked: helper for multi-link synchronization -
 * shim_lock is assumed to be locked at higher level
 * @sync_go: helper for multi-link synchronization
 * @sync_check_cmdsync_unlocked: helper for multi-link synchronization
 * and bank switch - shim_lock is assumed to be locked at higher level
 * @program_sdi: helper for codec command/control based on dev_num
 */
struct sdw_intel_hw_ops {
	void (*debugfs_init)(struct sdw_intel *sdw);
	void (*debugfs_exit)(struct sdw_intel *sdw);

	int (*get_link_count)(struct sdw_intel *sdw);

	int (*register_dai)(struct sdw_intel *sdw);

	void (*check_clock_stop)(struct sdw_intel *sdw);
	int (*start_bus)(struct sdw_intel *sdw);
	int (*start_bus_after_reset)(struct sdw_intel *sdw);
	int (*start_bus_after_clock_stop)(struct sdw_intel *sdw);
	int (*stop_bus)(struct sdw_intel *sdw, bool clock_stop);

	int (*link_power_up)(struct sdw_intel *sdw);
	int (*link_power_down)(struct sdw_intel *sdw);

	int  (*shim_check_wake)(struct sdw_intel *sdw);
	void (*shim_wake)(struct sdw_intel *sdw, bool wake_enable);

	int (*pre_bank_switch)(struct sdw_intel *sdw);
	int (*post_bank_switch)(struct sdw_intel *sdw);

	void (*sync_arm)(struct sdw_intel *sdw);
	int (*sync_go_unlocked)(struct sdw_intel *sdw);
	int (*sync_go)(struct sdw_intel *sdw);
	bool (*sync_check_cmdsync_unlocked)(struct sdw_intel *sdw);

	void (*program_sdi)(struct sdw_intel *sdw, int dev_num);

	int (*bpt_send_async)(struct sdw_intel *sdw, struct sdw_slave *slave,
			      struct sdw_bpt_msg *msg);
	int (*bpt_wait)(struct sdw_intel *sdw, struct sdw_slave *slave, struct sdw_bpt_msg *msg);
};

extern const struct sdw_intel_hw_ops sdw_intel_cnl_hw_ops;
extern const struct sdw_intel_hw_ops sdw_intel_lnl_hw_ops;

/*
 * IDA min selected to allow for 5 unconstrained devices per link,
 * and 6 system-unique Device Numbers for wake-capable devices.
 */

#define SDW_INTEL_DEV_NUM_IDA_MIN           6

/*
 * Max number of links supported in hardware
 */
#define SDW_INTEL_MAX_LINKS                5

#endif
