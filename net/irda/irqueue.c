/*********************************************************************
 *
 * Filename:      irqueue.c
 * Version:       0.3
 * Description:   General queue implementation
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Tue Jun  9 13:29:31 1998
 * Modified at:   Sun Dec 12 13:48:22 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * Modified at:   Thu Jan  4 14:29:10 CET 2001
 * Modified by:   Marc Zyngier <mzyngier@freesurf.fr>
 *
 *     Copyright (C) 1998-1999, Aage Kvalnes <aage@cs.uit.no>
 *     Copyright (C) 1998, Dag Brattli,
 *     All Rights Reserved.
 *
 *     This code is taken from the Vortex Operating System written by Aage
 *     Kvalnes. Aage has agreed that this code can use the GPL licence,
 *     although he does not use that licence in his own code.
 *
 *     This copyright does however _not_ include the ELF hash() function
 *     which I currently don't know which licence or copyright it
 *     has. Please inform me if you know.
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     Neither Dag Brattli nor University of Tromsø admit liability nor
 *     provide warranty for any of this software. This material is
 *     provided "AS-IS" and at no charge.
 *
 ********************************************************************/

/*
 * NOTE :
 * There are various problems with this package :
 *	o the hash function for ints is pathetic (but could be changed)
 *	o locking is sometime suspicious (especially during enumeration)
 *	o most users have only a few elements (== overhead)
 *	o most users never use search, so don't benefit from hashing
 * Problem already fixed :
 *	o not 64 bit compliant (most users do hashv = (int) self)
 *	o hashbin_remove() is broken => use hashbin_remove_this()
 * I think most users would be better served by a simple linked list
 * (like include/linux/list.h) with a global spinlock per list.
 * Jean II
 */

