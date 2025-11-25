// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (c) 2025, Google LLC.
 * Pasha Tatashin <pasha.tatashin@soleen.com>
 */

/**
 * DOC: Live Update Orchestrator (LUO)
 *
 * Live Update is a specialized, kexec-based reboot process that allows a
 * running kernel to be updated from one version to another while preserving
 * the state of selected resources and keeping designated hardware devices
 * operational. For these devices, DMA activity may continue throughout the
 * kernel transition.
 *
 * While the primary use case driving this work is supporting live updates of
 * the Linux kernel when it is used as a hypervisor in cloud environments, the
 * LUO framework itself is designed to be workload-agnostic. Live Update
 * facilitates a full kernel version upgrade for any type of system.
 *
 * For example, a non-hypervisor system running an in-memory cache like
 * memcached with many gigabytes of data can use LUO. The userspace service
 * can place its cache into a memfd, have its state preserved by LUO, and
 * restore it immediately after the kernel kexec.
 *
 * Whether the system is running virtual machines, containers, a
 * high-performance database, or networking services, LUO's primary goal is to
 * enable a full kernel update by preserving critical userspace state and
 * keeping essential devices operational.
 *
 * The core of LUO is a mechanism that tracks the progress of a live update,
 * along with a callback API that allows other kernel subsystems to participate
 * in the process. Example subsystems that can hook into LUO include: kvm,
 * iommu, interrupts, vfio, participating filesystems, and memory management.
 *
 * LUO uses Kexec Handover to transfer memory state from the current kernel to
 * the next kernel. For more details see
 * Documentation/core-api/kho/concepts.rst.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/io.h>
#include <linux/kexec_handover.h>
#include <linux/kho/abi/luo.h>
#include <linux/kobject.h>
#include <linux/libfdt.h>
#include <linux/liveupdate.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/sizes.h>
#include <linux/string.h>
#include <linux/unaligned.h>

#include "kexec_handover_internal.h"
#include "luo_internal.h"

static struct {
	bool enabled;
	void *fdt_out;
	void *fdt_in;
	u64 liveupdate_num;
} luo_global;

static int __init early_liveupdate_param(char *buf)
{
	return kstrtobool(buf, &luo_global.enabled);
}
early_param("liveupdate", early_liveupdate_param);

static int __init luo_early_startup(void)
{
	phys_addr_t fdt_phys;
	int err, ln_size;
	const void *ptr;

	if (!kho_is_enabled()) {
		if (liveupdate_enabled())
			pr_warn("Disabling liveupdate because KHO is disabled\n");
		luo_global.enabled = false;
		return 0;
	}

	/* Retrieve LUO subtree, and verify its format. */
	err = kho_retrieve_subtree(LUO_FDT_KHO_ENTRY_NAME, &fdt_phys);
	if (err) {
		if (err != -ENOENT) {
			pr_err("failed to retrieve FDT '%s' from KHO: %pe\n",
			       LUO_FDT_KHO_ENTRY_NAME, ERR_PTR(err));
			return err;
		}

		return 0;
	}

	luo_global.fdt_in = phys_to_virt(fdt_phys);
	err = fdt_node_check_compatible(luo_global.fdt_in, 0,
					LUO_FDT_COMPATIBLE);
	if (err) {
		pr_err("FDT '%s' is incompatible with '%s' [%d]\n",
		       LUO_FDT_KHO_ENTRY_NAME, LUO_FDT_COMPATIBLE, err);

		return -EINVAL;
	}

	ln_size = 0;
	ptr = fdt_getprop(luo_global.fdt_in, 0, LUO_FDT_LIVEUPDATE_NUM,
			  &ln_size);
	if (!ptr || ln_size != sizeof(luo_global.liveupdate_num)) {
		pr_err("Unable to get live update number '%s' [%d]\n",
		       LUO_FDT_LIVEUPDATE_NUM, ln_size);

		return -EINVAL;
	}

	luo_global.liveupdate_num = get_unaligned((u64 *)ptr);
	pr_info("Retrieved live update data, liveupdate number: %lld\n",
		luo_global.liveupdate_num);

	err = luo_session_setup_incoming(luo_global.fdt_in);
	if (err)
		return err;

	return 0;
}

