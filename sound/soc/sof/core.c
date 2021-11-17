// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
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
#include <sound/soc.h>
#include <sound/sof.h>
#include "sof-priv.h"
#include "ops.h"
#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_PROBES)
#include "probe.h"
#endif

/* see SOF_DBG_ flags */
int sof_core_debug;
module_param_named(sof_debug, sof_core_debug, int, 0444);
MODULE_PARM_DESC(sof_debug, "SOF core debug options (0x0 all off)");

/* SOF defaults if not provided by the platform in ms */
#define TIMEOUT_DEFAULT_IPC_MS  500
#define TIMEOUT_DEFAULT_BOOT_MS 2000

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
 *			FW Boot State Transition Diagram
 *
 *    +-----------------------------------------------------------------------+
 *    |									      |
 * ------------------	     ------------------				      |
 * |		    |	     |		      |				      |
 * |   BOOT_FAILED  |	     |  READY_FAILED  |-------------------------+     |
 * |		    |	     |	              |				|     |
 * ------------------	     ------------------				|     |
 *	^			    ^					|     |
 *	|			    |					|     |
 * (FW Boot Timeout)		(FW_READY FAIL)				|     |
 *	|			    |					|     |
 *	|			    |					|     |
 * ------------------		    |		   ------------------	|     |
 * |		    |		    |		   |		    |	|     |
 * |   IN_PROGRESS  |---------------+------------->|    COMPLETE    |	|     |
 * |		    | (FW Boot OK)   (FW_READY OK) |		    |	|     |
 * ------------------				   ------------------	|     |
 *	^						|		|     |
 *	|						|		|     |
 * (FW Loading OK)			       (System Suspend/Runtime Suspend)
 *	|						|		|     |
 *	|						|		|     |
 * ------------------		------------------	|		|     |
 * |		    |		|		 |<-----+		|     |
 * |   PREPARE	    |		|   NOT_STARTED  |<---------------------+     |
 * |		    |		|		 |<---------------------------+
 * ------------------		------------------
 *    |	    ^			    |	   ^
 *    |	    |			    |	   |
 *    |	    +-----------------------+	   |
 *    |		(DSP Probe OK)		   |
 *    |					   |
 *    |					   |
 *    +------------------------------------+
 *	(System Suspend/Runtime Suspend)
 */

static int sof_probe_continue(struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *plat_data = sdev->pdata;
	int ret;

	/* probe the DSP hardware */
	ret = snd_sof_probe(sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to probe DSP %d\n", ret);
		return ret;
	}

	sdev->fw_state = SOF_FW_BOOT_PREPARE;

	/* check machine info */
	ret = sof_machine_check(sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to get machine info %d\n",
			ret);
		goto dsp_err;
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
		ret = -ENOMEM;
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

	sdev->fw_state = SOF_FW_BOOT_IN_PROGRESS;

	/*
	 * Boot the firmware. The FW boot status will be modified
	 * in snd_sof_run_firmware() depending on the outcome.
	 */
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
		goto fw_trace_err;
	}

	ret = snd_sof_machine_register(sdev, plat_data);
	if (ret < 0) {
		dev_err(sdev->dev,
			"error: failed to register machine driver %d\n", ret);
		goto fw_trace_err;
	}

	/*
	 * Some platforms in SOF, ex: BYT, may not have their platform PM
	 * callbacks set. Increment the usage count so as to
	 * prevent the device from entering runtime suspend.
	 */
	if (!sof_ops(sdev)->runtime_suspend || !sof_ops(sdev)->runtime_resume)
		pm_runtime_get_noresume(sdev->dev);

	if (plat_data->sof_probe_complete)
		plat_data->sof_probe_complete(sdev->dev);

	sdev->probe_completed = true;

	return 0;

fw_trace_err:
	snd_sof_free_trace(sdev);
fw_run_err:
	snd_sof_fw_unload(sdev);
fw_load_err:
	snd_sof_ipc_free(sdev);
ipc_err:
dbg_err:
	snd_sof_free_debug(sdev);