/*
 * Notes on the concurrent access to hashbin and other SMP issues
 * -------------------------------------------------------------
 *	Hashbins are very often in the IrDA stack a global repository of
 * information, and therefore used in a very asynchronous manner following
 * various events (driver calls, timers, user calls...).
 *	Therefore, very often it is highly important to consider the
 * management of concurrent access to the hashbin and how to guarantee the
 * consistency of the operations on it.
 *
 *	First, we need to define the objective of locking :
 *		1) Protect user data (content pointed by the hashbin)
 *		2) Protect hashbin structure itself (linked list in each bin)
 *
 *			     OLD LOCKING
 *			     -----------
 *
 *	The previous locking strategy, either HB_LOCAL or HB_GLOBAL were
 * both inadequate in *both* aspect.
 *		o HB_GLOBAL was using a spinlock for each bin (local locking).
 *		o HB_LOCAL was disabling irq on *all* CPUs, so use a single
 *		  global semaphore.
 *	The problems were :
 *		A) Global irq disabling is no longer supported by the kernel
 *		B) No protection for the hashbin struct global data
 *			o hashbin_delete()
 *			o hb_current
 *		C) No protection for user data in some cases
 *
 *	A) HB_LOCAL use global irq disabling, so doesn't work on kernel
 * 2.5.X. Even when it is supported (kernel 2.4.X and earlier), its
 * performance is not satisfactory on SMP setups. Most hashbins were
 * HB_LOCAL, so (A) definitely need fixing.
 *	B) HB_LOCAL could be modified to fix (B). However, because HB_GLOBAL
 * lock only the individual bins, it will never be able to lock the
 * global data, so can't do (B).
 *	C) Some functions return pointer to data that is still in the
 * hashbin :
 *		o hashbin_find()
 *		o hashbin_get_first()
 *		o hashbin_get_next()
 *	As the data is still in the hashbin, it may be changed or free'd
 * while the caller is examinimg the data. In those case, locking can't
 * be done within the hashbin, but must include use of the data within
 * the caller.
 *	The caller can easily do this with HB_LOCAL (just disable irqs).
 * However, this is impossible with HB_GLOBAL because the caller has no
 * way to know the proper bin, so don't know which spinlock to use.
 *
 *	Quick summary : can no longer use HB_LOCAL, and HB_GLOBAL is
 * fundamentally broken and will never work.
 *
 *			     NEW LOCKING
 *			     -----------
 *
 *	To fix those problems, I've introduce a few changes in the
 * hashbin locking :
 *		1) New HB_LOCK scheme
 *		2) hashbin->hb_spinlock
 *		3) New hashbin usage policy
 *
 * HB_LOCK :
 * -------
 *	HB_LOCK is a locking scheme intermediate between the old HB_LOCAL
 * and HB_GLOBAL. It uses a single spinlock to protect the whole content
 * of the hashbin. As it is a single spinlock, it can protect the global
 * data of the hashbin and not only the bins themselves.
 *	HB_LOCK can only protect some of the hashbin calls, so it only lock
 * call that can be made 100% safe and leave other call unprotected.
 *	HB_LOCK in theory is slower than HB_GLOBAL, but as the hashbin
 * content is always small contention is not high, so it doesn't matter
 * much. HB_LOCK is probably faster than HB_LOCAL.
 *
 * hashbin->hb_spinlock :
 * --------------------
 *	The spinlock that HB_LOCK uses is available for caller, so that
 * the caller can protect unprotected calls (see below).
 *	If the caller want to do entirely its own locking (HB_NOLOCK), he
 * can do so and may use safely this spinlock.
 *	Locking is done like this :
 *		spin_lock_irqsave(&hashbin->hb_spinlock, flags);
 *	Releasing the lock :
 *		spin_unlock_irqrestore(&hashbin->hb_spinlock, flags);
 *
 * Safe & Protected calls :
 * ----------------------
 *	The following calls are safe or protected via HB_LOCK :
 *		o hashbin_new()		-> safe
 *		o hashbin_delete()
 *		o hashbin_insert()
 *		o hashbin_remove_first()
 *		o hashbin_remove()
 *		o hashbin_remove_this()
 *		o HASHBIN_GET_SIZE()	-> atomic
 *
 *	The following calls only protect the hashbin itself :
 *		o hashbin_lock_find()
 *		o hashbin_find_next()
 *
 * Unprotected calls :
 * -----------------
 *	The following calls need to be protected by the caller :
 *		o hashbin_find()
 *		o hashbin_get_first()
 *		o hashbin_get_next()
 *
 * Locking Policy :
 * --------------
 *	If the hashbin is used only in a single thread of execution
 * (explicitly or implicitely), you can use HB_NOLOCK
 *	If the calling module already provide concurrent access protection,
 * you may use HB_NOLOCK.
 *
 *	In all other cases, you need to use HB_LOCK and lock the hashbin
 * every time before calling one of the unprotected calls. You also must
 * use the pointer returned by the unprotected call within the locked
 * region.
 *
 * Extra care for enumeration :
 * --------------------------
 *	hashbin_get_first() and hashbin_get_next() use the hashbin to
 * store the current position, in hb_current.
 *	As long as the hashbin remains locked, this is safe. If you unlock
 * the hashbin, the current position may change if anybody else modify
 * or enumerate the hashbin.
 *	Summary : do the full enumeration while locked.
 *
 *	Alternatively, you may use hashbin_find_next(). But, this will
 * be slower, is more complex to use and doesn't protect the hashbin
 * content. So, care is needed here as well.
 *
 * Other issues :
 * ------------
 *	I believe that we are overdoing it by using spin_lock_irqsave()
 * and we should use only spin_lock_bh() or similar. But, I don't have
 * the balls to try it out.
 *	Don't believe that because hashbin are now (somewhat) SMP safe
 * that the rest of the code is. Higher layers tend to be safest,
 * but LAP and LMP would need some serious dedicated love.
 *
 * Jean II
 */
#include <linux/module.h>
#include <linux/slab.h>

#include <net/irda/irda.h>
#include <net/irda/irqueue.h>

