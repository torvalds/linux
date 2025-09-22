/* Public domain. */

#ifndef _LINUX_SYNC_FILE_H
#define _LINUX_SYNC_FILE_H

#include <linux/dma-fence.h>
#include <linux/dma-fence-array.h>
#include <linux/ktime.h>

struct sync_file {
	struct file *file;
	struct dma_fence *fence;
};

struct dma_fence *sync_file_get_fence(int);
struct sync_file *sync_file_create(struct dma_fence *);

#endif
