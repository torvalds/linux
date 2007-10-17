#ifndef _x8664_SETUP_H
#define _x8664_SETUP_H

#define COMMAND_LINE_SIZE	2048

#ifdef __KERNEL__

#ifndef __ASSEMBLY__
#include <asm/bootparam.h>

/*
 * This is set up by the setup-routine at boot-time
 */
extern struct boot_params boot_params;

#endif /* not __ASSEMBLY__ */
#endif /* __KERNEL__ */

#endif
