/*
 * Common SMP CPU bringup/teardown functions
 */
#include <linux/init.h>

#include "smpboot.h"

/**
 * smpboot_prepare - generic smpboot preparation
 */
int __cpuinit smpboot_prepare(unsigned int cpu)
{
	return 0;
}
