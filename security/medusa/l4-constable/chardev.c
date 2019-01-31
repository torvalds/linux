/*
 * L4 authorization server for Medusa DS9
 * Copyright (C) 2002 Milan Pikula <www@terminus.sk>, all rights reserved.
 *
 * This program comes with both BSD and GNU GPL v2 licenses. Check the
 * documentation for more information.
 * 
 *
 * This server communicates with an user-space
 * authorization daemon, using a character device
 *
 *	  /dev/medusa c 111 0		on Linux
 *	  /dev/medusa c 90 0		on NetBSD
 */

/* define this if you want fatal protocol errors to cause segfault of
 * auth. daemon. Note that issuing strange read(), write(), or trying
 * to access the character device multiple times at once is not considered
 * a protocol error. This triggers only if we REALLY get some junk from the
 * user-space.
 */
#define ERRORS_CAUSE_SEGFAULT

/* define this to support workaround of decisions for named process. This
 * is especially usefull when using GDB on constable.
 */
#define GDB_HACK

/* TODO: Check the calls to l3; they can't be called from a lock. */


#include <linux/medusa/l3/arch.h>

#include <linux/module.h>
#include <linux/types.h>
#include <linux/reboot.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/poll.h>
//#include <linux/devfs_fs_kernel.h>
#include <linux/semaphore.h>
#include <asm/atomic.h>
#include <asm/uaccess.h>
#include <linux/jiffies.h>

#define MEDUSA_MAJOR 111
#define MODULENAME "chardev/linux"
//#define wakeup(p) wake_up(p)
#define CURRENTPTR current
#define FAT_PTR_OFFSET_TYPE uint32_t
#define FAT_PTR_OFFSET sizeof(FAT_PTR_OFFSET_TYPE)

#include <linux/medusa/l3/registry.h>
#include <linux/medusa/l3/server.h>
#include <linux/medusa/l4/comm.h>

#include "teleport.h"

static int user_release(struct inode *inode, struct file *file);

static teleport_t teleport = {
	cycle: tpc_HALT,
};

/* constable, our brave userspace daemon */
static atomic_t constable_present = ATOMIC_INIT(0);
static struct task_struct * constable = NULL;
static struct task_struct * gdb = NULL;
static DEFINE_SEMAPHORE(constable_openclose);


/* fetch or update answer */
static atomic_t fetch_requests = ATOMIC_INIT(0);
static atomic_t update_requests = ATOMIC_INIT(0);

/* to-register queue for constable */
static MED_LOCK_DATA(registration_lock);
/* the following two are circular lists, they have to be global
 * because of put operations in user_close() */
static struct medusa_kclass_s * kclasses_registered = NULL;
static struct medusa_evtype_s * evtypes_registered = NULL;
static atomic_t announce_ready = ATOMIC_INIT(0);

/* a question from kernel to constable */
static atomic_t questions = ATOMIC_INIT(0);
static atomic_t questions_waiting = ATOMIC_INIT(0);
static atomic_t decision_request_id = ATOMIC_INIT(-1);
/* and the answer */
static medusa_answer_t user_answer = MED_ERR;

/* is the user-space currently sending us something? */
static atomic_t currently_receiving = ATOMIC_INIT(0);

static DECLARE_WAIT_QUEUE_HEAD(close_wait);

static DECLARE_WAIT_QUEUE_HEAD(userspace_answer);
static DECLARE_WAIT_QUEUE_HEAD(userspace_chardev);
static int recv_req_id = -1;
static struct semaphore take_answer;
static struct semaphore user_read_lock;
static struct semaphore queue_items;
static struct semaphore queue_lock;
static LIST_HEAD(tele_queue);
struct tele_item {
	teleport_insn_t *tele;
	struct list_head list;
	size_t size;
	void (*post)(void*);
};

// Array for memory caches
int cache_array_size = 0;
static struct kmem_cache **med_cache_array;

// Next three variables are used by user_open. They are here because we have to
// free the underlying data structures and clear them in user_close.
static size_t left_in_teleport = 0;
static struct tele_item *local_list_item;
static teleport_insn_t *processed_teleport;

static DEFINE_SEMAPHORE(ls_switch);
static DEFINE_SEMAPHORE(lock_sem);
static DEFINE_SEMAPHORE(prior_sem);
typedef struct {
	struct semaphore* lock_sem;
	struct semaphore* prior_sem;
	int counter;
} lightswitch_t;
static lightswitch_t lightswitch = {
	.lock_sem = &lock_sem,
	.counter = 0,
	.prior_sem = &prior_sem,
};

#ifdef GDB_HACK
static pid_t gdb_pid = -1;
//MODULE_PARM(gdb_pid, "i");
//MODULE_PARM_DESC(gdb_pid, "PID to exclude from monitoring");
#endif

/*******************************************************************************
 * kernel-space interface
 */

static medusa_answer_t l4_decide( struct medusa_event_s * event,
		struct medusa_kobject_s * o1,
		struct medusa_kobject_s * o2);
static int l4_add_kclass(struct medusa_kclass_s * cl);
static int l4_add_evtype(struct medusa_evtype_s * at);
static void l4_close_wake(void);

static struct medusa_authserver_s chardev_medusa = {
	MODULENAME,
	0,	/* use-count */
	l4_close_wake,		/* close */
	l4_add_kclass,		/* add_kclass */
	NULL,			/* del_kclass */
	l4_add_evtype,		/* add_evtype */
	NULL,			/* del_evtype */
	l4_decide		/* decide */
};

