/*
 * Generic pidhash and scalable, time-bounded PID allocator
 *
 * (C) 2002-2003 William Irwin, IBM
 * (C) 2004 William Irwin, Oracle
 * (C) 2002-2004 Ingo Molnar, Red Hat
 *
 * pid-structures are backing objects for tasks sharing a given ID to chain
 * against. There is very little to them aside from hashing them and
 * parking tasks using given ID's on a list.
 *
 * The hash is always changed with the tasklist_lock write-acquired,
 * and the hash is only accessed with the tasklist_lock at least
 * read-acquired, so there's no additional SMP locking needed here.
 *
 * We have a list of bitmap pages, which bitmaps represent the PID space.
 * Allocating and freeing PIDs is completely lockless. The worst-case
 * allocation scenario when all but one out of 1 million PIDs possible are
 * allocated already: the scanning of 32 list entries and at most PAGE_SIZE
 * bytes. The typical fastpath is a single successful setbit. Freeing is O(1).
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/hash.h>

#define pid_hashfn(nr) hash_long((unsigned long)nr, pidhash_shift)
static struct hlist_head *pid_hash[PIDTYPE_MAX];
static int pidhash_shift;

int pid_max = PID_MAX_DEFAULT;
int last_pid;

#define RESERVED_PIDS		300

int pid_max_min = RESERVED_PIDS + 1;
int pid_max_max = PID_MAX_LIMIT;

#define PIDMAP_ENTRIES		((PID_MAX_LIMIT + 8*PAGE_SIZE - 1)/PAGE_SIZE/8)
#define BITS_PER_PAGE		(PAGE_SIZE*8)
#define BITS_PER_PAGE_MASK	(BITS_PER_PAGE-1)
#define mk_pid(map, off)	(((map) - pidmap_array)*BITS_PER_PAGE + (off))
#define find_next_offset(map, off)					\
		find_next_zero_bit((map)->page, BITS_PER_PAGE, off)

/*
 * PID-map pages start out as NULL, they get allocated upon
 * first use and are never deallocated. This way a low pid_max
 * value does not cause lots of bitmaps to be allocated, but
 * the scheme scales to up to 4 million PIDs, runtime.
 */
typedef struct pidmap {
	atomic_t nr_free;
	void *page;
} pidmap_t;

static pidmap_t pidmap_array[PIDMAP_ENTRIES] =
	 { [ 0 ... PIDMAP_ENTRIES-1 ] = { ATOMIC_INIT(BITS_PER_PAGE), NULL } };

static  __cacheline_aligned_in_smp DEFINE_SPINLOCK(pidmap_lock);

fastcall void free_pidmap(int pid)
{
	pidmap_t *map = pidmap_array + pid / BITS_PER_PAGE;
	int offset = pid & BITS_PER_PAGE_MASK;

	clear_bit(offset, map->page);
	atomic_inc(&map->nr_free);
}

int alloc_pidmap(void)
{
	int i, offset, max_scan, pid, last = last_pid;
	pidmap_t *map;

	pid = last + 1;
	if (pid >= pid_max)
		pid = RESERVED_PIDS;
	offset = pid & BITS_PER_PAGE_MASK;
	map = &pidmap_array[pid/BITS_PER_PAGE];
	max_scan = (pid_max + BITS_PER_PAGE - 1)/BITS_PER_PAGE - !offset;
	for (i = 0; i <= max_scan; ++i) {
		if (unlikely(!map->page)) {
			unsigned long page = get_zeroed_page(GFP_KERNEL);
			/*
			 * Free the page if someone raced with us
			 * installing it:
			 */
			spin_lock(&pidmap_lock);
			if (map->page)
				free_page(page);
			else
				map->page = (void *)page;
			spin_unlock(&pidmap_lock);
			if (unlikely(!map->page))
				break;
		}
		if (likely(atomic_read(&map->nr_free))) {
			do {
				if (!test_and_set_bit(offset, map->page)) {
					atomic_dec(&map->nr_free);
					last_pid = pid;
					return pid;
				}
				offset = find_next_offset(map, offset);
				pid = mk_pid(map, offset);
			/*
			 * find_next_offset() found a bit, the pid from it
			 * is in-bounds, and if we fell back to the last
			 * bitmap block and the final block was the same
			 * as the starting point, pid is before last_pid.
			 */
			} while (offset < BITS_PER_PAGE && pid < pid_max &&
					(i != max_scan || pid < last ||
					    !((last+1) & BITS_PER_PAGE_MASK)));
		}
		if (map < &pidmap_array[(pid_max-1)/BITS_PER_PAGE]) {
			++map;
			offset = 0;
		} else {
			map = &pidmap_array[0];
			offset = RESERVED_PIDS;
			if (unlikely(last == offset))
				break;
		}
		pid = mk_pid(map, offset);
	}
	return -1;
}

