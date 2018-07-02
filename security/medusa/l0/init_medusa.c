#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/cred.h>
#include <linux/flex_array.h>
#include <linux/fs.h>
#include <linux/spinlock_types.h>
#include <linux/lsm_hooks.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/rculist.h>
#include <linux/lsm_hooks.h>
#include "init_medusa.h"


extern int medusa_l1_cred_alloc_blank(struct cred *cred, gfp_t gfp);
extern int medusa_l1_inode_alloc_security(struct inode *inode);
extern int medusa_l1_msg_msg_alloc_security(struct msg_msg *msg);
extern int medusa_l1_msg_queue_alloc_security(struct kern_ipc_perm *ipcp);
extern int medusa_l1_shm_alloc_security(struct kern_ipc_perm *ipcp);
extern int medusa_l1_sem_alloc_security(struct kern_ipc_perm *ipcp);
extern void medusa_l1_inode_free_security(struct inode *inode);
extern void medusa_l1_cred_free(struct cred *cred);
extern void medusa_l1_msg_msg_free_security(struct msg_msg *msg);
extern void medusa_l1_msg_queue_free_security(struct kern_ipc_perm *ipcp);
extern void medusa_l1_shm_free_security(struct kern_ipc_perm *ipcp);
extern void medusa_l1_sem_free_security(struct kern_ipc_perm *ipcp);

bool l1_initialized;
struct cred_list l0_cred_list;
struct inode_list l0_inode_list;
struct kern_ipc_perm_list l0_kern_ipc_perm_list;
struct msg_msg_list l0_msg_msg_list;
struct mutex l0_mutex;

int medusa_l0_msg_msg_alloc_security(struct msg_msg *msg)
{
	struct msg_msg_list *tmp;

	mutex_lock(&l0_mutex);
	if (l1_initialized) {
		mutex_unlock(&l0_mutex);
		return medusa_l1_msg_msg_alloc_security(msg);
	}

	tmp = (struct msg_msg_list *) kmalloc(sizeof(struct msg_msg_list), GFP_KERNEL);
	tmp->msg = msg;
	list_add(&(tmp->list), &(l0_msg_msg_list.list));

	mutex_unlock(&l0_mutex);
	return 0;
}

void medusa_l0_msg_msg_free_security(struct msg_msg *msg)
{
	struct list_head *pos, *q;
	struct msg_msg_list *tmp;

	mutex_lock(&l0_mutex);
	if (l1_initialized) {
		medusa_l1_msg_msg_free_security(msg);
		mutex_unlock(&l0_mutex);
		return;
	}

	list_for_each_safe(pos, q, &l0_msg_msg_list.list) {
		tmp = list_entry(pos, struct msg_msg_list, list);
		if(tmp->msg == msg) {
			list_del(pos);
			kfree(tmp);
		}
	}

	mutex_unlock(&l0_mutex);
}