/*******************************************************************************
 * memory cache interface
 */ 

/**
 * Computes binary logarithm rounded up
 */
static inline int get_log_index(int in) {
	int ret = 1;
	in--;
	while (in >>= 1)
		ret++;
	return ret;
}

/**
 * Creates a memory cache on a given index.
 * If there is not enough space in the array, it will reallocate it.
 * If a cache already exists, it does nothing.
 */
static struct kmem_cache* med_cache_create(size_t index) {
	if (index >= cache_array_size)
		return NULL;
	if (med_cache_array[index])
		return med_cache_array[index];
	med_cache_array[index] = kmem_cache_create("med_cache",
			1 << index,
			0,
			SLAB_HWCACHE_ALIGN,
			NULL);
	return med_cache_array[index];
}

/**
 * Allocates an array for memory caches and initializes it with nulls.
 */
static struct kmem_cache** alloc_med_cache_array(size_t size) {
	int i;
	if (med_cache_array)
		return med_cache_array;
	med_cache_array = (struct kmem_cache**)
		kmalloc(sizeof(struct kmem_cache*) * size, GFP_KERNEL);
	if (med_cache_array) {
		for (i = 0; i < size; i++)
			med_cache_array[i] = NULL;
		cache_array_size = size;
	}
	return med_cache_array;
}

/**
 * Reallocates the memory cache array for a given size.
 * If the size is smaller or the same as the existing array, it does nothing.
 */
static struct kmem_cache** realloc_med_cache_array(size_t size) {
	int i;
	struct kmem_cache** new_cache_array;
	if (size <= cache_array_size)
		return med_cache_array;
	new_cache_array = (struct kmem_cache**)
		krealloc(med_cache_array, sizeof(struct kmem_cache*) * size, GFP_KERNEL);
	if (new_cache_array) {
		for (i = cache_array_size; i < size; i++)
			med_cache_array[i] = NULL;
		cache_array_size = size;
	}
	return new_cache_array;
}

/**
 * Creates a memory cache for a given size if it doesn't exist
 */
static int med_cache_register(size_t size) {
	int log;
	size += FAT_PTR_OFFSET;
	log = get_log_index(size);
	if (log >= cache_array_size)
		if (!realloc_med_cache_array(log))
			return -ENOMEM;
	if (!med_cache_array[log])
		if (!med_cache_create(log))
			return -ENOMEM;
	return 0;
}

/**
 * Allocates memory from a memory pool chosen by the index argument
 * Warning - selected index has to take fat pointer offset into account
 */
static void* med_cache_alloc_index(size_t index) {
	void* ret;
	ret = kmem_cache_alloc(med_cache_array[index], GFP_KERNEL);
	*((FAT_PTR_OFFSET_TYPE*)ret) = index;
	ret = ((FAT_PTR_OFFSET_TYPE*)ret) + 1;
	return ret;
}

/**
 * Allocates memory from a memory pool chosen by the size argument
 */
static void* med_cache_alloc_size(size_t size) {
	int log;
	size += FAT_PTR_OFFSET;
	log = get_log_index(size);
	return med_cache_alloc_index(log);
}

/**
 * Frees previously allocated memory
 */
static void med_cache_free(void* mem) {
	int log;
	mem = ((FAT_PTR_OFFSET_TYPE*)mem) - 1;
	log = *((FAT_PTR_OFFSET_TYPE*)mem);
	kmem_cache_free(med_cache_array[log], mem);
}

/**
 * Destroys all memory caches in the array
 */
static void med_cache_destroy(void) {
	int i;
	for(i = 0; i < cache_array_size; i++) {
		if (med_cache_array[i])
			kmem_cache_destroy(med_cache_array[i]);
	}
}

/**
 * Frees the array of memory caches.
 */
static void free_med_cache_array(void) {
	med_cache_destroy();
	kfree(med_cache_array);
	med_cache_array = NULL;
}

/*
 * Used to clean up data structures after fetch or update.
 */
static void post_write(void* mem) {
	if (((teleport_insn_t*)mem)[1].args.put32.what == MEDUSA_COMM_FETCH_ANSWER)
		med_cache_free(((teleport_insn_t*)mem)[4].args.cutnpaste.from);
	med_cache_free(mem);
}

static int am_i_constable(void) {

	if (!constable)
		return 0;

	rcu_read_lock();
	if (task_tgid(current) == task_tgid(constable)) {
		rcu_read_unlock();
		return 1;
	}
	rcu_read_unlock();

	return 0;
}

static void l4_close_wake(void)
{
	wake_up(&close_wait);
}