/************************ QUEUE SUBROUTINES ************************/

/*
 * Hashbin
 */
#define GET_HASHBIN(x) ( x & HASHBIN_MASK )

/*
 * Function hash (name)
 *
 *    This function hash the input string 'name' using the ELF hash
 *    function for strings.
 */
static __u32 hash( const char* name)
{
	__u32 h = 0;
	__u32 g;

	while(*name) {
		h = (h<<4) + *name++;
		if ((g = (h & 0xf0000000)))
			h ^=g>>24;
		h &=~g;
	}
	return h;
}

/*
 * Function enqueue_first (queue, proc)
 *
 *    Insert item first in queue.
 *
 */
static void enqueue_first(irda_queue_t **queue, irda_queue_t* element)
{

	IRDA_DEBUG( 4, "%s()\n", __func__);

	/*
	 * Check if queue is empty.
	 */
	if ( *queue == NULL ) {
		/*
		 * Queue is empty.  Insert one element into the queue.
		 */
		element->q_next = element->q_prev = *queue = element;

	} else {
		/*
		 * Queue is not empty.  Insert element into front of queue.
		 */
		element->q_next          = (*queue);
		(*queue)->q_prev->q_next = element;
		element->q_prev          = (*queue)->q_prev;
		(*queue)->q_prev         = element;
		(*queue)                 = element;
	}
}


/*
 * Function dequeue (queue)
 *
 *    Remove first entry in queue
 *
 */
static irda_queue_t *dequeue_first(irda_queue_t **queue)
{
	irda_queue_t *ret;

	IRDA_DEBUG( 4, "dequeue_first()\n");

	/*
	 * Set return value
	 */
	ret =  *queue;

	if ( *queue == NULL ) {
		/*
		 * Queue was empty.
		 */
	} else if ( (*queue)->q_next == *queue ) {
		/*
		 *  Queue only contained a single element. It will now be
		 *  empty.
		 */
		*queue = NULL;
	} else {
		/*
		 * Queue contained several element.  Remove the first one.
		 */
		(*queue)->q_prev->q_next = (*queue)->q_next;
		(*queue)->q_next->q_prev = (*queue)->q_prev;
		*queue = (*queue)->q_next;
	}

	/*
	 * Return the removed entry (or NULL of queue was empty).
	 */
	return ret;
}

/*
 * Function dequeue_general (queue, element)
 *
 *
 */
static irda_queue_t *dequeue_general(irda_queue_t **queue, irda_queue_t* element)
{
	irda_queue_t *ret;

	IRDA_DEBUG( 4, "dequeue_general()\n");

	/*
	 * Set return value
	 */
	ret =  *queue;

	if ( *queue == NULL ) {
		/*
		 * Queue was empty.
		 */
	} else if ( (*queue)->q_next == *queue ) {
		/*
		 *  Queue only contained a single element. It will now be
		 *  empty.
		 */
		*queue = NULL;

	} else {
		/*
		 *  Remove specific element.
		 */
		element->q_prev->q_next = element->q_next;
		element->q_next->q_prev = element->q_prev;
		if ( (*queue) == element)
			(*queue) = element->q_next;
	}

	/*
	 * Return the removed entry (or NULL of queue was empty).
	 */
	return ret;
}

/************************ HASHBIN MANAGEMENT ************************/

/*
 * Function hashbin_create ( type, name )
 *
 *    Create hashbin!
 *
 */
hashbin_t *hashbin_new(int type)
{
	hashbin_t* hashbin;

	/*
	 * Allocate new hashbin
	 */
	hashbin = kzalloc(sizeof(*hashbin), GFP_ATOMIC);
	if (!hashbin)
		return NULL;

	/*
	 * Initialize structure
	 */
	hashbin->hb_type = type;
	hashbin->magic = HB_MAGIC;
	//hashbin->hb_current = NULL;

	/* Make sure all spinlock's are unlocked */
	if ( hashbin->hb_type & HB_LOCK ) {
		spin_lock_init(&hashbin->hb_spinlock);
	}

	return hashbin;
}
EXPORT_SYMBOL(hashbin_new);


