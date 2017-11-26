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
static MED_LOCK_DATA(constable_openclose);


/* fetch or update answer */
static atomic_t fetch_requests = ATOMIC_INIT(0);
static atomic_t update_requests = ATOMIC_INIT(0);
 static struct medusa_kclass_s * answ_kclass;
 static struct medusa_kobject_s * answ_kobj;
 static MCPptr_t answ_kclassid;
 static MCPptr_t answ_seq;
 static medusa_answer_t answ_result;

/* to-register queue for constable */
static MED_LOCK_DATA(registration_lock);
 static struct medusa_kclass_s * kclasses_to_register = NULL;
 static struct medusa_evtype_s * evtypes_to_register = NULL;
 /* the following two are circular lists */
 static struct medusa_kclass_s * kclasses_registered = NULL;
 static struct medusa_evtype_s * evtypes_registered = NULL;
 static atomic_t announce_ready = ATOMIC_INIT(0);

/* a question from kernel to constable */
static DEFINE_SEMAPHORE(constable_mutex);
 static atomic_t questions = ATOMIC_INIT(0);
 static atomic_t questions_waiting = ATOMIC_INIT(0);
 static atomic_t decision_request_id = ATOMIC_INIT(0);
 /* and the answer */
 static medusa_answer_t user_answer;
 static DECLARE_WAIT_QUEUE_HEAD(userspace);

/* is the user-space currently sending us something? */
static atomic_t currently_receiving = ATOMIC_INIT(0);
 static char recv_buf[32768]; /* hopefully enough */
 static MCPptr_t recv_type;
 static int recv_phase;

static DECLARE_WAIT_QUEUE_HEAD(close_wait);

static DECLARE_WAIT_QUEUE_HEAD(userspace_answer);
static DECLARE_WAIT_QUEUE_HEAD(userspace_chardev);
static int recv_req_id = -1;
static struct semaphore take_answer;
static struct semaphore fetch_update_lock;
static struct semaphore user_read_lock;
static struct semaphore queue_items;
static struct semaphore queue_lock;
static LIST_HEAD(tele_queue);
struct tele_item {
    teleport_insn_t *tele;
    struct list_head list;
    size_t size;
    void (*post)(const void*);
};
// Next three variables are used by user_open. They are here because we have to
// free the underlying data structures and clear them in user_close.
static size_t left_in_teleport = 0;
static struct tele_item *local_list_item;
static teleport_insn_t *processed_teleport;

static char* recv_type_name[] = {"FETCH_REQUEST", "UPDATE_REQUEST"};

#ifdef GDB_HACK
static pid_t gdb_pid = -1;
//MODULE_PARM(gdb_pid, "i");
//MODULE_PARM_DESC(gdb_pid, "PID to exclude from monitoring");
#endif

