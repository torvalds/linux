/*
 * RAM Oops/Panic logger
 *
 * Copyright (C) 2010 Marco Stornelli <marco.stornelli@gmail.com>
 * Copyright (C) 2011 Kees Cook <keescook@chromium.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/pstore.h>
#include <linux/time.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/pstore_ram.h>

#define RAMOOPS_KERNMSG_HDR "===="
#define MIN_MEM_SIZE 4096UL

static ulong record_size = MIN_MEM_SIZE;
module_param(record_size, ulong, 0400);
MODULE_PARM_DESC(record_size,
		"size of each dump done on oops/panic");

static ulong mem_address;
module_param(mem_address, ulong, 0400);
MODULE_PARM_DESC(mem_address,
		"start of reserved RAM used to store oops/panic logs");

static ulong mem_size;
module_param(mem_size, ulong, 0400);
MODULE_PARM_DESC(mem_size,
		"size of reserved RAM used to store oops/panic logs");

static int dump_oops = 1;
module_param(dump_oops, int, 0600);
MODULE_PARM_DESC(dump_oops,
		"set to 1 to dump oopses, 0 to only dump panics (default 1)");

struct ramoops_context {
	struct persistent_ram_zone **przs;
	phys_addr_t phys_addr;
	unsigned long size;
	size_t record_size;
	int dump_oops;
	unsigned int count;
	unsigned int max_count;
	unsigned int read_count;
	struct pstore_info pstore;
};

static struct platform_device *dummy;
static struct ramoops_platform_data *dummy_data;

static int ramoops_pstore_open(struct pstore_info *psi)
{
	struct ramoops_context *cxt = psi->data;

	cxt->read_count = 0;
	return 0;
}

static ssize_t ramoops_pstore_read(u64 *id, enum pstore_type_id *type,
				   struct timespec *time,
				   char **buf,
				   struct pstore_info *psi)
{
	ssize_t size;
	struct ramoops_context *cxt = psi->data;
	struct persistent_ram_zone *prz;

	if (cxt->read_count >= cxt->max_count)
		return -EINVAL;

	*id = cxt->read_count++;
	prz = cxt->przs[*id];

	/* Only supports dmesg output so far. */
	*type = PSTORE_TYPE_DMESG;
	/* TODO(kees): Bogus time for the moment. */
	time->tv_sec = 0;
	time->tv_nsec = 0;

	size = persistent_ram_old_size(prz);
	*buf = kmalloc(size, GFP_KERNEL);
	if (*buf == NULL)
		return -ENOMEM;
	memcpy(*buf, persistent_ram_old(prz), size);

	return size;
}

static size_t ramoops_write_kmsg_hdr(struct persistent_ram_zone *prz)
{
	char *hdr;
	struct timeval timestamp;
	size_t len;

	do_gettimeofday(&timestamp);
	hdr = kasprintf(GFP_ATOMIC, RAMOOPS_KERNMSG_HDR "%lu.%lu\n",
		(long)timestamp.tv_sec, (long)timestamp.tv_usec);
	WARN_ON_ONCE(!hdr);
	len = hdr ? strlen(hdr) : 0;
	persistent_ram_write(prz, hdr, len);
	kfree(hdr);

	return len;
}

static int ramoops_pstore_write(enum pstore_type_id type,
				enum kmsg_dump_reason reason,
				u64 *id,
				unsigned int part,
				size_t size, struct pstore_info *psi)
{
	struct ramoops_context *cxt = psi->data;
	struct persistent_ram_zone *prz = cxt->przs[cxt->count];
	size_t hlen;

	/* Currently ramoops is designed to only store dmesg dumps. */
	if (type != PSTORE_TYPE_DMESG)
		return -EINVAL;

	/* Out of the various dmesg dump types, ramoops is currently designed
	 * to only store crash logs, rather than storing general kernel logs.
	 */
	if (reason != KMSG_DUMP_OOPS &&
	    reason != KMSG_DUMP_PANIC)
		return -EINVAL;

	/* Skip Oopes when configured to do so. */
	if (reason == KMSG_DUMP_OOPS && !cxt->dump_oops)
		return -EINVAL;

	/* Explicitly only take the first part of any new crash.
	 * If our buffer is larger than kmsg_bytes, this can never happen,
	 * and if our buffer is smaller than kmsg_bytes, we don't want the
	 * report split across multiple records.
	 */
	if (part != 1)
		return -ENOSPC;

	hlen = ramoops_write_kmsg_hdr(prz);
	if (size + hlen > prz->buffer_size)
		size = prz->buffer_size - hlen;
	persistent_ram_write(prz, cxt->pstore.buf, size);

	cxt->count = (cxt->count + 1) % cxt->max_count;

	return 0;
}

static int ramoops_pstore_erase(enum pstore_type_id type, u64 id,
				struct pstore_info *psi)
{
	struct ramoops_context *cxt = psi->data;

	if (id >= cxt->max_count)
		return -EINVAL;

	persistent_ram_free_old(cxt->przs[id]);

	return 0;
}

static struct ramoops_context oops_cxt = {
	.pstore = {
		.owner	= THIS_MODULE,
		.name	= "ramoops",
		.open	= ramoops_pstore_open,
		.read	= ramoops_pstore_read,
		.write	= ramoops_pstore_write,
		.erase	= ramoops_pstore_erase,
	},
};