/*
 * Function hashbin_delete (hashbin, free_func)
 *
 *    Destroy hashbin, the free_func can be a user supplied special routine
 *    for deallocating this structure if it's complex. If not the user can
 *    just supply kfree, which should take care of the job.
 */
#ifdef CONFIG_LOCKDEP
static int hashbin_lock_depth = 0;
#endif
int hashbin_delete( hashbin_t* hashbin, FREE_FUNC free_func)
{
	irda_queue_t* queue;
	unsigned long flags = 0;
	int i;

	IRDA_ASSERT(hashbin != NULL, return -1;);
	IRDA_ASSERT(hashbin->magic == HB_MAGIC, return -1;);

	/* Synchronize */
	if ( hashbin->hb_type & HB_LOCK ) {
		spin_lock_irqsave_nested(&hashbin->hb_spinlock, flags,
					 hashbin_lock_depth++);
	}

	/*
	 *  Free the entries in the hashbin, TODO: use hashbin_clear when
	 *  it has been shown to work
	 */
	for (i = 0; i < HASHBIN_SIZE; i ++ ) {
		queue = dequeue_first((irda_queue_t**) &hashbin->hb_queue[i]);
		while (queue ) {
			if (free_func)
				(*free_func)(queue);
			queue = dequeue_first(
				(irda_queue_t**) &hashbin->hb_queue[i]);
		}
	}

	/* Cleanup local data */
	hashbin->hb_current = NULL;
	hashbin->magic = ~HB_MAGIC;

	/* Release lock */
	if ( hashbin->hb_type & HB_LOCK) {
		spin_unlock_irqrestore(&hashbin->hb_spinlock, flags);
#ifdef CONFIG_LOCKDEP
		hashbin_lock_depth--;
#endif
	}

	/*
	 *  Free the hashbin structure
	 */
	kfree(hashbin);

	return 0;
}
EXPORT_SYMBOL(hashbin_delete);

/********************* HASHBIN LIST OPERATIONS *********************/

/*
 * Function hashbin_insert (hashbin, entry, name)
 *
 *    Insert an entry into the hashbin
 *
 */
void hashbin_insert(hashbin_t* hashbin, irda_queue_t* entry, long hashv,
		    const char* name)
{
	unsigned long flags = 0;
	int bin;

	IRDA_DEBUG( 4, "%s()\n", __func__);

	IRDA_ASSERT( hashbin != NULL, return;);
	IRDA_ASSERT( hashbin->magic == HB_MAGIC, return;);

	/*
	 * Locate hashbin
	 */
	if ( name )
		hashv = hash( name );
	bin = GET_HASHBIN( hashv );

	/* Synchronize */
	if ( hashbin->hb_type & HB_LOCK ) {
		spin_lock_irqsave(&hashbin->hb_spinlock, flags);
	} /* Default is no-lock  */

	/*
	 * Store name and key
	 */
	entry->q_hash = hashv;
	if ( name )
		strlcpy( entry->q_name, name, sizeof(entry->q_name));

	/*
	 * Insert new entry first
	 */
	enqueue_first( (irda_queue_t**) &hashbin->hb_queue[ bin ],
		       entry);
	hashbin->hb_size++;

	/* Release lock */
	if ( hashbin->hb_type & HB_LOCK ) {
		spin_unlock_irqrestore(&hashbin->hb_spinlock, flags);
	} /* Default is no-lock  */
}
EXPORT_SYMBOL(hashbin_insert);

/*
 *  Function hashbin_remove_first (hashbin)
 *
 *    Remove first entry of the hashbin
 *
 * Note : this function no longer use hashbin_remove(), but does things
 * similar to hashbin_remove_this(), so can be considered safe.
 * Jean II
 */
