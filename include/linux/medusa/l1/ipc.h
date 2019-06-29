/* medusa/l1/ipc.h, (C) 2018 Viliam Mihalik
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

/**
 * types of System V IPC objects
 */
#define MED_IPC_SEM 0	/* Semaphore */
#define MED_IPC_MSG 1	/* Message */
#define MED_IPC_SHM 2	/* Shared memory */
#define MED_IPC_UNDEFINED 3

/**
 * struct medusa_l1_ipc_s - security struct for System V IPC objects (sem, msg, shm)
 *
 * @ipc_class - medusa_l1_ipc_s is stored in 'struct kern_ipc_perm' of each System V object,
 *	so we need store extra information about IPC type in 'ipc_class' struct member
 * @MEDUSA_OBJECT_VARS - members used in Medusa VS access evaluation process
 */
struct medusa_l1_ipc_s {
	unsigned int ipc_class;	/* type of a System V IPC object */
	MEDUSA_OBJECT_VARS;
};

extern medusa_answer_t medusa_ipc_permission(struct kern_ipc_perm *ipcp, u32 perms);
extern medusa_answer_t medusa_ipc_ctl(struct kern_ipc_perm *ipcp, int cmd);
extern medusa_answer_t medusa_ipc_associate(struct kern_ipc_perm *ipcp, int flag);
extern medusa_answer_t medusa_ipc_semop(struct kern_ipc_perm *ipcp, struct sembuf *sops, unsigned nsops, int alter);
extern medusa_answer_t medusa_ipc_shmat(struct kern_ipc_perm *ipcp, char __user *shmaddr, int shmflg);
extern medusa_answer_t medusa_ipc_msgsnd(struct kern_ipc_perm *ipcp, struct msg_msg *msg, int msgflg);
extern medusa_answer_t medusa_ipc_msgrcv(struct kern_ipc_perm *ipcp, struct msg_msg *msg, struct task_struct *target, long type, int mode);

/*
 * The following routine makes a support for many of access types,
 * and it is used both in L1 and L2 code. It is defined in
 * l2/evtype_getipc.c.
 */
extern medusa_answer_t ipc_kobj_validate_ipcp(struct kern_ipc_perm *ipcp);

#endif // _MEDUSA_L1_IPC_H
