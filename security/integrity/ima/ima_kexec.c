// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 IBM Corporation
 *
 * Authors:
 * Thiago Jung Bauermann <bauerman@linux.vnet.ibm.com>
 * Mimi Zohar <zohar@linux.vnet.ibm.com>
 */

#include <linux/seq_file.h>
#include <linux/vmalloc.h>
#include <linux/kexec.h>
#include <linux/of.h>
#include <linux/ima.h>
#include "ima.h"

#ifdef CONFIG_IMA_KEXEC
static int ima_dump_measurement_list(unsigned long *buffer_size, void **buffer,
				     unsigned long segment_size)
{
	struct ima_queue_entry *qe;
	struct seq_file file;
	struct ima_kexec_hdr khdr;
	int ret = 0;

	/* segment size can't change between kexec load and execute */
	file.buf = vmalloc(segment_size);
	if (!file.buf) {
		ret = -ENOMEM;
		goto out;
	}

	file.size = segment_size;
	file.read_pos = 0;
	file.count = sizeof(khdr);	/* reserved space */

	memset(&khdr, 0, sizeof(khdr));
	khdr.version = 1;
	list_for_each_entry_rcu(qe, &ima_measurements, later) {
		if (file.count < file.size) {
			khdr.count++;
			ima_measurements_show(&file, qe);
		} else {
			ret = -EINVAL;
			break;
		}
	}

	if (ret < 0)
		goto out;

	/*
	 * fill in reserved space with some buffer details
	 * (eg. version, buffer size, number of measurements)
	 */
	khdr.buffer_size = file.count;
	if (ima_canonical_fmt) {
		khdr.version = cpu_to_le16(khdr.version);
		khdr.count = cpu_to_le64(khdr.count);
		khdr.buffer_size = cpu_to_le64(khdr.buffer_size);
	}
	memcpy(file.buf, &khdr, sizeof(khdr));

	print_hex_dump_debug("ima dump: ", DUMP_PREFIX_NONE, 16, 1,
			     file.buf, file.count < 100 ? file.count : 100,
			     true);

	*buffer_size = file.count;
	*buffer = file.buf;
out:
	if (ret == -EINVAL)
		vfree(file.buf);
	return ret;
}

/*
 * Called during kexec_file_load so that IMA can add a segment to the kexec
 * image for the measurement list for the next kernel.
 *
 * This function assumes that kexec_lock is held.
 */
void ima_add_kexec_buffer(struct kimage *image)
{
	struct kexec_buf kbuf = { .image = image, .buf_align = PAGE_SIZE,
				  .buf_min = 0, .buf_max = ULONG_MAX,
				  .top_down = true };
	unsigned long binary_runtime_size;

	/* use more understandable variable names than defined in kbuf */
	void *kexec_buffer = NULL;
	size_t kexec_buffer_size;
	size_t kexec_segment_size;
	int ret;

	/*
	 * Reserve an extra half page of memory for additional measurements
	 * added during the kexec load.
	 */
	binary_runtime_size = ima_get_binary_runtime_size();
	if (binary_runtime_size >= ULONG_MAX - PAGE_SIZE)
		kexec_segment_size = ULONG_MAX;
	else
		kexec_segment_size = ALIGN(ima_get_binary_runtime_size() +
					   PAGE_SIZE / 2, PAGE_SIZE);
	if ((kexec_segment_size == ULONG_MAX) ||
	    ((kexec_segment_size >> PAGE_SHIFT) > totalram_pages() / 2)) {
		pr_err("Binary measurement list too large.\n");
		return;
	}

	ima_dump_measurement_list(&kexec_buffer_size, &kexec_buffer,
				  kexec_segment_size);
	if (!kexec_buffer) {
		pr_err("Not enough memory for the kexec measurement buffer.\n");
		return;
	}

	kbuf.buffer = kexec_buffer;
	kbuf.bufsz = kexec_buffer_size;
	kbuf.memsz = kexec_segment_size;
	ret = kexec_add_buffer(&kbuf);
	if (ret) {
		pr_err("Error passing over kexec measurement buffer.\n");
		vfree(kexec_buffer);
		return;
	}

	image->ima_buffer_addr = kbuf.mem;
	image->ima_buffer_size = kexec_segment_size;
	image->ima_buffer = kexec_buffer;

	pr_debug("kexec measurement buffer for the loaded kernel at 0x%lx.\n",
		 kbuf.mem);
}
#endif /* IMA_KEXEC */

/*
 * Restore the measurement list from the previous kernel.
 */
void __init ima_load_kexec_buffer(void)
{
	void *kexec_buffer = NULL;
	size_t kexec_buffer_size = 0;
	int rc;

	rc = ima_get_kexec_buffer(&kexec_buffer, &kexec_buffer_size);
	switch (rc) {
	case 0:
		rc = ima_restore_measurement_list(kexec_buffer_size,
						  kexec_buffer);
		if (rc != 0)
			pr_err("Failed to restore the measurement list: %d\n",
				rc);

		ima_free_kexec_buffer();
		break;
	case -ENOTSUPP:
		pr_debug("Restoring the measurement list not supported\n");
		break;
	case -ENOENT:
		pr_debug("No measurement list to restore\n");
		break;
	default:
		pr_debug("Error restoring the measurement list: %d\n", rc);
	}
}