static int l4_add_kclass(struct medusa_kclass_s * cl)
{
	teleport_insn_t *tele_mem_kclass;
	struct tele_item *local_tele_item;
	int attr_num = 1;
	struct medusa_attribute_s * attr_ptr;

	tele_mem_kclass = (teleport_insn_t*)
		med_cache_alloc_size(sizeof(teleport_insn_t) * 5);
	if (!tele_mem_kclass)
		return -ENOMEM;
	local_tele_item = (struct tele_item*)
		med_cache_alloc_size(sizeof(struct tele_item));
	if (!local_tele_item) {
		med_cache_free(tele_mem_kclass);
		return -ENOMEM;
	}

	med_get_kclass(cl); // put is in user_release

	med_cache_register(cl->kobject_size);

	MED_LOCK_W(registration_lock);
	barrier();
	atomic_inc(&announce_ready);
	barrier();

	cl->cinfo = (cinfo_t)kclasses_registered;
	kclasses_registered = cl;
	local_tele_item->size = 0;
	tele_mem_kclass[0].opcode = tp_PUTPtr;
	tele_mem_kclass[0].args.putPtr.what = 0;
	local_tele_item->size += sizeof(MCPptr_t);
	tele_mem_kclass[1].opcode = tp_PUT32;
	tele_mem_kclass[1].args.put32.what =
		MEDUSA_COMM_KCLASSDEF;
	local_tele_item->size += sizeof(uint32_t);
	tele_mem_kclass[2].opcode = tp_PUTKCLASS;
	tele_mem_kclass[2].args.putkclass.kclassdef = cl;
	local_tele_item->size += sizeof(struct medusa_comm_kclass_s);
	tele_mem_kclass[3].opcode = tp_PUTATTRS;
	tele_mem_kclass[3].args.putattrs.attrlist = cl->attr;
	attr_ptr = cl->attr;
	while (attr_ptr->type != MED_END) {
		attr_num++;
		attr_ptr++;
	}
	local_tele_item->size += attr_num * sizeof(struct medusa_comm_attribute_s);
	tele_mem_kclass[4].opcode = tp_HALT;
	local_tele_item->tele = tele_mem_kclass;
	local_tele_item->post = med_cache_free;
	down(&queue_lock);
	list_add_tail(&(local_tele_item->list), &tele_queue);
	up(&queue_lock);
	up(&queue_items);
	wake_up(&userspace_chardev);
	MED_UNLOCK_W(registration_lock);
	return MED_YES;
}

static int l4_add_evtype(struct medusa_evtype_s * at)
{
	teleport_insn_t *tele_mem_evtype;
	struct tele_item *local_tele_item;
	int attr_num = 1;
	struct medusa_attribute_s * attr_ptr;

	tele_mem_evtype = (teleport_insn_t*)
		med_cache_alloc_size(sizeof(teleport_insn_t)*5);
	if (!tele_mem_evtype)
		return -ENOMEM;
	local_tele_item = (struct tele_item*)
		med_cache_alloc_size(sizeof(struct tele_item));
	if (!local_tele_item) {
		med_cache_free(tele_mem_evtype);
		return -ENOMEM;
	}

	MED_LOCK_W(registration_lock);
	barrier();
	atomic_inc(&announce_ready);
	barrier();

	at->cinfo = (cinfo_t)evtypes_registered;
	evtypes_registered = at;
	local_tele_item->size = 0;
	tele_mem_evtype[0].opcode = tp_PUTPtr;
	tele_mem_evtype[0].args.putPtr.what = 0;
	local_tele_item->size += sizeof(MCPptr_t);
	tele_mem_evtype[1].opcode = tp_PUT32;
	tele_mem_evtype[1].args.put32.what =
		MEDUSA_COMM_EVTYPEDEF;
	local_tele_item->size += sizeof(uint32_t);
	tele_mem_evtype[2].opcode = tp_PUTEVTYPE;
	tele_mem_evtype[2].args.putevtype.evtypedef = at;
	local_tele_item->size += sizeof(struct medusa_comm_evtype_s);
	tele_mem_evtype[3].opcode = tp_PUTATTRS;
	tele_mem_evtype[3].args.putattrs.attrlist = at->attr;
	attr_ptr = at->attr;
	while (attr_ptr->type != MED_END) {
		attr_num++;
		attr_ptr++;
	}
	local_tele_item->size += attr_num * sizeof(struct medusa_comm_attribute_s);
	tele_mem_evtype[4].opcode = tp_HALT;
	local_tele_item->tele = tele_mem_evtype;
	local_tele_item->post = med_cache_free;
	down(&queue_lock);
	list_add_tail(&(local_tele_item->list), &tele_queue);
	up(&queue_lock);
	up(&queue_items);
	wake_up(&userspace_chardev);
	MED_UNLOCK_W(registration_lock);
	return MED_YES;
}

inline static void ls_lock(lightswitch_t* ls, struct semaphore* sem) {
	down(ls->prior_sem);
	up(ls->prior_sem);
	down(ls->lock_sem);
	ls->counter++;
	if (ls->counter == 1)
		down(sem);
	up(ls->lock_sem);
}

inline static void ls_unlock(lightswitch_t* ls, struct semaphore* sem) {
	down(ls->lock_sem);
	ls->counter--;
	if (!ls->counter)
		up(sem);
	up(ls->lock_sem);
}

/* the sad fact about this routine is that it sleeps...
 *
 * guess what? we can FULLY solve that silly problem on SMP,
 * eating one processor by a constable... ;) One can imagine
 * the performance improvement, and buy one more CPU in advance :)
 */
static medusa_answer_t l4_decide(struct medusa_event_s * event,
		struct medusa_kobject_s * o1, struct medusa_kobject_s * o2)
{
	int retval;
	int local_req_id;
	teleport_insn_t tele_mem_decide[6];
	struct tele_item *local_tele_item;

	if (in_interrupt()) {
		/* houston, we have a problem! */
		MED_PRINTF("decide called from interrupt context :(\n");
		return MED_ERR;
	}
	if (am_i_constable() || current == gdb) {
		return MED_OK;
	}

	if (current->pid < 1) {
		return MED_ERR;
	}
#ifdef GDB_HACK
	if (gdb_pid == current->pid) {
		return MED_OK;
	}
#endif

