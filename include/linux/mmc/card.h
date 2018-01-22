/*
 *  linux/include/linux/mmc/card.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Card driver specific definitions.
 */
#ifndef LINUX_MMC_CARD_H
#define LINUX_MMC_CARD_H

#include <linux/device.h>
#include <linux/mod_devicetable.h>

struct mmc_cid {
	unsigned int		manfid;
	char			prod_name[8];
	unsigned char		prv;
	unsigned int		serial;
	unsigned short		oemid;
	unsigned short		year;
	unsigned char		hwrev;
	unsigned char		fwrev;
	unsigned char		month;
};

struct mmc_csd {
	unsigned char		structure;
	unsigned char		mmca_vsn;
	unsigned short		cmdclass;
	unsigned short		taac_clks;
	unsigned int		taac_ns;
	unsigned int		c_size;
	unsigned int		r2w_factor;
	unsigned int		max_dtr;
	unsigned int		erase_size;		/* In sectors */
	unsigned int		read_blkbits;
	unsigned int		write_blkbits;
	unsigned int		capacity;
	unsigned int		read_partial:1,
				read_misalign:1,
				write_partial:1,
				write_misalign:1,
				dsr_imp:1;
};

struct mmc_ext_csd {
	u8			rev;
	u8			erase_group_def;
	u8			sec_feature_support;
	u8			rel_sectors;
	u8			rel_param;
	u8			part_config;
	u8			cache_ctrl;
	u8			rst_n_function;
	u8			max_packed_writes;
	u8			max_packed_reads;
	u8			packed_event_en;
	unsigned int		part_time;		/* Units: ms */
	unsigned int		sa_timeout;		/* Units: 100ns */
	unsigned int		generic_cmd6_time;	/* Units: 10ms */
	unsigned int            power_off_longtime;     /* Units: ms */
	u8			power_off_notification;	/* state */
	unsigned int		hs_max_dtr;
	unsigned int		hs200_max_dtr;
#define MMC_HIGH_26_MAX_DTR	26000000
#define MMC_HIGH_52_MAX_DTR	52000000
#define MMC_HIGH_DDR_MAX_DTR	52000000
#define MMC_HS200_MAX_DTR	200000000
	unsigned int		sectors;
	unsigned int		hc_erase_size;		/* In sectors */
	unsigned int		hc_erase_timeout;	/* In milliseconds */
	unsigned int		sec_trim_mult;	/* Secure trim multiplier  */
	unsigned int		sec_erase_mult;	/* Secure erase multiplier */
	unsigned int		trim_timeout;		/* In milliseconds */
	bool			partition_setting_completed;	/* enable bit */
	unsigned long long	enhanced_area_offset;	/* Units: Byte */
	unsigned int		enhanced_area_size;	/* Units: KB */
	unsigned int		cache_size;		/* Units: KB */
	bool			hpi_en;			/* HPI enablebit */
	bool			hpi;			/* HPI support bit */
	unsigned int		hpi_cmd;		/* cmd used as HPI */
	bool			bkops;		/* background support bit */
	bool			man_bkops_en;	/* manual bkops enable bit */
	bool			auto_bkops_en;	/* auto bkops enable bit */
	unsigned int            data_sector_size;       /* 512 bytes or 4KB */
	unsigned int            data_tag_unit_size;     /* DATA TAG UNIT size */
	unsigned int		boot_ro_lock;		/* ro lock support */
	bool			boot_ro_lockable;
	bool			ffu_capable;	/* Firmware upgrade support */
	bool			cmdq_en;	/* Command Queue enabled */
	bool			cmdq_support;	/* Command Queue supported */
	unsigned int		cmdq_depth;	/* Command Queue depth */
#define MMC_FIRMWARE_LEN 8
	u8			fwrev[MMC_FIRMWARE_LEN];  /* FW version */
	u8			raw_exception_status;	/* 54 */
	u8			raw_partition_support;	/* 160 */
	u8			raw_rpmb_size_mult;	/* 168 */
	u8			raw_erased_mem_count;	/* 181 */
	u8			strobe_support;		/* 184 */
	u8			raw_ext_csd_structure;	/* 194 */
	u8			raw_card_type;		/* 196 */
	u8			raw_driver_strength;	/* 197 */
	u8			out_of_int_time;	/* 198 */
	u8			raw_pwr_cl_52_195;	/* 200 */
	u8			raw_pwr_cl_26_195;	/* 201 */
	u8			raw_pwr_cl_52_360;	/* 202 */
	u8			raw_pwr_cl_26_360;	/* 203 */
	u8			raw_s_a_timeout;	/* 217 */
	u8			raw_hc_erase_gap_size;	/* 221 */
	u8			raw_erase_timeout_mult;	/* 223 */
	u8			raw_hc_erase_grp_size;	/* 224 */
	u8			raw_sec_trim_mult;	/* 229 */
	u8			raw_sec_erase_mult;	/* 230 */
	u8			raw_sec_feature_support;/* 231 */
	u8			raw_trim_mult;		/* 232 */
	u8			raw_pwr_cl_200_195;	/* 236 */
	u8			raw_pwr_cl_200_360;	/* 237 */
	u8			raw_pwr_cl_ddr_52_195;	/* 238 */
	u8			raw_pwr_cl_ddr_52_360;	/* 239 */
	u8			raw_pwr_cl_ddr_200_360;	/* 253 */
	u8			raw_bkops_status;	/* 246 */
	u8			raw_sectors[4];		/* 212 - 4 bytes */
	u8			pre_eol_info;		/* 267 */
	u8			device_life_time_est_typ_a;	/* 268 */
	u8			device_life_time_est_typ_b;	/* 269 */

