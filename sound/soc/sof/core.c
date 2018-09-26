// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//

#include <linux/pm_runtime.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <sound/core.h>
#include <sound/soc.h>
#include <sound/sof.h>
#include "sof-priv.h"
#include "ops.h"

/* SOF defaults if not provided by the platform in ms */
#define TIMEOUT_DEFAULT_IPC	5
#define TIMEOUT_DEFAULT_BOOT	100

/*
 * Generic object lookup APIs.
 */

struct snd_sof_pcm *snd_sof_find_spcm_dai(struct snd_sof_dev *sdev,
					  struct snd_soc_pcm_runtime *rtd)
{
	struct snd_sof_pcm *spcm = NULL;

	list_for_each_entry(spcm, &sdev->pcm_list, list) {
		if (le32_to_cpu(spcm->pcm.dai_id) == rtd->dai_link->id)
			return spcm;
	}

	return NULL;
}

struct snd_sof_pcm *snd_sof_find_spcm_name(struct snd_sof_dev *sdev,
					   char *name)
{
	struct snd_sof_pcm *spcm = NULL;

	list_for_each_entry(spcm, &sdev->pcm_list, list) {
		if (strcmp(spcm->pcm.dai_name, name) == 0)
			return spcm;

		if (strcmp(spcm->pcm.caps[0].name, name) == 0)
			return spcm;

		if (strcmp(spcm->pcm.caps[1].name, name) == 0)
			return spcm;
	}

	return NULL;
}

