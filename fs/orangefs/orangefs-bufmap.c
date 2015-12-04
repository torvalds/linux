/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */
#include "protocol.h"
#include "orangefs-kernel.h"
#include "orangefs-bufmap.h"

DECLARE_WAIT_QUEUE_HEAD(orangefs_bufmap_init_waitq);

static struct orangefs_bufmap {
	atomic_t refcnt;

	int desc_size;
	int desc_shift;
	int desc_count;
	int total_size;
	int page_count;

	struct page **page_array;
	struct orangefs_bufmap_desc *desc_array;

	/* array to track usage of buffer descriptors */
	int *buffer_index_array;
	spinlock_t buffer_index_lock;

	/* array to track usage of buffer descriptors for readdir */
	int readdir_index_array[ORANGEFS_READDIR_DEFAULT_DESC_COUNT];
	spinlock_t readdir_index_lock;
} *__orangefs_bufmap;

static DEFINE_SPINLOCK(orangefs_bufmap_lock);

static void
orangefs_bufmap_unmap(struct orangefs_bufmap *bufmap)
{
	int i;

	for (i = 0; i < bufmap->page_count; i++)
		page_cache_release(bufmap->page_array[i]);
}

static void
orangefs_bufmap_free(struct orangefs_bufmap *bufmap)
{
	kfree(bufmap->page_array);
	kfree(bufmap->desc_array);
	kfree(bufmap->buffer_index_array);
	kfree(bufmap);
}

struct orangefs_bufmap *orangefs_bufmap_ref(void)
{
	struct orangefs_bufmap *bufmap = NULL;

	spin_lock(&orangefs_bufmap_lock);
	if (__orangefs_bufmap) {
		bufmap = __orangefs_bufmap;
		atomic_inc(&bufmap->refcnt);
	}
	spin_unlock(&orangefs_bufmap_lock);
	return bufmap;
}

void orangefs_bufmap_unref(struct orangefs_bufmap *bufmap)
{
	if (atomic_dec_and_lock(&bufmap->refcnt, &orangefs_bufmap_lock)) {
		__orangefs_bufmap = NULL;
		spin_unlock(&orangefs_bufmap_lock);

		orangefs_bufmap_unmap(bufmap);
		orangefs_bufmap_free(bufmap);
	}
}

inline int orangefs_bufmap_size_query(void)
{
	struct orangefs_bufmap *bufmap = orangefs_bufmap_ref();
	int size = bufmap ? bufmap->desc_size : 0;

	orangefs_bufmap_unref(bufmap);
	return size;
}

inline int orangefs_bufmap_shift_query(void)
{
	struct orangefs_bufmap *bufmap = orangefs_bufmap_ref();
	int shift = bufmap ? bufmap->desc_shift : 0;

	orangefs_bufmap_unref(bufmap);
	return shift;
}

static DECLARE_WAIT_QUEUE_HEAD(bufmap_waitq);
static DECLARE_WAIT_QUEUE_HEAD(readdir_waitq);

/*
 * get_bufmap_init
 *
 * If bufmap_init is 1, then the shared memory system, including the
 * buffer_index_array, is available.  Otherwise, it is not.
 *
 * returns the value of bufmap_init
 */
int get_bufmap_init(void)
{
	return __orangefs_bufmap ? 1 : 0;
}


static struct orangefs_bufmap *
orangefs_bufmap_alloc(struct ORANGEFS_dev_map_desc *user_desc)
{
	struct orangefs_bufmap *bufmap;

	bufmap = kzalloc(sizeof(*bufmap), GFP_KERNEL);
	if (!bufmap)
		goto out;

	atomic_set(&bufmap->refcnt, 1);
	bufmap->total_size = user_desc->total_size;
	bufmap->desc_count = user_desc->count;
	bufmap->desc_size = user_desc->size;
	bufmap->desc_shift = ilog2(bufmap->desc_size);

	spin_lock_init(&bufmap->buffer_index_lock);
	bufmap->buffer_index_array =
		kcalloc(bufmap->desc_count, sizeof(int), GFP_KERNEL);
	if (!bufmap->buffer_index_array) {
		gossip_err("orangefs: could not allocate %d buffer indices\n",
				bufmap->desc_count);
		goto out_free_bufmap;
	}
	spin_lock_init(&bufmap->readdir_index_lock);

	bufmap->desc_array =
		kcalloc(bufmap->desc_count, sizeof(struct orangefs_bufmap_desc),
			GFP_KERNEL);
	if (!bufmap->desc_array) {
		gossip_err("orangefs: could not allocate %d descriptors\n",
				bufmap->desc_count);
		goto out_free_index_array;
	}

	bufmap->page_count = bufmap->total_size / PAGE_SIZE;

	/* allocate storage to track our page mappings */
	bufmap->page_array =
		kcalloc(bufmap->page_count, sizeof(struct page *), GFP_KERNEL);
	if (!bufmap->page_array)
		goto out_free_desc_array;

	return bufmap;

out_free_desc_array:
	kfree(bufmap->desc_array);
out_free_index_array:
	kfree(bufmap->buffer_index_array);
out_free_bufmap:
	kfree(bufmap);
out:
	return NULL;
}