/***********************************************************************
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

int am_i_constable(void) {
	struct task_struct* task;
	struct list_head *i;
	int ret = 0;

	if (!constable)
		return 0;

	if (constable == current)
		return 1;

	rcu_read_lock();
	if (current->parent == constable || current->real_parent == constable) {
		ret = 1;
		goto out;
	}

	list_for_each(i, &current->real_parent->children) {
		task = list_entry(i, struct task_struct, sibling);
		if (task == constable) {
			ret = 1;
			goto out;
		}
	}

out:
	rcu_read_unlock();
	return ret;
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
    struct medusa_kclass_s * p;

	med_get_kclass(cl);
	MED_LOCK_W(registration_lock);
	// cl->cinfo = (cinfo_t)kclasses_to_register;
	// kclasses_to_register=cl;
    // while (kclasses_to_register) {
      barrier();
      atomic_inc(&announce_ready);
      barrier();
      tele_mem_kclass = (teleport_insn_t*) kmalloc(sizeof(teleport_insn_t) * 5, GFP_KERNEL);

      p = cl;
      // kclasses_to_register = (struct medusa_kclass_s *)p->cinfo;

      p->cinfo = (cinfo_t)kclasses_registered;
      kclasses_registered = p;
      local_tele_item = (struct tele_item*) kmalloc(sizeof(struct tele_item), GFP_KERNEL);
      local_tele_item->size = 0;
      tele_mem_kclass[0].opcode = tp_PUTPtr;
      tele_mem_kclass[0].args.putPtr.what = 0;
      local_tele_item->size += sizeof(MCPptr_t);
      tele_mem_kclass[1].opcode = tp_PUT32;
      tele_mem_kclass[1].args.put32.what =
          MEDUSA_COMM_KCLASSDEF;
      local_tele_item->size += sizeof(uint32_t);
      tele_mem_kclass[2].opcode = tp_PUTKCLASS;
      tele_mem_kclass[2].args.putkclass.kclassdef = p;
      local_tele_item->size += sizeof(struct medusa_comm_kclass_s);
      tele_mem_kclass[3].opcode = tp_PUTATTRS;
      tele_mem_kclass[3].args.putattrs.attrlist = p->attr;
      attr_ptr = p->attr;
      while (attr_ptr->type != MED_END) {
        attr_num++;
        attr_ptr++;
      }
      local_tele_item->size += attr_num * sizeof(struct medusa_comm_attribute_s);
      tele_mem_kclass[4].opcode = tp_HALT;
      local_tele_item->tele = tele_mem_kclass;
      local_tele_item->post = kfree;
      down(&queue_lock);
      printk("l4_add_kclass: adding %p\n", tele_mem_kclass);
      list_add_tail(&(local_tele_item->list), &tele_queue);
      up(&queue_lock);
      up(&queue_items);
	  wake_up(&userspace_chardev);
    // }
	MED_UNLOCK_W(registration_lock);
	return MED_YES;
}

static int l4_add_evtype(struct medusa_evtype_s * at)
{
    teleport_insn_t *tele_mem_evtype;
    struct tele_item *local_tele_item;
    int attr_num = 1;
	struct medusa_attribute_s * attr_ptr;
    struct medusa_evtype_s * p;
	MED_LOCK_W(registration_lock);
	// at->cinfo = (cinfo_t)evtypes_to_register;
	// evtypes_to_register=at;
  // while (evtypes_to_register) {
      barrier();
      atomic_inc(&announce_ready);
      barrier();
      tele_mem_evtype = (teleport_insn_t*) kmalloc(sizeof(teleport_insn_t) * 5, GFP_KERNEL);

      p = at;
      // evtypes_to_register = (struct medusa_evtype_s *)p->cinfo;

      p->cinfo = (cinfo_t)evtypes_registered;
      evtypes_registered = p;
      local_tele_item = (struct tele_item*) kmalloc(sizeof(struct tele_item), GFP_KERNEL);
      local_tele_item->size = 0;
      tele_mem_evtype[0].opcode = tp_PUTPtr;
      tele_mem_evtype[0].args.putPtr.what = 0;
      local_tele_item->size += sizeof(MCPptr_t);
      tele_mem_evtype[1].opcode = tp_PUT32;
      tele_mem_evtype[1].args.put32.what =
          MEDUSA_COMM_EVTYPEDEF;
      local_tele_item->size += sizeof(uint32_t);
      tele_mem_evtype[2].opcode = tp_PUTEVTYPE;
      tele_mem_evtype[2].args.putevtype.evtypedef = p;
      local_tele_item->size += sizeof(struct medusa_comm_evtype_s);
      tele_mem_evtype[3].opcode = tp_PUTATTRS;
      tele_mem_evtype[3].args.putattrs.attrlist = p->attr;
      attr_ptr = p->attr;
      while (attr_ptr->type != MED_END) {
        attr_num++;
        attr_ptr++;
      }
      local_tele_item->size += attr_num * sizeof(struct medusa_comm_attribute_s);
      tele_mem_evtype[4].opcode = tp_HALT;
      local_tele_item->tele = tele_mem_evtype;
      local_tele_item->post = kfree;
      down(&queue_lock);
      printk("l4_add_evtype: adding %p\n", tele_mem_evtype);
      list_add_tail(&(local_tele_item->list), &tele_queue);
      up(&queue_lock);
      up(&queue_items);
	  wake_up(&userspace_chardev);
  // }
	MED_UNLOCK_W(registration_lock);
	return MED_YES;
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
    local_tele_item = (struct tele_item*) kmalloc(sizeof(struct tele_item), GFP_KERNEL);
    local_tele_item->tele = tele_mem_decide;
    local_tele_item->size = 0;
    local_tele_item->post = NULL;

	if (in_interrupt()) {
		/* houston, we have a problem! */
		MED_PRINTF("decide called from interrupt context :(\n");
		return MED_ERR;
	}
	if (am_i_constable() || current == gdb)
		return MED_OK;

	if (current->pid < 1)
		return MED_ERR;
