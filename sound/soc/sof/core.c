// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//

#include <linux/firmware.h>
#include <linux/module.h>
#include <sound/soc.h>
#include <sound/sof.h>
#include "sof-priv.h"
#include "sof-of-dev.h"
#include "ops.h"

#define CREATE_TRACE_POINTS
#include <trace/events/sof.h>

/* Module parameters for firmware, topology and IPC type override */
static char *override_fw_path;
module_param_named(fw_path, override_fw_path, charp, 0444);
MODULE_PARM_DESC(fw_path, "alternate path for SOF firmware.");

static char *override_fw_filename;
module_param_named(fw_filename, override_fw_filename, charp, 0444);
MODULE_PARM_DESC(fw_filename, "alternate filename for SOF firmware.");

static char *override_lib_path;
module_param_named(lib_path, override_lib_path, charp, 0444);
MODULE_PARM_DESC(lib_path, "alternate path for SOF firmware libraries.");

static char *override_tplg_path;
module_param_named(tplg_path, override_tplg_path, charp, 0444);
MODULE_PARM_DESC(tplg_path, "alternate path for SOF topology.");

static char *override_tplg_filename;
module_param_named(tplg_filename, override_tplg_filename, charp, 0444);
MODULE_PARM_DESC(tplg_filename, "alternate filename for SOF topology.");

static int override_ipc_type = -1;
module_param_named(ipc_type, override_ipc_type, int, 0444);
MODULE_PARM_DESC(ipc_type, "Force SOF IPC type. 0 - IPC3, 1 - IPC4");

/* see SOF_DBG_ flags */
static int sof_core_debug =  IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG_ENABLE_FIRMWARE_TRACE);
module_param_named(sof_debug, sof_core_debug, int, 0444);
MODULE_PARM_DESC(sof_debug, "SOF core debug options (0x0 all off)");

#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG)
static unsigned int sof_ipc_timeout_ms;
static unsigned int sof_boot_timeout_ms;
module_param_named(ipc_timeout, sof_ipc_timeout_ms, uint, 0444);
MODULE_PARM_DESC(ipc_timeout,
		 "Set the IPC timeout value in ms (0 to use the platform default)");
module_param_named(boot_timeout, sof_boot_timeout_ms, uint, 0444);
MODULE_PARM_DESC(boot_timeout,
		 "Set the DSP boot timeout value in ms (0 to use the platform default)");
#endif

/* SOF defaults if not provided by the platform in ms */
#define TIMEOUT_DEFAULT_IPC_MS  500
#define TIMEOUT_DEFAULT_BOOT_MS 2000

/**
 * sof_debug_check_flag - check if a given flag(s) is set in sof_core_debug
 * @mask: Flag or combination of flags to check
 *
 * Returns true if all bits set in mask is also set in sof_core_debug, otherwise
 * false
 */
bool sof_debug_check_flag(int mask)
{
	if ((sof_core_debug & mask) == mask)
		return true;

	return false;
}
EXPORT_SYMBOL(sof_debug_check_flag);

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

/**
 * sof_print_oops_and_stack - Handle the printing of DSP oops and stack trace
 * @sdev: Pointer to the device's sdev
 * @level: prink log level to use for the printing
 * @panic_code: the panic code
 * @tracep_code: tracepoint code
 * @oops: Pointer to DSP specific oops data
 * @panic_info: Pointer to the received panic information message
 * @stack: Pointer to the call stack data
 * @stack_words: Number of words in the stack data
 *
 * helper to be called from .dbg_dump callbacks. No error code is
 * provided, it's left as an exercise for the caller of .dbg_dump
 * (typically IPC or loader)
 */