static int __init liveupdate_early_init(void)
{
	int err;

	err = luo_early_startup();
	if (err) {
		luo_global.enabled = false;
		luo_restore_fail("The incoming tree failed to initialize properly [%pe], disabling live update\n",
				 ERR_PTR(err));
	}

	return err;
}
early_initcall(liveupdate_early_init);

/* Called during boot to create outgoing LUO fdt tree */
static int __init luo_fdt_setup(void)
{
	const u64 ln = luo_global.liveupdate_num + 1;
	void *fdt_out;
	int err;

	fdt_out = kho_alloc_preserve(LUO_FDT_SIZE);
	if (IS_ERR(fdt_out)) {
		pr_err("failed to allocate/preserve FDT memory\n");
		return PTR_ERR(fdt_out);
	}

	err = fdt_create(fdt_out, LUO_FDT_SIZE);
	err |= fdt_finish_reservemap(fdt_out);
	err |= fdt_begin_node(fdt_out, "");
	err |= fdt_property_string(fdt_out, "compatible", LUO_FDT_COMPATIBLE);
	err |= fdt_property(fdt_out, LUO_FDT_LIVEUPDATE_NUM, &ln, sizeof(ln));
	err |= luo_session_setup_outgoing(fdt_out);
	err |= fdt_end_node(fdt_out);
	err |= fdt_finish(fdt_out);
	if (err)
		goto exit_free;

	err = kho_add_subtree(LUO_FDT_KHO_ENTRY_NAME, fdt_out);
	if (err)
		goto exit_free;
	luo_global.fdt_out = fdt_out;

	return 0;

exit_free:
	kho_unpreserve_free(fdt_out);
	pr_err("failed to prepare LUO FDT: %d\n", err);

	return err;
}

/*
 * late initcall because it initializes the outgoing tree that is needed only
 * once userspace starts using /dev/liveupdate.
 */
static int __init luo_late_startup(void)
{
	int err;

	if (!liveupdate_enabled())
		return 0;

	err = luo_fdt_setup();
	if (err)
		luo_global.enabled = false;

	return err;
}
late_initcall(luo_late_startup);

/* Public Functions */

/**
 * liveupdate_reboot() - Kernel reboot notifier for live update final
 * serialization.
 *
 * This function is invoked directly from the reboot() syscall pathway
 * if kexec is in progress.
 *
 * If any callback fails, this function aborts KHO, undoes the freeze()
 * callbacks, and returns an error.
 */
int liveupdate_reboot(void)
{
	int err;

	if (!liveupdate_enabled())
		return 0;

	err = luo_session_serialize();
	if (err)
		return err;

	err = kho_finalize();
	if (err) {
		pr_err("kho_finalize failed %d\n", err);
		/*
		 * kho_finalize() may return libfdt errors, to aboid passing to
		 * userspace unknown errors, change this to EAGAIN.
		 */
		err = -EAGAIN;
	}

	return err;
}

/**
 * liveupdate_enabled - Check if the live update feature is enabled.
 *
 * This function returns the state of the live update feature flag, which
 * can be controlled via the ``liveupdate`` kernel command-line parameter.
 *
 * @return true if live update is enabled, false otherwise.
 */
bool liveupdate_enabled(void)
{
	return luo_global.enabled;
}

struct luo_device_state {
	struct miscdevice miscdev;
};

static const struct file_operations luo_fops = {
	.owner		= THIS_MODULE,
};

static struct luo_device_state luo_dev = {
	.miscdev = {
		.minor = MISC_DYNAMIC_MINOR,
		.name  = "liveupdate",
		.fops  = &luo_fops,
	},
};

static int __init liveupdate_ioctl_init(void)
{
	if (!liveupdate_enabled())
		return 0;

	return misc_register(&luo_dev.miscdev);
}
late_initcall(liveupdate_ioctl_init);