#ifdef GDB_HACK
	if (gdb_pid == current->pid)
		return MED_OK;
#endif

	/* end before sleeping, if possible */
	if (!atomic_read(&constable_present))
		return MED_ERR;

    // Local telemem structure (no need to kfree it)
#define decision_evtype (event->evtype_id)
	tele_mem_decide[0].opcode = tp_PUTPtr;
	tele_mem_decide[0].args.putPtr.what = (MCPptr_t)decision_evtype; // possibility to encryption JK march 2015
  local_tele_item->size += sizeof(MCPptr_t);
	tele_mem_decide[1].opcode = tp_PUT32;
	tele_mem_decide[1].args.put32.what = atomic_read(&decision_request_id);
    // GET REQ ID
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
    // PREPARE FOR NEXT DECISION
    atomic_inc(&decision_request_id);
#undef decision_evtype
    // INSERT TELEPORT STRUCTURE TO THE QUEUE
    down(&queue_lock);
    printk("l4_decide: adding %p, id: %i\n", tele_mem_decide, local_req_id);
    list_add_tail(&(local_tele_item->list), &tele_queue);
    up(&queue_lock);
    up(&queue_items);
	atomic_inc(&questions);
	wake_up(&userspace_chardev);
    // WAIT UNTIL ANSWER IS READY
    atomic_inc(&questions_waiting);
	if (wait_event_timeout(userspace_answer,
                           local_req_id == recv_req_id, 5*HZ) == 0){
        user_release(NULL, NULL);
    }

	if (atomic_read(&constable_present)) {
        atomic_dec(&questions_waiting);
		retval = user_answer;
    }
	else
		retval = MED_ERR;
  up(&take_answer);
	barrier();
    printk("l4_decide: retval is %i\n", retval);
	return retval;
}

/***********************************************************************
 * user-space interface
 */

static ssize_t user_read(struct file *filp, char *buf, size_t count, loff_t * ppos);
static ssize_t user_write(struct file *filp, const char *buf, size_t count, loff_t * ppos);
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
static char * userspace_buf;

static ssize_t to_user(void * from, size_t len)
{ /* we verify the access rights elsewhere */
	if (__copy_to_user(userspace_buf, from, len));
	userspace_buf += len;
	return len;
}

void decrement_counters(teleport_insn_t* tele) {
  if (tele[1].opcode == tp_HALT)
    return;
  switch (tele[2].opcode) {
    case tp_CUTNPASTE: // Authorization server answer
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
    case tp_PUTKCLASS :
    case tp_PUTEVTYPE :
      atomic_dec(&announce_ready);
      break;
  }
}

static inline void teleport_pop(void) {
  down(&queue_items);
  down(&queue_lock);
  local_list_item = list_first_entry(&tele_queue, struct tele_item, list);
  processed_teleport = local_list_item->tele;
  left_in_teleport = local_list_item->size;
  list_del(&(local_list_item->list));
  up(&queue_lock);
  teleport_reset(&teleport, &(processed_teleport[0]), to_user);
  decrement_counters(processed_teleport);
}

static inline void teleport_put(void) {
  if (local_list_item->post)
    local_list_item->post(processed_teleport);
  kfree(local_list_item);
  processed_teleport = NULL;
  local_list_item = NULL;
}

/*
 * READ()
 */