static int
orangefs_bufmap_map(struct orangefs_bufmap *bufmap,
		struct ORANGEFS_dev_map_desc *user_desc)
{
	int pages_per_desc = bufmap->desc_size / PAGE_SIZE;
	int offset = 0, ret, i;

	/* map the pages */
	ret = get_user_pages_fast((unsigned long)user_desc->ptr,
			     bufmap->page_count, 1, bufmap->page_array);

	if (ret < 0)
		return ret;

	if (ret != bufmap->page_count) {
		gossip_err("orangefs error: asked for %d pages, only got %d.\n",
				bufmap->page_count, ret);

		for (i = 0; i < ret; i++) {
			SetPageError(bufmap->page_array[i]);
			page_cache_release(bufmap->page_array[i]);
		}
		return -ENOMEM;
	}

	/*
	 * ideally we want to get kernel space pointers for each page, but
	 * we can't kmap that many pages at once if highmem is being used.
	 * so instead, we just kmap/kunmap the page address each time the
	 * kaddr is needed.
	 */
	for (i = 0; i < bufmap->page_count; i++)
		flush_dcache_page(bufmap->page_array[i]);

	/* build a list of available descriptors */
	for (offset = 0, i = 0; i < bufmap->desc_count; i++) {
		bufmap->desc_array[i].page_array = &bufmap->page_array[offset];
		bufmap->desc_array[i].array_count = pages_per_desc;
		bufmap->desc_array[i].uaddr =
		    (user_desc->ptr + (i * pages_per_desc * PAGE_SIZE));
		offset += pages_per_desc;
	}

	return 0;
}

/*
 * orangefs_bufmap_initialize()
 *
 * initializes the mapped buffer interface
 *
 * returns 0 on success, -errno on failure
 */
int orangefs_bufmap_initialize(struct ORANGEFS_dev_map_desc *user_desc)
{
	struct orangefs_bufmap *bufmap;
	int ret = -EINVAL;

	gossip_debug(GOSSIP_BUFMAP_DEBUG,
		     "orangefs_bufmap_initialize: called (ptr ("
		     "%p) sz (%d) cnt(%d).\n",
		     user_desc->ptr,
		     user_desc->size,
		     user_desc->count);

	/*
	 * sanity check alignment and size of buffer that caller wants to
	 * work with
	 */
	if (PAGE_ALIGN((unsigned long)user_desc->ptr) !=
	    (unsigned long)user_desc->ptr) {
		gossip_err("orangefs error: memory alignment (front). %p\n",
			   user_desc->ptr);
		goto out;
	}

	if (PAGE_ALIGN(((unsigned long)user_desc->ptr + user_desc->total_size))
	    != (unsigned long)(user_desc->ptr + user_desc->total_size)) {
		gossip_err("orangefs error: memory alignment (back).(%p + %d)\n",
			   user_desc->ptr,
			   user_desc->total_size);
		goto out;
	}

	if (user_desc->total_size != (user_desc->size * user_desc->count)) {
		gossip_err("orangefs error: user provided an oddly sized buffer: (%d, %d, %d)\n",
			   user_desc->total_size,
			   user_desc->size,
			   user_desc->count);
		goto out;
	}

	if ((user_desc->size % PAGE_SIZE) != 0) {
		gossip_err("orangefs error: bufmap size not page size divisible (%d).\n",
			   user_desc->size);
		goto out;
	}

	ret = -ENOMEM;
	bufmap = orangefs_bufmap_alloc(user_desc);
	if (!bufmap)
		goto out;

	ret = orangefs_bufmap_map(bufmap, user_desc);
	if (ret)
		goto out_free_bufmap;


	spin_lock(&orangefs_bufmap_lock);
	if (__orangefs_bufmap) {
		spin_unlock(&orangefs_bufmap_lock);
		gossip_err("orangefs: error: bufmap already initialized.\n");
		ret = -EALREADY;
		goto out_unmap_bufmap;
	}
	__orangefs_bufmap = bufmap;
	spin_unlock(&orangefs_bufmap_lock);

	/*
	 * If there are operations in orangefs_bufmap_init_waitq, wake them up.
	 * This scenario occurs when the client-core is restarted and I/O
	 * requests in the in-progress or waiting tables are restarted.  I/O
	 * requests cannot be restarted until the shared memory system is
	 * completely re-initialized, so we put the I/O requests in this
	 * waitq until initialization has completed.  NOTE:  the I/O requests
	 * are also on a timer, so they don't wait forever just in case the
	 * client-core doesn't come back up.
	 */
	wake_up_interruptible(&orangefs_bufmap_init_waitq);

	gossip_debug(GOSSIP_BUFMAP_DEBUG,
		     "orangefs_bufmap_initialize: exiting normally\n");
	return 0;

out_unmap_bufmap:
	orangefs_bufmap_unmap(bufmap);
out_free_bufmap:
	orangefs_bufmap_free(bufmap);
out:
	return ret;
}

