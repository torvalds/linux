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
#include <linux/medusa/l3/constants.h>

extern medusa_answer_t medusa_ipc_perm(struct kern_ipc_perm *ipcp, u32 perms);

#define MED_IPC_SEM 0
#define MED_IPC_MSG 1
#define MED_IPC_SHM 2


struct medusa_l1_ipc_s {
	unsigned int ipc_class;
	MEDUSA_SUBJECT_VARS;
	MEDUSA_OBJECT_VARS;
};

#endif