void sof_print_oops_and_stack(struct snd_sof_dev *sdev, const char *level,
			      u32 panic_code, u32 tracep_code, void *oops,
			      struct sof_ipc_panic_info *panic_info,
			      void *stack, size_t stack_words)
{
	u32 code;
	int i;

	/* is firmware dead ? */
	if ((panic_code & SOF_IPC_PANIC_MAGIC_MASK) != SOF_IPC_PANIC_MAGIC) {
		dev_printk(level, sdev->dev, "unexpected fault %#010x trace %#010x\n",
			   panic_code, tracep_code);
		return; /* no fault ? */
	}

	code = panic_code & (SOF_IPC_PANIC_MAGIC_MASK | SOF_IPC_PANIC_CODE_MASK);

	for (i = 0; i < ARRAY_SIZE(panic_msg); i++) {
		if (panic_msg[i].id == code) {
			dev_printk(level, sdev->dev, "reason: %s (%#x)\n",
				   panic_msg[i].msg, code & SOF_IPC_PANIC_CODE_MASK);
			dev_printk(level, sdev->dev, "trace point: %#010x\n", tracep_code);
			goto out;
		}
	}

	/* unknown error */
	dev_printk(level, sdev->dev, "unknown panic code: %#x\n",
		   code & SOF_IPC_PANIC_CODE_MASK);
	dev_printk(level, sdev->dev, "trace point: %#010x\n", tracep_code);

out:
	dev_printk(level, sdev->dev, "panic at %s:%d\n", panic_info->filename,
		   panic_info->linenum);
	sof_oops(sdev, level, oops);
	sof_stack(sdev, level, oops, stack, stack_words);
}
EXPORT_SYMBOL(sof_print_oops_and_stack);

/* Helper to manage DSP state */
void sof_set_fw_state(struct snd_sof_dev *sdev, enum sof_fw_state new_state)
{
	if (sdev->fw_state == new_state)
		return;

	dev_dbg(sdev->dev, "fw_state change: %d -> %d\n", sdev->fw_state, new_state);
	sdev->fw_state = new_state;

	switch (new_state) {
	case SOF_FW_BOOT_NOT_STARTED:
	case SOF_FW_BOOT_COMPLETE:
	case SOF_FW_CRASHED:
		sof_client_fw_state_dispatcher(sdev);
		fallthrough;
	default:
		break;
	}
}
EXPORT_SYMBOL(sof_set_fw_state);

static struct snd_sof_of_mach *sof_of_machine_select(struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *sof_pdata = sdev->pdata;
	const struct sof_dev_desc *desc = sof_pdata->desc;
	struct snd_sof_of_mach *mach = desc->of_machines;

	if (!mach)
		return NULL;

	for (; mach->compatible; mach++) {
		if (of_machine_is_compatible(mach->compatible)) {
			sof_pdata->tplg_filename = mach->sof_tplg_filename;
			if (mach->fw_filename)
				sof_pdata->fw_filename = mach->fw_filename;

			return mach;
		}
	}

	return NULL;
}

/* SOF Driver enumeration */
static int sof_machine_check(struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *sof_pdata = sdev->pdata;
	const struct sof_dev_desc *desc = sof_pdata->desc;
	struct snd_soc_acpi_mach *mach;

	if (!IS_ENABLED(CONFIG_SND_SOC_SOF_FORCE_NOCODEC_MODE)) {
		const struct snd_sof_of_mach *of_mach;

		if (IS_ENABLED(CONFIG_SND_SOC_SOF_NOCODEC_DEBUG_SUPPORT) &&
		    sof_debug_check_flag(SOF_DBG_FORCE_NOCODEC))
			goto nocodec;

		/* find machine */
		mach = snd_sof_machine_select(sdev);
		if (mach) {
			sof_pdata->machine = mach;

			if (sof_pdata->subsystem_id_set) {
				mach->mach_params.subsystem_vendor = sof_pdata->subsystem_vendor;
				mach->mach_params.subsystem_device = sof_pdata->subsystem_device;
				mach->mach_params.subsystem_id_set = true;
			}

			snd_sof_set_mach_params(mach, sdev);
			return 0;
		}

		of_mach = sof_of_machine_select(sdev);
		if (of_mach) {
			sof_pdata->of_machine = of_mach;
			return 0;
		}

		if (!IS_ENABLED(CONFIG_SND_SOC_SOF_NOCODEC)) {
			dev_err(sdev->dev, "error: no matching ASoC machine driver found - aborting probe\n");
			return -ENODEV;
		}
	} else {
		dev_warn(sdev->dev, "Force to use nocodec mode\n");
	}

nocodec:
	/* select nocodec mode */
	dev_warn(sdev->dev, "Using nocodec machine driver\n");
	mach = devm_kzalloc(sdev->dev, sizeof(*mach), GFP_KERNEL);
	if (!mach)
		return -ENOMEM;

	mach->drv_name = "sof-nocodec";
	if (!sof_pdata->tplg_filename)
		sof_pdata->tplg_filename = desc->nocodec_tplg_filename;

	sof_pdata->machine = mach;
	snd_sof_set_mach_params(mach, sdev);

	return 0;
}

