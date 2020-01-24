// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//

#include <linux/firmware.h>
#include <linux/module.h>
#include <asm/unaligned.h>
#include <sound/soc.h>
#include <sound/sof.h>
#include "sof-priv.h"
#include "ops.h"

/* see SOF_DBG_ flags */
int sof_core_debug;
module_param_named(sof_debug, sof_core_debug, int, 0444);
MODULE_PARM_DESC(sof_debug, "SOF core debug options (0x0 all off)");

/* SOF defaults if not provided by the platform in ms */
#define TIMEOUT_DEFAULT_IPC_MS  500
#define TIMEOUT_DEFAULT_BOOT_MS 2000

/*
 * Generic object lookup APIs.
 */

struct snd_sof_pcm *snd_sof_find_spcm_name(struct snd_sof_dev *sdev,
					   const char *name)
{
	struct snd_sof_pcm *spcm;

	list_for_each_entry(spcm, &sdev->pcm_list, list) {
		/* match with PCM dai name */
		if (strcmp(spcm->pcm.dai_name, name) == 0)
			return spcm;

		/* match with playback caps name if set */
		if (*spcm->pcm.caps[0].name &&
		    !strcmp(spcm->pcm.caps[0].name, name))
			return spcm;

		/* match with capture caps name if set */
		if (*spcm->pcm.caps[1].name &&
		    !strcmp(spcm->pcm.caps[1].name, name))
			return spcm;
	}

	return NULL;
}

struct snd_sof_pcm *snd_sof_find_spcm_comp(struct snd_sof_dev *sdev,
					   unsigned int comp_id,
					   int *direction)
{
	struct snd_sof_pcm *spcm;

	list_for_each_entry(spcm, &sdev->pcm_list, list) {
		if (spcm->stream[SNDRV_PCM_STREAM_PLAYBACK].comp_id == comp_id) {
			*direction = SNDRV_PCM_STREAM_PLAYBACK;
			return spcm;
		}
		if (spcm->stream[SNDRV_PCM_STREAM_CAPTURE].comp_id == comp_id) {
			*direction = SNDRV_PCM_STREAM_CAPTURE;
			return spcm;
		}
	}

	return NULL;
}

struct snd_sof_pcm *snd_sof_find_spcm_pcm_id(struct snd_sof_dev *sdev,
					     unsigned int pcm_id)
{
	struct snd_sof_pcm *spcm;

	list_for_each_entry(spcm, &sdev->pcm_list, list) {
		if (le32_to_cpu(spcm->pcm.pcm_id) == pcm_id)
			return spcm;
	}

	return NULL;
}

struct snd_sof_widget *snd_sof_find_swidget(struct snd_sof_dev *sdev,
					    const char *name)
{
	struct snd_sof_widget *swidget;

	list_for_each_entry(swidget, &sdev->widget_list, list) {
		if (strcmp(name, swidget->widget->name) == 0)
			return swidget;
	}

	return NULL;
}

/* find widget by stream name and direction */
struct snd_sof_widget *snd_sof_find_swidget_sname(struct snd_sof_dev *sdev,
						  const char *pcm_name, int dir)
{
	struct snd_sof_widget *swidget;
	enum snd_soc_dapm_type type;

	if (dir == SNDRV_PCM_STREAM_PLAYBACK)
		type = snd_soc_dapm_aif_in;
	else
		type = snd_soc_dapm_aif_out;

	list_for_each_entry(swidget, &sdev->widget_list, list) {
		if (!strcmp(pcm_name, swidget->widget->sname) && swidget->id == type)
			return swidget;
	}

	return NULL;
}

struct snd_sof_dai *snd_sof_find_dai(struct snd_sof_dev *sdev,
				     const char *name)
{
	struct snd_sof_dai *dai;

	list_for_each_entry(dai, &sdev->dai_list, list) {
		if (dai->name && (strcmp(name, dai->name) == 0))
			return dai;
	}

	return NULL;
}

bool snd_sof_dsp_d0i3_on_suspend(struct snd_sof_dev *sdev)
{
	struct snd_sof_pcm *spcm;

	list_for_each_entry(spcm, &sdev->pcm_list, list) {
		if (spcm->stream[SNDRV_PCM_STREAM_PLAYBACK].suspend_ignored ||
		    spcm->stream[SNDRV_PCM_STREAM_CAPTURE].suspend_ignored)
			return true;
	}

	return false;
}

/*
 * FW Panic/fault handling.
 */

struct sof_panic_msg {
	u32 id;
	const char *msg;
};