	local_tele_item = (struct tele_item*)
		med_cache_alloc_size(sizeof(struct tele_item));
	if (!local_tele_item)
		return MED_ERR;
	local_tele_item->tele = tele_mem_decide;
	local_tele_item->size = 0;
	local_tele_item->post = NULL;

#define decision_evtype (event->evtype_id)
	tele_mem_decide[0].opcode = tp_PUTPtr;
	tele_mem_decide[0].args.putPtr.what = (MCPptr_t)decision_evtype; // possibility to encryption JK march 2015
	local_tele_item->size += sizeof(MCPptr_t);
	tele_mem_decide[1].opcode = tp_PUT32;
	tele_mem_decide[1].args.put32.what = atomic_inc_return(&decision_request_id);
	local_req_id = tele_mem_decide[1].args.put32.what;
	local_tele_item->size += sizeof(uint32_t);
	tele_mem_decide[2].opcode = tp_CUTNPASTE;
	tele_mem_decide[2].args.cutnpaste.from = (unsigned char *)event;
	tele_mem_decide[2].args.cutnpaste.count = decision_evtype->event_size;
	local_tele_item->size += decision_evtype->event_size;
	tele_mem_decide[3].opcode = tp_CUTNPASTE;
	tele_mem_decide[3].args.cutnpaste.from = (unsigned char *)o1;
	tele_mem_decide[3].args.cutnpaste.count =
		decision_evtype->arg_kclass[0]->kobject_size;
	local_tele_item->size += decision_evtype->arg_kclass[0]->kobject_size;
	if (o1 == o2) {
		tele_mem_decide[4].opcode = tp_HALT;
	} else {
		tele_mem_decide[4].opcode = tp_CUTNPASTE;
		tele_mem_decide[4].args.cutnpaste.from =
			(unsigned char *)o2;
		tele_mem_decide[4].args.cutnpaste.count =
			decision_evtype->arg_kclass[1]->kobject_size;
		local_tele_item->size += decision_evtype->arg_kclass[1]->kobject_size;
		tele_mem_decide[5].opcode = tp_HALT;
	}

	ls_lock(&lightswitch, &ls_switch);

	if (!atomic_read(&constable_present)) {
		med_cache_free(local_tele_item);
		ls_unlock(&lightswitch, &ls_switch);
		return MED_ERR;
	}
	pr_info("medusa: new question %i\n", local_req_id);
	// prepare for next decision
#undef decision_evtype
	// insert teleport structure to the queue
	down(&queue_lock);
	list_add_tail(&(local_tele_item->list), &tele_queue);
	up(&queue_lock);
	up(&queue_items);
	atomic_inc(&questions);
	wake_up(&userspace_chardev);
	// wait until answer is ready
	ls_unlock(&lightswitch, &ls_switch);
	if (wait_event_timeout(userspace_answer,
				local_req_id == recv_req_id || \
				!atomic_read(&constable_present), 5*HZ) == 0){
		pr_err("medusa: answer not received for %i\n", local_req_id);
		user_release(NULL, NULL);
	}

	if (atomic_read(&constable_present)) {
		atomic_dec(&questions_waiting);
		retval = user_answer;
		pr_info("medusa: question %i answered %i\n", local_req_id, retval);
	}
	else {
		retval = MED_ERR;
	}
	up(&take_answer);
	barrier();
	return retval;
}

/***********************************************************************
 * user-space interface
 */

static ssize_t user_read(struct file *filp, char __user *buf, size_t count, loff_t * ppos);
static ssize_t user_write(struct file *filp, const char __user *buf, size_t count, loff_t * ppos);
static unsigned int user_poll(struct file *filp, poll_table * wait);
static int user_open(struct inode *inode, struct file *file);
static int user_release(struct inode *inode, struct file *file);

static struct file_operations fops = {
read:		user_read,
		 write:		user_write,
		 llseek:		no_llseek, /* -ESPIPE */
		 poll:		user_poll,
		 open:		user_open,
		 release:	user_release
			 /* we don't support async IO. I have no idea, when to call kill_fasync
			  * to be correct. Only on decisions? Or also on answers to user-space
			  * questions? Not a big problem, though... noone seems to be supporting
			  * it anyway :). If you need it, let me know. <www@terminus.sk>
			  */
			 /* also, we don't like the ioctl() - we hope the character device can
			  * be used over the network.
			  */
};
static char __user * userspace_buf;

static ssize_t to_user(void * from, size_t len)
{ /* we verify the access rights elsewhere */
	if (__copy_to_user(userspace_buf, from, len));
	userspace_buf += len;
	return len;
}

static void decrement_counters(teleport_insn_t* tele) {
	if (tele[1].opcode == tp_HALT)
		return;
	switch (tele[2].opcode) {
		case tp_CUTNPASTE: // Authorization server answer
			atomic_inc(&questions_waiting);
			atomic_dec(&questions);
			break;
		case tp_PUTPtr: // Fetch or update
			switch (tele[1].args.put32.what) {
				case MEDUSA_COMM_FETCH_ANSWER:
					atomic_dec(&fetch_requests);
					break;
				case MEDUSA_COMM_UPDATE_ANSWER:
					atomic_dec(&update_requests);
					break;
			}
			break;
		case tp_PUTKCLASS:
		case tp_PUTEVTYPE:
			atomic_dec(&announce_ready);
			break;
	}
}

#define DOWN(m) do {  \
	if (down_trylock(m)) { \
		printk("Strasny vypis: %d\n", __LINE__); \
	} \
} while(0)

