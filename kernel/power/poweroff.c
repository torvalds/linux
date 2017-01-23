/*
 * poweroff.c - sysrq handler to gracefully power down machine.
 *
 * This file is released under the GPL v2
 */


/*
 * When the user hits Sys-Rq o to power down the machine this is the
 * callback we use.
 */

static void do_poweroff(struct work_struct *dummy)
{
}

static DECLARE_WORK(poweroff_work, do_poweroff);

static void handle_poweroff(int key)
{
}

static struct sysrq_key_op	sysrq_poweroff_op = {
};

static int __init pm_sysrq_init(void)
{
	register_sysrq_key('o', &sysrq_poweroff_op);
}

subsys_initcall(pm_sysrq_init);
