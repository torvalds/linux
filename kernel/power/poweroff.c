/*
 * poweroff.c - sysrq handler to gracefully power down machine.
 *
 * This file is released under the GPL v2
 */

#include <linux/kernel.h>
#include <linux/sysrq.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/workqueue.h>
#include <linux/reboot.h>
#include <linux/cpumask.h>

/*
 * When the user hits Sys-Rq o to power down the machine this is the
 * callback we use.
 */

static void do_poweroff(struct work_struct *dummy)
{
	kernel_power_off();
}

static DECLARE_WORK(poweroff_work, do_poweroff);

static void handle_poweroff(int key)
{
	/* run sysrq poweroff on boot cpu */
	schedule_work_on(cpumask_first(cpu_online_mask), &poweroff_work);
}

static struct sysrq_key_op	sysrq_poweroff_op = {
	.handler        = handle_poweroff,
	.help_msg       = "poweroff(o)",
	.action_msg     = "Power Off",
	.enable_mask	= SYSRQ_ENABLE_BOOT,
};

static int __init pm_sysrq_init(void)
{
	register_sysrq_key('o', &sysrq_poweroff_op);
	return 0;
}

subsys_initcall(pm_sysrq_init);