/*
 * orangefs_bufmap_finalize()
 *
 * shuts down the mapped buffer interface and releases any resources
 * associated with it
 *
 * no return value
 */
void orangefs_bufmap_finalize(void)
{
	gossip_debug(GOSSIP_BUFMAP_DEBUG, "orangefs_bufmap_finalize: called\n");
	BUG_ON(!__orangefs_bufmap);
	orangefs_bufmap_unref(__orangefs_bufmap);
	gossip_debug(GOSSIP_BUFMAP_DEBUG,
		     "orangefs_bufmap_finalize: exiting normally\n");
}

struct slot_args {
	int slot_count;
	int *slot_array;
	spinlock_t *slot_lock;
	wait_queue_head_t *slot_wq;
};

static int wait_for_a_slot(struct slot_args *slargs, int *buffer_index)
{
	int ret = -1;
	int i = 0;
	DECLARE_WAITQUEUE(my_wait, current);


	add_wait_queue_exclusive(slargs->slot_wq, &my_wait);

	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);

		/*
		 * check for available desc, slot_lock is the appropriate
		 * index_lock
		 */
		spin_lock(slargs->slot_lock);
		for (i = 0; i < slargs->slot_count; i++)
			if (slargs->slot_array[i] == 0) {
				slargs->slot_array[i] = 1;
				*buffer_index = i;
				ret = 0;
				break;
			}
		spin_unlock(slargs->slot_lock);

		/* if we acquired a buffer, then break out of while */
		if (ret == 0)
			break;

		if (!signal_pending(current)) {
			int timeout =
			    MSECS_TO_JIFFIES(1000 * slot_timeout_secs);
			gossip_debug(GOSSIP_BUFMAP_DEBUG,
				     "[BUFMAP]: waiting %d "
				     "seconds for a slot\n",
				     slot_timeout_secs);
			if (!schedule_timeout(timeout)) {
				gossip_debug(GOSSIP_BUFMAP_DEBUG,
					     "*** wait_for_a_slot timed out\n");
				ret = -ETIMEDOUT;
				break;
			}
			gossip_debug(GOSSIP_BUFMAP_DEBUG,
			  "[BUFMAP]: woken up by a slot becoming available.\n");
			continue;
		}

		gossip_debug(GOSSIP_BUFMAP_DEBUG, "orangefs: %s interrupted.\n",
			     __func__);
		ret = -EINTR;
		break;
	}

	set_current_state(TASK_RUNNING);
	remove_wait_queue(slargs->slot_wq, &my_wait);
	return ret;
}

static void put_back_slot(struct slot_args *slargs, int buffer_index)
{
	/* slot_lock is the appropriate index_lock */
	spin_lock(slargs->slot_lock);
	if (buffer_index < 0 || buffer_index >= slargs->slot_count) {
		spin_unlock(slargs->slot_lock);
		return;
	}

	/* put the desc back on the queue */
	slargs->slot_array[buffer_index] = 0;
	spin_unlock(slargs->slot_lock);

	/* wake up anyone who may be sleeping on the queue */
	wake_up_interruptible(slargs->slot_wq);
}

/*
 * orangefs_bufmap_get()
 *
 * gets a free mapped buffer descriptor, will sleep until one becomes
 * available if necessary
 *
 * returns 0 on success, -errno on failure
 */
