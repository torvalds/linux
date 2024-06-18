#include <linux/device.h>
#include <linux/dma-map-ops.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/module.h>

#define NR_TESTS (100)

struct dma_pool_pair {
	dma_addr_t dma;
	void *v;
};

struct dmapool_parms {
	size_t size;
	size_t align;
	size_t boundary;
};

static const struct dmapool_parms pool_parms[] = {
	{ .size = 16, .align = 16, .boundary = 0 },
	{ .size = 64, .align = 64, .boundary = 0 },
	{ .size = 256, .align = 256, .boundary = 0 },
	{ .size = 1024, .align = 1024, .boundary = 0 },
	{ .size = 4096, .align = 4096, .boundary = 0 },
	{ .size = 68, .align = 32, .boundary = 4096 },
};

static struct dma_pool *pool;
static struct device test_dev;
static u64 dma_mask;

static inline int nr_blocks(int size)
{
	return clamp_t(int, (PAGE_SIZE / size) * 512, 1024, 8192);
}

static int dmapool_test_alloc(struct dma_pool_pair *p, int blocks)
{
	int i;

	for (i = 0; i < blocks; i++) {
		p[i].v = dma_pool_alloc(pool, GFP_KERNEL,
					&p[i].dma);
		if (!p[i].v)
			goto pool_fail;
	}

	for (i = 0; i < blocks; i++)
		dma_pool_free(pool, p[i].v, p[i].dma);

	return 0;

pool_fail:
	for (--i; i >= 0; i--)
		dma_pool_free(pool, p[i].v, p[i].dma);
	return -ENOMEM;
}

static int dmapool_test_block(const struct dmapool_parms *parms)
{
	int blocks = nr_blocks(parms->size);
	ktime_t start_time, end_time;
	struct dma_pool_pair *p;
	int i, ret;

	p = kcalloc(blocks, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	pool = dma_pool_create("test pool", &test_dev, parms->size,
			       parms->align, parms->boundary);
	if (!pool) {
		ret = -ENOMEM;
		goto free_pairs;
	}

	start_time = ktime_get();
	for (i = 0; i < NR_TESTS; i++) {
		ret = dmapool_test_alloc(p, blocks);
		if (ret)
			goto free_pool;
		if (need_resched())
			cond_resched();
	}
	end_time = ktime_get();

	printk("dmapool test: size:%-4zu align:%-4zu blocks:%-4d time:%llu\n",
		parms->size, parms->align, blocks,
		ktime_us_delta(end_time, start_time));

free_pool:
	dma_pool_destroy(pool);
free_pairs:
	kfree(p);
	return ret;
}

static void dmapool_test_release(struct device *dev)
{
}

static int dmapool_checks(void)
{
	int i, ret;

	ret = dev_set_name(&test_dev, "dmapool-test");
	if (ret)
		return ret;

	ret = device_register(&test_dev);
	if (ret) {
		printk("%s: register failed:%d\n", __func__, ret);
		goto put_device;
	}

	test_dev.release = dmapool_test_release;
	set_dma_ops(&test_dev, NULL);
	test_dev.dma_mask = &dma_mask;
	ret = dma_set_mask_and_coherent(&test_dev, DMA_BIT_MASK(64));
	if (ret) {
		printk("%s: mask failed:%d\n", __func__, ret);
		goto del_device;
	}

	for (i = 0; i < ARRAY_SIZE(pool_parms); i++) {
		ret = dmapool_test_block(&pool_parms[i]);
		if (ret)
			break;
	}

del_device:
	device_del(&test_dev);
put_device:
	put_device(&test_dev);
	return ret;
}

static void dmapool_exit(void)
{
}

module_init(dmapool_checks);
module_exit(dmapool_exit);
MODULE_LICENSE("GPL");
