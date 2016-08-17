/*
 *  Copyright (C) 2010 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 *  Solaris Porting Layer (SPL) Thread Specific Data Implementation.
 *
 *  Thread specific data has implemented using a hash table, this avoids
 *  the need to add a member to the task structure and allows maximum
 *  portability between kernels.  This implementation has been optimized
 *  to keep the tsd_set() and tsd_get() times as small as possible.
 *
 *  The majority of the entries in the hash table are for specific tsd
 *  entries.  These entries are hashed by the product of their key and
 *  pid because by design the key and pid are guaranteed to be unique.
 *  Their product also has the desirable properly that it will be uniformly
 *  distributed over the hash bins providing neither the pid nor key is zero.
 *  Under linux the zero pid is always the init process and thus won't be
 *  used, and this implementation is careful to never to assign a zero key.
 *  By default the hash table is sized to 512 bins which is expected to
 *  be sufficient for light to moderate usage of thread specific data.
 *
 *  The hash table contains two additional type of entries.  They first
 *  type is entry is called a 'key' entry and it is added to the hash during
 *  tsd_create().  It is used to store the address of the destructor function
 *  and it is used as an anchor point.  All tsd entries which use the same
 *  key will be linked to this entry.  This is used during tsd_destory() to
 *  quickly call the destructor function for all tsd associated with the key.
 *  The 'key' entry may be looked up with tsd_hash_search() by passing the
 *  key you wish to lookup and DTOR_PID constant as the pid.
 *
 *  The second type of entry is called a 'pid' entry and it is added to the
 *  hash the first time a process set a key.  The 'pid' entry is also used
 *  as an anchor and all tsd for the process will be linked to it.  This
 *  list is using during tsd_exit() to ensure all registered destructors
 *  are run for the process.  The 'pid' entry may be looked up with
 *  tsd_hash_search() by passing the PID_KEY constant as the key, and
 *  the process pid.  Note that tsd_exit() is called by thread_exit()
 *  so if your using the Solaris thread API you should not need to call
 *  tsd_exit() directly.
 *
 */

#include <sys/kmem.h>
#include <sys/thread.h>
#include <sys/tsd.h>
#include <linux/hash.h>

typedef struct tsd_hash_bin {
	spinlock_t		hb_lock;
	struct hlist_head	hb_head;
} tsd_hash_bin_t;

typedef struct tsd_hash_table {
	spinlock_t		ht_lock;
	uint_t			ht_bits;
	uint_t			ht_key;
	tsd_hash_bin_t		*ht_bins;
} tsd_hash_table_t;

typedef struct tsd_hash_entry {
	uint_t			he_key;
	pid_t			he_pid;
	dtor_func_t		he_dtor;
	void			*he_value;
	struct hlist_node	he_list;
	struct list_head	he_key_list;
	struct list_head	he_pid_list;
} tsd_hash_entry_t;

static tsd_hash_table_t *tsd_hash_table = NULL;


/*
 * tsd_hash_search - searches hash table for tsd_hash_entry
 * @table: hash table
 * @key: search key
 * @pid: search pid
 */
static tsd_hash_entry_t *
tsd_hash_search(tsd_hash_table_t *table, uint_t key, pid_t pid)
{
	struct hlist_node *node;
	tsd_hash_entry_t *entry;
	tsd_hash_bin_t *bin;
	ulong_t hash;

	hash = hash_long((ulong_t)key * (ulong_t)pid, table->ht_bits);
	bin = &table->ht_bins[hash];
	spin_lock(&bin->hb_lock);
	hlist_for_each(node, &bin->hb_head) {
		entry = list_entry(node, tsd_hash_entry_t, he_list);
		if ((entry->he_key == key) && (entry->he_pid == pid)) {
			spin_unlock(&bin->hb_lock);
			return (entry);
		}
	}

	spin_unlock(&bin->hb_lock);
	return (NULL);
}

/*
 * tsd_hash_dtor - call the destructor and free all entries on the list
 * @work: list of hash entries
 *
 * For a list of entries which have all already been removed from the
 * hash call their registered destructor then free the associated memory.
 */