void *hashbin_remove_first( hashbin_t *hashbin)
{
	unsigned long flags = 0;
	irda_queue_t *entry = NULL;

	/* Synchronize */
	if ( hashbin->hb_type & HB_LOCK ) {
		spin_lock_irqsave(&hashbin->hb_spinlock, flags);
	} /* Default is no-lock  */

	entry = hashbin_get_first( hashbin);
	if ( entry != NULL) {
		int	bin;
		long	hashv;
		/*
		 * Locate hashbin
		 */
		hashv = entry->q_hash;
		bin = GET_HASHBIN( hashv );

		/*
		 * Dequeue the entry...
		 */
		dequeue_general( (irda_queue_t**) &hashbin->hb_queue[ bin ],
				 (irda_queue_t*) entry );
		hashbin->hb_size--;
		entry->q_next = NULL;
		entry->q_prev = NULL;

		/*
		 *  Check if this item is the currently selected item, and in
		 *  that case we must reset hb_current
		 */
		if ( entry == hashbin->hb_current)
			hashbin->hb_current = NULL;
	}

	/* Release lock */
	if ( hashbin->hb_type & HB_LOCK ) {
		spin_unlock_irqrestore(&hashbin->hb_spinlock, flags);
	} /* Default is no-lock  */

	return entry;
}


/*
 *  Function hashbin_remove (hashbin, hashv, name)
 *
 *    Remove entry with the given name
 *
 *  The use of this function is highly discouraged, because the whole
 *  concept behind hashbin_remove() is broken. In many cases, it's not
 *  possible to guarantee the unicity of the index (either hashv or name),
 *  leading to removing the WRONG entry.
 *  The only simple safe use is :
 *		hashbin_remove(hasbin, (int) self, NULL);
 *  In other case, you must think hard to guarantee unicity of the index.
 *  Jean II
 */
void* hashbin_remove( hashbin_t* hashbin, long hashv, const char* name)
{
	int bin, found = FALSE;
	unsigned long flags = 0;
	irda_queue_t* entry;

	IRDA_DEBUG( 4, "%s()\n", __func__);

	IRDA_ASSERT( hashbin != NULL, return NULL;);
	IRDA_ASSERT( hashbin->magic == HB_MAGIC, return NULL;);

	/*
	 * Locate hashbin
	 */
	if ( name )
		hashv = hash( name );
	bin = GET_HASHBIN( hashv );

	/* Synchronize */
	if ( hashbin->hb_type & HB_LOCK ) {
		spin_lock_irqsave(&hashbin->hb_spinlock, flags);
	} /* Default is no-lock  */

	/*
	 * Search for entry
	 */
	entry = hashbin->hb_queue[ bin ];
	if ( entry ) {
		do {
			/*
			 * Check for key
			 */
			if ( entry->q_hash == hashv ) {
				/*
				 * Name compare too?
				 */
				if ( name ) {
					if ( strcmp( entry->q_name, name) == 0)
					{
						found = TRUE;
						break;
					}
				} else {
					found = TRUE;
					break;
				}
			}
			entry = entry->q_next;
		} while ( entry != hashbin->hb_queue[ bin ] );
	}

	/*
	 * If entry was found, dequeue it
	 */
	if ( found ) {
		dequeue_general( (irda_queue_t**) &hashbin->hb_queue[ bin ],
				 (irda_queue_t*) entry );
		hashbin->hb_size--;

		/*
		 *  Check if this item is the currently selected item, and in
		 *  that case we must reset hb_current
		 */
		if ( entry == hashbin->hb_current)
			hashbin->hb_current = NULL;
	}

	/* Release lock */
	if ( hashbin->hb_type & HB_LOCK ) {
		spin_unlock_irqrestore(&hashbin->hb_spinlock, flags);
	} /* Default is no-lock  */


	/* Return */
	if ( found )
		return entry;
	else
		return NULL;

}
EXPORT_SYMBOL(hashbin_remove);

