// SPDX-License-Identifier: GPL-2.0-only
/*
 * Sample fifo dma implementation
 *
 * Copyright (C) 2010 Stefani Seibold <stefani@seibold.net>
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
	unsigned int		nents;
	struct scatterlist	sg[10];

	printk(KERN_INFO "DMA fifo test start\n");

	if (kfifo_alloc(&fifo, FIFO_SIZE, GFP_KERNEL)) {
		printk(KERN_WARNING "error kfifo_alloc\n");
		return -ENOMEM;
	}

	printk(KERN_INFO "queue size: %u\n", kfifo_size(&fifo));

	kfifo_in(&fifo, "test", 4);

	for (i = 0; i != 9; i++)
		kfifo_put(&fifo, i);

	/* kick away first byte */
	kfifo_skip(&fifo);

	printk(KERN_INFO "queue len: %u\n", kfifo_len(&fifo));

	/*
	 * Configure the kfifo buffer to receive data from DMA input.
	 *
	 *  .--------------------------------------.
	 *  | 0 | 1 | 2 | ... | 12 | 13 | ... | 31 |
	 *  |---|------------------|---------------|
	 *   \_/ \________________/ \_____________/
	 *    \          \                  \
	 *     \          \_allocated data   \
	 *      \_*free space*                \_*free space*
	 *
	 * We need two different SG entries: one for the free space area at the
	 * end of the kfifo buffer (19 bytes) and another for the first free
	 * byte at the beginning, after the kfifo_skip().
	 */
	sg_init_table(sg, ARRAY_SIZE(sg));
	nents = kfifo_dma_in_prepare(&fifo, sg, ARRAY_SIZE(sg), FIFO_SIZE);
	printk(KERN_INFO "DMA sgl entries: %d\n", nents);
	if (!nents) {
		/* fifo is full and no sgl was created */
		printk(KERN_WARNING "error kfifo_dma_in_prepare\n");
		return -EIO;
	}

	/* receive data */
	printk(KERN_INFO "scatterlist for receive:\n");
	for (i = 0; i < nents; i++) {
		printk(KERN_INFO
		"sg[%d] -> "
		"page %p offset 0x%.8x length 0x%.8x\n",
			i, sg_page(&sg[i]), sg[i].offset, sg[i].length);

		if (sg_is_last(&sg[i]))
			break;
	}

	/* put here your code to setup and exectute the dma operation */
	/* ... */

	/* example: zero bytes received */
	ret = 0;

	/* finish the dma operation and update the received data */
	kfifo_dma_in_finish(&fifo, ret);

	/* Prepare to transmit data, example: 8 bytes */
	nents = kfifo_dma_out_prepare(&fifo, sg, ARRAY_SIZE(sg), 8);
	printk(KERN_INFO "DMA sgl entries: %d\n", nents);
	if (!nents) {
		/* no data was available and no sgl was created */
		printk(KERN_WARNING "error kfifo_dma_out_prepare\n");
		return -EIO;
	}

	printk(KERN_INFO "scatterlist for transmit:\n");
	for (i = 0; i < nents; i++) {
		printk(KERN_INFO
		"sg[%d] -> "
		"page %p offset 0x%.8x length 0x%.8x\n",
			i, sg_page(&sg[i]), sg[i].offset, sg[i].length);

		if (sg_is_last(&sg[i]))
			break;
	}

	/* put here your code to setup and exectute the dma operation */
	/* ... */

	/* example: 5 bytes transmitted */
	ret = 5;

	/* finish the dma operation and update the transmitted data */
	kfifo_dma_out_finish(&fifo, ret);

	ret = kfifo_len(&fifo);
	printk(KERN_INFO "queue len: %u\n", kfifo_len(&fifo));

	if (ret != 7) {
		printk(KERN_WARNING "size mismatch: test failed");
		return -EIO;
	}
	printk(KERN_INFO "test passed\n");

	return 0;
}

static void __exit example_exit(void)
{
	kfifo_free(&fifo);
}

module_init(example_init);
module_exit(example_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Stefani Seibold <stefani@seibold.net>");