static void
tsd_hash_dtor(struct hlist_head *work)
{
	tsd_hash_entry_t *entry;

	while (!hlist_empty(work)) {
		entry = hlist_entry(work->first, tsd_hash_entry_t, he_list);
		hlist_del(&entry->he_list);

		if (entry->he_dtor && entry->he_pid != DTOR_PID)
			entry->he_dtor(entry->he_value);

		kmem_free(entry, sizeof (tsd_hash_entry_t));
	}
}

/*
 * tsd_hash_add - adds an entry to hash table
 * @table: hash table
 * @key: search key
 * @pid: search pid
 *
 * The caller is responsible for ensuring the unique key/pid do not
 * already exist in the hash table.  This possible because all entries
 * are thread specific thus a concurrent thread will never attempt to
 * add this key/pid.  Because multiple bins must be checked to add
 * links to the dtor and pid entries the entire table is locked.
 */
static int
tsd_hash_add(tsd_hash_table_t *table, uint_t key, pid_t pid, void *value)
{
	tsd_hash_entry_t *entry, *dtor_entry, *pid_entry;
	tsd_hash_bin_t *bin;
	ulong_t hash;
	int rc = 0;

	ASSERT3P(tsd_hash_search(table, key, pid), ==, NULL);

	/* New entry allocate structure, set value, and add to hash */
	entry = kmem_alloc(sizeof (tsd_hash_entry_t), KM_PUSHPAGE);
	if (entry == NULL)
		return (ENOMEM);

	entry->he_key = key;
	entry->he_pid = pid;
	entry->he_value = value;
	INIT_HLIST_NODE(&entry->he_list);
	INIT_LIST_HEAD(&entry->he_key_list);
	INIT_LIST_HEAD(&entry->he_pid_list);

	spin_lock(&table->ht_lock);

	/* Destructor entry must exist for all valid keys */
	dtor_entry = tsd_hash_search(table, entry->he_key, DTOR_PID);
	ASSERT3P(dtor_entry, !=, NULL);
	entry->he_dtor = dtor_entry->he_dtor;

	/* Process entry must exist for all valid processes */
	pid_entry = tsd_hash_search(table, PID_KEY, entry->he_pid);
	ASSERT3P(pid_entry, !=, NULL);

	hash = hash_long((ulong_t)key * (ulong_t)pid, table->ht_bits);
	bin = &table->ht_bins[hash];
	spin_lock(&bin->hb_lock);

	/* Add to the hash, key, and pid lists */
	hlist_add_head(&entry->he_list, &bin->hb_head);
	list_add(&entry->he_key_list, &dtor_entry->he_key_list);
	list_add(&entry->he_pid_list, &pid_entry->he_pid_list);

	spin_unlock(&bin->hb_lock);
	spin_unlock(&table->ht_lock);

	return (rc);
}

/*
 * tsd_hash_add_key - adds a destructor entry to the hash table
 * @table: hash table
 * @keyp: search key
 * @dtor: key destructor
 *
 * For every unique key there is a single entry in the hash which is used
 * as anchor.  All other thread specific entries for this key are linked
 * to this anchor via the 'he_key_list' list head.  On return they keyp
 * will be set to the next available key for the hash table.
 */