/* standard FW panic types */
static const struct sof_panic_msg panic_msg[] = {
	{SOF_IPC_PANIC_MEM, "out of memory"},
	{SOF_IPC_PANIC_WORK, "work subsystem init failed"},
	{SOF_IPC_PANIC_IPC, "IPC subsystem init failed"},
	{SOF_IPC_PANIC_ARCH, "arch init failed"},
	{SOF_IPC_PANIC_PLATFORM, "platform init failed"},
	{SOF_IPC_PANIC_TASK, "scheduler init failed"},
	{SOF_IPC_PANIC_EXCEPTION, "runtime exception"},
	{SOF_IPC_PANIC_DEADLOCK, "deadlock"},
	{SOF_IPC_PANIC_STACK, "stack overflow"},
	{SOF_IPC_PANIC_IDLE, "can't enter idle"},
	{SOF_IPC_PANIC_WFI, "invalid wait state"},
	{SOF_IPC_PANIC_ASSERT, "assertion failed"},
};

/*
 * helper to be called from .dbg_dump callbacks. No error code is
 * provided, it's left as an exercise for the caller of .dbg_dump
 * (typically IPC or loader)
 */
void snd_sof_get_status(struct snd_sof_dev *sdev, u32 panic_code,
			u32 tracep_code, void *oops,
			struct sof_ipc_panic_info *panic_info,
			void *stack, size_t stack_words)
{
	u32 code;
	int i;

	/* is firmware dead ? */
	if ((panic_code & SOF_IPC_PANIC_MAGIC_MASK) != SOF_IPC_PANIC_MAGIC) {
		dev_err(sdev->dev, "error: unexpected fault 0x%8.8x trace 0x%8.8x\n",
			panic_code, tracep_code);
		return; /* no fault ? */
	}

	code = panic_code & (SOF_IPC_PANIC_MAGIC_MASK | SOF_IPC_PANIC_CODE_MASK);

	for (i = 0; i < ARRAY_SIZE(panic_msg); i++) {
		if (panic_msg[i].id == code) {
			dev_err(sdev->dev, "error: %s\n", panic_msg[i].msg);
			dev_err(sdev->dev, "error: trace point %8.8x\n",
				tracep_code);
			goto out;
		}
	}

	/* unknown error */
	dev_err(sdev->dev, "error: unknown reason %8.8x\n", panic_code);
	dev_err(sdev->dev, "error: trace point %8.8x\n", tracep_code);

out:
	dev_err(sdev->dev, "error: panic at %s:%d\n",
		panic_info->filename, panic_info->linenum);
	sof_oops(sdev, oops);
	sof_stack(sdev, oops, stack, stack_words);
}
EXPORT_SYMBOL(snd_sof_get_status);

/*
 * Generic buffer page table creation.
 * Take the each physical page address and drop the least significant unused
 * bits from each (based on PAGE_SIZE). Then pack valid page address bits
 * into compressed page table.
 */

int snd_sof_create_page_table(struct snd_sof_dev *sdev,
			      struct snd_dma_buffer *dmab,
			      unsigned char *page_table, size_t size)
{
	int i, pages;

	pages = snd_sgbuf_aligned_pages(size);

	dev_dbg(sdev->dev, "generating page table for %p size 0x%zx pages %d\n",
		dmab->area, size, pages);

	for (i = 0; i < pages; i++) {
		/*
		 * The number of valid address bits for each page is 20.
		 * idx determines the byte position within page_table
		 * where the current page's address is stored
		 * in the compressed page_table.
		 * This can be calculated by multiplying the page number by 2.5.
		 */
		u32 idx = (5 * i) >> 1;
		u32 pfn = snd_sgbuf_get_addr(dmab, i * PAGE_SIZE) >> PAGE_SHIFT;
		u8 *pg_table;

		dev_vdbg(sdev->dev, "pfn i %i idx %d pfn %x\n", i, idx, pfn);

		pg_table = (u8 *)(page_table + idx);

		/*
		 * pagetable compression:
		 * byte 0     byte 1     byte 2     byte 3     byte 4     byte 5
		 * ___________pfn 0__________ __________pfn 1___________  _pfn 2...
		 * .... ....  .... ....  .... ....  .... ....  .... ....  ....
		 * It is created by:
		 * 1. set current location to 0, PFN index i to 0
		 * 2. put pfn[i] at current location in Little Endian byte order
		 * 3. calculate an intermediate value as
		 *    x = (pfn[i+1] << 4) | (pfn[i] & 0xf)
		 * 4. put x at offset (current location + 2) in LE byte order
		 * 5. increment current location by 5 bytes, increment i by 2
		 * 6. continue to (2)
		 */
		if (i & 1)
			put_unaligned_le32((pg_table[0] & 0xf) | pfn << 4,
					   pg_table);
		else
			put_unaligned_le32(pfn, pg_table);
	}

	return pages;
}

