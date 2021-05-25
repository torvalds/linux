#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/genhd.h>
#include <linux/blkdev.h>
#include <linux/kthread.h>
#include <linux/delay.h>

MODULE_AUTHOR("SO2");
MODULE_DESCRIPTION("Relay disk");
MODULE_LICENSE("GPL");

#define KERN_LOG_LEVEL		KERN_ALERT

#define PHYSICAL_DISK_NAME	"/dev/vdb"
#define KERNEL_SECTOR_SIZE	512

#define MAX_BIO 30000
#define MAX_THREAD 20
#define MAX_RUNS 40


/* pointer to physical device structure */
static struct block_device *phys_bdev;

struct bio *bio[MAX_BIO];
struct page *page[MAX_BIO];

static void alloc_io(struct block_device *bdev)
{
	int i;

	for (i = 0; i < MAX_BIO; i++) {
		bio[i] = bio_alloc(GFP_NOIO, 1);
		bio[i]->bi_disk = bdev->bd_disk;
		bio[i]->bi_opf = REQ_OP_READ;

		bio[i]->bi_iter.bi_sector = i;
		page[i] = alloc_page(GFP_NOIO);
		bio_add_page(bio[i], page[i], KERNEL_SECTOR_SIZE, 0);
	}
}

static struct block_device *open_disk(char *name)
{
	struct block_device *bdev;

	bdev = blkdev_get_by_path(name, FMODE_READ | FMODE_WRITE | FMODE_EXCL, THIS_MODULE);
	if (IS_ERR(bdev)) {
		printk(KERN_ERR "blkdev_get_by_path\n");
		return NULL;
	}

	return bdev;
}

int my_thread_f(void *data)
{
	int part, sec, i, run;

	part = (int) data;
	sec = MAX_BIO / MAX_THREAD;

	for (run = 0; run < MAX_RUNS; run++) {
		for (i = sec * part; i < (part + 1) * sec; i++)
			submit_bio_wait(bio[i]);
		msleep(30 * 1000);
	}

	do_exit(0);
}

static int __init relay_init(void)
{
	int i = 0;

	phys_bdev = open_disk(PHYSICAL_DISK_NAME);
	if (phys_bdev == NULL) {
		printk(KERN_ERR "[relay_init] No such device\n");
		return -EINVAL;
	}

	alloc_io(phys_bdev);

	for (i = 0; i < MAX_THREAD; i++)
		kthread_run(my_thread_f, i, "%skwriterd%d", "my", (void *)i);

	return 0;
}

static void close_disk(struct block_device *bdev)
{
	blkdev_put(bdev, FMODE_READ | FMODE_WRITE | FMODE_EXCL);
}

static void __exit relay_exit(void)
{
	int i;

	for (i = 0; i < MAX_BIO; i++) {
		bio_put(bio[i]);
		__free_page(page[i]);
	}
	close_disk(phys_bdev);
}

module_init(relay_init);
module_exit(relay_exit);