static int sof_select_ipc_and_paths(struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *plat_data = sdev->pdata;
	struct sof_loadable_file_profile *base_profile = &plat_data->ipc_file_profile_base;
	struct sof_loadable_file_profile out_profile;
	struct device *dev = sdev->dev;
	int ret;

	if (base_profile->ipc_type != plat_data->desc->ipc_default)
		dev_info(dev,
			 "Module parameter used, overriding default IPC %d to %d\n",
			 plat_data->desc->ipc_default, base_profile->ipc_type);

	if (base_profile->fw_path)
		dev_dbg(dev, "Module parameter used, changed fw path to %s\n",
			base_profile->fw_path);
	else if (base_profile->fw_path_postfix)
		dev_dbg(dev, "Path postfix appended to default fw path: %s\n",
			base_profile->fw_path_postfix);

	if (base_profile->fw_lib_path)
		dev_dbg(dev, "Module parameter used, changed fw_lib path to %s\n",
			base_profile->fw_lib_path);
	else if (base_profile->fw_lib_path_postfix)
		dev_dbg(dev, "Path postfix appended to default fw_lib path: %s\n",
			base_profile->fw_lib_path_postfix);

	if (base_profile->fw_name)
		dev_dbg(dev, "Module parameter used, changed fw filename to %s\n",
			base_profile->fw_name);

	if (base_profile->tplg_path)
		dev_dbg(dev, "Module parameter used, changed tplg path to %s\n",
			base_profile->tplg_path);

	if (base_profile->tplg_name)
		dev_dbg(dev, "Module parameter used, changed tplg name to %s\n",
			base_profile->tplg_name);

	ret = sof_create_ipc_file_profile(sdev, base_profile, &out_profile);
	if (ret)
		return ret;

	plat_data->ipc_type = out_profile.ipc_type;
	plat_data->fw_filename = out_profile.fw_name;
	plat_data->fw_filename_prefix = out_profile.fw_path;
	plat_data->fw_lib_prefix = out_profile.fw_lib_path;
	plat_data->tplg_filename_prefix = out_profile.tplg_path;

	return 0;
}

static int validate_sof_ops(struct snd_sof_dev *sdev)
{
	int ret;

	/* init ops, if necessary */
	ret = sof_ops_init(sdev);
	if (ret < 0)
		return ret;

	/* check all mandatory ops */
	if (!sof_ops(sdev) || !sof_ops(sdev)->probe) {
		dev_err(sdev->dev, "missing mandatory ops\n");
		sof_ops_free(sdev);
		return -EINVAL;
	}

	if (!sdev->dspless_mode_selected &&
	    (!sof_ops(sdev)->run || !sof_ops(sdev)->block_read ||
	     !sof_ops(sdev)->block_write || !sof_ops(sdev)->send_msg ||
	     !sof_ops(sdev)->load_firmware || !sof_ops(sdev)->ipc_msg_data)) {
		dev_err(sdev->dev, "missing mandatory DSP ops\n");
		sof_ops_free(sdev);
		return -EINVAL;
	}

	return 0;
}

static int sof_init_sof_ops(struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *plat_data = sdev->pdata;
	struct sof_loadable_file_profile *base_profile = &plat_data->ipc_file_profile_base;

	/* check IPC support */
	if (!(BIT(base_profile->ipc_type) & plat_data->desc->ipc_supported_mask)) {
		dev_err(sdev->dev,
			"ipc_type %d is not supported on this platform, mask is %#x\n",
			base_profile->ipc_type, plat_data->desc->ipc_supported_mask);
		return -EINVAL;
	}

	/*
	 * Save the selected IPC type and a topology name override before
	 * selecting ops since platform code might need this information
	 */
	plat_data->ipc_type = base_profile->ipc_type;
	plat_data->tplg_filename = base_profile->tplg_name;

	return validate_sof_ops(sdev);
}

