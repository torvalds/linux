
#include <linux/wait.h>
#include <linux/backing-dev.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/writeback.h>
#include <linux/device.h>


static struct class *bdi_class;

static ssize_t read_ahead_kb_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct backing_dev_info *bdi = dev_get_drvdata(dev);
	char *end;
	unsigned long read_ahead_kb;
	ssize_t ret = -EINVAL;

	read_ahead_kb = simple_strtoul(buf, &end, 10);
	if (*buf && (end[0] == '\0' || (end[0] == '\n' && end[1] == '\0'))) {
		bdi->ra_pages = read_ahead_kb >> (PAGE_SHIFT - 10);
		ret = count;
	}
	return ret;
}

#define K(pages) ((pages) << (PAGE_SHIFT - 10))

#define BDI_SHOW(name, expr)						\
static ssize_t name##_show(struct device *dev,				\
			   struct device_attribute *attr, char *page)	\
{									\
	struct backing_dev_info *bdi = dev_get_drvdata(dev);		\
									\
	return snprintf(page, PAGE_SIZE-1, "%lld\n", (long long)expr);	\
}

BDI_SHOW(read_ahead_kb, K(bdi->ra_pages))

BDI_SHOW(reclaimable_kb, K(bdi_stat(bdi, BDI_RECLAIMABLE)))
BDI_SHOW(writeback_kb, K(bdi_stat(bdi, BDI_WRITEBACK)))

static inline unsigned long get_dirty(struct backing_dev_info *bdi, int i)
{
	unsigned long thresh[3];

	get_dirty_limits(&thresh[0], &thresh[1], &thresh[2], bdi);

	return thresh[i];
}

BDI_SHOW(dirty_kb, K(get_dirty(bdi, 1)))
BDI_SHOW(bdi_dirty_kb, K(get_dirty(bdi, 2)))

static ssize_t min_ratio_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct backing_dev_info *bdi = dev_get_drvdata(dev);
	char *end;
	unsigned int ratio;
	ssize_t ret = -EINVAL;

	ratio = simple_strtoul(buf, &end, 10);
	if (*buf && (end[0] == '\0' || (end[0] == '\n' && end[1] == '\0'))) {
		ret = bdi_set_min_ratio(bdi, ratio);
		if (!ret)
			ret = count;
	}
	return ret;
}
BDI_SHOW(min_ratio, bdi->min_ratio)

#define __ATTR_RW(attr) __ATTR(attr, 0644, attr##_show, attr##_store)

static struct device_attribute bdi_dev_attrs[] = {
	__ATTR_RW(read_ahead_kb),
	__ATTR_RO(reclaimable_kb),
	__ATTR_RO(writeback_kb),
	__ATTR_RO(dirty_kb),
	__ATTR_RO(bdi_dirty_kb),
	__ATTR_RW(min_ratio),
	__ATTR_NULL,
};

static __init int bdi_class_init(void)
{
	bdi_class = class_create(THIS_MODULE, "bdi");
	bdi_class->dev_attrs = bdi_dev_attrs;
	return 0;
}

core_initcall(bdi_class_init);

int bdi_register(struct backing_dev_info *bdi, struct device *parent,
		const char *fmt, ...)
{
	char *name;
	va_list args;
	int ret = 0;
	struct device *dev;

	va_start(args, fmt);
	name = kvasprintf(GFP_KERNEL, fmt, args);
	va_end(args);

	if (!name)
		return -ENOMEM;

	dev = device_create(bdi_class, parent, MKDEV(0, 0), name);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto exit;
	}

	bdi->dev = dev;
	dev_set_drvdata(bdi->dev, bdi);

exit:
	kfree(name);
	return ret;
}
EXPORT_SYMBOL(bdi_register);

int bdi_register_dev(struct backing_dev_info *bdi, dev_t dev)
{
	return bdi_register(bdi, NULL, "%u:%u", MAJOR(dev), MINOR(dev));
}
EXPORT_SYMBOL(bdi_register_dev);

void bdi_unregister(struct backing_dev_info *bdi)
{
	if (bdi->dev) {
		device_unregister(bdi->dev);
		bdi->dev = NULL;
	}
}
EXPORT_SYMBOL(bdi_unregister);

int bdi_init(struct backing_dev_info *bdi)
{
	int i;
	int err;

	bdi->dev = NULL;

	bdi->min_ratio = 0;

	for (i = 0; i < NR_BDI_STAT_ITEMS; i++) {
		err = percpu_counter_init_irq(&bdi->bdi_stat[i], 0);
		if (err)
			goto err;
	}

	bdi->dirty_exceeded = 0;
	err = prop_local_init_percpu(&bdi->completions);

	if (err) {
err:
		while (i--)
			percpu_counter_destroy(&bdi->bdi_stat[i]);
	}

	return err;
}
EXPORT_SYMBOL(bdi_init);

void bdi_destroy(struct backing_dev_info *bdi)
{
	int i;

	bdi_unregister(bdi);

	for (i = 0; i < NR_BDI_STAT_ITEMS; i++)
		percpu_counter_destroy(&bdi->bdi_stat[i]);

	prop_local_destroy_percpu(&bdi->completions);
}
EXPORT_SYMBOL(bdi_destroy);

static wait_queue_head_t congestion_wqh[2] = {
		__WAIT_QUEUE_HEAD_INITIALIZER(congestion_wqh[0]),
		__WAIT_QUEUE_HEAD_INITIALIZER(congestion_wqh[1])
	};


void clear_bdi_congested(struct backing_dev_info *bdi, int rw)
{
	enum bdi_state bit;
	wait_queue_head_t *wqh = &congestion_wqh[rw];

	bit = (rw == WRITE) ? BDI_write_congested : BDI_read_congested;
	clear_bit(bit, &bdi->state);
	smp_mb__after_clear_bit();
	if (waitqueue_active(wqh))
		wake_up(wqh);
}
EXPORT_SYMBOL(clear_bdi_congested);

void set_bdi_congested(struct backing_dev_info *bdi, int rw)
{
	enum bdi_state bit;

	bit = (rw == WRITE) ? BDI_write_congested : BDI_read_congested;
	set_bit(bit, &bdi->state);
}
EXPORT_SYMBOL(set_bdi_congested);

/**
 * congestion_wait - wait for a backing_dev to become uncongested
 * @rw: READ or WRITE
 * @timeout: timeout in jiffies
 *
 * Waits for up to @timeout jiffies for a backing_dev (any backing_dev) to exit
 * write congestion.  If no backing_devs are congested then just wait for the
 * next write to be completed.
 */
long congestion_wait(int rw, long timeout)
{
	long ret;
	DEFINE_WAIT(wait);
	wait_queue_head_t *wqh = &congestion_wqh[rw];

	prepare_to_wait(wqh, &wait, TASK_UNINTERRUPTIBLE);
	ret = io_schedule_timeout(timeout);
	finish_wait(wqh, &wait);
	return ret;
}
EXPORT_SYMBOL(congestion_wait);

