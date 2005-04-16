#ifndef _ASM_NUMNODES_H_
#define _ASM_NUMNODES_H_

#include <linux/config.h>

#ifdef CONFIG_DISCONTIGMEM

#if defined(CONFIG_CHIP_M32700)
#define	NODES_SHIFT	1	/* Max 2 Nodes */
#endif	/* CONFIG_CHIP_M32700 */

#endif	/* CONFIG_DISCONTIGMEM */

#endif	/* _ASM_NUMNODES_H_ */