static int sof_init_environment(struct snd_sof_dev *sdev)
{
	struct snd_sof_pdata *plat_data = sdev->pdata;
	struct sof_loadable_file_profile *base_profile = &plat_data->ipc_file_profile_base;
	int ret;

	/* probe the DSP hardware */
	ret = snd_sof_probe(sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "failed to probe DSP %d\n", ret);
		goto err_sof_probe;
	}

	/* check machine info */
	ret = sof_machine_check(sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "failed to get machine info %d\n", ret);
		goto err_machine_check;
	}

	ret = sof_select_ipc_and_paths(sdev);
	if (ret) {
		goto err_machine_check;
	} else if (plat_data->ipc_type != base_profile->ipc_type) {
		/* IPC type changed, re-initialize the ops */
		sof_ops_free(sdev);

		ret = validate_sof_ops(sdev);
		if (ret < 0) {
			snd_sof_remove(sdev);
			snd_sof_remove_late(sdev);
			return ret;
		}
	}

	return 0;

err_machine_check:
	snd_sof_remove(sdev);
err_sof_probe:
	snd_sof_remove_late(sdev);
	sof_ops_free(sdev);

	return ret;
}

/*
 *			FW Boot State Transition Diagram
 *
 *    +----------------------------------------------------------------------+
 *    |									     |
 * ------------------	     ------------------				     |
 * |		    |	     |		      |				     |
 * |   BOOT_FAILED  |<-------|  READY_FAILED  |				     |
 * |		    |<--+    |	              |	   ------------------	     |
 * ------------------	|    ------------------	   |		    |	     |
 *	^		|	    ^		   |	CRASHED	    |---+    |
 *	|		|	    |		   |		    |	|    |
 * (FW Boot Timeout)	|	(FW_READY FAIL)	   ------------------	|    |
 *	|		|	    |		     ^			|    |
 *	|		|	    |		     |(DSP Panic)	|    |
 * ------------------	|	    |		   ------------------	|    |
 * |		    |	|	    |		   |		    |	|    |
 * |   IN_PROGRESS  |---------------+------------->|    COMPLETE    |	|    |
 * |		    | (FW Boot OK)   (FW_READY OK) |		    |	|    |
 * ------------------	|			   ------------------	|    |
 *	^		|				|		|    |
 *	|		|				|		|    |
 * (FW Loading OK)	|			(System Suspend/Runtime Suspend)
 *	|		|				|		|    |
 *	|	(FW Loading Fail)			|		|    |
 * ------------------	|	------------------	|		|    |
 * |		    |	|	|		 |<-----+		|    |
 * |   PREPARE	    |---+	|   NOT_STARTED  |<---------------------+    |
 * |		    |		|		 |<--------------------------+
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

	/* Initialize loadable file paths and check the environment validity */
	ret = sof_init_environment(sdev);
	if (ret)
		return ret;

	sof_set_fw_state(sdev, SOF_FW_BOOT_PREPARE);

	/* set up platform component driver */
	snd_sof_new_platform_drv(sdev);

	if (sdev->dspless_mode_selected) {
		sof_set_fw_state(sdev, SOF_DSPLESS_MODE);
		goto skip_dsp_init;
	}

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
		sof_set_fw_state(sdev, SOF_FW_BOOT_FAILED);
		goto fw_load_err;
	}

	sof_set_fw_state(sdev, SOF_FW_BOOT_IN_PROGRESS);

	/*
	 * Boot the firmware. The FW boot status will be modified
	 * in snd_sof_run_firmware() depending on the outcome.
	 */
	ret = snd_sof_run_firmware(sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to boot DSP firmware %d\n",
			ret);
		sof_set_fw_state(sdev, SOF_FW_BOOT_FAILED);
		goto fw_run_err;
	}

	if (sof_debug_check_flag(SOF_DBG_ENABLE_TRACE)) {
		sdev->fw_trace_is_supported = true;

		/* init firmware tracing */
		ret = sof_fw_trace_init(sdev);
		if (ret < 0) {
			/* non fatal */
			dev_warn(sdev->dev, "failed to initialize firmware tracing %d\n",
				 ret);
		}
	} else {
		dev_dbg(sdev->dev, "SOF firmware trace disabled\n");
	}