static int medusa_l0_ipc_alloc_security(struct kern_ipc_perm *ipcp, int (*medusa_l1_ipc_alloc_security)(struct kern_ipc_perm *))
{
	struct kern_ipc_perm_list *tmp;

	mutex_lock(&l0_mutex);
	if (l1_initialized) {
		mutex_unlock(&l0_mutex);
		return medusa_l1_ipc_alloc_security(ipcp);
	}

	tmp = (struct kern_ipc_perm_list *) kmalloc(sizeof(struct kern_ipc_perm_list), GFP_KERNEL);
	tmp->ipcp = ipcp;
	tmp->medusa_l1_ipc_alloc_security = medusa_l1_ipc_alloc_security;
	list_add(&(tmp->list), &(l0_kern_ipc_perm_list.list));

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
		if(tmp->ipcp == ipcp) {
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
    mutex_lock(&l0_mutex);
    if(l1_initialized) { 
        printk("medusa: special case when l1 has been initialized (l0_inode_alloc_security)\n");
        mutex_unlock(&l0_mutex);
        //list_del_rcu(&medusa_l0_hooks[0].list);
        return medusa_l1_inode_alloc_security(inode);
    }
    else {
        struct inode_list *tmp = (struct inode_list *) kmalloc(sizeof(struct inode_list), GFP_KERNEL);
        tmp->inode = inode; 
        list_add(&(tmp->list), &(l0_inode_list.list));
        // printk("medusa: inode has been added to the list (l0_inode_alloc_security)\n");
    }
    mutex_unlock(&l0_mutex);
    return 0;
}

static void medusa_l0_inode_free_security(struct inode *inode)
{
    mutex_lock(&l0_mutex);
    if(l1_initialized) {
        printk("medusa: special case when l1 has been initialized (l0_inode_free_security)\n");
        // list_del_rcu(&medusa_l0_hooks[1].list);
        medusa_l1_inode_free_security(inode);
        mutex_unlock(&l0_mutex);
    }
    else {
        struct list_head *pos, *q;
        struct inode_list *tmp;
        list_for_each_safe(pos, q, &l0_inode_list.list) {
            tmp = list_entry(pos, struct inode_list, list);
            if(tmp->inode == inode) {
                list_del(pos);
                kfree(tmp);
            }
        }
    }
    mutex_unlock(&l0_mutex);
}

static int medusa_l0_cred_alloc_blank(struct cred *cred, gfp_t gfp)
{
    mutex_lock(&l0_mutex);
    if(l1_initialized) {
        printk("medusa: special case when l1 has been initialized (l0_cred_alloc_blank)\n");
        // list_del_rcu(&medusa_l0_hooks[2].list);
        mutex_unlock(&l0_mutex);
        return medusa_l1_cred_alloc_blank(cred, gfp);
    }
    else {
        struct cred_list *tmp = (struct cred_list*) kmalloc(sizeof(struct cred_list), GFP_KERNEL);
        tmp->cred = cred;
        tmp->gfp = gfp;
        list_add(&(tmp->list), &(l0_cred_list.list));
        // printk("medusa: cred has been added to the list (l0_cred_alloc_blank)\n");
    }
    mutex_unlock(&l0_mutex);
    return 0;
}

static void medusa_l0_cred_free(struct cred *cred)
{
    mutex_lock(&l0_mutex);
    if(l1_initialized) {
        // list_del_rcu(&medusa_l0_hooks[3].list);
        medusa_l1_cred_free(cred);
        mutex_unlock(&l0_mutex);
    }
    else {
        struct list_head *pos, *q;
        struct cred_list *tmp;
        list_for_each_safe(pos, q, &l0_cred_list.list){
            tmp = list_entry(pos, struct cred_list, list);
            if(tmp->cred == cred){
                list_del(pos);
                kfree(tmp);
            }
        }
    }
    mutex_unlock(&l0_mutex);
}

struct security_hook_list medusa_l0_hooks[] = {
	LSM_HOOK_INIT(inode_alloc_security, medusa_l0_inode_alloc_security),
	LSM_HOOK_INIT(inode_free_security, medusa_l0_inode_free_security),
	LSM_HOOK_INIT(cred_alloc_blank, medusa_l0_cred_alloc_blank),
	LSM_HOOK_INIT(cred_free, medusa_l0_cred_free),
	LSM_HOOK_INIT(msg_msg_alloc_security, medusa_l0_msg_msg_alloc_security),
	LSM_HOOK_INIT(msg_queue_alloc_security, medusa_l0_msg_queue_alloc_security),
	LSM_HOOK_INIT(shm_alloc_security, medusa_l0_shm_alloc_security),
	LSM_HOOK_INIT(sem_alloc_security, medusa_l0_sem_alloc_security),
	LSM_HOOK_INIT(msg_msg_free_security, medusa_l0_msg_msg_free_security),
	LSM_HOOK_INIT(msg_queue_free_security, medusa_l0_msg_queue_free_security),
	LSM_HOOK_INIT(shm_free_security, medusa_l0_shm_free_security),
	LSM_HOOK_INIT(sem_free_security, medusa_l0_sem_free_security),
};

static int __init medusa_l0_init(void)
{
	mutex_init(&l0_mutex);

	l1_initialized = false;

	if (!security_module_enable("medusa"))
		return 0;

	//init lists
	INIT_LIST_HEAD(&l0_inode_list.list);
	INIT_LIST_HEAD(&l0_cred_list.list);
	INIT_LIST_HEAD(&l0_msg_msg_list.list);
	INIT_LIST_HEAD(&l0_kern_ipc_perm_list.list);

	printk("medusa: l0 registered with the kernel");
	security_add_hooks(medusa_l0_hooks, ARRAY_SIZE(medusa_l0_hooks), "medusa");
	return 0;
}

security_initcall(medusa_l0_init);