struct pid * fastcall find_pid(enum pid_type type, int nr)
{
	struct hlist_node *elem;
	struct pid *pid;

	hlist_for_each_entry_rcu(pid, elem,
			&pid_hash[type][pid_hashfn(nr)], pid_chain) {
		if (pid->nr == nr)
			return pid;
	}
	return NULL;
}

int fastcall attach_pid(task_t *task, enum pid_type type, int nr)
{
	struct pid *pid, *task_pid;

	task_pid = &task->pids[type];
	pid = find_pid(type, nr);
	task_pid->nr = nr;
	if (pid == NULL) {
		INIT_LIST_HEAD(&task_pid->pid_list);
		hlist_add_head_rcu(&task_pid->pid_chain,
				   &pid_hash[type][pid_hashfn(nr)]);
	} else {
		INIT_HLIST_NODE(&task_pid->pid_chain);
		list_add_tail_rcu(&task_pid->pid_list, &pid->pid_list);
	}

	return 0;
}

static fastcall int __detach_pid(task_t *task, enum pid_type type)
{
	struct pid *pid, *pid_next;
	int nr = 0;

	pid = &task->pids[type];
	if (!hlist_unhashed(&pid->pid_chain)) {

		if (list_empty(&pid->pid_list)) {
			nr = pid->nr;
			hlist_del_rcu(&pid->pid_chain);
		} else {
			pid_next = list_entry(pid->pid_list.next,
						struct pid, pid_list);
			/* insert next pid from pid_list to hash */
			hlist_replace_rcu(&pid->pid_chain,
					  &pid_next->pid_chain);
		}
	}

	list_del_rcu(&pid->pid_list);
	pid->nr = 0;

	return nr;
}

void fastcall detach_pid(task_t *task, enum pid_type type)
{
	int tmp, nr;

	nr = __detach_pid(task, type);
	if (!nr)
		return;

	for (tmp = PIDTYPE_MAX; --tmp >= 0; )
		if (tmp != type && find_pid(tmp, nr))
			return;

	free_pidmap(nr);
}

task_t *find_task_by_pid_type(int type, int nr)
{
	struct pid *pid;

	pid = find_pid(type, nr);
	if (!pid)
		return NULL;

	return pid_task(&pid->pid_list, type);
}

EXPORT_SYMBOL(find_task_by_pid_type);

/*
 * The pid hash table is scaled according to the amount of memory in the
 * machine.  From a minimum of 16 slots up to 4096 slots at one gigabyte or
 * more.
 */
void __init pidhash_init(void)
{
	int i, j, pidhash_size;
	unsigned long megabytes = nr_kernel_pages >> (20 - PAGE_SHIFT);

	pidhash_shift = max(4, fls(megabytes * 4));
	pidhash_shift = min(12, pidhash_shift);
	pidhash_size = 1 << pidhash_shift;

	printk("PID hash table entries: %d (order: %d, %Zd bytes)\n",
		pidhash_size, pidhash_shift,
		PIDTYPE_MAX * pidhash_size * sizeof(struct hlist_head));

	for (i = 0; i < PIDTYPE_MAX; i++) {
		pid_hash[i] = alloc_bootmem(pidhash_size *
					sizeof(*(pid_hash[i])));
		if (!pid_hash[i])
			panic("Could not alloc pidhash!\n");
		for (j = 0; j < pidhash_size; j++)
			INIT_HLIST_HEAD(&pid_hash[i][j]);
	}
}

void __init pidmap_init(void)
{
	pidmap_array->page = (void *)get_zeroed_page(GFP_KERNEL);
	/* Reserve PID 0. We never call free_pidmap(0) */
	set_bit(0, pidmap_array->page);
	atomic_dec(&pidmap_array->nr_free);
}