static ssize_t user_read(struct file * filp, char * buf,
		size_t count, loff_t * ppos)
{
    ssize_t retval;
    size_t retval_sum = 0;
    int empty;
    static int counter = 0;
    int local_counter = counter++;
    char comm[TASK_COMM_LEN];

    printk("user_read: starting %i with %i to read\n", local_counter, count);
	if (!am_i_constable()) {
        printk("user_read: EPERM error %i\n", local_counter);
        printk("user_read constable pointer %p\n", constable);
        printk("user_read was called by  %s\n", get_task_comm(comm, constable));

		return -EPERM;
    }
	if (*ppos != filp->f_pos) {
        printk("user_read: ppos error %i\n", local_counter);
		return -ESPIPE;
    }
	if (!access_ok(VERIFY_WRITE, buf, count)){
        printk("user_read: EFAULT error %i\n", local_counter);
		return -EFAULT;
    }

	/* do we have an unfinished write? (e.g. dumb user-space) */
	if (atomic_read(&currently_receiving)) {
        printk("user_read: unfininshed write\n");
		return -EIO;
    }
  // Lock it before someone can change the userspace_buf
  down(&user_read_lock);
	userspace_buf = buf;
      // Get an item from the queue
    if (!left_in_teleport) {
      // Get a new item only if the previous teleport has been fully transported
      printk("user_read: Nothing left in teleport, popping it %i\n", local_counter);
      teleport_pop();
    }
    while (1) {
      retval = teleport_cycle(&teleport, count);
      printk("user_read: after teleport_read %i\n", local_counter);
      if (retval < 0) { /* unexpected error; data lost */
        if (retval_sum > 0) {
            // TODO Chcek if this is worth it.
            // something was processed, so we leave current teleport loaded
            up(&user_read_lock);
            printk("user_read: return error eith sum %i\n", local_counter);
            return retval_sum;
        } else {
            // this teleport was broken, we get rid of it
            left_in_teleport = 0;
            teleport_put();
            up(&user_read_lock);
            printk("user_read: unexpected error %i\n", local_counter);
            return retval;
        }
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
        empty = down_trylock(&queue_items);
        if (empty) {
          break;
        }
        teleport_pop();
      } else {
        // Something was left in teleport
        if (retval == 0 && teleport.cycle == tpc_HALT) {
            // Discard current teleport
            left_in_teleport = 0;
            teleport_put();
            // Get new teleport
            teleport_pop();
            continue;
        }
        break;
      }
    }
  if (retval_sum > 0 || teleport.cycle != tpc_HALT) {
    up(&user_read_lock);
    printk("user_read: return normal %i\n", local_counter);
    return retval_sum;
  }

  // Something is still in teleport, but we didn't transport any data
  up(&user_read_lock);
  // printk("user_read: returning zero %i\n", local_counter);
  // return 0;
}

/*
 * WRITE()
 */
# define GET_UPTO(to_read) do {							\
	int howmuch = (to_read)-recv_phase;					\
	if (howmuch > (signed int)count)					\
		howmuch = count;						\
	if (howmuch <= 0)							\
		break;								\
	if (__copy_from_user(recv_buf+recv_phase-sizeof(MCPptr_t), buf, howmuch));	\
	buf += howmuch; count -= howmuch; recv_phase += howmuch;		\
} while (0)