static int
tsd_hash_add_key(tsd_hash_table_t *table, uint_t *keyp, dtor_func_t dtor)
{
	tsd_hash_entry_t *tmp_entry, *entry;
	tsd_hash_bin_t *bin;
	ulong_t hash;
	int keys_checked = 0;

	ASSERT3P(table, !=, NULL);

	/* Allocate entry to be used as a destructor for this key */
	entry = kmem_alloc(sizeof (tsd_hash_entry_t), KM_PUSHPAGE);
	if (entry == NULL)
		return (ENOMEM);

	/* Determine next available key value */
	spin_lock(&table->ht_lock);
	do {
		/* Limited to TSD_KEYS_MAX concurrent unique keys */
		if (table->ht_key++ > TSD_KEYS_MAX)
			table->ht_key = 1;

		/* Ensure failure when all TSD_KEYS_MAX keys are in use */
		if (keys_checked++ >= TSD_KEYS_MAX) {
			spin_unlock(&table->ht_lock);
			return (ENOENT);
		}

		tmp_entry = tsd_hash_search(table, table->ht_key, DTOR_PID);
	} while (tmp_entry);

	/* Add destructor entry in to hash table */
	entry->he_key = *keyp = table->ht_key;
	entry->he_pid = DTOR_PID;
	entry->he_dtor = dtor;
	entry->he_value = NULL;
	INIT_HLIST_NODE(&entry->he_list);
	INIT_LIST_HEAD(&entry->he_key_list);
	INIT_LIST_HEAD(&entry->he_pid_list);

	hash = hash_long((ulong_t)*keyp * (ulong_t)DTOR_PID, table->ht_bits);
	bin = &table->ht_bins[hash];
	spin_lock(&bin->hb_lock);

	hlist_add_head(&entry->he_list, &bin->hb_head);

	spin_unlock(&bin->hb_lock);
	spin_unlock(&table->ht_lock);

	return (0);
}

/*
 * tsd_hash_add_pid - adds a process entry to the hash table
 * @table: hash table
 * @pid: search pid
 *
 * For every process these is a single entry in the hash which is used
 * as anchor.  All other thread specific entries for this process are
 * linked to this anchor via the 'he_pid_list' list head.
 */
static int
tsd_hash_add_pid(tsd_hash_table_t *table, pid_t pid)
{
	tsd_hash_entry_t *entry;
	tsd_hash_bin_t *bin;
	ulong_t hash;

	/* Allocate entry to be used as the process reference */
	entry = kmem_alloc(sizeof (tsd_hash_entry_t), KM_PUSHPAGE);
	if (entry == NULL)
		return (ENOMEM);

	spin_lock(&table->ht_lock);
	entry->he_key = PID_KEY;
	entry->he_pid = pid;
	entry->he_dtor = NULL;
	entry->he_value = NULL;
	INIT_HLIST_NODE(&entry->he_list);
	INIT_LIST_HEAD(&entry->he_key_list);
	INIT_LIST_HEAD(&entry->he_pid_list);

	hash = hash_long((ulong_t)PID_KEY * (ulong_t)pid, table->ht_bits);
	bin = &table->ht_bins[hash];
	spin_lock(&bin->hb_lock);

	hlist_add_head(&entry->he_list, &bin->hb_head);

	spin_unlock(&bin->hb_lock);
	spin_unlock(&table->ht_lock);

	return (0);
}

/*
 * tsd_hash_del - delete an entry from hash table, key, and pid lists
 * @table: hash table
 * @key: search key
 * @pid: search pid
 */
static void
tsd_hash_del(tsd_hash_table_t *table, tsd_hash_entry_t *entry)
{
	hlist_del(&entry->he_list);
	list_del_init(&entry->he_key_list);
	list_del_init(&entry->he_pid_list);
}

/*
 * tsd_hash_table_init - allocate a hash table
 * @bits: hash table size
 *
 * A hash table with 2^bits bins will be created, it may not be resized
 * after the fact and must be free'd with tsd_hash_table_fini().
 */
static tsd_hash_table_t *
tsd_hash_table_init(uint_t bits)
{
	tsd_hash_table_t *table;
	int hash, size = (1 << bits);

	table = kmem_zalloc(sizeof (tsd_hash_table_t), KM_SLEEP);
	if (table == NULL)
		return (NULL);

	table->ht_bins = kmem_zalloc(sizeof (tsd_hash_bin_t) * size, KM_SLEEP);
	if (table->ht_bins == NULL) {
		kmem_free(table, sizeof (tsd_hash_table_t));
		return (NULL);
	}

	for (hash = 0; hash < size; hash++) {
		spin_lock_init(&table->ht_bins[hash].hb_lock);
		INIT_HLIST_HEAD(&table->ht_bins[hash].hb_head);
	}

	spin_lock_init(&table->ht_lock);
	table->ht_bits = bits;
	table->ht_key = 1;

	return (table);
}

