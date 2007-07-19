/* bugs.h: Sparc64 probes for various bugs.
 *
 * Copyright (C) 1996, 2007 David S. Miller (davem@davemloft.net)
 */
#include <asm/sstate.h>

static void __init check_bugs(void)
{
	sstate_running();
}
