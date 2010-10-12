/*
 * Sample fifo dma implementation
 *
 * Copyright (C) 2010 Stefani Seibold <stefani@seibold.net>
 *
 * Released under the GPL version 2 only.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kfifo.h>

/*
 * This module shows how to handle fifo dma operations.
 */

/* fifo size in elements (bytes) */
#define FIFO_SIZE	32

static struct kfifo fifo;

static int __init example_init(void)
{
	int			i;
	unsigned int		ret;
	struct scatterlist	sg[10];

	printk(KERN_INFO "DMA fifo test start\n");

	if (kfifo_alloc(&fifo, FIFO_SIZE, GFP_KERNEL)) {
		printk(KERN_ERR "error kfifo_alloc\n");
		return 1;
	}

	printk(KERN_INFO "queue size: %u\n", kfifo_size(&fifo));

	kfifo_in(&fifo, "test", 4);

	for (i = 0; i != 9; i++)
		kfifo_put(&fifo, &i);

	/* kick away first byte */
	ret = kfifo_get(&fifo, &i);

	printk(KERN_INFO "queue len: %u\n", kfifo_len(&fifo));

	ret = kfifo_dma_in_prepare(&fifo, sg, ARRAY_SIZE(sg), FIFO_SIZE);
	printk(KERN_INFO "DMA sgl entries: %d\n", ret);

	/* if 0 was returned, fifo is full and no sgl was created */
	if (ret) {
		printk(KERN_INFO "scatterlist for receive:\n");
		for (i = 0; i < ARRAY_SIZE(sg); i++) {
			printk(KERN_INFO
			"sg[%d] -> "
			"page_link 0x%.8lx offset 0x%.8x length 0x%.8x\n",
				i, sg[i].page_link, sg[i].offset, sg[i].length);

			if (sg_is_last(&sg[i]))
				break;
		}

		/* but here your code to setup and exectute the dma operation */
		/* ... */

		/* example: zero bytes received */
		ret = 0;

		/* finish the dma operation and update the received data */
		kfifo_dma_in_finish(&fifo, ret);
	}

	ret = kfifo_dma_out_prepare(&fifo, sg, ARRAY_SIZE(sg), 8);
	printk(KERN_INFO "DMA sgl entries: %d\n", ret);

	/* if 0 was returned, no data was available and no sgl was created */
	if (ret) {
		printk(KERN_INFO "scatterlist for transmit:\n");
		for (i = 0; i < ARRAY_SIZE(sg); i++) {
			printk(KERN_INFO
			"sg[%d] -> "
			"page_link 0x%.8lx offset 0x%.8x length 0x%.8x\n",
				i, sg[i].page_link, sg[i].offset, sg[i].length);

			if (sg_is_last(&sg[i]))
				break;
		}

		/* but here your code to setup and exectute the dma operation */
		/* ... */

		/* example: 5 bytes transmitted */
		ret = 5;

		/* finish the dma operation and update the transmitted data */
		kfifo_dma_out_finish(&fifo, ret);
	}

	printk(KERN_INFO "queue len: %u\n", kfifo_len(&fifo));

	return 0;
}

static void __exit example_exit(void)
{
#ifdef DYNAMIC
	kfifo_free(&test);
#endif
}

module_init(example_init);
module_exit(example_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Stefani Seibold <stefani@seibold.net>");