/*
 * SOF Driver enumeration.
 */
static int sof_machine_check(struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *plat_data = sdev->pdata;
#if IS_ENABLED(CONFIG_SND_SOC_SOF_NOCODEC)
	struct snd_soc_acpi_mach *machine;
	int ret;
#endif

	if (plat_data->machine)
		return 0;

#if !IS_ENABLED(CONFIG_SND_SOC_SOF_NOCODEC)
	dev_err(sdev->dev, "error: no matching ASoC machine driver found - aborting probe\n");
	return -ENODEV;
#else
	/* fallback to nocodec mode */
	dev_warn(sdev->dev, "No ASoC machine driver found - using nocodec\n");
	machine = devm_kzalloc(sdev->dev, sizeof(*machine), GFP_KERNEL);
	if (!machine)
		return -ENOMEM;

	ret = sof_nocodec_setup(sdev->dev, plat_data, machine,
				plat_data->desc, plat_data->desc->ops);
	if (ret < 0)
		return ret;

	plat_data->machine = machine;

	return 0;
#endif
}

static int sof_probe_continue(struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *plat_data = sdev->pdata;
	const char *drv_name;
	const void *mach;
	int size;
	int ret;

	/* probe the DSP hardware */
	ret = snd_sof_probe(sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to probe DSP %d\n", ret);
		return ret;
	}

	/* check machine info */
	ret = sof_machine_check(sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to get machine info %d\n",
			ret);
		goto dbg_err;
	}

	/* set up platform component driver */
	snd_sof_new_platform_drv(sdev);

	/* register any debug/trace capabilities */
	ret = snd_sof_dbg_init(sdev);
	if (ret < 0) {
		/*
		 * debugfs issues are suppressed in snd_sof_dbg_init() since
		 * we cannot rely on debugfs
		 * here we trap errors due to memory allocation only.
		 */
		dev_err(sdev->dev, "error: failed to init DSP trace/debug %d\n",
			ret);
		goto dbg_err;
	}

	/* init the IPC */
	sdev->ipc = snd_sof_ipc_init(sdev);
	if (!sdev->ipc) {
		dev_err(sdev->dev, "error: failed to init DSP IPC %d\n", ret);
		goto ipc_err;
	}

	/* load the firmware */
	ret = snd_sof_load_firmware(sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to load DSP firmware %d\n",
			ret);
		goto fw_load_err;
	}

	/* boot the firmware */
	ret = snd_sof_run_firmware(sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to boot DSP firmware %d\n",
			ret);
		goto fw_run_err;
	}

	if (IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_ENABLE_FIRMWARE_TRACE) ||
	    (sof_core_debug & SOF_DBG_ENABLE_TRACE)) {
		sdev->dtrace_is_supported = true;

		/* init DMA trace */
		ret = snd_sof_init_trace(sdev);
		if (ret < 0) {
			/* non fatal */
			dev_warn(sdev->dev,
				 "warning: failed to initialize trace %d\n",
				 ret);
		}
	} else {
		dev_dbg(sdev->dev, "SOF firmware trace disabled\n");
	}

	/* hereafter all FW boot flows are for PM reasons */
	sdev->first_boot = false;

	/* now register audio DSP platform driver and dai */
	ret = devm_snd_soc_register_component(sdev->dev, &sdev->plat_drv,
					      sof_ops(sdev)->drv,
					      sof_ops(sdev)->num_drv);
	if (ret < 0) {
		dev_err(sdev->dev,
			"error: failed to register DSP DAI driver %d\n", ret);
		goto fw_run_err;
	}

	drv_name = plat_data->machine->drv_name;
	mach = (const void *)plat_data->machine;
	size = sizeof(*plat_data->machine);

	/* register machine driver, pass machine info as pdata */
	plat_data->pdev_mach =
		platform_device_register_data(sdev->dev, drv_name,
					      PLATFORM_DEVID_NONE, mach, size);

	if (IS_ERR(plat_data->pdev_mach)) {
		ret = PTR_ERR(plat_data->pdev_mach);
		goto fw_run_err;
	}

	dev_dbg(sdev->dev, "created machine %s\n",
		dev_name(&plat_data->pdev_mach->dev));

	if (plat_data->sof_probe_complete)
		plat_data->sof_probe_complete(sdev->dev);

	return 0;

#if !IS_ENABLED(CONFIG_SND_SOC_SOF_PROBE_WORK_QUEUE)
fw_run_err:
	snd_sof_fw_unload(sdev);
fw_load_err:
	snd_sof_ipc_free(sdev);
ipc_err:
	snd_sof_free_debug(sdev);
dbg_err:
	snd_sof_remove(sdev);