/*
 *  Function hashbin_remove_this (hashbin, entry)
 *
 *    Remove entry with the given name
 *
 * In some cases, the user of hashbin can't guarantee the unicity
 * of either the hashv or name.
 * In those cases, using the above function is guaranteed to cause troubles,
 * so we use this one instead...
 * And by the way, it's also faster, because we skip the search phase ;-)
 */
void* hashbin_remove_this( hashbin_t* hashbin, irda_queue_t* entry)
{
	unsigned long flags = 0;
	int	bin;
	long	hashv;

	IRDA_DEBUG( 4, "%s()\n", __func__);

	IRDA_ASSERT( hashbin != NULL, return NULL;);
	IRDA_ASSERT( hashbin->magic == HB_MAGIC, return NULL;);
	IRDA_ASSERT( entry != NULL, return NULL;);

	/* Synchronize */
	if ( hashbin->hb_type & HB_LOCK ) {
		spin_lock_irqsave(&hashbin->hb_spinlock, flags);
	} /* Default is no-lock  */

	/* Check if valid and not already removed... */
	if((entry->q_next == NULL) || (entry->q_prev == NULL)) {
		entry = NULL;
		goto out;
	}

	/*
	 * Locate hashbin
	 */
	hashv = entry->q_hash;
	bin = GET_HASHBIN( hashv );

	/*
	 * Dequeue the entry...
	 */
	dequeue_general( (irda_queue_t**) &hashbin->hb_queue[ bin ],
			 (irda_queue_t*) entry );
	hashbin->hb_size--;
	entry->q_next = NULL;
	entry->q_prev = NULL;

	/*
	 *  Check if this item is the currently selected item, and in
	 *  that case we must reset hb_current
	 */
	if ( entry == hashbin->hb_current)
		hashbin->hb_current = NULL;
out:
	/* Release lock */
	if ( hashbin->hb_type & HB_LOCK ) {
		spin_unlock_irqrestore(&hashbin->hb_spinlock, flags);
	} /* Default is no-lock  */

	return entry;
}
EXPORT_SYMBOL(hashbin_remove_this);

/*********************** HASHBIN ENUMERATION ***********************/

/*
 * Function hashbin_common_find (hashbin, hashv, name)
 *
 *    Find item with the given hashv or name
 *
 */
void* hashbin_find( hashbin_t* hashbin, long hashv, const char* name )
{
	int bin;
	irda_queue_t* entry;

	IRDA_DEBUG( 4, "hashbin_find()\n");

	IRDA_ASSERT( hashbin != NULL, return NULL;);
	IRDA_ASSERT( hashbin->magic == HB_MAGIC, return NULL;);

	/*
	 * Locate hashbin
	 */
	if ( name )
		hashv = hash( name );
	bin = GET_HASHBIN( hashv );

	/*
	 * Search for entry
	 */
	entry = hashbin->hb_queue[ bin];
	if ( entry ) {
		do {
			/*
			 * Check for key
			 */
			if ( entry->q_hash == hashv ) {
				/*
				 * Name compare too?
				 */
				if ( name ) {
					if ( strcmp( entry->q_name, name ) == 0 ) {
						return entry;
					}
				} else {
					return entry;
				}
			}
			entry = entry->q_next;
		} while ( entry != hashbin->hb_queue[ bin ] );
	}

	return NULL;
}
EXPORT_SYMBOL(hashbin_find);

/*
 * Function hashbin_lock_find (hashbin, hashv, name)
 *
 *    Find item with the given hashv or name
 *
 * Same, but with spinlock protection...
 * I call it safe, but it's only safe with respect to the hashbin, not its
 * content. - Jean II
 */
void* hashbin_lock_find( hashbin_t* hashbin, long hashv, const char* name )
{
	unsigned long flags = 0;
	irda_queue_t* entry;

	/* Synchronize */
	spin_lock_irqsave(&hashbin->hb_spinlock, flags);

	/*
	 * Search for entry
	 */
	entry = (irda_queue_t* ) hashbin_find( hashbin, hashv, name );

	/* Release lock */
	spin_unlock_irqrestore(&hashbin->hb_spinlock, flags);

	return entry;
}
EXPORT_SYMBOL(hashbin_lock_find);