struct snd_sof_pcm *snd_sof_find_spcm_comp(struct snd_sof_dev *sdev,
					   unsigned int comp_id,
					   int *direction)
{
	struct snd_sof_pcm *spcm = NULL;

	list_for_each_entry(spcm, &sdev->pcm_list, list) {
		if (spcm->stream[SNDRV_PCM_STREAM_PLAYBACK].comp_id ==
			comp_id) {
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
	struct snd_sof_pcm *spcm = NULL;

	list_for_each_entry(spcm, &sdev->pcm_list, list) {
		if (le32_to_cpu(spcm->pcm.pcm_id) == pcm_id)
			return spcm;
	}

	return NULL;
}

struct snd_sof_widget *snd_sof_find_swidget(struct snd_sof_dev *sdev,
					    char *name)
{
	struct snd_sof_widget *swidget = NULL;

	list_for_each_entry(swidget, &sdev->widget_list, list) {
		if (strcmp(name, swidget->widget->name) == 0)
			return swidget;
	}

	return NULL;
}

struct snd_sof_dai *snd_sof_find_dai(struct snd_sof_dev *sdev,
				     char *name)
{
	struct snd_sof_dai *dai = NULL;

	list_for_each_entry(dai, &sdev->dai_list, list) {
		if (!dai->name)
			continue;

		if (strcmp(name, dai->name) == 0)
			return dai;
	}

	return NULL;
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
};

int snd_sof_get_status(struct snd_sof_dev *sdev, u32 panic_code,
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
		return 0; /* no fault ? */
	}

	code = panic_code &
		(SOF_IPC_PANIC_MAGIC_MASK | SOF_IPC_PANIC_CODE_MASK);

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
	dev_err(sdev->dev, "error: panic happen at %s:%d\n",
		panic_info->filename, panic_info->linenum);
	sof_oops(sdev, oops);
	sof_stack(sdev, oops, stack, stack_words);
	return -EFAULT;
}
EXPORT_SYMBOL(snd_sof_get_status);

/*
 * Generic buffer page table creation.
 * Take the each physical page address and drop the least significant unused
 * bites from each (based on PAGE_SIZE). Then pack valid page address bits
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
		u32 idx = (((i << 2) + i)) >> 1;
		u32 pfn = snd_sgbuf_get_addr(dmab, i * PAGE_SIZE) >> PAGE_SHIFT;
		u32 *pg_table;

		dev_dbg(sdev->dev, "pfn i %i idx %d pfn %x\n", i, idx, pfn);

		pg_table = (u32 *)(page_table + idx);

		if (i & 1)
			*pg_table |= (pfn << 4);
		else
			*pg_table |= pfn;
	}

	return pages;
}

/*
 * SOF Driver enumeration.
 */

static int sof_probe(struct platform_device *pdev)
{
	struct snd_sof_pdata *plat_data = dev_get_platdata(&pdev->dev);
	struct snd_sof_dev *sdev;
	int ret;

	sdev = devm_kzalloc(&pdev->dev, sizeof(*sdev), GFP_KERNEL);
	if (!sdev)
		return -ENOMEM;

	dev_dbg(&pdev->dev, "probing SOF DSP device....\n");

	/* initialize sof device */
	sdev->dev = &pdev->dev;
	sdev->parent = plat_data->dev;
	if (plat_data->type == SOF_DEVICE_PCI)
		sdev->pci = container_of(plat_data->dev, struct pci_dev, dev);
	sdev->ops = plat_data->machine->pdata;

	sdev->pdata = plat_data;
	INIT_LIST_HEAD(&sdev->pcm_list);
	INIT_LIST_HEAD(&sdev->kcontrol_list);
	INIT_LIST_HEAD(&sdev->widget_list);
	INIT_LIST_HEAD(&sdev->dai_list);
	INIT_LIST_HEAD(&sdev->route_list);
	dev_set_drvdata(&pdev->dev, sdev);
	spin_lock_init(&sdev->ipc_lock);
	spin_lock_init(&sdev->hw_lock);

	/* set up platform component driver */
	snd_sof_new_platform_drv(sdev);

	/* set default timeouts if none provided */
	if (plat_data->desc->ipc_timeout == 0)
		sdev->ipc_timeout = TIMEOUT_DEFAULT_IPC;
	else
		sdev->ipc_timeout = plat_data->desc->ipc_timeout;
	if (plat_data->desc->boot_timeout == 0)
		sdev->boot_timeout = TIMEOUT_DEFAULT_BOOT;
	else
		sdev->boot_timeout = plat_data->desc->boot_timeout;

	/* probe the DSP hardware */
	ret = snd_sof_probe(sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to probe DSP %d\n", ret);
		return ret;
	}

	/* register any debug/trace capabilities */
	ret = snd_sof_dbg_init(sdev);
	if (ret < 0) {
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
	ret = snd_sof_load_firmware(sdev, true);
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

	/* now register audio DSP platform driver and dai */
	ret = snd_soc_register_component(&pdev->dev,  &sdev->plat_drv,
					 sdev->ops->drv,
					 sdev->ops->num_drv);
	if (ret < 0) {
		dev_err(sdev->dev,
			"error: failed to register DSP DAI driver %d\n", ret);
		goto comp_err;
	}

	/* init DMA trace */
	ret = snd_sof_init_trace(sdev);
	if (ret < 0) {
		/* non fatal */
		dev_warn(sdev->dev,
			 "warning: failed to initialize trace %d\n", ret);
	}

	/* autosuspend sof device */
	pm_runtime_mark_last_busy(sdev->dev);
	pm_runtime_put_autosuspend(sdev->dev);

	/* autosuspend pci/acpi/spi device */
	pm_runtime_mark_last_busy(plat_data->dev);
	pm_runtime_put_autosuspend(plat_data->dev);

	return 0;

comp_err:
	snd_soc_unregister_component(&pdev->dev);
	snd_sof_free_topology(sdev);
fw_run_err:
	snd_sof_fw_unload(sdev);
fw_load_err:
	snd_sof_ipc_free(sdev);
ipc_err:
	snd_sof_free_debug(sdev);
dbg_err:
	snd_sof_remove(sdev);

	return ret;
}

static int sof_remove(struct platform_device *pdev)
{
	struct snd_sof_dev *sdev = dev_get_drvdata(&pdev->dev);

	snd_soc_unregister_component(&pdev->dev);
	snd_sof_fw_unload(sdev);
	snd_sof_ipc_free(sdev);
	snd_sof_free_debug(sdev);
	snd_sof_release_trace(sdev);
	snd_sof_remove(sdev);
	return 0;
}

void snd_sof_shutdown(struct device *dev)
{
}
EXPORT_SYMBOL(snd_sof_shutdown);

static struct platform_driver sof_driver = {
	.driver = {
		.name = "sof-audio",
	},

	.probe = sof_probe,
	.remove = sof_remove,
};
module_platform_driver(sof_driver);

MODULE_AUTHOR("Liam Girdwood");
MODULE_DESCRIPTION("Sound Open Firmware (SOF) Core");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("platform:sof-audio");
