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
 * the next kernel. For more details see Documentation/core-api/kho/index.rst.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
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

	err = luo_flb_setup_incoming(luo_global.fdt_in);

	return err;
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
	err |= luo_flb_setup_outgoing(fdt_out);
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

	luo_flb_serialize();

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

/**
 * DOC: LUO ioctl Interface
 *
 * The IOCTL user-space control interface for the LUO subsystem.
 * It registers a character device, typically found at ``/dev/liveupdate``,
 * which allows a userspace agent to manage the LUO state machine and its
 * associated resources, such as preservable file descriptors.
 *
 * To ensure that the state machine is controlled by a single entity, access
 * to this device is exclusive: only one process is permitted to have
 * ``/dev/liveupdate`` open at any given time. Subsequent open attempts will
 * fail with -EBUSY until the first process closes its file descriptor.
 * This singleton model simplifies state management by preventing conflicting
 * commands from multiple userspace agents.
 */

struct luo_device_state {
	struct miscdevice miscdev;
	atomic_t in_use;
};

static int luo_ioctl_create_session(struct luo_ucmd *ucmd)
{
	struct liveupdate_ioctl_create_session *argp = ucmd->cmd;
	struct file *file;
	int err;

	argp->fd = get_unused_fd_flags(O_CLOEXEC);
	if (argp->fd < 0)
		return argp->fd;

	err = luo_session_create(argp->name, &file);
	if (err)
		goto err_put_fd;

	err = luo_ucmd_respond(ucmd, sizeof(*argp));
	if (err)
		goto err_put_file;

	fd_install(argp->fd, file);

	return 0;

err_put_file:
	fput(file);
err_put_fd:
	put_unused_fd(argp->fd);

	return err;
}

static int luo_ioctl_retrieve_session(struct luo_ucmd *ucmd)
{
	struct liveupdate_ioctl_retrieve_session *argp = ucmd->cmd;
	struct file *file;
	int err;

	argp->fd = get_unused_fd_flags(O_CLOEXEC);
	if (argp->fd < 0)
		return argp->fd;

	err = luo_session_retrieve(argp->name, &file);
	if (err < 0)
		goto err_put_fd;

	err = luo_ucmd_respond(ucmd, sizeof(*argp));
	if (err)
		goto err_put_file;

	fd_install(argp->fd, file);

	return 0;

err_put_file:
	fput(file);
err_put_fd:
	put_unused_fd(argp->fd);

	return err;
}

static int luo_open(struct inode *inodep, struct file *filep)
{
	struct luo_device_state *ldev = container_of(filep->private_data,
						     struct luo_device_state,
						     miscdev);

	if (atomic_cmpxchg(&ldev->in_use, 0, 1))
		return -EBUSY;

	/* Always return -EIO to user if deserialization fail */
	if (luo_session_deserialize()) {
		atomic_set(&ldev->in_use, 0);
		return -EIO;
	}

	return 0;
}

static int luo_release(struct inode *inodep, struct file *filep)
{
	struct luo_device_state *ldev = container_of(filep->private_data,
						     struct luo_device_state,
						     miscdev);
	atomic_set(&ldev->in_use, 0);

	return 0;
}

union ucmd_buffer {
	struct liveupdate_ioctl_create_session create;
	struct liveupdate_ioctl_retrieve_session retrieve;
};

struct luo_ioctl_op {
	unsigned int size;
	unsigned int min_size;
	unsigned int ioctl_num;
	int (*execute)(struct luo_ucmd *ucmd);
};

#define IOCTL_OP(_ioctl, _fn, _struct, _last)                                  \
	[_IOC_NR(_ioctl) - LIVEUPDATE_CMD_BASE] = {                            \
		.size = sizeof(_struct) +                                      \
			BUILD_BUG_ON_ZERO(sizeof(union ucmd_buffer) <          \
					  sizeof(_struct)),                    \
		.min_size = offsetofend(_struct, _last),                       \
		.ioctl_num = _ioctl,                                           \
		.execute = _fn,                                                \
	}

static const struct luo_ioctl_op luo_ioctl_ops[] = {
	IOCTL_OP(LIVEUPDATE_IOCTL_CREATE_SESSION, luo_ioctl_create_session,
		 struct liveupdate_ioctl_create_session, name),
	IOCTL_OP(LIVEUPDATE_IOCTL_RETRIEVE_SESSION, luo_ioctl_retrieve_session,
		 struct liveupdate_ioctl_retrieve_session, name),
};

static long luo_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
	const struct luo_ioctl_op *op;
	struct luo_ucmd ucmd = {};
	union ucmd_buffer buf;
	unsigned int nr;
	int err;

	nr = _IOC_NR(cmd);
	if (nr - LIVEUPDATE_CMD_BASE >= ARRAY_SIZE(luo_ioctl_ops))
		return -EINVAL;

	ucmd.ubuffer = (void __user *)arg;
	err = get_user(ucmd.user_size, (u32 __user *)ucmd.ubuffer);
	if (err)
		return err;

	op = &luo_ioctl_ops[nr - LIVEUPDATE_CMD_BASE];
	if (op->ioctl_num != cmd)
		return -ENOIOCTLCMD;
	if (ucmd.user_size < op->min_size)
		return -EINVAL;

	ucmd.cmd = &buf;
	err = copy_struct_from_user(ucmd.cmd, op->size, ucmd.ubuffer,
				    ucmd.user_size);
	if (err)
		return err;

	return op->execute(&ucmd);
}

static const struct file_operations luo_fops = {
	.owner		= THIS_MODULE,
	.open		= luo_open,
	.release	= luo_release,
	.unlocked_ioctl	= luo_ioctl,
};

static struct luo_device_state luo_dev = {
	.miscdev = {
		.minor = MISC_DYNAMIC_MINOR,
		.name  = "liveupdate",
		.fops  = &luo_fops,
	},
	.in_use = ATOMIC_INIT(0),
};

static int __init liveupdate_ioctl_init(void)
{
	if (!liveupdate_enabled())
		return 0;

	return misc_register(&luo_dev.miscdev);
}
late_initcall(liveupdate_ioctl_init);
