// SPDX-License-Identifier: GPL-2.0
#include <linux/unistd.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/minix_fs.h>
#include <linux/romfs_fs.h>
#include <linux/initrd.h>
#include <linux/sched.h>
#include <linux/freezer.h>
#include <linux/kmod.h>
#include <uapi/linux/mount.h>

#include "do_mounts.h"

unsigned long initrd_start, initrd_end;
int initrd_below_start_ok;
unsigned int real_root_dev;	/* do_proc_dointvec cannot handle kdev_t */
static int __initdata mount_initrd = 1;

phys_addr_t phys_initrd_start __initdata;
unsigned long phys_initrd_size __initdata;

static int __init no_initrd(char *str)
{
	mount_initrd = 0;
	return 1;
}

__setup("noinitrd", no_initrd);

static int __init early_initrdmem(char *p)
{
	phys_addr_t start;
	unsigned long size;
	char *endp;

	start = memparse(p, &endp);
	if (*endp == ',') {
		size = memparse(endp + 1, NULL);

		phys_initrd_start = start;
		phys_initrd_size = size;
	}
	return 0;
}
early_param("initrdmem", early_initrdmem);

static int __init early_initrd(char *p)
{
	return early_initrdmem(p);
}
early_param("initrd", early_initrd);

static int __init init_linuxrc(struct subprocess_info *info, struct cred *new)
{
	ksys_unshare(CLONE_FS | CLONE_FILES);
	console_on_rootfs();
	/* move initrd over / and chdir/chroot in initrd root */
	init_chdir("/root");
	init_mount(".", "/", NULL, MS_MOVE, NULL);
	init_chroot(".");
	ksys_setsid();
	return 0;
}

static void __init handle_initrd(void)
{
	struct subprocess_info *info;
	static char *argv[] = { "linuxrc", NULL, };
	extern char *envp_init[];
	int error;

	pr_warn("using deprecated initrd support, will be removed in 2021.\n");

	real_root_dev = new_encode_dev(ROOT_DEV);
	create_dev("/dev/root.old", Root_RAM0);
	/* mount initrd on rootfs' /root */
	mount_block_root("/dev/root.old", root_mountflags & ~MS_RDONLY);
	init_mkdir("/old", 0700);
	init_chdir("/old");

	/*
	 * In case that a resume from disk is carried out by linuxrc or one of
	 * its children, we need to tell the freezer not to wait for us.
	 */
	current->flags |= PF_FREEZER_SKIP;

	info = call_usermodehelper_setup("/linuxrc", argv, envp_init,
					 GFP_KERNEL, init_linuxrc, NULL, NULL);
	if (!info)
		return;
	call_usermodehelper_exec(info, UMH_WAIT_PROC);

	current->flags &= ~PF_FREEZER_SKIP;

	/* move initrd to rootfs' /old */
	init_mount("..", ".", NULL, MS_MOVE, NULL);
	/* switch root and cwd back to / of rootfs */
	init_chroot("..");

	if (new_decode_dev(real_root_dev) == Root_RAM0) {
		init_chdir("/old");
		return;
	}

	init_chdir("/");
	ROOT_DEV = new_decode_dev(real_root_dev);
	mount_root();

	printk(KERN_NOTICE "Trying to move old root to /initrd ... ");
	error = init_mount("/old", "/root/initrd", NULL, MS_MOVE, NULL);
	if (!error)
		printk("okay\n");
	else {
		if (error == -ENOENT)
			printk("/initrd does not exist. Ignored.\n");
		else
			printk("failed\n");
		printk(KERN_NOTICE "Unmounting old root\n");
		init_umount("/old", MNT_DETACH);
	}
}

bool __init initrd_load(void)
{
	if (mount_initrd) {
		create_dev("/dev/ram", Root_RAM0);
		/*
		 * Load the initrd data into /dev/ram0. Execute it as initrd
		 * unless /dev/ram0 is supposed to be our actual root device,
		 * in that case the ram disk is just set up here, and gets
		 * mounted in the normal path.
		 */
		if (rd_load_image("/initrd.image") && ROOT_DEV != Root_RAM0) {
			init_unlink("/initrd.image");
			handle_initrd();
			return true;
		}
	}
	init_unlink("/initrd.image");
	return false;
}
