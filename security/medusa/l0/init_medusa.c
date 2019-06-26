#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/spinlock_types.h>
#include <linux/lsm_hooks.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/rculist.h>
#include <linux/lsm_hooks.h>
#include <linux/sched/signal.h>
#include "init_medusa.h"
#include "../l2/kobject_process.h"
#include "../l2/kobject_file.h"
#include "../l2/kobject_ipc.h"

extern int medusa_l1_task_alloc(struct task_struct *task, unsigned long clone_flags);
extern void medusa_l1_task_free(struct task_struct *task);
extern int medusa_l1_inode_alloc_security(struct inode *inode);
extern void medusa_l1_inode_free_security(struct inode *inode);
extern int medusa_l1_msg_queue_alloc_security(struct kern_ipc_perm *ipcp);
extern void medusa_l1_msg_queue_free_security(struct kern_ipc_perm *ipcp);
extern int medusa_l1_shm_alloc_security(struct kern_ipc_perm *ipcp);
extern void medusa_l1_shm_free_security(struct kern_ipc_perm *ipcp);
extern int medusa_l1_sem_alloc_security(struct kern_ipc_perm *ipcp);
extern void medusa_l1_sem_free_security(struct kern_ipc_perm *ipcp);

bool l1_initialized;
struct task_list l0_task_list;
struct inode_list l0_inode_list;
struct kern_ipc_perm_list l0_kern_ipc_perm_list;
struct mutex l0_mutex;

static int medusa_l0_ipc_alloc_security(struct kern_ipc_perm *ipcp, int (*medusa_l1_ipc_alloc_security)(struct kern_ipc_perm *))
{
	struct kern_ipc_perm_list *list;
	int ret = 0;

	mutex_lock(&l0_mutex);
	if (l1_initialized) {
		ret = medusa_l1_ipc_alloc_security(ipcp);
		goto out_unlock;
	}

	list = (struct kern_ipc_perm_list *) kzalloc(sizeof(struct kern_ipc_perm_list), GFP_KERNEL);
	if (!list) {
		printk("medusa: ERROR: no memory ipc_alloc_security");
		ret = -ENOMEM;
		goto out_unlock;
	}
	list->ipcp = ipcp;
	list->medusa_l1_ipc_alloc_security = medusa_l1_ipc_alloc_security;
	list_add(&(list->list), &(l0_kern_ipc_perm_list.list));

out_unlock:
	mutex_unlock(&l0_mutex);
	return 0;
}

static void medusa_l0_ipc_free_security(struct kern_ipc_perm *ipcp, void (*medusa_l1_ipc_free_security)(struct kern_ipc_perm *))
{
	struct list_head *pos, *q;
	struct kern_ipc_perm_list *tmp;

	mutex_lock(&l0_mutex);
	if (l1_initialized) {
		medusa_l1_ipc_free_security(ipcp);
		mutex_unlock(&l0_mutex);
		return;
	}

	list_for_each_safe(pos, q, &l0_kern_ipc_perm_list.list) {
		tmp = list_entry(pos, struct kern_ipc_perm_list, list);
		if (tmp->ipcp == ipcp) {
			list_del(pos);
			kfree(tmp);
		}
	}
	mutex_unlock(&l0_mutex);
}

int medusa_l0_msg_queue_alloc_security(struct kern_ipc_perm *ipcp)
{
	return medusa_l0_ipc_alloc_security(ipcp, medusa_l1_msg_queue_alloc_security);
}

void medusa_l0_msg_queue_free_security(struct kern_ipc_perm *ipcp)
{
	medusa_l0_ipc_free_security(ipcp, medusa_l1_msg_queue_free_security);
}

int medusa_l0_shm_alloc_security(struct kern_ipc_perm *ipcp)
{
	return medusa_l0_ipc_alloc_security(ipcp, medusa_l1_shm_alloc_security);
}

void medusa_l0_shm_free_security(struct kern_ipc_perm *ipcp)
{
	medusa_l0_ipc_free_security(ipcp, medusa_l1_shm_free_security);
}

int medusa_l0_sem_alloc_security(struct kern_ipc_perm *ipcp)
{
	return medusa_l0_ipc_alloc_security(ipcp, medusa_l1_sem_alloc_security);
}

void medusa_l0_sem_free_security(struct kern_ipc_perm *ipcp)
{
	medusa_l0_ipc_free_security(ipcp, medusa_l1_sem_free_security);
}

int medusa_l0_inode_alloc_security(struct inode *inode)
{
	struct inode_list *list;
	int ret = 0;

	mutex_lock(&l0_mutex);
	if (l1_initialized) {
		ret = medusa_l1_inode_alloc_security(inode);
		goto out_unlock;
	}

	list = (struct inode_list *) kzalloc(sizeof(struct inode_list), GFP_KERNEL);
	if (!list) {
		printk("medusa: ERROR: no memory inode_alloc_security");
		ret = -ENOMEM;
		goto out_unlock;
	}
	list->inode = inode;
	list_add(&(list->list), &(l0_inode_list.list));

out_unlock:
	mutex_unlock(&l0_mutex);
	return ret;
}