/*
 * trylock - if true, don't block
 * returns 1 if queue is empty, otherwise 0
 * returns -EPIPE if Constable was disconnected
 * while waiting for new event
 */
static inline int teleport_pop(int trylock) {
	if (trylock) {
		if (down_trylock(&queue_items))
			return 1;
	} else {
		ls_unlock(&lightswitch, &ls_switch);
		while (down_timeout(&queue_items, 5*HZ) == -ETIME) {
			ls_lock(&lightswitch, &ls_switch);
			if (!atomic_read(&constable_present))
				return -EPIPE;
			ls_unlock(&lightswitch, &ls_switch);
		}
		ls_lock(&lightswitch, &ls_switch);
	}
	DOWN(&queue_lock);
	local_list_item = list_first_entry(&tele_queue, struct tele_item, list);
	processed_teleport = local_list_item->tele;
	left_in_teleport = local_list_item->size;
	list_del(&(local_list_item->list));
	up(&queue_lock);
	teleport_reset(&teleport, &(processed_teleport[0]), to_user);
	decrement_counters(processed_teleport);
	return 0;
}

static inline void teleport_put(void) {
	if (local_list_item->post)
		local_list_item->post(processed_teleport);
	med_cache_free(local_list_item);
	processed_teleport = NULL;
	local_list_item = NULL;
}


/*
 * READ()
 */
static ssize_t user_read(struct file * filp, char __user * buf,
		size_t count, loff_t * ppos)
{
	ssize_t retval;
	size_t retval_sum = 0;

	ls_lock(&lightswitch, &ls_switch);

	if (!atomic_read(&constable_present)) {
		ls_unlock(&lightswitch, &ls_switch);
		return -EPIPE;
	}

	if (!am_i_constable()) {
		ls_unlock(&lightswitch, &ls_switch);
		return -EPERM;
	}
	if (*ppos != filp->f_pos) {
		ls_unlock(&lightswitch, &ls_switch);
		return -ESPIPE;
	}
	if (!access_ok(VERIFY_WRITE, buf, count)){
		ls_unlock(&lightswitch, &ls_switch);
		return -EFAULT;
	}

	// Lock it before someone can change the userspace_buf
	DOWN(&user_read_lock);
	userspace_buf = buf;
	// Get an item from the queue
	if (!left_in_teleport) {
		// Get a new item only if the previous teleport has been fully transported
		if (teleport_pop(0) == -EPIPE) {
			ls_unlock(&lightswitch, &ls_switch);
			return -EPIPE;
		}
	}
	while (1) {
		retval = teleport_cycle(&teleport, count);
		if (retval < 0) { /* unexpected error; data lost */
			// this teleport was broken, we get rid of it
			left_in_teleport = 0;
			teleport_put();
			up(&user_read_lock);
			ls_unlock(&lightswitch, &ls_switch);
			return retval;
		}
		left_in_teleport -= retval;
		count -= retval;
		retval_sum += retval;
		if (!left_in_teleport) {
			// We can get rid of current teleport
			teleport_put();
			if (!count)
				break;
			// Userspace wants more data
			if (teleport_pop(1))
				break;
			// left in teleport will be always zero, because while loop in
	  // teleport_reset loops while count is not zero until it encounters 
	// tpc_HALT
		} else {
			// Something was left in teleport
			if (retval == 0 && teleport.cycle == tpc_HALT) {
				// Discard current teleport
				left_in_teleport = 0;
				teleport_put();
				// Get new teleport
				if (teleport_pop(0) == -EPIPE) {
					ls_unlock(&lightswitch, &ls_switch);
					return -EPIPE;
				}
				continue;
			}
			break;
		}
	} // while
	if (retval_sum > 0 || teleport.cycle != tpc_HALT) {
		up(&user_read_lock);
		ls_unlock(&lightswitch, &ls_switch);
		return retval_sum;
	}

	// Something is still in teleport, but we didn't transport any data
	up(&user_read_lock);
	ls_unlock(&lightswitch, &ls_switch);
	return 0;
}

/*
 * WRITE()
 */