	unsigned int            feature_support;
#define MMC_DISCARD_FEATURE	BIT(0)                  /* CMD38 feature */
};

struct sd_scr {
	unsigned char		sda_vsn;
	unsigned char		sda_spec3;
	unsigned char		bus_widths;
#define SD_SCR_BUS_WIDTH_1	(1<<0)
#define SD_SCR_BUS_WIDTH_4	(1<<2)
	unsigned char		cmds;
#define SD_SCR_CMD20_SUPPORT   (1<<0)
#define SD_SCR_CMD23_SUPPORT   (1<<1)
};

struct sd_ssr {
	unsigned int		au;			/* In sectors */
	unsigned int		erase_timeout;		/* In milliseconds */
	unsigned int		erase_offset;		/* In milliseconds */
};

struct sd_switch_caps {
	unsigned int		hs_max_dtr;
	unsigned int		uhs_max_dtr;
#define HIGH_SPEED_MAX_DTR	50000000
#define UHS_SDR104_MAX_DTR	208000000
#define UHS_SDR50_MAX_DTR	100000000
#define UHS_DDR50_MAX_DTR	50000000
#define UHS_SDR25_MAX_DTR	UHS_DDR50_MAX_DTR
#define UHS_SDR12_MAX_DTR	25000000
	unsigned int		sd3_bus_mode;
#define UHS_SDR12_BUS_SPEED	0
#define HIGH_SPEED_BUS_SPEED	1
#define UHS_SDR25_BUS_SPEED	1
#define UHS_SDR50_BUS_SPEED	2
#define UHS_SDR104_BUS_SPEED	3
#define UHS_DDR50_BUS_SPEED	4

#define SD_MODE_HIGH_SPEED	(1 << HIGH_SPEED_BUS_SPEED)
#define SD_MODE_UHS_SDR12	(1 << UHS_SDR12_BUS_SPEED)
#define SD_MODE_UHS_SDR25	(1 << UHS_SDR25_BUS_SPEED)
#define SD_MODE_UHS_SDR50	(1 << UHS_SDR50_BUS_SPEED)
#define SD_MODE_UHS_SDR104	(1 << UHS_SDR104_BUS_SPEED)
#define SD_MODE_UHS_DDR50	(1 << UHS_DDR50_BUS_SPEED)
	unsigned int		sd3_drv_type;
#define SD_DRIVER_TYPE_B	0x01
#define SD_DRIVER_TYPE_A	0x02
#define SD_DRIVER_TYPE_C	0x04
#define SD_DRIVER_TYPE_D	0x08
	unsigned int		sd3_curr_limit;
#define SD_SET_CURRENT_LIMIT_200	0
#define SD_SET_CURRENT_LIMIT_400	1
#define SD_SET_CURRENT_LIMIT_600	2
#define SD_SET_CURRENT_LIMIT_800	3
#define SD_SET_CURRENT_NO_CHANGE	(-1)

#define SD_MAX_CURRENT_200	(1 << SD_SET_CURRENT_LIMIT_200)
#define SD_MAX_CURRENT_400	(1 << SD_SET_CURRENT_LIMIT_400)
#define SD_MAX_CURRENT_600	(1 << SD_SET_CURRENT_LIMIT_600)
#define SD_MAX_CURRENT_800	(1 << SD_SET_CURRENT_LIMIT_800)
};