static ssize_t user_write(struct file *filp, const char *buf, size_t count, loff_t * ppos)
{
	size_t orig_count = count;
	struct medusa_kclass_s * cl;
    teleport_insn_t *tele_mem_write;
    struct tele_item *local_tele_item;
    static int counter = 0;
    int local_counter = counter++;
    printk("user_write: entry %i\n", local_counter);

	if (!am_i_constable()) {
        printk("user_write: EPERM error\n");
		return -EPERM;
    }
	if (*ppos != filp->f_pos) {
        printk("user_write: ppos error\n");
		return -ESPIPE;
    }
	if (!access_ok(VERIFY_READ, buf, count)) {
        printk("user_write: EFAULT error\n");
		return -EFAULT;
    }

	while (count) {
    printk("user_write: count is %i\n", count);
		if (!atomic_read(&currently_receiving)) {
			recv_phase = 0;
			atomic_set(&currently_receiving, 1);
		}
        printk("buf: ");
        int i;
        for (i = 0; i < count/8; i++) {
            printk("%.16llx\n", *(((uint64_t*)buf)+i));
        }
		if (recv_phase < sizeof(MCPptr_t)) {
			int to_read = sizeof(MCPptr_t) - recv_phase;
			if (to_read > count)
				to_read = count;
			if (__copy_from_user(((char *)&recv_type)+recv_phase, buf,
					to_read));
			buf += to_read; count -= to_read;
			recv_phase += to_read;
			if (recv_phase < sizeof(MCPptr_t))
				continue;
		}
        down(&take_answer);
		if (recv_type == MEDUSA_COMM_AUTHANSWER) {
            // TODO Process something
			int size = (2*sizeof(MCPptr_t)+ sizeof(uint16_t));
			GET_UPTO(size); // TODO change !!!
			if (recv_phase < size) {
                // TODO What to do here?
                up(&take_answer);
				continue;
            }
			user_answer = *(int16_t *)(recv_buf+sizeof(MCPptr_t));
            recv_req_id = *(int *)(recv_buf);
			barrier();
            // WAKE UP CORRECT PROCESS
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
      printk("user_write: fetch or update: %s\n", recv_type_name[index]);
      up(&take_answer);
      down(&fetch_update_lock);
			GET_UPTO(sizeof(MCPptr_t)*3);
			if (recv_phase < sizeof(MCPptr_t)*3) {
                up(&fetch_update_lock);
				continue;
            }

			cl = med_get_kclass_by_pointer(
				*(struct medusa_kclass_s **)(recv_buf) // posibility to decrypt JK march 2015
			);
			if (!cl) {
				MED_PRINTF(MODULENAME ": protocol error at write(): unknown kclass 0x%p!\n", (void*)(*(MCPptr_t*)(recv_buf)));
				atomic_set(&currently_receiving, 0);
                up(&fetch_update_lock);
#ifdef ERRORS_CAUSE_SEGFAULT
				return -EFAULT;
#else
				break;
#endif
			}
			GET_UPTO(sizeof(MCPptr_t)*3+cl->kobject_size);
			if (recv_phase < sizeof(MCPptr_t)*3+cl->kobject_size) {
                // Write has to be atomic for concurrent teleport to work
                // Otherwise there might be a run condition
				med_put_kclass(cl);
                answ_result = MED_ERR;
                up(&fetch_update_lock);
                break;
			}
			if (atomic_read(&fetch_requests) || atomic_read(&update_requests)) {
				/* not so much to do... */
				med_put_kclass(answ_kclass);
			}
			answ_kclass = cl;
			if (recv_type == MEDUSA_COMM_FETCH_REQUEST) {
				if (cl->fetch)
					answ_kobj = cl->fetch((struct medusa_kobject_s *)
								(recv_buf+sizeof(MCPptr_t)*2));
				else {
					answ_kobj = NULL;
                    printk("user_write: cl->fetch is not set\n");
                }
			} else {
				if (cl->update)
					answ_result = cl->update(
						(struct medusa_kobject_s *)(recv_buf+sizeof(MCPptr_t)*2));
				else
					answ_result = MED_ERR;
			}
			answ_kclassid = (*(MCPptr_t*)(recv_buf));
			answ_seq = *(((MCPptr_t*)(recv_buf))+1);
            // Dynamic telemem structure for fetch/update
            tele_mem_write = (teleport_insn_t*) kmalloc(sizeof(teleport_insn_t) * 6, GFP_KERNEL);
            local_tele_item = (struct tele_item*) kmalloc(sizeof(struct tele_item), GFP_KERNEL);
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
                tele_mem_write[4].args.cutnpaste.count = answ_kclass->kobject_size;
                local_tele_item->size += answ_kclass->kobject_size;
                tele_mem_write[5].opcode = tp_HALT;
            } else
                tele_mem_write[4].opcode = tp_HALT;
            med_put_kclass(answ_kclass); /* slightly too soon */ // TODO Find out what is this
            local_tele_item->tele = tele_mem_write;
            local_tele_item->post = kfree;
            down(&queue_lock);
            printk("user_write: adding %p\n", tele_mem_write);
            list_add(&(local_tele_item->list), &tele_queue);
            up(&queue_lock);
			atomic_set(&currently_receiving, 0);
            up(&fetch_update_lock);
            up(&queue_items);

		} else {
            up(&take_answer);
			MED_PRINTF(MODULENAME ": protocol error at write(): unknown command %llx!\n", (MCPptr_t)recv_type);
			atomic_set(&currently_receiving, 0);
#ifdef ERRORS_CAUSE_SEGFAULT
            return -EFAULT;
#endif
		}
	}
  printk("user_write: return %i\n", local_counter);
	return orig_count;
}

/*
 * POLL()
 */
