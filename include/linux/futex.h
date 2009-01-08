#ifndef _LINUX_FUTEX_H
#define _LINUX_FUTEX_H

#include <linux/compiler.h>
#include <linux/types.h>

struct inode;
struct mm_struct;
struct task_struct;
union ktime;

/* Second argument to futex syscall */


#define FUTEX_WAIT		0
#define FUTEX_WAKE		1
#define FUTEX_FD		2
#define FUTEX_REQUEUE		3
#define FUTEX_CMP_REQUEUE	4
#define FUTEX_WAKE_OP		5
#define FUTEX_LOCK_PI		6
#define FUTEX_UNLOCK_PI		7
#define FUTEX_TRYLOCK_PI	8
#define FUTEX_WAIT_BITSET	9
#define FUTEX_WAKE_BITSET	10

#define FUTEX_PRIVATE_FLAG	128
#define FUTEX_CLOCK_REALTIME	256
#define FUTEX_CMD_MASK		~(FUTEX_PRIVATE_FLAG | FUTEX_CLOCK_REALTIME)

#define FUTEX_WAIT_PRIVATE	(FUTEX_WAIT | FUTEX_PRIVATE_FLAG)
#define FUTEX_WAKE_PRIVATE	(FUTEX_WAKE | FUTEX_PRIVATE_FLAG)
#define FUTEX_REQUEUE_PRIVATE	(FUTEX_REQUEUE | FUTEX_PRIVATE_FLAG)
#define FUTEX_CMP_REQUEUE_PRIVATE (FUTEX_CMP_REQUEUE | FUTEX_PRIVATE_FLAG)
#define FUTEX_WAKE_OP_PRIVATE	(FUTEX_WAKE_OP | FUTEX_PRIVATE_FLAG)
#define FUTEX_LOCK_PI_PRIVATE	(FUTEX_LOCK_PI | FUTEX_PRIVATE_FLAG)
#define FUTEX_UNLOCK_PI_PRIVATE	(FUTEX_UNLOCK_PI | FUTEX_PRIVATE_FLAG)
#define FUTEX_TRYLOCK_PI_PRIVATE (FUTEX_TRYLOCK_PI | FUTEX_PRIVATE_FLAG)
#define FUTEX_WAIT_BITSET_PRIVATE	(FUTEX_WAIT_BITS | FUTEX_PRIVATE_FLAG)
#define FUTEX_WAKE_BITSET_PRIVATE	(FUTEX_WAKE_BITS | FUTEX_PRIVATE_FLAG)

/*
 * Support for robust futexes: the kernel cleans up held futexes at
 * thread exit time.
 */

/*
 * Per-lock list entry - embedded in user-space locks, somewhere close
 * to the futex field. (Note: user-space uses a double-linked list to
 * achieve O(1) list add and remove, but the kernel only needs to know
 * about the forward link)
 *
 * NOTE: this structure is part of the syscall ABI, and must not be
 * changed.
 */
struct robust_list {
	struct robust_list __user *next;
};

/*
 * Per-thread list head:
 *
 * NOTE: this structure is part of the syscall ABI, and must only be
 * changed if the change is first communicated with the glibc folks.
 * (When an incompatible change is done, we'll increase the structure
 *  size, which glibc will detect)
 */
struct robust_list_head {
	/*
	 * The head of the list. Points back to itself if empty:
	 */
	struct robust_list list;

	/*
	 * This relative offset is set by user-space, it gives the kernel
	 * the relative position of the futex field to examine. This way
	 * we keep userspace flexible, to freely shape its data-structure,
	 * without hardcoding any particular offset into the kernel:
	 */
	long futex_offset;

	/*
	 * The death of the thread may race with userspace setting
	 * up a lock's links. So to handle this race, userspace first
	 * sets this field to the address of the to-be-taken lock,
	 * then does the lock acquire, and then adds itself to the
	 * list, and then clears this field. Hence the kernel will
	 * always have full knowledge of all locks that the thread
	 * _might_ have taken. We check the owner TID in any case,
	 * so only truly owned locks will be handled.
	 */
	struct robust_list __user *list_op_pending;
};

/*
 * Are there any waiters for this robust futex:
 */
#define FUTEX_WAITERS		0x80000000

