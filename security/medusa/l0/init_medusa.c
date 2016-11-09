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
extern void medusa_l1_inode_free_security(struct inode *inode);
extern void medusa_l1_cred_free(struct cred *cred);

bool l1_initialized = false;
struct cred_list tmp_cred_list;
struct inode_list tmp_inode_list;
DEFINE_SPINLOCK(medusa_init_lock);

int medusa_l0_inode_alloc_security(struct inode *inode){
    spin_lock(&medusa_init_lock);
    if(l1_initialized){
        printk("special case when l1 is initialiezd yet\n");
        spin_unlock(&medusa_init_lock);
        list_del_rcu(&medusa_tmp_hooks[0].list);
        return medusa_l1_inode_alloc_security(inode);
    }
    else{
        struct inode_list *tmp = (struct inode_list *) kmalloc(sizeof(struct inode_list), GFP_KERNEL);
        tmp->inode = inode; 
        list_add(&(tmp->list), &(tmp_inode_list.list));
        // printk("inode added to array\n");
    }
    spin_unlock(&medusa_init_lock);
    return 0;
}

static void medusa_l0_inode_free_security(struct inode *inode){
    spin_lock(&medusa_init_lock);
    if(l1_initialized){
        printk("special case when l1 is initialiezd yet\n");
        list_del_rcu(&medusa_tmp_hooks[1].list);
        medusa_l1_inode_free_security(inode);
        spin_unlock(&medusa_init_lock);
    }
    else{
        struct list_head *pos, *q;
        struct inode_list *tmp;
        list_for_each_safe(pos, q, &tmp_inode_list.list){
            tmp = list_entry(pos, struct inode_list, list);
            if(tmp->inode == inode){
                list_del(pos);
                kfree(tmp); 
            }
        }
 /*
       list_for_each_entry(tmp, &tmp_inode_list.list, list){
            if(tmp->inode == inode)
                list_del(&tmp->list); 
        }
*/
        //printk("inode deleted to array\n");
    }
    spin_unlock(&medusa_init_lock);
}

static int medusa_l0_cred_alloc_blank(struct cred *cred, gfp_t gfp){
    spin_lock(&medusa_init_lock);
    if(l1_initialized){
        printk("special case when l1 is initialiezd yet\n");
        list_del_rcu(&medusa_tmp_hooks[2].list);
        spin_unlock(&medusa_init_lock);
        return medusa_l1_cred_alloc_blank(cred, gfp);
    }
    else{
        struct cred_list *tmp = (struct cred_list *) kmalloc(sizeof(struct cred_list), GFP_KERNEL);
        tmp->task = cred;
        list_add(&(tmp->list), &(tmp_cred_list.list));
        //printk("task added to array\n");
    }
    spin_unlock(&medusa_init_lock);
    return 0;
}

static void medusa_l0_cred_free(struct cred *cred){
    spin_lock(&medusa_init_lock);
    if(l1_initialized){
        list_del_rcu(&medusa_tmp_hooks[3].list);
        medusa_l1_cred_free(cred);
        spin_unlock(&medusa_init_lock);
    }
    else{

        struct list_head *pos, *q;
        struct cred_list *tmp;
        list_for_each_safe(pos, q, &tmp_cred_list.list){
            tmp = list_entry(pos, struct cred_list, list);
            if(tmp->task == cred){
                list_del(pos);
                kfree(tmp); 
            }
        }
        /*struct cred_list *tmp = (struct cred_list *) kmalloc(sizeof(struct cred_list), GFP_KERNEL);
        list_for_each_entry(tmp, &tmp_cred_list.list, list){
            if(tmp->task == cred)
                list_del(&tmp->list); 
        }*/
       // printk("task deleted to array\n");
    }
    spin_unlock(&medusa_init_lock);
}

struct security_hook_list medusa_tmp_hooks[] = {
    LSM_HOOK_INIT(inode_alloc_security, medusa_l0_inode_alloc_security),
    LSM_HOOK_INIT(inode_free_security, medusa_l0_inode_free_security),
    LSM_HOOK_INIT(cred_alloc_blank, medusa_l0_cred_alloc_blank),
    LSM_HOOK_INIT(cred_free, medusa_l0_cred_free) 
};

static int __init medusa_l0_init(void){

    if (!security_module_enable("medusa")) {
        return 0;
    }

    //init lists
    INIT_LIST_HEAD(&tmp_inode_list.list);
    INIT_LIST_HEAD(&tmp_cred_list.list);
    
    printk("medusa l0 initialized");
    security_add_hooks(medusa_tmp_hooks, ARRAY_SIZE(medusa_tmp_hooks));
    return 0;
}


security_initcall(medusa_l0_init);
