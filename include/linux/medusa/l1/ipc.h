/* medusa/l1/ipc.h, (C) 2002 Milan Pikula
 *
 * IPC struct extension: this structure is appended to in-kernel data,
 * and we define it separately just to make l1 code shorter.
 *
 * for another data structure - kobject, describing ipc for upper layers - 
 * see l2/ipc_kobject.[ch].
 */

#ifndef _MEDUSA_L1_IPC_H
#define _MEDUSA_L1_IPC_H

#include <linux/medusa/l3/model.h>

struct medusa_l1_ipc_s {
	MEDUSA_OBJECT_VARS;
};

#endif

