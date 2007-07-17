#ifndef _ASM_FB_H_
#define _ASM_FB_H_

#include <linux/fs.h>
#include <asm/page.h>

static inline void fb_pgprotect(struct file *file, struct vm_area_struct *vma,
				unsigned long off)
{
	pgprot_val(vma->vm_page_prot) |= _PAGE_NO_CACHE;
}

#endif /* _ASM_FB_H_ */
