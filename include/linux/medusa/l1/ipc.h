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

#include <linux/mutex.h>
#include <linux/msg.h>
#include <linux/medusa/l3/model.h>
#include <linux/medusa/l3/constants.h>

#pragma GCC optimize ("Og")

extern medusa_answer_t medusa_ipc_validate(struct kern_ipc_perm *ipcp);
extern medusa_answer_t medusa_ipc_permission(struct kern_ipc_perm *ipcp, u32 perms);
extern medusa_answer_t medusa_ipc_ctl(struct kern_ipc_perm *ipcp, int cmd);
extern medusa_answer_t medusa_ipc_associate(struct kern_ipc_perm *ipcp, int flag);
extern medusa_answer_t medusa_ipc_semop(struct kern_ipc_perm *ipcp, struct sembuf *sops, unsigned nsops, int alter);
extern medusa_answer_t medusa_ipc_shmat(struct kern_ipc_perm *ipcp, char __user *shmaddr, int shmflg);
extern medusa_answer_t medusa_ipc_msgsnd(struct kern_ipc_perm *ipcp, struct msg_msg *msg, int msgflg);
extern medusa_answer_t medusa_ipc_msgrcv(struct kern_ipc_perm *ipcp, struct msg_msg *msg, struct task_struct *target, long type, int mode);

#define MED_IPC_SEM 0
#define MED_IPC_MSG 1
#define MED_IPC_SHM 2

struct medusa_l1_ipc_s {
	unsigned int ipc_class;
	struct mutex rwmutex;
	MEDUSA_SUBJECT_VARS;
	MEDUSA_OBJECT_VARS;
};

#define ipc_security(ipcp) ((struct medusa_l1_ipc_s*)ipcp->security)

#endif