skip_dsp_init:
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

	ret = sof_register_clients(sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "failed to register clients %d\n", ret);
		goto sof_machine_err;
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

sof_machine_err:
	snd_sof_machine_unregister(sdev, plat_data);
fw_trace_err:
	sof_fw_trace_free(sdev);
fw_run_err:
	snd_sof_fw_unload(sdev);
fw_load_err:
	snd_sof_ipc_free(sdev);
ipc_err:
dbg_err:
	snd_sof_free_debug(sdev);
	snd_sof_remove(sdev);
	snd_sof_remove_late(sdev);
	sof_ops_free(sdev);

	/* all resources freed, update state to match */
	sof_set_fw_state(sdev, SOF_FW_BOOT_NOT_STARTED);
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

static void
sof_apply_profile_override(struct sof_loadable_file_profile *path_override)
{
	if (override_ipc_type >= 0 && override_ipc_type < SOF_IPC_TYPE_COUNT)
		path_override->ipc_type = override_ipc_type;
	if (override_fw_path)
		path_override->fw_path = override_fw_path;
	if (override_fw_filename)
		path_override->fw_name = override_fw_filename;
	if (override_lib_path)
		path_override->fw_lib_path = override_lib_path;
	if (override_tplg_path)
		path_override->tplg_path = override_tplg_path;
	if (override_tplg_filename)
		path_override->tplg_name = override_tplg_filename;
}