static unsigned int user_poll(struct file *filp, poll_table * wait)
{
	unsigned int mask = POLLOUT | POLLWRNORM;
	// unsigned int mask = POLLIN | POLLRDNORM;

	if (!am_i_constable())
		return -EPERM;
    
    // if (atomic_read(&questions_waiting)) {
    //     printk("user poll: fast answer\n");
    //     return mask;
    // }

    printk("user_poll: before wait\n");
    poll_wait(filp, &userspace_chardev, wait);
    printk("user_poll: fetches %i\n", atomic_read(&fetch_requests));
    printk("user_poll: updates %i\n", atomic_read(&update_requests));
    printk("user_poll: announces %i\n", atomic_read(&announce_ready));
    printk("user_poll: questions %i\n", atomic_read(&questions));
    if (atomic_read(&currently_receiving)) {
        printk("user_poll: receiving\n");
        mask = POLLOUT | POLLWRNORM;
    }
    if (teleport.cycle != tpc_HALT) {
        printk("user_poll: teleport not halted\n");
        mask = POLLIN | POLLRDNORM;
    }
    else if (atomic_read(&fetch_requests) || atomic_read(&update_requests) ||
            atomic_read(&announce_ready) || atomic_read(&questions)) {
        printk("user_poll: something is ready\n");
        mask = POLLIN | POLLRDNORM;
    }
    printk("user_poll: return\n");
    return mask;
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

	MED_LOCK_W(constable_openclose);
	if (atomic_read(&constable_present))
		goto out;

	constable = CURRENTPTR;
	if (strstr(current->parent->comm, "gdb"))
		gdb = current->parent;

    // Reset semaphores
    sema_init(&take_answer, 1);
    sema_init(&fetch_update_lock, 1);
    sema_init(&user_read_lock, 1);
    sema_init(&queue_items, 0);
    sema_init(&queue_lock, 1);

	atomic_set(&currently_receiving, 0);
  tele_mem_open = (teleport_insn_t*) kmalloc(sizeof(teleport_insn_t) * 2, GFP_KERNEL);
  local_tele_item = (struct tele_item*) kmalloc(sizeof(struct tele_item), GFP_KERNEL);
	tele_mem_open[0].opcode = tp_PUTPtr;
	tele_mem_open[0].args.putPtr.what = (MCPptr_t)MEDUSA_COMM_GREETING;
  local_tele_item->size = sizeof(MCPptr_t);
	tele_mem_open[1].opcode = tp_HALT;
  local_tele_item->tele = tele_mem_open;
  local_tele_item->post = kfree;
  down(&queue_lock);
  printk("user_open: adding %p\n", tele_mem_open);
  list_add_tail(&(local_tele_item->list), &tele_queue);
  up(&queue_lock);
  up(&queue_items);

	/* this must be the last thing done */
	atomic_set(&constable_present, 1);
	MED_UNLOCK_W(constable_openclose);

	evtypes_to_register = NULL;
	kclasses_to_register = NULL;

    init_waitqueue_head(&userspace_answer);

	MED_REGISTER_AUTHSERVER(chardev_medusa);
	return 0; /* success */
out:
	MED_UNLOCK_W(constable_openclose);
	return retval;
}

/*
 * CLOSE()
 */
static int user_release(struct inode *inode, struct file *file)
{
  struct list_head *pos, *next;
	DECLARE_WAITQUEUE(wait,current);

	if (!atomic_read(&constable_present))
		return 0;

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
	if (atomic_read(&fetch_requests) || atomic_read(&update_requests)) {
		med_put_kclass(answ_kclass);
		atomic_set(&fetch_requests, 0);
		atomic_set(&update_requests, 0);
	}

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
	MED_LOCK_W(constable_openclose);

  /* Let's have sequence of calls:
   * a) user process calls l4_decide and waits on wait_event_timeout(&userspace_answer)
   * b) auth server sends answer (function user_write)
   *    In this function, the value of user_answer is set
   * c) Authorization server calls user_close()
   *    At this point, user answer is still valid, so we need to wait until call
   *    in step a) finishes
   *    For this purpose, we handle user_answer with a semaphore take_answer.
   *    constable_present has to be cleared after this procedure bacause it's
   *    tested in a) after wake up
   */
  down(&take_answer);
	user_answer = MED_ERR; // XXXXX change to MED_ERR !!
  up(&take_answer);
	atomic_set(&constable_present, 0);
	constable = NULL;

	// complete(&userspace_answer);	/* the one which already might be in */
    wake_up_all(&userspace_answer);

	atomic_set(&questions, 0);
	atomic_set(&questions_waiting, 0);
	atomic_set(&announce_ready, 0);

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

	MED_UNLOCK_W(constable_openclose);
	if (am_i_constable())
		schedule();
	else
		MED_PRINTF("Authorization server is not responding.\n");
	remove_wait_queue(&close_wait, &wait);
	//MOD_DEC_USE_COUNT; Not needed anymore? JK
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