/*
 * Function hashbin_find (hashbin, hashv, name, pnext)
 *
 *    Find an item with the given hashv or name, and its successor
 *
 * This function allow to do concurrent enumerations without the
 * need to lock over the whole session, because the caller keep the
 * context of the search. On the other hand, it might fail and return
 * NULL if the entry is removed. - Jean II
 */
void* hashbin_find_next( hashbin_t* hashbin, long hashv, const char* name,
			 void ** pnext)
{
	unsigned long flags = 0;
	irda_queue_t* entry;

	/* Synchronize */
	spin_lock_irqsave(&hashbin->hb_spinlock, flags);

	/*
	 * Search for current entry
	 * This allow to check if the current item is still in the
	 * hashbin or has been removed.
	 */
	entry = (irda_queue_t* ) hashbin_find( hashbin, hashv, name );

	/*
	 * Trick hashbin_get_next() to return what we want
	 */
	if(entry) {
		hashbin->hb_current = entry;
		*pnext = hashbin_get_next( hashbin );
	} else
		*pnext = NULL;

	/* Release lock */
	spin_unlock_irqrestore(&hashbin->hb_spinlock, flags);

	return entry;
}

/*
 * Function hashbin_get_first (hashbin)
 *
 *    Get a pointer to first element in hashbin, this function must be
 *    called before any calls to hashbin_get_next()!
 *
 */
irda_queue_t *hashbin_get_first( hashbin_t* hashbin)
{
	irda_queue_t *entry;
	int i;

	IRDA_ASSERT( hashbin != NULL, return NULL;);
	IRDA_ASSERT( hashbin->magic == HB_MAGIC, return NULL;);

	if ( hashbin == NULL)
		return NULL;

	for ( i = 0; i < HASHBIN_SIZE; i ++ ) {
		entry = hashbin->hb_queue[ i];
		if ( entry) {
			hashbin->hb_current = entry;
			return entry;
		}
	}
	/*
	 *  Did not find any item in hashbin
	 */
	return NULL;
}
EXPORT_SYMBOL(hashbin_get_first);

/*
 * Function hashbin_get_next (hashbin)
 *
 *    Get next item in hashbin. A series of hashbin_get_next() calls must
 *    be started by a call to hashbin_get_first(). The function returns
 *    NULL when all items have been traversed
 *
 * The context of the search is stored within the hashbin, so you must
 * protect yourself from concurrent enumerations. - Jean II
 */
irda_queue_t *hashbin_get_next( hashbin_t *hashbin)
{
	irda_queue_t* entry;
	int bin;
	int i;

	IRDA_ASSERT( hashbin != NULL, return NULL;);
	IRDA_ASSERT( hashbin->magic == HB_MAGIC, return NULL;);

	if ( hashbin->hb_current == NULL) {
		IRDA_ASSERT( hashbin->hb_current != NULL, return NULL;);
		return NULL;
	}
	entry = hashbin->hb_current->q_next;
	bin = GET_HASHBIN( entry->q_hash);

	/*
	 *  Make sure that we are not back at the beginning of the queue
	 *  again
	 */
	if ( entry != hashbin->hb_queue[ bin ]) {
		hashbin->hb_current = entry;

		return entry;
	}

	/*
	 *  Check that this is not the last queue in hashbin
	 */
	if ( bin >= HASHBIN_SIZE)
		return NULL;

	/*
	 *  Move to next queue in hashbin
	 */
	bin++;
	for ( i = bin; i < HASHBIN_SIZE; i++ ) {
		entry = hashbin->hb_queue[ i];
		if ( entry) {
			hashbin->hb_current = entry;

			return entry;
		}
	}
	return NULL;
}
EXPORT_SYMBOL(hashbin_get_next);