struct sdio_cccr {
	unsigned int		sdio_vsn;
	unsigned int		sd_vsn;
	unsigned int		multi_block:1,
				low_speed:1,
				wide_bus:1,
				high_power:1,
				high_speed:1,
				disable_cd:1;
};

struct sdio_cis {
	unsigned short		vendor;
	unsigned short		device;
	unsigned short		blksize;
	unsigned int		max_dtr;
};

struct mmc_host;
struct sdio_func;
struct sdio_func_tuple;
struct mmc_queue_req;

#define SDIO_MAX_FUNCS		7

/* The number of MMC physical partitions.  These consist of:
 * boot partitions (2), general purpose partitions (4) and
 * RPMB partition (1) in MMC v4.4.
 */
#define MMC_NUM_BOOT_PARTITION	2
#define MMC_NUM_GP_PARTITION	4
#define MMC_NUM_PHY_PARTITION	7
#define MAX_MMC_PART_NAME_LEN	20

/*
 * MMC Physical partitions
 */
struct mmc_part {
	unsigned int	size;	/* partition size (in bytes) */
	unsigned int	part_cfg;	/* partition type */
	char	name[MAX_MMC_PART_NAME_LEN];
	bool	force_ro;	/* to make boot parts RO by default */
	unsigned int	area_type;
#define MMC_BLK_DATA_AREA_MAIN	(1<<0)
#define MMC_BLK_DATA_AREA_BOOT	(1<<1)
#define MMC_BLK_DATA_AREA_GP	(1<<2)
#define MMC_BLK_DATA_AREA_RPMB	(1<<3)
};

/*
 * MMC device
 */
