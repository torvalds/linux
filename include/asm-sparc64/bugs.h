/* bugs.h: Sparc64 probes for various bugs.
 *
 * Copyright (C) 1996, 2007 David S. Miller (davem@davemloft.net)
 */
#include <asm/sstate.h>

extern unsigned long loops_per_jiffy;

static void __init check_bugs(void)
{
#ifndef CONFIG_SMP
	cpu_data(0).udelay_val = loops_per_jiffy;
#endif
	sstate_running();
}
