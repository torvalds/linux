/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_GENERIC_FB_H_
#define __ASM_GENERIC_FB_H_

/*
 * Only include this header file from your architecture's <asm/fb.h>.
 */

#include <linux/mm_types.h>
#include <linux/pgtable.h>

struct fb_info;
struct file;

#ifndef fb_pgprotect
#define fb_pgprotect fb_pgprotect
static inline void fb_pgprotect(struct file *file, struct vm_area_struct *vma,
				unsigned long off)
{
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
}
#endif

#ifndef fb_is_primary_device
#define fb_is_primary_device fb_is_primary_device
static inline int fb_is_primary_device(struct fb_info *info)
{
	return 0;
}
#endif

#endif /* __ASM_GENERIC_FB_H_ */
