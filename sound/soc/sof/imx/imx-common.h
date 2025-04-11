/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */

#ifndef __IMX_COMMON_H__
#define __IMX_COMMON_H__

#include <linux/clk.h>
#include <linux/of_platform.h>
#include <sound/sof/xtensa.h>

#include "../sof-of-dev.h"
#include "../ops.h"

#define EXCEPT_MAX_HDR_SIZE	0x400
#define IMX8_STACK_DUMP_SIZE 32

/* chip_info refers to the data stored in struct sof_dev_desc's chip_info */
#define get_chip_info(sdev)\
	((const struct imx_chip_info *)((sdev)->pdata->desc->chip_info))

/* chip_pdata refers to the data stored in struct imx_common_data's chip_pdata */
#define get_chip_pdata(sdev)\
	(((struct imx_common_data *)((sdev)->pdata->hw_pdata))->chip_pdata)

/* can be used if:
 *	1) The only supported IPC version is IPC3.
 *	2) The default paths/FW name match values below.
 *
 * otherwise, just explicitly declare the structure
 */
#define IMX_SOF_DEV_DESC(mach_name, of_machs,				\
			 mach_chip_info, mach_ops, mach_ops_init)	\
static struct sof_dev_desc sof_of_##mach_name##_desc = {		\
	.of_machines = of_machs,					\
	.chip_info = mach_chip_info,					\
	.ipc_supported_mask = BIT(SOF_IPC_TYPE_3),			\
	.ipc_default = SOF_IPC_TYPE_3,					\
	.default_fw_path = {						\
		[SOF_IPC_TYPE_3] = "imx/sof",				\
	},								\
	.default_tplg_path = {						\
		[SOF_IPC_TYPE_3] = "imx/sof-tplg",			\
	},								\
	.default_fw_filename = {					\
		[SOF_IPC_TYPE_3] = "sof-" #mach_name ".ri",		\
	},								\
	.ops = mach_ops,						\
	.ops_init = mach_ops_init,					\
}

/* to be used alongside IMX_SOF_DEV_DESC() */
#define IMX_SOF_DEV_DESC_NAME(mach_name) sof_of_##mach_name##_desc

/* dai driver entry w/ playback and capture caps. If one direction is missing
 * then set the channels to 0.
 */
#define IMX_SOF_DAI_DRV_ENTRY(dai_name, pb_cmin, pb_cmax, cap_cmin, cap_cmax)	\
{										\
	.name = dai_name,							\
	.playback = {								\
		.channels_min = pb_cmin,					\
		.channels_max = pb_cmax,					\
	},									\
	.capture = {								\
		.channels_min = cap_cmin,					\
		.channels_max = cap_cmax,					\
	},									\
}

/* use if playback and capture have the same min/max channel count */
#define IMX_SOF_DAI_DRV_ENTRY_BIDIR(dai_name, cmin, cmax)\
	IMX_SOF_DAI_DRV_ENTRY(dai_name, cmin, cmax, cmin, cmax)

struct imx_ipc_info {
	/* true if core is able to write a panic code to the debug box */
	bool has_panic_code;
	/* offset to mailbox in which firmware initially writes FW_READY */
	int boot_mbox_offset;
	/* offset to region at which the mailboxes start */
	int window_offset;
};

struct imx_chip_ops {
	/* called after clocks and PDs are enabled */
	int (*probe)(struct snd_sof_dev *sdev);
	/* used directly by the SOF core */
	int (*core_kick)(struct snd_sof_dev *sdev);
	/* called during suspend()/remove() before clocks are disabled */
	int (*core_shutdown)(struct snd_sof_dev *sdev);
	/* used directly by the SOF core */
	int (*core_reset)(struct snd_sof_dev *sdev);
};

struct imx_memory_info {
	const char *name;
	bool reserved;
};

struct imx_chip_info {
	struct imx_ipc_info ipc_info;
	/* does the chip have a reserved memory region for DMA? */
	bool has_dma_reserved;
	struct imx_memory_info *memory;
	struct snd_soc_dai_driver *drv;
	int num_drv;
	/* optional */
	const struct imx_chip_ops *ops;
};

struct imx_common_data {
	struct platform_device *ipc_dev;
	struct imx_dsp_ipc *ipc_handle;
	/* core may have no clocks */
	struct clk_bulk_data *clks;
	int clk_num;
	/* core may have no PDs */
	struct dev_pm_domain_list *pd_list;
	void *chip_pdata;
};

static inline int imx_chip_core_kick(struct snd_sof_dev *sdev)
{
	const struct imx_chip_ops *ops = get_chip_info(sdev)->ops;

	if (ops && ops->core_kick)
		return ops->core_kick(sdev);

	return 0;
}

static inline int imx_chip_core_shutdown(struct snd_sof_dev *sdev)
{
	const struct imx_chip_ops *ops = get_chip_info(sdev)->ops;

	if (ops && ops->core_shutdown)
		return ops->core_shutdown(sdev);

	return 0;
}

static inline int imx_chip_core_reset(struct snd_sof_dev *sdev)
{
	const struct imx_chip_ops *ops = get_chip_info(sdev)->ops;

	if (ops && ops->core_reset)
		return ops->core_reset(sdev);

	return 0;
}

static inline int imx_chip_probe(struct snd_sof_dev *sdev)
{
	const struct imx_chip_ops *ops = get_chip_info(sdev)->ops;

	if (ops && ops->probe)
		return ops->probe(sdev);

	return 0;
}

void imx8_get_registers(struct snd_sof_dev *sdev,
			struct sof_ipc_dsp_oops_xtensa *xoops,
			struct sof_ipc_panic_info *panic_info,
			u32 *stack, size_t stack_words);

void imx8_dump(struct snd_sof_dev *sdev, u32 flags);

extern const struct snd_sof_dsp_ops sof_imx_ops;

#endif