/*
 * tsd_hash_table_fini - free a hash table
 * @table: hash table
 *
 * Free a hash table allocated by tsd_hash_table_init().  If the hash
 * table is not empty this function will call the proper destructor for
 * all remaining entries before freeing the memory used by those entries.
 */
static void
tsd_hash_table_fini(tsd_hash_table_t *table)
{
	HLIST_HEAD(work);
	tsd_hash_bin_t *bin;
	tsd_hash_entry_t *entry;
	int size, i;

	ASSERT3P(table, !=, NULL);
	spin_lock(&table->ht_lock);
	for (i = 0, size = (1 << table->ht_bits); i < size; i++) {
		bin = &table->ht_bins[i];
		spin_lock(&bin->hb_lock);
		while (!hlist_empty(&bin->hb_head)) {
			entry = hlist_entry(bin->hb_head.first,
			    tsd_hash_entry_t, he_list);
			tsd_hash_del(table, entry);
			hlist_add_head(&entry->he_list, &work);
		}
		spin_unlock(&bin->hb_lock);
	}
	spin_unlock(&table->ht_lock);

	tsd_hash_dtor(&work);
	kmem_free(table->ht_bins, sizeof (tsd_hash_bin_t)*(1<<table->ht_bits));
	kmem_free(table, sizeof (tsd_hash_table_t));
}

/*
 * tsd_remove_entry - remove a tsd entry for this thread
 * @entry: entry to remove
 *
 * Remove the thread specific data @entry for this thread.
 * If this is the last entry for this thread, also remove the PID entry.
 */
static void
tsd_remove_entry(tsd_hash_entry_t *entry)
{
	HLIST_HEAD(work);
	tsd_hash_table_t *table;
	tsd_hash_entry_t *pid_entry;
	tsd_hash_bin_t *pid_entry_bin, *entry_bin;
	ulong_t hash;

	table = tsd_hash_table;
	ASSERT3P(table, !=, NULL);
	ASSERT3P(entry, !=, NULL);

	spin_lock(&table->ht_lock);

	hash = hash_long((ulong_t)entry->he_key *
	    (ulong_t)entry->he_pid, table->ht_bits);
	entry_bin = &table->ht_bins[hash];

	/* save the possible pid_entry */
	pid_entry = list_entry(entry->he_pid_list.next, tsd_hash_entry_t,
	    he_pid_list);

	/* remove entry */
	spin_lock(&entry_bin->hb_lock);
	tsd_hash_del(table, entry);
	hlist_add_head(&entry->he_list, &work);
	spin_unlock(&entry_bin->hb_lock);

	/* if pid_entry is indeed pid_entry, then remove it if it's empty */
	if (pid_entry->he_key == PID_KEY &&
	    list_empty(&pid_entry->he_pid_list)) {
		hash = hash_long((ulong_t)pid_entry->he_key *
		    (ulong_t)pid_entry->he_pid, table->ht_bits);
		pid_entry_bin = &table->ht_bins[hash];

		spin_lock(&pid_entry_bin->hb_lock);
		tsd_hash_del(table, pid_entry);
		hlist_add_head(&pid_entry->he_list, &work);
		spin_unlock(&pid_entry_bin->hb_lock);
	}

	spin_unlock(&table->ht_lock);

	tsd_hash_dtor(&work);
}

/*
 * tsd_set - set thread specific data
 * @key: lookup key
 * @value: value to set
 *
 * Caller must prevent racing tsd_create() or tsd_destroy(), protected
 * from racing tsd_get() or tsd_set() because it is thread specific.
 * This function has been optimized to be fast for the update case.
 * When setting the tsd initially it will be slower due to additional
 * required locking and potential memory allocations.
 */
