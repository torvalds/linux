#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/syscalls.h>
#include <linux/medusa/l3/kobject.h>
#include <linux/medusa/l3/model.h>
#include <linux/medusa/l3/registry.h>
#include <linux/medusa/l3/constants.h>
#include <linux/medusa/l1/ipc.h>

#define ipc_security(ipc) ((struct medusa_l1_ipc_s*)(ipcp->security))

/*
 * medusa_ipc_perm - struct holding relevant entries from 'kern_ipc_perm' (see linux/ipc.h)
 */
struct medusa_ipc_perm {
	bool        deleted;
	int     id;
	key_t       key;
	kuid_t      uid;
 	kgid_t      gid;
	kuid_t      cuid;
	kgid_t      cgid;
	umode_t     mode;
	unsigned long   seq;
};

/**
 * ipc_kobject - kobject structure for System V IPC: sem, msg, shm
 *
 * @ipc_class - type of System V IPC (sem, or msg, or shm)
 * @
 */
struct ipc_kobject {	
	unsigned int ipc_class;
	struct medusa_ipc_perm ipc_perm;
	MEDUSA_OBJECT_VARS;
};
extern MED_DECLARE_KCLASSOF(ipc_kobject);

struct ipc_kobject * ipc_kern2kobj(struct ipc_kobject *, struct kern_ipc_perm *);
medusa_answer_t ipc_kobj2kern(struct ipc_kobject *, struct kern_ipc_perm *);

struct medusa_kobject_s * ipc_fetch(struct medusa_kobject_s *);
medusa_answer_t ipc_update(struct medusa_kobject_s * kobj);