static int __init ramoops_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ramoops_platform_data *pdata = pdev->dev.platform_data;
	struct ramoops_context *cxt = &oops_cxt;
	int err = -EINVAL;
	int i;

	/* Only a single ramoops area allowed at a time, so fail extra
	 * probes.
	 */
	if (cxt->max_count)
		goto fail_out;

	if (!pdata->mem_size || !pdata->record_size) {
		pr_err("The memory size and the record size must be "
			"non-zero\n");
		goto fail_out;
	}

	pdata->mem_size = rounddown_pow_of_two(pdata->mem_size);
	pdata->record_size = rounddown_pow_of_two(pdata->record_size);

	/* Check for the minimum memory size */
	if (pdata->mem_size < MIN_MEM_SIZE &&
			pdata->record_size < MIN_MEM_SIZE) {
		pr_err("memory size too small, minimum is %lu\n",
			MIN_MEM_SIZE);
		goto fail_out;
	}

	if (pdata->mem_size < pdata->record_size) {
		pr_err("The memory size must be larger than the "
			"records size\n");
		goto fail_out;
	}

	cxt->max_count = pdata->mem_size / pdata->record_size;
	cxt->count = 0;
	cxt->size = pdata->mem_size;
	cxt->phys_addr = pdata->mem_address;
	cxt->record_size = pdata->record_size;
	cxt->dump_oops = pdata->dump_oops;

	cxt->przs = kzalloc(sizeof(*cxt->przs) * cxt->max_count, GFP_KERNEL);
	if (!cxt->przs) {
		err = -ENOMEM;
		dev_err(dev, "failed to initialize a prz array\n");
		goto fail_out;
	}

	for (i = 0; i < cxt->max_count; i++) {
		size_t sz = cxt->record_size;
		phys_addr_t start = cxt->phys_addr + sz * i;

		cxt->przs[i] = persistent_ram_new(start, sz, 0);
		if (IS_ERR(cxt->przs[i])) {
			err = PTR_ERR(cxt->przs[i]);
			dev_err(dev, "failed to request mem region (0x%zx@0x%llx): %d\n",
				sz, (unsigned long long)start, err);
			goto fail_przs;
		}
	}

	cxt->pstore.data = cxt;
	cxt->pstore.bufsize = cxt->przs[0]->buffer_size;
	cxt->pstore.buf = kmalloc(cxt->pstore.bufsize, GFP_KERNEL);
	spin_lock_init(&cxt->pstore.buf_lock);
	if (!cxt->pstore.buf) {
		pr_err("cannot allocate pstore buffer\n");
		goto fail_clear;
	}

	err = pstore_register(&cxt->pstore);
	if (err) {
		pr_err("registering with pstore failed\n");
		goto fail_buf;
	}

	/*
	 * Update the module parameter variables as well so they are visible
	 * through /sys/module/ramoops/parameters/
	 */
	mem_size = pdata->mem_size;
	mem_address = pdata->mem_address;
	record_size = pdata->record_size;
	dump_oops = pdata->dump_oops;

	pr_info("attached 0x%lx@0x%llx (%ux0x%zx)\n",
		cxt->size, (unsigned long long)cxt->phys_addr,
		cxt->max_count, cxt->record_size);

	return 0;

fail_buf:
	kfree(cxt->pstore.buf);
fail_clear:
	cxt->pstore.bufsize = 0;
	cxt->max_count = 0;
fail_przs:
	for (i = 0; cxt->przs[i]; i++)
		persistent_ram_free(cxt->przs[i]);
	kfree(cxt->przs);
fail_out:
	return err;
}

static int __exit ramoops_remove(struct platform_device *pdev)
{
#if 0
	/* TODO(kees): We cannot unload ramoops since pstore doesn't support
	 * unregistering yet.
	 */
	struct ramoops_context *cxt = &oops_cxt;

	iounmap(cxt->virt_addr);
	release_mem_region(cxt->phys_addr, cxt->size);
	cxt->max_count = 0;

	/* TODO(kees): When pstore supports unregistering, call it here. */
	kfree(cxt->pstore.buf);
	cxt->pstore.bufsize = 0;

	return 0;
#endif
	return -EBUSY;
}

static struct platform_driver ramoops_driver = {
	.remove		= __exit_p(ramoops_remove),
	.driver		= {
		.name	= "ramoops",
		.owner	= THIS_MODULE,
	},
};

static int __init ramoops_init(void)
{
	int ret;
	ret = platform_driver_probe(&ramoops_driver, ramoops_probe);
	if (ret == -ENODEV) {
		/*
		 * If we didn't find a platform device, we use module parameters
		 * building platform data on the fly.
		 */
		pr_info("platform device not found, using module parameters\n");
		dummy_data = kzalloc(sizeof(struct ramoops_platform_data),
				     GFP_KERNEL);
		if (!dummy_data)
			return -ENOMEM;
		dummy_data->mem_size = mem_size;
		dummy_data->mem_address = mem_address;
		dummy_data->record_size = record_size;
		dummy_data->dump_oops = dump_oops;
		dummy = platform_create_bundle(&ramoops_driver, ramoops_probe,
			NULL, 0, dummy_data,
			sizeof(struct ramoops_platform_data));

		if (IS_ERR(dummy))
			ret = PTR_ERR(dummy);
		else
			ret = 0;
	}

	return ret;
}

static void __exit ramoops_exit(void)
{
	platform_driver_unregister(&ramoops_driver);
	kfree(dummy_data);
}

module_init(ramoops_init);
module_exit(ramoops_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marco Stornelli <marco.stornelli@gmail.com>");
MODULE_DESCRIPTION("RAM Oops/Panic logger/driver");
