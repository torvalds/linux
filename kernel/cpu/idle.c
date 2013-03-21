/*
 * Generic entry point for the idle threads
 */
#include <linux/sched.h>
#include <linux/cpu.h>

void cpu_startup_entry(enum cpuhp_state state)
{
	cpu_idle();
}