static ssize_t user_write(struct file *filp, const char __user *buf, size_t count, loff_t * ppos)
{
	size_t orig_count = count;
	struct medusa_kclass_s * cl;
	teleport_insn_t *tele_mem_write;
	struct tele_item *local_tele_item;
	medusa_answer_t answ_result;
	int recv_phase;
	MCPptr_t recv_type;
	MCPptr_t answ_kclassid = 0;
	struct medusa_kobject_s * answ_kobj = NULL;
	MCPptr_t answ_seq = 0;
	char recv_buf[sizeof(MCPptr_t)*2];
	char *kclass_buf;

	// Lightswitch
	// has to be there so close can't occur during write
	ls_lock(&lightswitch, &ls_switch);

	if (!atomic_read(&constable_present)) {
		ls_unlock(&lightswitch, &ls_switch);
		pr_err("write: constable not present\n");
		return -EPIPE;
	}

	if (!am_i_constable()) {
		ls_unlock(&lightswitch, &ls_switch);
		pr_err("write: not called by constable\n");
		return -EPERM;
	}
	if (*ppos != filp->f_pos) {
		ls_unlock(&lightswitch, &ls_switch);
		pr_err("write: uncorrect file position\n");
		return -ESPIPE;
	}
	if (!access_ok(VERIFY_READ, buf, count)) {
		ls_unlock(&lightswitch, &ls_switch);
		pr_err("write: cant read buffer\n");
		return -EFAULT;
	}

	if (!atomic_read(&currently_receiving)) {
		recv_phase = 0;
		atomic_set(&currently_receiving, 1);
	} /*else {
	  printk("user_read: unfinished write\n");
	  ls_unlock(&lightswitch, &ls_switch);
	  atomic_set(&read_in_progress, 0);
	  return -EAGAIN;
	  }*/

	printk("buf: ");
	int i;
	for (i = 0; i < count/8; i++) {
		printk("%.16llx\n", *(((uint64_t*)buf)+i));
	}

	if (__copy_from_user(((char *)&recv_type), buf,
				sizeof(MCPptr_t))) {
		ls_unlock(&lightswitch, &ls_switch);
		pr_err("write: cant copy buffer\n");
		return -EFAULT;
	}
	buf += sizeof(MCPptr_t);
	count -= sizeof(MCPptr_t);

	// Type of the message is received
	down(&take_answer);
	if (recv_type == MEDUSA_COMM_AUTHANSWER) {
		// TODO Process something
		if (__copy_from_user(recv_buf, buf, sizeof(int16_t) + sizeof(MCPptr_t))) {
			up(&take_answer);
			ls_unlock(&lightswitch, &ls_switch);
			atomic_set(&currently_receiving, 0);
			pr_err("write: cant copy buffer\n");
			return -EFAULT;
		}
		buf += sizeof(int16_t) + sizeof(MCPptr_t);
		count -= sizeof(int16_t) + sizeof(MCPptr_t);

		user_answer = *(int16_t *)(recv_buf+sizeof(MCPptr_t));
		recv_req_id = *(int *)(recv_buf);
		barrier();
		// wake up correct process
		wake_up_all(&userspace_answer);
		atomic_set(&currently_receiving, 0);
	} else if (recv_type == MEDUSA_COMM_FETCH_REQUEST ||
			recv_type == MEDUSA_COMM_UPDATE_REQUEST) {
		int index;
		switch(recv_type) {
			case MEDUSA_COMM_FETCH_REQUEST:
				index = 0;
				break;
			case MEDUSA_COMM_UPDATE_REQUEST:
				index = 1;
				break;
		}
		up(&take_answer);
		if (__copy_from_user(recv_buf, buf, sizeof(MCPptr_t)*2)) {
			ls_unlock(&lightswitch, &ls_switch);
			atomic_set(&currently_receiving, 0);
			pr_err("write: cant copy buffer\n");
			return -EFAULT;
		}
		buf += sizeof(MCPptr_t)*2;
		count -= sizeof(MCPptr_t)*2;

		cl = med_get_kclass_by_pointer(
				*(struct medusa_kclass_s **)(recv_buf) // posibility to decrypt JK march 2015
				);
		if (!cl) {
			MED_PRINTF(MODULENAME ": protocol error at write(): unknown kclass 0x%p!\n", (void*)(*(MCPptr_t*)(recv_buf)));
			atomic_set(&currently_receiving, 0);
#ifdef ERRORS_CAUSE_SEGFAULT
			ls_unlock(&lightswitch, &ls_switch);
			return -EFAULT;
#else
			break;
#endif
		}
		kclass_buf = (char*) med_cache_alloc_size(cl->kobject_size);
		if (__copy_from_user(kclass_buf, buf, cl->kobject_size)) {
			med_cache_free(kclass_buf);
			atomic_set(&currently_receiving, 0);
			ls_unlock(&lightswitch, &ls_switch);
			pr_err("write: cant copy buffer\n");
			return -EFAULT;
		}
		buf += cl->kobject_size;
		count -= cl->kobject_size;

		// if (atomic_read(&fetch_requests) || atomic_read(&update_requests)) {
	// 	/* not so much to do... */
  // 	med_put_kclass(answ_kclass);
  //     // ked si to uzivatel precita, tak urob put - tam, kde sa rusi objekt
  // }

		answ_kclassid = (*(MCPptr_t*)(recv_buf));
		answ_seq = *(((MCPptr_t*)(recv_buf))+1);


		if (recv_type == MEDUSA_COMM_FETCH_REQUEST) {
			if (cl->fetch)
				answ_kobj = cl->fetch((struct medusa_kobject_s *)
						kclass_buf);
			else {
				answ_kobj = NULL;
			}
		} else {
			if (cl->update)
				answ_result = cl->update(
						(struct medusa_kobject_s *)kclass_buf);
			else
				answ_result = MED_ERR;
			med_cache_free(kclass_buf);
		}
		// Dynamic telemem structure for fetch/update
		tele_mem_write = (teleport_insn_t*) med_cache_alloc_size(sizeof(teleport_insn_t)*6);
		if (!tele_mem_write)
			return -ENOMEM;
		local_tele_item = (struct tele_item*) med_cache_alloc_size(sizeof(struct tele_item));
		if (!local_tele_item) {
			med_cache_free(tele_mem_write);
			return -ENOMEM;
		}
		local_tele_item->size = 0;
		tele_mem_write[0].opcode = tp_PUTPtr;
		tele_mem_write[0].args.putPtr.what = 0;
		local_tele_item->size += sizeof(MCPptr_t);
		tele_mem_write[1].opcode = tp_PUT32;
		if (recv_type == MEDUSA_COMM_FETCH_REQUEST) { /* fetch */
			atomic_inc(&fetch_requests);
			tele_mem_write[1].args.put32.what = answ_kobj ?
				MEDUSA_COMM_FETCH_ANSWER : MEDUSA_COMM_FETCH_ERROR;
		} else { /* update */
			tele_mem_write[1].args.put32.what = MEDUSA_COMM_UPDATE_ANSWER;
		}
		local_tele_item->size += sizeof(uint32_t);
		tele_mem_write[2].opcode = tp_PUTPtr;
		tele_mem_write[2].args.putPtr.what = (MCPptr_t)answ_kclassid;
		local_tele_item->size += sizeof(MCPptr_t);
		tele_mem_write[3].opcode = tp_PUTPtr;
		tele_mem_write[3].args.putPtr.what = (MCPptr_t)answ_seq;
		local_tele_item->size += sizeof(MCPptr_t);
		if (recv_type == MEDUSA_COMM_UPDATE_REQUEST) {
			atomic_inc(&update_requests);
			tele_mem_write[4].opcode = tp_PUT32;
			tele_mem_write[4].args.put32.what = answ_result;
			local_tele_item->size += sizeof(uint32_t);
			tele_mem_write[5].opcode = tp_HALT;
		} else if (answ_kobj) {
			tele_mem_write[4].opcode = tp_CUTNPASTE;
			tele_mem_write[4].args.cutnpaste.from = (void *)answ_kobj;
			tele_mem_write[4].args.cutnpaste.count = cl->kobject_size;
			local_tele_item->size += cl->kobject_size;
			tele_mem_write[5].opcode = tp_HALT;
		} else
			tele_mem_write[4].opcode = tp_HALT;
		med_put_kclass(cl); /* slightly too soon */ // TODO Find out what is this
		local_tele_item->tele = tele_mem_write;
		local_tele_item->post = post_write;
		down(&queue_lock);
		list_add(&(local_tele_item->list), &tele_queue);
		up(&queue_lock);
		atomic_set(&currently_receiving, 0);
		up(&queue_items);
		wake_up(&userspace_chardev);
	} else {
		up(&take_answer);
		MED_PRINTF(MODULENAME ": protocol error at write(): unknown command %llx!\n", (MCPptr_t)recv_type);
		atomic_set(&currently_receiving, 0);
#ifdef ERRORS_CAUSE_SEGFAULT
		ls_unlock(&lightswitch, &ls_switch);
		return -EFAULT;
#endif
	}
	ls_unlock(&lightswitch, &ls_switch);
	return orig_count;
}