int snd_sof_device_probe(struct device *dev, struct snd_sof_pdata *plat_data)
{
	struct snd_sof_dev *sdev;
	int ret;

	sdev = devm_kzalloc(dev, sizeof(*sdev), GFP_KERNEL);
	if (!sdev)
		return -ENOMEM;

	/* initialize sof device */
	sdev->dev = dev;

	/* initialize default DSP power state */
	sdev->dsp_power_state.state = SOF_DSP_PM_D0;

	sdev->pdata = plat_data;
	sdev->first_boot = true;
	dev_set_drvdata(dev, sdev);

	if (sof_core_debug)
		dev_info(dev, "sof_debug value: %#x\n", sof_core_debug);

	if (sof_debug_check_flag(SOF_DBG_DSPLESS_MODE)) {
		if (plat_data->desc->dspless_mode_supported) {
			dev_info(dev, "Switching to DSPless mode\n");
			sdev->dspless_mode_selected = true;
		} else {
			dev_info(dev, "DSPless mode is not supported by the platform\n");
		}
	}

	sof_apply_profile_override(&plat_data->ipc_file_profile_base);

	/* Initialize sof_ops based on the initial selected IPC version */
	ret = sof_init_sof_ops(sdev);
	if (ret)
		return ret;

	INIT_LIST_HEAD(&sdev->pcm_list);
	INIT_LIST_HEAD(&sdev->kcontrol_list);
	INIT_LIST_HEAD(&sdev->widget_list);
	INIT_LIST_HEAD(&sdev->pipeline_list);
	INIT_LIST_HEAD(&sdev->dai_list);
	INIT_LIST_HEAD(&sdev->dai_link_list);
	INIT_LIST_HEAD(&sdev->route_list);
	INIT_LIST_HEAD(&sdev->ipc_client_list);
	INIT_LIST_HEAD(&sdev->ipc_rx_handler_list);
	INIT_LIST_HEAD(&sdev->fw_state_handler_list);
	spin_lock_init(&sdev->ipc_lock);
	spin_lock_init(&sdev->hw_lock);
	mutex_init(&sdev->power_state_access);
	mutex_init(&sdev->ipc_client_mutex);
	mutex_init(&sdev->client_event_handler_mutex);

	/* set default timeouts if none provided */
	if (plat_data->desc->ipc_timeout == 0)
		sdev->ipc_timeout = TIMEOUT_DEFAULT_IPC_MS;
	else
		sdev->ipc_timeout = plat_data->desc->ipc_timeout;
	if (plat_data->desc->boot_timeout == 0)
		sdev->boot_timeout = TIMEOUT_DEFAULT_BOOT_MS;
	else
		sdev->boot_timeout = plat_data->desc->boot_timeout;

#if IS_ENABLED(CONFIG_SND_SOC_SOF_DEBUG)
	/* Override the timeout values with module parameter, if set */
	if (sof_ipc_timeout_ms)
		sdev->ipc_timeout = sof_ipc_timeout_ms;

	if (sof_boot_timeout_ms)
		sdev->boot_timeout = sof_boot_timeout_ms;
#endif

	sof_set_fw_state(sdev, SOF_FW_BOOT_NOT_STARTED);

	/*
	 * first pass of probe which isn't allowed to run in a work-queue,
	 * typically to rely on -EPROBE_DEFER dependencies
	 */
	ret = snd_sof_probe_early(sdev);
	if (ret < 0)
		return ret;

	if (IS_ENABLED(CONFIG_SND_SOC_SOF_PROBE_WORK_QUEUE)) {
		INIT_WORK(&sdev->probe_work, sof_probe_work);
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
	bool aborted = false;

	if (IS_ENABLED(CONFIG_SND_SOC_SOF_PROBE_WORK_QUEUE))
		aborted = cancel_work_sync(&sdev->probe_work);

	/*
	 * Unregister any registered client device first before IPC and debugfs
	 * to allow client drivers to be removed cleanly
	 */
	sof_unregister_clients(sdev);

	/*
	 * Unregister machine driver. This will unbind the snd_card which
	 * will remove the component driver and unload the topology
	 * before freeing the snd_card.
	 */
	snd_sof_machine_unregister(sdev, pdata);

	/*
	 * Balance the runtime pm usage count in case we are faced with an
	 * exception and we forcably prevented D3 power state to preserve
	 * context
	 */
	if (sdev->d3_prevented) {
		sdev->d3_prevented = false;
		pm_runtime_put_noidle(sdev->dev);
	}

	if (sdev->fw_state > SOF_FW_BOOT_NOT_STARTED) {
		sof_fw_trace_free(sdev);
		ret = snd_sof_dsp_power_down_notify(sdev);
		if (ret < 0)
			dev_warn(dev, "error: %d failed to prepare DSP for device removal",
				 ret);

		snd_sof_ipc_free(sdev);
		snd_sof_free_debug(sdev);
		snd_sof_remove(sdev);
		snd_sof_remove_late(sdev);
		sof_ops_free(sdev);
	} else if (aborted) {
		/* probe_work never ran */
		snd_sof_remove_late(sdev);
		sof_ops_free(sdev);
	}

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

	if (sdev->fw_state == SOF_FW_BOOT_COMPLETE) {
		sof_fw_trace_free(sdev);
		return snd_sof_shutdown(sdev);
	}

	return 0;
}
EXPORT_SYMBOL(snd_sof_device_shutdown);

/* Machine driver registering and unregistering */
int sof_machine_register(struct snd_sof_dev *sdev, void *pdata)
{
	struct snd_sof_pdata *plat_data = pdata;
	const char *drv_name;
	const void *mach;
	int size;

	drv_name = plat_data->machine->drv_name;
	mach = plat_data->machine;
	size = sizeof(*plat_data->machine);

	/* register machine driver, pass machine info as pdata */
	plat_data->pdev_mach =
		platform_device_register_data(sdev->dev, drv_name,
					      PLATFORM_DEVID_NONE, mach, size);
	if (IS_ERR(plat_data->pdev_mach))
		return PTR_ERR(plat_data->pdev_mach);

	dev_dbg(sdev->dev, "created machine %s\n",
		dev_name(&plat_data->pdev_mach->dev));

	return 0;
}
EXPORT_SYMBOL(sof_machine_register);

void sof_machine_unregister(struct snd_sof_dev *sdev, void *pdata)
{
	struct snd_sof_pdata *plat_data = pdata;

	platform_device_unregister(plat_data->pdev_mach);
}
EXPORT_SYMBOL(sof_machine_unregister);

MODULE_AUTHOR("Liam Girdwood");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Sound Open Firmware (SOF) Core");
MODULE_ALIAS("platform:sof-audio");
MODULE_IMPORT_NS("SND_SOC_SOF_CLIENT");