int orangefs_bufmap_get(struct orangefs_bufmap **mapp, int *buffer_index)
{
	struct orangefs_bufmap *bufmap = orangefs_bufmap_ref();
	struct slot_args slargs;
	int ret;

	if (!bufmap) {
		gossip_err("orangefs: please confirm that pvfs2-client daemon is running.\n");
		return -EIO;
	}

	slargs.slot_count = bufmap->desc_count;
	slargs.slot_array = bufmap->buffer_index_array;
	slargs.slot_lock = &bufmap->buffer_index_lock;
	slargs.slot_wq = &bufmap_waitq;
	ret = wait_for_a_slot(&slargs, buffer_index);
	if (ret)
		orangefs_bufmap_unref(bufmap);
	*mapp = bufmap;
	return ret;
}

/*
 * orangefs_bufmap_put()
 *
 * returns a mapped buffer descriptor to the collection
 *
 * no return value
 */
void orangefs_bufmap_put(struct orangefs_bufmap *bufmap, int buffer_index)
{
	struct slot_args slargs;

	slargs.slot_count = bufmap->desc_count;
	slargs.slot_array = bufmap->buffer_index_array;
	slargs.slot_lock = &bufmap->buffer_index_lock;
	slargs.slot_wq = &bufmap_waitq;
	put_back_slot(&slargs, buffer_index);
	orangefs_bufmap_unref(bufmap);
}

/*
 * readdir_index_get()
 *
 * gets a free descriptor, will sleep until one becomes
 * available if necessary.
 * Although the readdir buffers are not mapped into kernel space
 * we could do that at a later point of time. Regardless, these
 * indices are used by the client-core.
 *
 * returns 0 on success, -errno on failure
 */
int readdir_index_get(struct orangefs_bufmap **mapp, int *buffer_index)
{
	struct orangefs_bufmap *bufmap = orangefs_bufmap_ref();
	struct slot_args slargs;
	int ret;

	if (!bufmap) {
		gossip_err("orangefs: please confirm that pvfs2-client daemon is running.\n");
		return -EIO;
	}

	slargs.slot_count = ORANGEFS_READDIR_DEFAULT_DESC_COUNT;
	slargs.slot_array = bufmap->readdir_index_array;
	slargs.slot_lock = &bufmap->readdir_index_lock;
	slargs.slot_wq = &readdir_waitq;
	ret = wait_for_a_slot(&slargs, buffer_index);
	if (ret)
		orangefs_bufmap_unref(bufmap);
	*mapp = bufmap;
	return ret;
}

void readdir_index_put(struct orangefs_bufmap *bufmap, int buffer_index)
{
	struct slot_args slargs;

	slargs.slot_count = ORANGEFS_READDIR_DEFAULT_DESC_COUNT;
	slargs.slot_array = bufmap->readdir_index_array;
	slargs.slot_lock = &bufmap->readdir_index_lock;
	slargs.slot_wq = &readdir_waitq;
	put_back_slot(&slargs, buffer_index);
	orangefs_bufmap_unref(bufmap);
}

int orangefs_bufmap_copy_from_iovec(struct orangefs_bufmap *bufmap,
				struct iov_iter *iter,
				int buffer_index,
				size_t size)
{
	struct orangefs_bufmap_desc *to = &bufmap->desc_array[buffer_index];
	int i;

	gossip_debug(GOSSIP_BUFMAP_DEBUG,
		     "%s: buffer_index:%d: size:%zu:\n",
		     __func__, buffer_index, size);


	for (i = 0; size; i++) {
		struct page *page = to->page_array[i];
		size_t n = size;
		if (n > PAGE_SIZE)
			n = PAGE_SIZE;
		n = copy_page_from_iter(page, 0, n, iter);
		if (!n)
			return -EFAULT;
		size -= n;
	}
	return 0;

}

/*
 * Iterate through the array of pages containing the bytes from
 * a file being read.
 *
 */
int orangefs_bufmap_copy_to_iovec(struct orangefs_bufmap *bufmap,
				    struct iov_iter *iter,
				    int buffer_index,
				    size_t size)
{
	struct orangefs_bufmap_desc *from = &bufmap->desc_array[buffer_index];
	int i;

	gossip_debug(GOSSIP_BUFMAP_DEBUG,
		     "%s: buffer_index:%d: size:%zu:\n",
		     __func__, buffer_index, size);


	for (i = 0; size; i++) {
		struct page *page = from->page_array[i];
		size_t n = size;
		if (n > PAGE_SIZE)
			n = PAGE_SIZE;
		n = copy_page_to_iter(page, 0, n, iter);
		if (!n)
			return -EFAULT;
		size -= n;
	}
	return 0;
}
