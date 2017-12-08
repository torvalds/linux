/*
 * SPDX-License-Identifier: GPL-2.0
 * Mainly for aufs which mmap(2) different file and wants to print different
 * path in /proc/PID/maps.
 * Call these functions via macros defined in linux/mm.h.
 *
 * See Documentation/filesystems/aufs/design/06mmap.txt
 *
 * Copyright (c) 2014-2017 Junjro R. Okajima
 * Copyright (c) 2014 Ian Campbell
 */

#include <linux/mm.h>
#include <linux/file.h>
#include <linux/fs.h>

/* #define PRFILE_TRACE */
static inline void prfile_trace(struct file *f, struct file *pr,
			      const char func[], int line, const char func2[])
{
#ifdef PRFILE_TRACE
	if (pr)
		pr_info("%s:%d: %s, %pD2\n", func, line, func2, f);
#endif
}

void vma_do_file_update_time(struct vm_area_struct *vma, const char func[],
			     int line)
{
	struct file *f = vma->vm_file, *pr = vma->vm_prfile;

	prfile_trace(f, pr, func, line, __func__);
	file_update_time(f);
	if (f && pr)
		file_update_time(pr);
}

struct file *vma_do_pr_or_file(struct vm_area_struct *vma, const char func[],
			       int line)
{
	struct file *f = vma->vm_file, *pr = vma->vm_prfile;

	prfile_trace(f, pr, func, line, __func__);
	return (f && pr) ? pr : f;
}

void vma_do_get_file(struct vm_area_struct *vma, const char func[], int line)
{
	struct file *f = vma->vm_file, *pr = vma->vm_prfile;

	prfile_trace(f, pr, func, line, __func__);
	get_file(f);
	if (f && pr)
		get_file(pr);
}

void vma_do_fput(struct vm_area_struct *vma, const char func[], int line)
{
	struct file *f = vma->vm_file, *pr = vma->vm_prfile;

	prfile_trace(f, pr, func, line, __func__);
	fput(f);
	if (f && pr)
		fput(pr);
}

#ifndef CONFIG_MMU
struct file *vmr_do_pr_or_file(struct vm_region *region, const char func[],
			       int line)
{
	struct file *f = region->vm_file, *pr = region->vm_prfile;

	prfile_trace(f, pr, func, line, __func__);
	return (f && pr) ? pr : f;
}

void vmr_do_fput(struct vm_region *region, const char func[], int line)
{
	struct file *f = region->vm_file, *pr = region->vm_prfile;

	prfile_trace(f, pr, func, line, __func__);
	fput(f);
	if (f && pr)
		fput(pr);
}
#endif /* !CONFIG_MMU */
