#ifndef _ASMi386_TIME_H
#define _ASMi386_TIME_H

#include <linux/efi.h>
#include "mach_time.h"

static inline unsigned long native_get_wallclock(void)
{
	unsigned long retval;

	if (efi_enabled)
		retval = efi_get_time();
	else
		retval = mach_get_cmos_time();

	return retval;
}

static inline int native_set_wallclock(unsigned long nowtime)
{
	int retval;

	if (efi_enabled)
		retval = efi_set_rtc_mmss(nowtime);
	else
		retval = mach_set_rtc_mmss(nowtime);

	return retval;
}

#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#else /* !CONFIG_PARAVIRT */

#define get_wallclock() native_get_wallclock()
#define set_wallclock(x) native_set_wallclock(x)
#define do_time_init() time_init_hook()

#endif /* CONFIG_PARAVIRT */

#endif