/*
 * POLL()
 */
static unsigned int user_poll(struct file *filp, poll_table * wait)
{
	if (!am_i_constable())
		return -EPERM;

	if (!atomic_read(&constable_present))
		return -EPIPE;
	poll_wait(filp, &userspace_chardev, wait);
	if (teleport.cycle != tpc_HALT) {
		return POLLIN | POLLRDNORM;
	}
	else if (atomic_read(&fetch_requests) || atomic_read(&update_requests) ||
			atomic_read(&announce_ready) || atomic_read(&questions)) {
		return POLLIN | POLLRDNORM;
	}
	else if (atomic_read(&questions_waiting)) {
		return POLLOUT | POLLWRNORM;
	}
	// userspace_chardev wakes up only when adding teleport to the queue
	// for user to read
	return POLLOUT | POLLWRNORM;
}

/*
 * OPEN()
 */
static int user_open(struct inode *inode, struct file *file)
{
	int retval = -EPERM;
	teleport_insn_t *tele_mem_open;
	struct tele_item *local_tele_item;

	//MOD_INC_USE_COUNT; Not needed anymore JK

	down(&constable_openclose);
	if (atomic_read(&constable_present))
		goto good_out;

	if (!alloc_med_cache_array(15)) {
		retval = -ENOMEM;
		goto out;
	}
	if (med_cache_register(sizeof(struct tele_item))) {
		retval = -ENOMEM;
		goto out;
	}
	if (med_cache_register(sizeof(teleport_insn_t)*2)) {
		retval = -ENOMEM;
		goto out;
	}
	if (med_cache_register(sizeof(teleport_insn_t)*5)) {
		retval = -ENOMEM;
		goto out;
	}
	if (med_cache_register(sizeof(teleport_insn_t)*6)) {
		retval = -ENOMEM;
		goto out;
	}
	tele_mem_open = (teleport_insn_t*) med_cache_alloc_size(sizeof(teleport_insn_t)*2);
	if (!tele_mem_open) {
		retval = -ENOMEM;
		goto out;
	}
	local_tele_item = (struct tele_item*) med_cache_alloc_size(sizeof(struct tele_item));
	if (!local_tele_item) {
		retval = -ENOMEM;
		goto out;
	}

	constable = CURRENTPTR;
	if (strstr(current->parent->comm, "gdb"))
		gdb = current->parent;

	teleport.cycle = tpc_HALT;
	// Reset semaphores
	sema_init(&take_answer, 1);
	sema_init(&user_read_lock, 1);
	sema_init(&queue_items, 0);
	sema_init(&queue_lock, 1);

	atomic_set(&currently_receiving, 0);
	tele_mem_open[0].opcode = tp_PUTPtr;
	tele_mem_open[0].args.putPtr.what = (MCPptr_t)MEDUSA_COMM_GREETING;
	local_tele_item->size = sizeof(MCPptr_t);
	tele_mem_open[1].opcode = tp_HALT;
	local_tele_item->tele = tele_mem_open;
	local_tele_item->post = med_cache_free;
	down(&queue_lock);
	list_add_tail(&(local_tele_item->list), &tele_queue);
	up(&queue_lock);
	up(&queue_items);
	wake_up(&userspace_chardev);

	/* this must be the last thing done */
	atomic_set(&constable_present, 1);
	init_waitqueue_head(&userspace_answer);
	up(&constable_openclose);

	MED_REGISTER_AUTHSERVER(chardev_medusa);
	return 0; /* success */
out:
	if (tele_mem_open)
		med_cache_free(tele_mem_open);
	med_cache_destroy();
good_out:
	up(&constable_openclose);
	return retval;
}