static void medusa_l0_inode_free_security(struct inode *inode)
{
	mutex_lock(&l0_mutex);
	if (l1_initialized) {
		medusa_l1_inode_free_security(inode);
		mutex_unlock(&l0_mutex);
	}
	else {
		struct list_head *pos, *q;
		struct inode_list *tmp;
		list_for_each_safe(pos, q, &l0_inode_list.list) {
			tmp = list_entry(pos, struct inode_list, list);
			if (tmp->inode == inode) {
				list_del(pos);
				kfree(tmp);
			}
		}
	}
	mutex_unlock(&l0_mutex);
}

static int medusa_l0_task_alloc(struct task_struct *task, unsigned long clone_flags)
{
	struct task_list *tmp;
	int ret = 0;

	mutex_lock(&l0_mutex);
	if (l1_initialized) {
		ret = medusa_l1_task_alloc(task, clone_flags);
		goto out_unlock;
	}

	tmp = (struct task_list*) kzalloc(sizeof(struct task_list), GFP_KERNEL);
	if (!tmp) {
		printk("medusa: ERROR: no memory task_alloc_security");
		ret = -ENOMEM;
		goto out_unlock;
	}
	tmp->task = task;
	tmp->clone_flags = clone_flags;
	list_add(&(tmp->list), &(l0_task_list.list));

out_unlock:
	mutex_unlock(&l0_mutex);
	return 0;
}

static void medusa_l0_task_free(struct task_struct *task)
{
	mutex_lock(&l0_mutex);
	if (l1_initialized) {
		medusa_l1_task_free(task);
		mutex_unlock(&l0_mutex);
	}
	else {
		struct list_head *pos, *q;
		struct task_list *tmp;
		list_for_each_safe(pos, q, &l0_task_list.list) {
			tmp = list_entry(pos, struct task_list, list);
			if (tmp->task == task) {
				list_del(pos);
				kfree(tmp);
			}
		}
	}
	mutex_unlock(&l0_mutex);
}

struct security_hook_list medusa_l0_hooks[] = {
	LSM_HOOK_INIT(task_alloc, medusa_l0_task_alloc),
	LSM_HOOK_INIT(task_free, medusa_l0_task_free),

	LSM_HOOK_INIT(inode_alloc_security, medusa_l0_inode_alloc_security),
	LSM_HOOK_INIT(inode_free_security, medusa_l0_inode_free_security),

	LSM_HOOK_INIT(msg_queue_alloc_security, medusa_l0_msg_queue_alloc_security),
	LSM_HOOK_INIT(msg_queue_free_security, medusa_l0_msg_queue_free_security),

	LSM_HOOK_INIT(shm_alloc_security, medusa_l0_shm_alloc_security),
	LSM_HOOK_INIT(shm_free_security, medusa_l0_shm_free_security),

	LSM_HOOK_INIT(sem_alloc_security, medusa_l0_sem_alloc_security),
	LSM_HOOK_INIT(sem_free_security, medusa_l0_sem_free_security),
};

static int __init medusa_l0_init(void)
{
	mutex_init(&l0_mutex);
	l1_initialized = false;

	//init lists
	INIT_LIST_HEAD(&l0_inode_list.list);
	INIT_LIST_HEAD(&l0_task_list.list);
	INIT_LIST_HEAD(&l0_kern_ipc_perm_list.list);

	printk("medusa: l0 registered with the kernel");
	security_add_hooks(medusa_l0_hooks, ARRAY_SIZE(medusa_l0_hooks), "medusa");
	return 0;
}

/*
TODO:
Refactor L0/L1 using this variable (initialization).
Consider L0/L1 merge.
*/
int medusa_enabled __lsm_ro_after_init = 1;

struct lsm_blob_sizes medusa_blob_sizes __lsm_ro_after_init = {
	.lbs_cred = 0,
	.lbs_file = 0,
	.lbs_inode = sizeof(struct medusa_l1_inode_s),
	.lbs_ipc = sizeof(struct medusa_l1_ipc_s),
	.lbs_msg_msg = 0,
	.lbs_task = sizeof(struct medusa_l1_task_s),
};

DEFINE_LSM(medusa) = {
	.name = "medusa",
	.order = LSM_ORDER_MUTABLE,
	.flags = LSM_FLAG_LEGACY_MAJOR | LSM_FLAG_EXCLUSIVE,
	.enabled = &medusa_enabled,
	.init = medusa_l0_init,
	.blobs = &medusa_blob_sizes,
};