int
tsd_set(uint_t key, void *value)
{
	tsd_hash_table_t *table;
	tsd_hash_entry_t *entry;
	pid_t pid;
	int rc;
	/* mark remove if value is NULL */
	boolean_t remove = (value == NULL);

	table = tsd_hash_table;
	pid = curthread->pid;
	ASSERT3P(table, !=, NULL);

	if ((key == 0) || (key > TSD_KEYS_MAX))
		return (EINVAL);

	/* Entry already exists in hash table update value */
	entry = tsd_hash_search(table, key, pid);
	if (entry) {
		entry->he_value = value;
		/* remove the entry */
		if (remove)
			tsd_remove_entry(entry);
		return (0);
	}

	/* don't create entry if value is NULL */
	if (remove)
		return (0);

	/* Add a process entry to the hash if not yet exists */
	entry = tsd_hash_search(table, PID_KEY, pid);
	if (entry == NULL) {
		rc = tsd_hash_add_pid(table, pid);
		if (rc)
			return (rc);
	}

	rc = tsd_hash_add(table, key, pid, value);
	return (rc);
}
EXPORT_SYMBOL(tsd_set);

/*
 * tsd_get - get thread specific data
 * @key: lookup key
 *
 * Caller must prevent racing tsd_create() or tsd_destroy().  This
 * implementation is designed to be fast and scalable, it does not
 * lock the entire table only a single hash bin.
 */
void *
tsd_get(uint_t key)
{
	tsd_hash_entry_t *entry;

	ASSERT3P(tsd_hash_table, !=, NULL);

	if ((key == 0) || (key > TSD_KEYS_MAX))
		return (NULL);

	entry = tsd_hash_search(tsd_hash_table, key, curthread->pid);
	if (entry == NULL)
		return (NULL);

	return (entry->he_value);
}
EXPORT_SYMBOL(tsd_get);

/*
 * tsd_get_by_thread - get thread specific data for specified thread
 * @key: lookup key
 * @thread: thread to lookup
 *
 * Caller must prevent racing tsd_create() or tsd_destroy().  This
 * implementation is designed to be fast and scalable, it does not
 * lock the entire table only a single hash bin.
 */
void *
tsd_get_by_thread(uint_t key, kthread_t *thread)
{
	tsd_hash_entry_t *entry;

	ASSERT3P(tsd_hash_table, !=, NULL);

	if ((key == 0) || (key > TSD_KEYS_MAX))
		return (NULL);

	entry = tsd_hash_search(tsd_hash_table, key, thread->pid);
	if (entry == NULL)
		return (NULL);

	return (entry->he_value);
}
EXPORT_SYMBOL(tsd_get_by_thread);

/*
 * tsd_create - create thread specific data key
 * @keyp: lookup key address
 * @dtor: destructor called during tsd_destroy() or tsd_exit()
 *
 * Provided key must be set to 0 or it assumed to be already in use.
 * The dtor is allowed to be NULL in which case no additional cleanup
 * for the data is performed during tsd_destroy() or tsd_exit().
 *
 * Caller must prevent racing tsd_set() or tsd_get(), this function is
 * safe from racing tsd_create(), tsd_destroy(), and tsd_exit().
 */
void
tsd_create(uint_t *keyp, dtor_func_t dtor)
{
	ASSERT3P(keyp, !=, NULL);
	if (*keyp)
		return;

	(void) tsd_hash_add_key(tsd_hash_table, keyp, dtor);
}
EXPORT_SYMBOL(tsd_create);

/*
 * tsd_destroy - destroy thread specific data
 * @keyp: lookup key address
 *
 * Destroys the thread specific data on all threads which use this key.
 *
 * Caller must prevent racing tsd_set() or tsd_get(), this function is
 * safe from racing tsd_create(), tsd_destroy(), and tsd_exit().
 */