dsp_err:
	snd_sof_remove(sdev);

	/* all resources freed, update state to match */
	sdev->fw_state = SOF_FW_BOOT_NOT_STARTED;
	sdev->first_boot = true;

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

	/* initialize default DSP power state */
	sdev->dsp_power_state.state = SOF_DSP_PM_D0;

	sdev->pdata = plat_data;
	sdev->first_boot = true;
	sdev->fw_state = SOF_FW_BOOT_NOT_STARTED;
#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_PROBES)
	sdev->extractor_stream_tag = SOF_PROBE_INVALID_NODE_ID;
#endif
	dev_set_drvdata(dev, sdev);

	/* check all mandatory ops */
	if (!sof_ops(sdev) || !sof_ops(sdev)->probe || !sof_ops(sdev)->run ||
	    !sof_ops(sdev)->block_read || !sof_ops(sdev)->block_write ||
	    !sof_ops(sdev)->send_msg || !sof_ops(sdev)->load_firmware ||
	    !sof_ops(sdev)->ipc_msg_data || !sof_ops(sdev)->ipc_pcm_params ||
	    !sof_ops(sdev)->fw_ready) {
		dev_err(dev, "error: missing mandatory ops\n");
		return -EINVAL;
	}

	INIT_LIST_HEAD(&sdev->pcm_list);
	INIT_LIST_HEAD(&sdev->kcontrol_list);
	INIT_LIST_HEAD(&sdev->widget_list);
	INIT_LIST_HEAD(&sdev->dai_list);
	INIT_LIST_HEAD(&sdev->route_list);
	spin_lock_init(&sdev->ipc_lock);
	spin_lock_init(&sdev->hw_lock);
	mutex_init(&sdev->power_state_access);

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

bool snd_sof_device_probe_completed(struct device *dev)
{
	struct snd_sof_dev *sdev = dev_get_drvdata(dev);

	return sdev->probe_completed;
}
EXPORT_SYMBOL(snd_sof_device_probe_completed);

int snd_sof_device_remove(struct device *dev)
{
	struct snd_sof_dev *sdev = dev_get_drvdata(dev);
	struct snd_sof_pdata *pdata = sdev->pdata;
	int ret;

	if (IS_ENABLED(CONFIG_SND_SOC_SOF_PROBE_WORK_QUEUE))
		cancel_work_sync(&sdev->probe_work);

	if (sdev->fw_state > SOF_FW_BOOT_NOT_STARTED) {
		ret = snd_sof_dsp_power_down_notify(sdev);
		if (ret < 0)
			dev_warn(dev, "error: %d failed to prepare DSP for device removal",
				 ret);

		snd_sof_ipc_free(sdev);
		snd_sof_free_debug(sdev);
		snd_sof_free_trace(sdev);
	}

	/*
	 * Unregister machine driver. This will unbind the snd_card which
	 * will remove the component driver and unload the topology
	 * before freeing the snd_card.
	 */
	snd_sof_machine_unregister(sdev, pdata);

	/*
	 * Unregistering the machine driver results in unloading the topology.
	 * Some widgets, ex: scheduler, attempt to power down the core they are
	 * scheduled on, when they are unloaded. Therefore, the DSP must be
	 * removed only after the topology has been unloaded.
	 */
	if (sdev->fw_state > SOF_FW_BOOT_NOT_STARTED)
		snd_sof_remove(sdev);

	/* release firmware */
	snd_sof_fw_unload(sdev);

	return 0;
}
EXPORT_SYMBOL(snd_sof_device_remove);

int snd_sof_device_shutdown(struct device *dev)
{
	struct snd_sof_dev *sdev = dev_get_drvdata(dev);

	if (IS_ENABLED(CONFIG_SND_SOC_SOF_PROBE_WORK_QUEUE))
		cancel_work_sync(&sdev->probe_work);

	if (sdev->fw_state == SOF_FW_BOOT_COMPLETE)
		return snd_sof_shutdown(sdev);

	return 0;
}
EXPORT_SYMBOL(snd_sof_device_shutdown);

MODULE_AUTHOR("Liam Girdwood");
MODULE_DESCRIPTION("Sound Open Firmware (SOF) Core");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("platform:sof-audio");