#else

	/*
	 * when the probe_continue is handled in a work queue, the
	 * probe does not fail so we don't release resources here.
	 * They will be released with an explicit call to
	 * snd_sof_device_remove() when the PCI/ACPI device is removed
	 */

fw_run_err:
fw_load_err:
ipc_err:
dbg_err:

#endif

	return ret;
}

static void sof_probe_work(struct work_struct *work)
{
	struct snd_sof_dev *sdev =
		container_of(work, struct snd_sof_dev, probe_work);
	int ret;

	ret = sof_probe_continue(sdev);
	if (ret < 0) {
		/* errors cannot be propagated, log */
		dev_err(sdev->dev, "error: %s failed err: %d\n", __func__, ret);
	}
}

int snd_sof_device_probe(struct device *dev, struct snd_sof_pdata *plat_data)
{
	struct snd_sof_dev *sdev;

	sdev = devm_kzalloc(dev, sizeof(*sdev), GFP_KERNEL);
	if (!sdev)
		return -ENOMEM;

	/* initialize sof device */
	sdev->dev = dev;

	/* initialize default D0 sub-state */
	sdev->d0_substate = SOF_DSP_D0I0;

	sdev->pdata = plat_data;
	sdev->first_boot = true;
	dev_set_drvdata(dev, sdev);

	/* check all mandatory ops */
	if (!sof_ops(sdev) || !sof_ops(sdev)->probe || !sof_ops(sdev)->run ||
	    !sof_ops(sdev)->block_read || !sof_ops(sdev)->block_write ||
	    !sof_ops(sdev)->send_msg || !sof_ops(sdev)->load_firmware ||
	    !sof_ops(sdev)->ipc_msg_data || !sof_ops(sdev)->ipc_pcm_params ||
	    !sof_ops(sdev)->fw_ready)
		return -EINVAL;

	INIT_LIST_HEAD(&sdev->pcm_list);
	INIT_LIST_HEAD(&sdev->kcontrol_list);
	INIT_LIST_HEAD(&sdev->widget_list);
	INIT_LIST_HEAD(&sdev->dai_list);
	INIT_LIST_HEAD(&sdev->route_list);
	spin_lock_init(&sdev->ipc_lock);
	spin_lock_init(&sdev->hw_lock);

	if (IS_ENABLED(CONFIG_SND_SOC_SOF_PROBE_WORK_QUEUE))
		INIT_WORK(&sdev->probe_work, sof_probe_work);

	/* set default timeouts if none provided */
	if (plat_data->desc->ipc_timeout == 0)
		sdev->ipc_timeout = TIMEOUT_DEFAULT_IPC_MS;
	else
		sdev->ipc_timeout = plat_data->desc->ipc_timeout;
	if (plat_data->desc->boot_timeout == 0)
		sdev->boot_timeout = TIMEOUT_DEFAULT_BOOT_MS;
	else
		sdev->boot_timeout = plat_data->desc->boot_timeout;

	if (IS_ENABLED(CONFIG_SND_SOC_SOF_PROBE_WORK_QUEUE)) {
		schedule_work(&sdev->probe_work);
		return 0;
	}

	return sof_probe_continue(sdev);
}
EXPORT_SYMBOL(snd_sof_device_probe);

int snd_sof_device_remove(struct device *dev)
{
	struct snd_sof_dev *sdev = dev_get_drvdata(dev);
	struct snd_sof_pdata *pdata = sdev->pdata;

	if (IS_ENABLED(CONFIG_SND_SOC_SOF_PROBE_WORK_QUEUE))
		cancel_work_sync(&sdev->probe_work);

	snd_sof_fw_unload(sdev);
	snd_sof_ipc_free(sdev);
	snd_sof_free_debug(sdev);
	snd_sof_free_trace(sdev);

	/*
	 * Unregister machine driver. This will unbind the snd_card which
	 * will remove the component driver and unload the topology
	 * before freeing the snd_card.
	 */
	if (!IS_ERR_OR_NULL(pdata->pdev_mach))
		platform_device_unregister(pdata->pdev_mach);

	/*
	 * Unregistering the machine driver results in unloading the topology.
	 * Some widgets, ex: scheduler, attempt to power down the core they are
	 * scheduled on, when they are unloaded. Therefore, the DSP must be
	 * removed only after the topology has been unloaded.
	 */
	snd_sof_remove(sdev);

	/* release firmware */
	release_firmware(pdata->fw);
	pdata->fw = NULL;

	return 0;
}
EXPORT_SYMBOL(snd_sof_device_remove);

MODULE_AUTHOR("Liam Girdwood");
MODULE_DESCRIPTION("Sound Open Firmware (SOF) Core");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("platform:sof-audio");