void
tsd_destroy(uint_t *keyp)
{
	HLIST_HEAD(work);
	tsd_hash_table_t *table;
	tsd_hash_entry_t *dtor_entry, *entry;
	tsd_hash_bin_t *dtor_entry_bin, *entry_bin;
	ulong_t hash;

	table = tsd_hash_table;
	ASSERT3P(table, !=, NULL);

	spin_lock(&table->ht_lock);
	dtor_entry = tsd_hash_search(table, *keyp, DTOR_PID);
	if (dtor_entry == NULL) {
		spin_unlock(&table->ht_lock);
		return;
	}

	/*
	 * All threads which use this key must be linked off of the
	 * DTOR_PID entry.  They are removed from the hash table and
	 * linked in to a private working list to be destroyed.
	 */
	while (!list_empty(&dtor_entry->he_key_list)) {
		entry = list_entry(dtor_entry->he_key_list.next,
		    tsd_hash_entry_t, he_key_list);
		ASSERT3U(dtor_entry->he_key, ==, entry->he_key);
		ASSERT3P(dtor_entry->he_dtor, ==, entry->he_dtor);

		hash = hash_long((ulong_t)entry->he_key *
		    (ulong_t)entry->he_pid, table->ht_bits);
		entry_bin = &table->ht_bins[hash];

		spin_lock(&entry_bin->hb_lock);
		tsd_hash_del(table, entry);
		hlist_add_head(&entry->he_list, &work);
		spin_unlock(&entry_bin->hb_lock);
	}

	hash = hash_long((ulong_t)dtor_entry->he_key *
	    (ulong_t)dtor_entry->he_pid, table->ht_bits);
	dtor_entry_bin = &table->ht_bins[hash];

	spin_lock(&dtor_entry_bin->hb_lock);
	tsd_hash_del(table, dtor_entry);
	hlist_add_head(&dtor_entry->he_list, &work);
	spin_unlock(&dtor_entry_bin->hb_lock);
	spin_unlock(&table->ht_lock);

	tsd_hash_dtor(&work);
	*keyp = 0;
}
EXPORT_SYMBOL(tsd_destroy);

/*
 * tsd_exit - destroys all thread specific data for this thread
 *
 * Destroys all the thread specific data for this thread.
 *
 * Caller must prevent racing tsd_set() or tsd_get(), this function is
 * safe from racing tsd_create(), tsd_destroy(), and tsd_exit().
 */
void
tsd_exit(void)
{
	HLIST_HEAD(work);
	tsd_hash_table_t *table;
	tsd_hash_entry_t *pid_entry, *entry;
	tsd_hash_bin_t *pid_entry_bin, *entry_bin;
	ulong_t hash;

	table = tsd_hash_table;
	ASSERT3P(table, !=, NULL);

	spin_lock(&table->ht_lock);
	pid_entry = tsd_hash_search(table, PID_KEY, curthread->pid);
	if (pid_entry == NULL) {
		spin_unlock(&table->ht_lock);
		return;
	}

	/*
	 * All keys associated with this pid must be linked off of the
	 * PID_KEY entry.  They are removed from the hash table and
	 * linked in to a private working list to be destroyed.
	 */

	while (!list_empty(&pid_entry->he_pid_list)) {
		entry = list_entry(pid_entry->he_pid_list.next,
		    tsd_hash_entry_t, he_pid_list);
		ASSERT3U(pid_entry->he_pid, ==, entry->he_pid);

		hash = hash_long((ulong_t)entry->he_key *
		    (ulong_t)entry->he_pid, table->ht_bits);
		entry_bin = &table->ht_bins[hash];

		spin_lock(&entry_bin->hb_lock);
		tsd_hash_del(table, entry);
		hlist_add_head(&entry->he_list, &work);
		spin_unlock(&entry_bin->hb_lock);
	}

	hash = hash_long((ulong_t)pid_entry->he_key *
	    (ulong_t)pid_entry->he_pid, table->ht_bits);
	pid_entry_bin = &table->ht_bins[hash];

	spin_lock(&pid_entry_bin->hb_lock);
	tsd_hash_del(table, pid_entry);
	hlist_add_head(&pid_entry->he_list, &work);
	spin_unlock(&pid_entry_bin->hb_lock);
	spin_unlock(&table->ht_lock);

	tsd_hash_dtor(&work);
}
EXPORT_SYMBOL(tsd_exit);

int
spl_tsd_init(void)
{
	tsd_hash_table = tsd_hash_table_init(TSD_HASH_TABLE_BITS_DEFAULT);
	if (tsd_hash_table == NULL)
		return (1);

	return (0);
}

void
spl_tsd_fini(void)
{
	tsd_hash_table_fini(tsd_hash_table);
	tsd_hash_table = NULL;
}
