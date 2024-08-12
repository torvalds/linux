/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2015 Intel Deutschland GmbH
 */
#ifndef __DEVCOREDUMP_H
#define __DEVCOREDUMP_H

#include <linux/device.h>
#include <linux/module.h>
#include <linux/vmalloc.h>

#include <linux/scatterlist.h>
#include <linux/slab.h>

/* if data isn't read by userspace after 5 minutes then delete it */
#define DEVCD_TIMEOUT	(HZ * 60 * 5)

/*
 * _devcd_free_sgtable - free all the memory of the given scatterlist table
 * (i.e. both pages and scatterlist instances)
 * NOTE: if two tables allocated and chained using the sg_chain function then
 * this function should be called only once on the first table
 * @table: pointer to sg_table to free
 */
static inline void _devcd_free_sgtable(struct scatterlist *table)
{
	int i;
	struct page *page;
	struct scatterlist *iter;
	struct scatterlist *delete_iter;

	/* free pages */
	iter = table;
	for_each_sg(table, iter, sg_nents(table), i) {
		page = sg_page(iter);
		if (page)
			__free_page(page);
	}

	/* then free all chained tables */
	iter = table;
	delete_iter = table;	/* always points on a head of a table */
	while (!sg_is_last(iter)) {
		iter++;
		if (sg_is_chain(iter)) {
			iter = sg_chain_ptr(iter);
			kfree(delete_iter);
			delete_iter = iter;
		}
	}

	/* free the last table */
	kfree(delete_iter);
}

#ifdef CONFIG_DEV_COREDUMP
void dev_coredumpv(struct device *dev, void *data, size_t datalen,
		   gfp_t gfp);

void dev_coredumpm_timeout(struct device *dev, struct module *owner,
			   void *data, size_t datalen, gfp_t gfp,
			   ssize_t (*read)(char *buffer, loff_t offset,
					   size_t count, void *data,
					   size_t datalen),
			   void (*free)(void *data),
			   unsigned long timeout);

void dev_coredumpsg(struct device *dev, struct scatterlist *table,
		    size_t datalen, gfp_t gfp);

void dev_coredump_put(struct device *dev);
#else
static inline void dev_coredumpv(struct device *dev, void *data,
				 size_t datalen, gfp_t gfp)
{
	vfree(data);
}

static inline void
dev_coredumpm_timeout(struct device *dev, struct module *owner,
		      void *data, size_t datalen, gfp_t gfp,
		      ssize_t (*read)(char *buffer, loff_t offset,
				      size_t count, void *data,
				      size_t datalen),
		      void (*free)(void *data),
		      unsigned long timeout)
{
	free(data);
}

static inline void dev_coredumpsg(struct device *dev, struct scatterlist *table,
				  size_t datalen, gfp_t gfp)
{
	_devcd_free_sgtable(table);
}
static inline void dev_coredump_put(struct device *dev)
{
}
#endif /* CONFIG_DEV_COREDUMP */

/**
 * dev_coredumpm - create device coredump with read/free methods
 * @dev: the struct device for the crashed device
 * @owner: the module that contains the read/free functions, use %THIS_MODULE
 * @data: data cookie for the @read/@free functions
 * @datalen: length of the data
 * @gfp: allocation flags
 * @read: function to read from the given buffer
 * @free: function to free the given buffer
 *
 * Creates a new device coredump for the given device. If a previous one hasn't
 * been read yet, the new coredump is discarded. The data lifetime is determined
 * by the device coredump framework and when it is no longer needed the @free
 * function will be called to free the data.
 */
static inline void dev_coredumpm(struct device *dev, struct module *owner,
				 void *data, size_t datalen, gfp_t gfp,
				 ssize_t (*read)(char *buffer, loff_t offset, size_t count,
						 void *data, size_t datalen),
				void (*free)(void *data))
{
	dev_coredumpm_timeout(dev, owner, data, datalen, gfp, read, free,
			      DEVCD_TIMEOUT);
}

#endif /* __DEVCOREDUMP_H */