/*
 * The kernel signals via this bit that a thread holding a futex
 * has exited without unlocking the futex. The kernel also does
 * a FUTEX_WAKE on such futexes, after setting the bit, to wake
 * up any possible waiters:
 */
#define FUTEX_OWNER_DIED	0x40000000

/*
 * The rest of the robust-futex field is for the TID:
 */
#define FUTEX_TID_MASK		0x3fffffff

/*
 * This limit protects against a deliberately circular list.
 * (Not worth introducing an rlimit for it)
 */
#define ROBUST_LIST_LIMIT	2048

/*
 * bitset with all bits set for the FUTEX_xxx_BITSET OPs to request a
 * match of any bit.
 */
#define FUTEX_BITSET_MATCH_ANY	0xffffffff

#ifdef __KERNEL__
long do_futex(u32 __user *uaddr, int op, u32 val, union ktime *timeout,
	      u32 __user *uaddr2, u32 val2, u32 val3);

extern int
handle_futex_death(u32 __user *uaddr, struct task_struct *curr, int pi);

/*
 * Futexes are matched on equal values of this key.
 * The key type depends on whether it's a shared or private mapping.
 * Don't rearrange members without looking at hash_futex().
 *
 * offset is aligned to a multiple of sizeof(u32) (== 4) by definition.
 * We use the two low order bits of offset to tell what is the kind of key :
 *  00 : Private process futex (PTHREAD_PROCESS_PRIVATE)
 *       (no reference on an inode or mm)
 *  01 : Shared futex (PTHREAD_PROCESS_SHARED)
 *	mapped on a file (reference on the underlying inode)
 *  10 : Shared futex (PTHREAD_PROCESS_SHARED)
 *       (but private mapping on an mm, and reference taken on it)
*/

#define FUT_OFF_INODE    1 /* We set bit 0 if key has a reference on inode */
#define FUT_OFF_MMSHARED 2 /* We set bit 1 if key has a reference on mm */

union futex_key {
	struct {
		unsigned long pgoff;
		struct inode *inode;
		int offset;
	} shared;
	struct {
		unsigned long address;
		struct mm_struct *mm;
		int offset;
	} private;
	struct {
		unsigned long word;
		void *ptr;
		int offset;
	} both;
};

#define FUTEX_KEY_INIT (union futex_key) { .both = { .ptr = NULL } }

#ifdef CONFIG_FUTEX
extern void exit_robust_list(struct task_struct *curr);
extern void exit_pi_state_list(struct task_struct *curr);
extern int futex_cmpxchg_enabled;
#else
static inline void exit_robust_list(struct task_struct *curr)
{
}
static inline void exit_pi_state_list(struct task_struct *curr)
{
}
#endif
#endif /* __KERNEL__ */

#define FUTEX_OP_SET		0	/* *(int *)UADDR2 = OPARG; */
#define FUTEX_OP_ADD		1	/* *(int *)UADDR2 += OPARG; */
#define FUTEX_OP_OR		2	/* *(int *)UADDR2 |= OPARG; */
#define FUTEX_OP_ANDN		3	/* *(int *)UADDR2 &= ~OPARG; */
#define FUTEX_OP_XOR		4	/* *(int *)UADDR2 ^= OPARG; */

#define FUTEX_OP_OPARG_SHIFT	8	/* Use (1 << OPARG) instead of OPARG.  */

#define FUTEX_OP_CMP_EQ		0	/* if (oldval == CMPARG) wake */
#define FUTEX_OP_CMP_NE		1	/* if (oldval != CMPARG) wake */
#define FUTEX_OP_CMP_LT		2	/* if (oldval < CMPARG) wake */
#define FUTEX_OP_CMP_LE		3	/* if (oldval <= CMPARG) wake */
#define FUTEX_OP_CMP_GT		4	/* if (oldval > CMPARG) wake */
#define FUTEX_OP_CMP_GE		5	/* if (oldval >= CMPARG) wake */

/* FUTEX_WAKE_OP will perform atomically
   int oldval = *(int *)UADDR2;
   *(int *)UADDR2 = oldval OP OPARG;
   if (oldval CMP CMPARG)
     wake UADDR2;  */

#define FUTEX_OP(op, oparg, cmp, cmparg) \
  (((op & 0xf) << 28) | ((cmp & 0xf) << 24)		\
   | ((oparg & 0xfff) << 12) | (cmparg & 0xfff))

#endif
