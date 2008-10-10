#ifndef ASM_X86__FB_H
#define ASM_X86__FB_H

#include <linux/fb.h>
#include <linux/fs.h>
#include <asm/page.h>

static inline void fb_pgprotect(struct file *file, struct vm_area_struct *vma,
				unsigned long off)
{
	if (boot_cpu_data.x86 > 3)
		pgprot_val(vma->vm_page_prot) |= _PAGE_PCD;
}

#ifdef CONFIG_X86_32
extern int fb_is_primary_device(struct fb_info *info);
#else
static inline int fb_is_primary_device(struct fb_info *info) { return 0; }
#endif

#endif /* ASM_X86__FB_H */