/*
 * CLOSE()
 */
static int user_release(struct inode *inode, struct file *file)
{
	struct list_head *pos, *next;
	DECLARE_WAITQUEUE(wait,current);

	// Operation close has to wait for read and write system calls to finish
	// Close has priority, so starvation can't occur
	down(&prior_sem);
	down(&ls_switch);
	up(&prior_sem);

	if (!atomic_read(&constable_present)) {
		up(&ls_switch);
		return 0;
	}

	/* this function is invoked also from context of process which requires decision
	 after 5s of inactivity of our brave user space authorization server constable;
	 so we comment next two lines ;) */
	/*
	 if (!am_i_constable())
	 return 0;
	 */
	MED_LOCK_W(registration_lock);
	if (evtypes_registered) {
		struct medusa_evtype_s * p1, * p2;
		p1 = evtypes_registered;
		do {
			p2 = p1;
			p1 = (struct medusa_evtype_s *)p1->cinfo;
			// med_put_evtype(p2);
		} while (p1);
	}
	evtypes_registered = NULL;
	if (kclasses_registered) {
		struct medusa_kclass_s * p1, * p2;
		p1 = kclasses_registered;
		do {
			p2 = p1;
			p1 = (struct medusa_kclass_s *)p1->cinfo;
			med_put_kclass(p2);
		} while (p1);
	}
	kclasses_registered = NULL;
	MED_UNLOCK_W(registration_lock);
	atomic_set(&fetch_requests, 0);
	atomic_set(&update_requests, 0);

	MED_PRINTF("Security daemon unregistered.\n");
#if defined(CONFIG_MEDUSA_HALT)
	MED_PRINTF("No security daemon, system halted.\n");
	notifier_call_chain(&reboot_notifier_list, SYS_HALT, NULL);
	machine_halt();
#elif defined(CONFIG_MEDUSA_REBOOT)
	MED_PRINTF("No security daemon, rebooting system.\n");
	ctrl_alt_del();
#endif
	add_wait_queue(&close_wait, &wait);
	set_current_state(TASK_UNINTERRUPTIBLE);
	MED_UNREGISTER_AUTHSERVER(chardev_medusa);
	down(&constable_openclose);

	// All threads waiting for an answer will get an error, order of these
	// functions is important!
	user_answer = MED_ERR;
	atomic_set(&constable_present, 0);
	constable = NULL;
	gdb = NULL;

	atomic_set(&questions, 0);
	atomic_set(&questions_waiting, 0);
	atomic_set(&announce_ready, 0);
	atomic_set(&decision_request_id, -1);
	atomic_set(&currently_receiving, 0);
	recv_req_id = -1;

	// Clear the teleport queue
	left_in_teleport = 0;
	if (local_list_item)
		teleport_put();
	down(&queue_lock);
	list_for_each_safe(pos, next, &tele_queue) {
		local_list_item = list_entry(pos, struct tele_item, list);
		processed_teleport = local_list_item->tele;
		list_del(&(local_list_item->list));
		teleport_put();
	}
	up(&queue_lock);

	free_med_cache_array();

	up(&constable_openclose);
	// wake up waiting processes, this has to be outside of constable_openclose
	// lock because wake_up_all causes context switch (locking and unlocking
	// cpu may not be the same)
	wake_up_all(&userspace_answer);
	if (am_i_constable())
		schedule();
	else
		MED_PRINTF("Authorization server is not responding.\n");
	remove_wait_queue(&close_wait, &wait);
	//MOD_DEC_USE_COUNT; Not needed anymore? JK


	teleport.cycle = tpc_HALT;
	up(&ls_switch);
	return 0;
}

static struct class* medusa_class;
static struct device* medusa_device;

static int chardev_constable_init(void)
{
	MED_PRINTF(MODULENAME ": registering L4 character device with major %d\n", MEDUSA_MAJOR);
	if (register_chrdev(MEDUSA_MAJOR, MODULENAME, &fops)) {
		MED_PRINTF(MODULENAME ": cannot register character device with major %d\n", MEDUSA_MAJOR);
		return -1;
	}

	medusa_class = class_create(THIS_MODULE, "medusa");
	if (IS_ERR(medusa_class)) {
		MED_PRINTF(MODULENAME ": failed to register device class '%s'\n", "medusa");
		return -1;
	}

	/* With a class, the easiest way to instantiate a device is to call device_create() */
	medusa_device = device_create(medusa_class, NULL, MKDEV(MEDUSA_MAJOR, 0), NULL, "medusa");
	if (IS_ERR(medusa_device)) {
		MED_PRINTF(MODULENAME ": failed to create device '%s'\n", "medusa");
		return -1;
	}
	return 0;
}

static void chardev_constable_exit(void)
{
	device_destroy(medusa_class, MKDEV(MEDUSA_MAJOR, 0));
	class_unregister(medusa_class);
	class_destroy(medusa_class);

	unregister_chrdev(MEDUSA_MAJOR, MODULENAME);
}

module_init(chardev_constable_init);
module_exit(chardev_constable_exit);
MODULE_LICENSE("GPL");