struct mmc_card {
	struct mmc_host		*host;		/* the host this device belongs to */
	struct device		dev;		/* the device */
	u32			ocr;		/* the current OCR setting */
	unsigned int		rca;		/* relative card address of device */
	unsigned int		type;		/* card type */
#define MMC_TYPE_MMC		0		/* MMC card */
#define MMC_TYPE_SD		1		/* SD card */
#define MMC_TYPE_SDIO		2		/* SDIO card */
#define MMC_TYPE_SD_COMBO	3		/* SD combo (IO+mem) card */
	unsigned int		state;		/* (our) card state */
	unsigned int		quirks; 	/* card quirks */
#define MMC_QUIRK_LENIENT_FN0	(1<<0)		/* allow SDIO FN0 writes outside of the VS CCCR range */
#define MMC_QUIRK_BLKSZ_FOR_BYTE_MODE (1<<1)	/* use func->cur_blksize */
						/* for byte mode */
#define MMC_QUIRK_NONSTD_SDIO	(1<<2)		/* non-standard SDIO card attached */
						/* (missing CIA registers) */
#define MMC_QUIRK_NONSTD_FUNC_IF (1<<4)		/* SDIO card has nonstd function interfaces */
#define MMC_QUIRK_DISABLE_CD	(1<<5)		/* disconnect CD/DAT[3] resistor */
#define MMC_QUIRK_INAND_CMD38	(1<<6)		/* iNAND devices have broken CMD38 */
#define MMC_QUIRK_BLK_NO_CMD23	(1<<7)		/* Avoid CMD23 for regular multiblock */
#define MMC_QUIRK_BROKEN_BYTE_MODE_512 (1<<8)	/* Avoid sending 512 bytes in */
						/* byte mode */
#define MMC_QUIRK_LONG_READ_TIME (1<<9)		/* Data read time > CSD says */
#define MMC_QUIRK_SEC_ERASE_TRIM_BROKEN (1<<10)	/* Skip secure for erase/trim */
#define MMC_QUIRK_BROKEN_IRQ_POLLING	(1<<11)	/* Polling SDIO_CCCR_INTx could create a fake interrupt */
#define MMC_QUIRK_TRIM_BROKEN	(1<<12)		/* Skip trim */
#define MMC_QUIRK_BROKEN_HPI	(1<<13)		/* Disable broken HPI support */

	bool			reenable_cmdq;	/* Re-enable Command Queue */

	unsigned int		erase_size;	/* erase size in sectors */
 	unsigned int		erase_shift;	/* if erase unit is power 2 */
 	unsigned int		pref_erase;	/* in sectors */
	unsigned int		eg_boundary;	/* don't cross erase-group boundaries */
 	u8			erased_byte;	/* value of erased bytes */

	u32			raw_cid[4];	/* raw card CID */
	u32			raw_csd[4];	/* raw card CSD */
	u32			raw_scr[2];	/* raw card SCR */
	u32			raw_ssr[16];	/* raw card SSR */
	struct mmc_cid		cid;		/* card identification */
	struct mmc_csd		csd;		/* card specific */
	struct mmc_ext_csd	ext_csd;	/* mmc v4 extended card specific */
	struct sd_scr		scr;		/* extra SD information */
	struct sd_ssr		ssr;		/* yet more SD information */
	struct sd_switch_caps	sw_caps;	/* switch (CMD6) caps */

	unsigned int		sdio_funcs;	/* number of SDIO functions */
	struct sdio_cccr	cccr;		/* common card info */
	struct sdio_cis		cis;		/* common tuple info */
	struct sdio_func	*sdio_func[SDIO_MAX_FUNCS]; /* SDIO functions (devices) */
	struct sdio_func	*sdio_single_irq; /* SDIO function when only one IRQ active */
	unsigned		num_info;	/* number of info strings */
	const char		**info;		/* info strings */
	struct sdio_func_tuple	*tuples;	/* unknown common tuples */

	unsigned int		sd_bus_speed;	/* Bus Speed Mode set for the card */
	unsigned int		mmc_avail_type;	/* supported device type by both host and card */
	unsigned int		drive_strength;	/* for UHS-I, HS200 or HS400 */

	struct dentry		*debugfs_root;
	struct mmc_part	part[MMC_NUM_PHY_PARTITION]; /* physical partitions */
	unsigned int    nr_parts;

	unsigned int		bouncesz;	/* Bounce buffer size */
};

static inline bool mmc_large_sector(struct mmc_card *card)
{
	return card->ext_csd.data_sector_size == 4096;
}

bool mmc_card_is_blockaddr(struct mmc_card *card);

#define mmc_card_mmc(c)		((c)->type == MMC_TYPE_MMC)
#define mmc_card_sd(c)		((c)->type == MMC_TYPE_SD)
#define mmc_card_sdio(c)	((c)->type == MMC_TYPE_SDIO)

#endif /* LINUX_MMC_CARD_H */
