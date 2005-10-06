/*
 * linux/kernel/power/swsusp.c
 *
 * This file is to realize architecture-independent
 * machine suspend feature using pretty near only high-level routines
 *
 * Copyright (C) 1998-2001 Gabor Kuti <seasons@fornax.hu>
 * Copyright (C) 1998,2001-2004 Pavel Machek <pavel@suse.cz>
 *
 * This file is released under the GPLv2.
 *
 * I'd like to thank the following people for their work:
 *
 * Pavel Machek <pavel@ucw.cz>:
 * Modifications, defectiveness pointing, being with me at the very beginning,
 * suspend to swap space, stop all tasks. Port to 2.4.18-ac and 2.5.17.
 *
 * Steve Doddi <dirk@loth.demon.co.uk>:
 * Support the possibility of hardware state restoring.
 *
 * Raph <grey.havens@earthling.net>:
 * Support for preserving states of network devices and virtual console
 * (including X and svgatextmode)
 *
 * Kurt Garloff <garloff@suse.de>:
 * Straightened the critical function in order to prevent compilers from
 * playing tricks with local variables.
 *
 * Andreas Mohr <a.mohr@mailto.de>
 *
 * Alex Badea <vampire@go.ro>:
 * Fixed runaway init
 *
 * Andreas Steinmetz <ast@domdv.de>:
 * Added encrypted suspend option
 *
 * More state savers are welcome. Especially for the scsi layer...
 *
 * For TODOs,FIXMEs also look in Documentation/power/swsusp.txt
 */

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/suspend.h>
#include <linux/smp_lock.h>
#include <linux/file.h>
#include <linux/utsname.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/reboot.h>
#include <linux/bitops.h>
#include <linux/vt_kern.h>
#include <linux/kbd_kern.h>
#include <linux/keyboard.h>
#include <linux/spinlock.h>
#include <linux/genhd.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/swap.h>
#include <linux/pm.h>
#include <linux/device.h>
#include <linux/buffer_head.h>
#include <linux/swapops.h>
#include <linux/bootmem.h>
#include <linux/syscalls.h>
#include <linux/console.h>
#include <linux/highmem.h>
#include <linux/bio.h>
#include <linux/mount.h>

#include <asm/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/io.h>

#include <linux/random.h>
#include <linux/crypto.h>
#include <asm/scatterlist.h>

#include "power.h"

#define CIPHER "aes"
#define MAXKEY 32
#define MAXIV  32

/* References to section boundaries */
extern const void __nosave_begin, __nosave_end;

/* Variables to be preserved over suspend */
static int nr_copy_pages_check;

extern char resume_file[];

/* Local variables that should not be affected by save */
static unsigned int nr_copy_pages __nosavedata = 0;

/* Suspend pagedir is allocated before final copy, therefore it
   must be freed after resume

   Warning: this is evil. There are actually two pagedirs at time of
   resume. One is "pagedir_save", which is empty frame allocated at
   time of suspend, that must be freed. Second is "pagedir_nosave",
   allocated at time of resume, that travels through memory not to
   collide with anything.

   Warning: this is even more evil than it seems. Pagedirs this file
   talks about are completely different from page directories used by
   MMU hardware.
 */
suspend_pagedir_t *pagedir_nosave __nosavedata = NULL;
static suspend_pagedir_t *pagedir_save;

#define SWSUSP_SIG	"S1SUSPEND"

static struct swsusp_header {
	char reserved[PAGE_SIZE - 20 - MAXKEY - MAXIV - sizeof(swp_entry_t)];
	u8 key_iv[MAXKEY+MAXIV];
	swp_entry_t swsusp_info;
	char	orig_sig[10];
	char	sig[10];
} __attribute__((packed, aligned(PAGE_SIZE))) swsusp_header;

static struct swsusp_info swsusp_info;

/*
 * XXX: We try to keep some more pages free so that I/O operations succeed
 * without paging. Might this be more?
 */
#define PAGES_FOR_IO	512

/*
 * Saving part...
 */

/* We memorize in swapfile_used what swap devices are used for suspension */
#define SWAPFILE_UNUSED    0
#define SWAPFILE_SUSPEND   1	/* This is the suspending device */
#define SWAPFILE_IGNORED   2	/* Those are other swap devices ignored for suspension */

static unsigned short swapfile_used[MAX_SWAPFILES];
static unsigned short root_swap;

static int write_page(unsigned long addr, swp_entry_t * loc);
static int bio_read_page(pgoff_t page_off, void * page);

static u8 key_iv[MAXKEY+MAXIV];

#ifdef CONFIG_SWSUSP_ENCRYPT

static int crypto_init(int mode, void **mem)
{
	int error = 0;
	int len;
	char *modemsg;
	struct crypto_tfm *tfm;

	modemsg = mode ? "suspend not possible" : "resume not possible";

	tfm = crypto_alloc_tfm(CIPHER, CRYPTO_TFM_MODE_CBC);
	if(!tfm) {
		printk(KERN_ERR "swsusp: no tfm, %s\n", modemsg);
		error = -EINVAL;
		goto out;
	}

	if(MAXKEY < crypto_tfm_alg_min_keysize(tfm)) {
		printk(KERN_ERR "swsusp: key buffer too small, %s\n", modemsg);
		error = -ENOKEY;
		goto fail;
	}

	if (mode)
		get_random_bytes(key_iv, MAXKEY+MAXIV);

	len = crypto_tfm_alg_max_keysize(tfm);
	if (len > MAXKEY)
		len = MAXKEY;

	if (crypto_cipher_setkey(tfm, key_iv, len)) {
		printk(KERN_ERR "swsusp: key setup failure, %s\n", modemsg);
		error = -EKEYREJECTED;
		goto fail;
	}

	len = crypto_tfm_alg_ivsize(tfm);

	if (MAXIV < len) {
		printk(KERN_ERR "swsusp: iv buffer too small, %s\n", modemsg);
		error = -EOVERFLOW;
		goto fail;
	}

	crypto_cipher_set_iv(tfm, key_iv+MAXKEY, len);

	*mem=(void *)tfm;

	goto out;

fail:	crypto_free_tfm(tfm);
out:	return error;
}

static __inline__ void crypto_exit(void *mem)
{
	crypto_free_tfm((struct crypto_tfm *)mem);
}

static __inline__ int crypto_write(struct pbe *p, void *mem)
{
	int error = 0;
	struct scatterlist src, dst;

	src.page   = virt_to_page(p->address);
	src.offset = 0;
	src.length = PAGE_SIZE;
	dst.page   = virt_to_page((void *)&swsusp_header);
	dst.offset = 0;
	dst.length = PAGE_SIZE;

	error = crypto_cipher_encrypt((struct crypto_tfm *)mem, &dst, &src,
					PAGE_SIZE);

	if (!error)
		error = write_page((unsigned long)&swsusp_header,
				&(p->swap_address));
	return error;
}

static __inline__ int crypto_read(struct pbe *p, void *mem)
{
	int error = 0;
	struct scatterlist src, dst;

	error = bio_read_page(swp_offset(p->swap_address), (void *)p->address);
	if (!error) {
		src.offset = 0;
		src.length = PAGE_SIZE;
		dst.offset = 0;
		dst.length = PAGE_SIZE;
		src.page = dst.page = virt_to_page((void *)p->address);

		error = crypto_cipher_decrypt((struct crypto_tfm *)mem, &dst,
						&src, PAGE_SIZE);
	}
	return error;
}
#else
static __inline__ int crypto_init(int mode, void *mem)
{
	return 0;
}

static __inline__ void crypto_exit(void *mem)
{
}

static __inline__ int crypto_write(struct pbe *p, void *mem)
{
	return write_page(p->address, &(p->swap_address));
}

static __inline__ int crypto_read(struct pbe *p, void *mem)
{
	return bio_read_page(swp_offset(p->swap_address), (void *)p->address);
}
#endif

static int mark_swapfiles(swp_entry_t prev)
{
	int error;

	rw_swap_page_sync(READ,
			  swp_entry(root_swap, 0),
			  virt_to_page((unsigned long)&swsusp_header));
	if (!memcmp("SWAP-SPACE",swsusp_header.sig, 10) ||
	    !memcmp("SWAPSPACE2",swsusp_header.sig, 10)) {
		memcpy(swsusp_header.orig_sig,swsusp_header.sig, 10);
		memcpy(swsusp_header.sig,SWSUSP_SIG, 10);
		memcpy(swsusp_header.key_iv, key_iv, MAXKEY+MAXIV);
		swsusp_header.swsusp_info = prev;
		error = rw_swap_page_sync(WRITE,
					  swp_entry(root_swap, 0),
					  virt_to_page((unsigned long)
						       &swsusp_header));
	} else {
		pr_debug("swsusp: Partition is not swap space.\n");
		error = -ENODEV;
	}
	return error;
}

/*
 * Check whether the swap device is the specified resume
 * device, irrespective of whether they are specified by
 * identical names.
 *
 * (Thus, device inode aliasing is allowed.  You can say /dev/hda4
 * instead of /dev/ide/host0/bus0/target0/lun0/part4 [if using devfs]
 * and they'll be considered the same device.  This is *necessary* for
 * devfs, since the resume code can only recognize the form /dev/hda4,
 * but the suspend code would see the long name.)
 */
static int is_resume_device(const struct swap_info_struct *swap_info)
{
	struct file *file = swap_info->swap_file;
	struct inode *inode = file->f_dentry->d_inode;

	return S_ISBLK(inode->i_mode) &&
		swsusp_resume_device == MKDEV(imajor(inode), iminor(inode));
}

static int swsusp_swap_check(void) /* This is called before saving image */
{
	int i, len;

	len=strlen(resume_file);
	root_swap = 0xFFFF;

	spin_lock(&swap_lock);
	for (i=0; i<MAX_SWAPFILES; i++) {
		if (!(swap_info[i].flags & SWP_WRITEOK)) {
			swapfile_used[i]=SWAPFILE_UNUSED;
		} else {
			if (!len) {
	    			printk(KERN_WARNING "resume= option should be used to set suspend device" );
				if (root_swap == 0xFFFF) {
					swapfile_used[i] = SWAPFILE_SUSPEND;
					root_swap = i;
				} else
					swapfile_used[i] = SWAPFILE_IGNORED;
			} else {
	  			/* we ignore all swap devices that are not the resume_file */
				if (is_resume_device(&swap_info[i])) {
					swapfile_used[i] = SWAPFILE_SUSPEND;
					root_swap = i;
				} else {
				  	swapfile_used[i] = SWAPFILE_IGNORED;
				}
			}
		}
	}
	spin_unlock(&swap_lock);
	return (root_swap != 0xffff) ? 0 : -ENODEV;
}

/**
 * This is called after saving image so modification
 * will be lost after resume... and that's what we want.
 * we make the device unusable. A new call to
 * lock_swapdevices can unlock the devices.
 */
static void lock_swapdevices(void)
{
	int i;

	spin_lock(&swap_lock);
	for (i = 0; i< MAX_SWAPFILES; i++)
		if (swapfile_used[i] == SWAPFILE_IGNORED) {
			swap_info[i].flags ^= SWP_WRITEOK;
		}
	spin_unlock(&swap_lock);
}

/**
 *	write_page - Write one page to a fresh swap location.
 *	@addr:	Address we're writing.
 *	@loc:	Place to store the entry we used.
 *
 *	Allocate a new swap entry and 'sync' it. Note we discard -EIO
 *	errors. That is an artifact left over from swsusp. It did not
 *	check the return of rw_swap_page_sync() at all, since most pages
 *	written back to swap would return -EIO.
 *	This is a partial improvement, since we will at least return other
 *	errors, though we need to eventually fix the damn code.
 */
static int write_page(unsigned long addr, swp_entry_t * loc)
{
	swp_entry_t entry;
	int error = 0;

	entry = get_swap_page();
	if (swp_offset(entry) &&
	    swapfile_used[swp_type(entry)] == SWAPFILE_SUSPEND) {
		error = rw_swap_page_sync(WRITE, entry,
					  virt_to_page(addr));
		if (error == -EIO)
			error = 0;
		if (!error)
			*loc = entry;
	} else
		error = -ENOSPC;
	return error;
}

/**
 *	data_free - Free the swap entries used by the saved image.
 *
 *	Walk the list of used swap entries and free each one.
 *	This is only used for cleanup when suspend fails.
 */
static void data_free(void)
{
	swp_entry_t entry;
	struct pbe * p;

	for_each_pbe(p, pagedir_nosave) {
		entry = p->swap_address;
		if (entry.val)
			swap_free(entry);
		else
			break;
	}
}

/**
 *	data_write - Write saved image to swap.
 *
 *	Walk the list of pages in the image and sync each one to swap.
 */
static int data_write(void)
{
	int error = 0, i = 0;
	unsigned int mod = nr_copy_pages / 100;
	struct pbe *p;
	void *tfm;

	if ((error = crypto_init(1, &tfm)))
		return error;

	if (!mod)
		mod = 1;

	printk( "Writing data to swap (%d pages)...     ", nr_copy_pages );
	for_each_pbe (p, pagedir_nosave) {
		if (!(i%mod))
			printk( "\b\b\b\b%3d%%", i / mod );
		if ((error = crypto_write(p, tfm))) {
			crypto_exit(tfm);
			return error;
		}
		i++;
	}
	printk("\b\b\b\bdone\n");
	crypto_exit(tfm);
	return error;
}

static void dump_info(void)
{
	pr_debug(" swsusp: Version: %u\n",swsusp_info.version_code);
	pr_debug(" swsusp: Num Pages: %ld\n",swsusp_info.num_physpages);
	pr_debug(" swsusp: UTS Sys: %s\n",swsusp_info.uts.sysname);
	pr_debug(" swsusp: UTS Node: %s\n",swsusp_info.uts.nodename);
	pr_debug(" swsusp: UTS Release: %s\n",swsusp_info.uts.release);
	pr_debug(" swsusp: UTS Version: %s\n",swsusp_info.uts.version);
	pr_debug(" swsusp: UTS Machine: %s\n",swsusp_info.uts.machine);
	pr_debug(" swsusp: UTS Domain: %s\n",swsusp_info.uts.domainname);
	pr_debug(" swsusp: CPUs: %d\n",swsusp_info.cpus);
	pr_debug(" swsusp: Image: %ld Pages\n",swsusp_info.image_pages);
	pr_debug(" swsusp: Pagedir: %ld Pages\n",swsusp_info.pagedir_pages);
}

static void init_header(void)
{
	memset(&swsusp_info, 0, sizeof(swsusp_info));
	swsusp_info.version_code = LINUX_VERSION_CODE;
	swsusp_info.num_physpages = num_physpages;
	memcpy(&swsusp_info.uts, &system_utsname, sizeof(system_utsname));

	swsusp_info.suspend_pagedir = pagedir_nosave;
	swsusp_info.cpus = num_online_cpus();
	swsusp_info.image_pages = nr_copy_pages;
}

static int close_swap(void)
{
	swp_entry_t entry;
	int error;

	dump_info();
	error = write_page((unsigned long)&swsusp_info, &entry);
	if (!error) {
		printk( "S" );
		error = mark_swapfiles(entry);
		printk( "|\n" );
	}
	return error;
}

/**
 *	free_pagedir_entries - Free pages used by the page directory.
 *
 *	This is used during suspend for error recovery.
 */

static void free_pagedir_entries(void)
{
	int i;

	for (i = 0; i < swsusp_info.pagedir_pages; i++)
		swap_free(swsusp_info.pagedir[i]);
}


/**
 *	write_pagedir - Write the array of pages holding the page directory.
 *	@last:	Last swap entry we write (needed for header).
 */

static int write_pagedir(void)
{
	int error = 0;
	unsigned n = 0;
	struct pbe * pbe;

	printk( "Writing pagedir...");
	for_each_pb_page (pbe, pagedir_nosave) {
		if ((error = write_page((unsigned long)pbe, &swsusp_info.pagedir[n++])))
			return error;
	}

	swsusp_info.pagedir_pages = n;
	printk("done (%u pages)\n", n);
	return error;
}

/**
 *	write_suspend_image - Write entire image and metadata.
 *
 */
static int write_suspend_image(void)
{
	int error;

	init_header();
	if ((error = data_write()))
		goto FreeData;

	if ((error = write_pagedir()))
		goto FreePagedir;

	if ((error = close_swap()))
		goto FreePagedir;
 Done:
	memset(key_iv, 0, MAXKEY+MAXIV);
	return error;
 FreePagedir:
	free_pagedir_entries();
 FreeData:
	data_free();
	goto Done;
}


#ifdef CONFIG_HIGHMEM
struct highmem_page {
	char *data;
	struct page *page;
	struct highmem_page *next;
};

static struct highmem_page *highmem_copy;

static int save_highmem_zone(struct zone *zone)
{
	unsigned long zone_pfn;
	mark_free_pages(zone);
	for (zone_pfn = 0; zone_pfn < zone->spanned_pages; ++zone_pfn) {
		struct page *page;
		struct highmem_page *save;
		void *kaddr;
		unsigned long pfn = zone_pfn + zone->zone_start_pfn;

		if (!(pfn%1000))
			printk(".");
		if (!pfn_valid(pfn))
			continue;
		page = pfn_to_page(pfn);
		/*
		 * This condition results from rvmalloc() sans vmalloc_32()
		 * and architectural memory reservations. This should be
		 * corrected eventually when the cases giving rise to this
		 * are better understood.
		 */
		if (PageReserved(page)) {
			printk("highmem reserved page?!\n");
			continue;
		}
		BUG_ON(PageNosave(page));
		if (PageNosaveFree(page))
			continue;
		save = kmalloc(sizeof(struct highmem_page), GFP_ATOMIC);
		if (!save)
			return -ENOMEM;
		save->next = highmem_copy;
		save->page = page;
		save->data = (void *) get_zeroed_page(GFP_ATOMIC);
		if (!save->data) {
			kfree(save);
			return -ENOMEM;
		}
		kaddr = kmap_atomic(page, KM_USER0);
		memcpy(save->data, kaddr, PAGE_SIZE);
		kunmap_atomic(kaddr, KM_USER0);
		highmem_copy = save;
	}
	return 0;
}
#endif /* CONFIG_HIGHMEM */


static int save_highmem(void)
{
#ifdef CONFIG_HIGHMEM
	struct zone *zone;
	int res = 0;

	pr_debug("swsusp: Saving Highmem\n");
	for_each_zone (zone) {
		if (is_highmem(zone))
			res = save_highmem_zone(zone);
		if (res)
			return res;
	}
#endif
	return 0;
}

static int restore_highmem(void)
{
#ifdef CONFIG_HIGHMEM
	printk("swsusp: Restoring Highmem\n");
	while (highmem_copy) {
		struct highmem_page *save = highmem_copy;
		void *kaddr;
		highmem_copy = save->next;

		kaddr = kmap_atomic(save->page, KM_USER0);
		memcpy(kaddr, save->data, PAGE_SIZE);
		kunmap_atomic(kaddr, KM_USER0);
		free_page((long) save->data);
		kfree(save);
	}
#endif
	return 0;
}


static int pfn_is_nosave(unsigned long pfn)
{
	unsigned long nosave_begin_pfn = __pa(&__nosave_begin) >> PAGE_SHIFT;
	unsigned long nosave_end_pfn = PAGE_ALIGN(__pa(&__nosave_end)) >> PAGE_SHIFT;
	return (pfn >= nosave_begin_pfn) && (pfn < nosave_end_pfn);
}

/**
 *	saveable - Determine whether a page should be cloned or not.
 *	@pfn:	The page
 *
 *	We save a page if it's Reserved, and not in the range of pages
 *	statically defined as 'unsaveable', or if it isn't reserved, and
 *	isn't part of a free chunk of pages.
 */

static int saveable(struct zone * zone, unsigned long * zone_pfn)
{
	unsigned long pfn = *zone_pfn + zone->zone_start_pfn;
	struct page * page;

	if (!pfn_valid(pfn))
		return 0;

	page = pfn_to_page(pfn);
	BUG_ON(PageReserved(page) && PageNosave(page));
	if (PageNosave(page))
		return 0;
	if (PageReserved(page) && pfn_is_nosave(pfn)) {
		pr_debug("[nosave pfn 0x%lx]", pfn);
		return 0;
	}
	if (PageNosaveFree(page))
		return 0;

	return 1;
}

static void count_data_pages(void)
{
	struct zone *zone;
	unsigned long zone_pfn;

	nr_copy_pages = 0;

	for_each_zone (zone) {
		if (is_highmem(zone))
			continue;
		mark_free_pages(zone);
		for (zone_pfn = 0; zone_pfn < zone->spanned_pages; ++zone_pfn)
			nr_copy_pages += saveable(zone, &zone_pfn);
	}
}


static void copy_data_pages(void)
{
	struct zone *zone;
	unsigned long zone_pfn;
	struct pbe * pbe = pagedir_nosave;

	pr_debug("copy_data_pages(): pages to copy: %d\n", nr_copy_pages);
	for_each_zone (zone) {
		if (is_highmem(zone))
			continue;
		mark_free_pages(zone);
		for (zone_pfn = 0; zone_pfn < zone->spanned_pages; ++zone_pfn) {
			if (saveable(zone, &zone_pfn)) {
				struct page * page;
				page = pfn_to_page(zone_pfn + zone->zone_start_pfn);
				BUG_ON(!pbe);
				pbe->orig_address = (long) page_address(page);
				/* copy_page is not usable for copying task structs. */
				memcpy((void *)pbe->address, (void *)pbe->orig_address, PAGE_SIZE);
				pbe = pbe->next;
			}
		}
	}
	BUG_ON(pbe);
}


/**
 *	calc_nr - Determine the number of pages needed for a pbe list.
 */

static int calc_nr(int nr_copy)
{
	return nr_copy + (nr_copy+PBES_PER_PAGE-2)/(PBES_PER_PAGE-1);
}

/**
 *	free_pagedir - free pages allocated with alloc_pagedir()
 */

static inline void free_pagedir(struct pbe *pblist)
{
	struct pbe *pbe;

	while (pblist) {
		pbe = (pblist + PB_PAGE_SKIP)->next;
		free_page((unsigned long)pblist);
		pblist = pbe;
	}
}

/**
 *	fill_pb_page - Create a list of PBEs on a given memory page
 */

static inline void fill_pb_page(struct pbe *pbpage)
{
	struct pbe *p;

	p = pbpage;
	pbpage += PB_PAGE_SKIP;
	do
		p->next = p + 1;
	while (++p < pbpage);
}

/**
 *	create_pbe_list - Create a list of PBEs on top of a given chain
 *	of memory pages allocated with alloc_pagedir()
 */

static void create_pbe_list(struct pbe *pblist, unsigned nr_pages)
{
	struct pbe *pbpage, *p;
	unsigned num = PBES_PER_PAGE;

	for_each_pb_page (pbpage, pblist) {
		if (num >= nr_pages)
			break;

		fill_pb_page(pbpage);
		num += PBES_PER_PAGE;
	}
	if (pbpage) {
		for (num -= PBES_PER_PAGE - 1, p = pbpage; num < nr_pages; p++, num++)
			p->next = p + 1;
		p->next = NULL;
	}
	pr_debug("create_pbe_list(): initialized %d PBEs\n", num);
}

/**
 *	alloc_pagedir - Allocate the page directory.
 *
 *	First, determine exactly how many pages we need and
 *	allocate them.
 *
 *	We arrange the pages in a chain: each page is an array of PBES_PER_PAGE
 *	struct pbe elements (pbes) and the last element in the page points
 *	to the next page.
 *
 *	On each page we set up a list of struct_pbe elements.
 */

static struct pbe * alloc_pagedir(unsigned nr_pages)
{
	unsigned num;
	struct pbe *pblist, *pbe;

	if (!nr_pages)
		return NULL;

	pr_debug("alloc_pagedir(): nr_pages = %d\n", nr_pages);
	pblist = (struct pbe *)get_zeroed_page(GFP_ATOMIC | __GFP_COLD);
	for (pbe = pblist, num = PBES_PER_PAGE; pbe && num < nr_pages;
        		pbe = pbe->next, num += PBES_PER_PAGE) {
		pbe += PB_PAGE_SKIP;
		pbe->next = (struct pbe *)get_zeroed_page(GFP_ATOMIC | __GFP_COLD);
	}
	if (!pbe) { /* get_zeroed_page() failed */
		free_pagedir(pblist);
		pblist = NULL;
        }
	return pblist;
}

/**
 *	free_image_pages - Free pages allocated for snapshot
 */

static void free_image_pages(void)
{
	struct pbe * p;

	for_each_pbe (p, pagedir_save) {
		if (p->address) {
			ClearPageNosave(virt_to_page(p->address));
			free_page(p->address);
			p->address = 0;
		}
	}
}

/**
 *	alloc_image_pages - Allocate pages for the snapshot.
 */

static int alloc_image_pages(void)
{
	struct pbe * p;

	for_each_pbe (p, pagedir_save) {
		p->address = get_zeroed_page(GFP_ATOMIC | __GFP_COLD);
		if (!p->address)
			return -ENOMEM;
		SetPageNosave(virt_to_page(p->address));
	}
	return 0;
}

/* Free pages we allocated for suspend. Suspend pages are alocated
 * before atomic copy, so we need to free them after resume.
 */
void swsusp_free(void)
{
	BUG_ON(PageNosave(virt_to_page(pagedir_save)));
	BUG_ON(PageNosaveFree(virt_to_page(pagedir_save)));
	free_image_pages();
	free_pagedir(pagedir_save);
}


/**
 *	enough_free_mem - Make sure we enough free memory to snapshot.
 *
 *	Returns TRUE or FALSE after checking the number of available
 *	free pages.
 */

static int enough_free_mem(void)
{
	if (nr_free_pages() < (nr_copy_pages + PAGES_FOR_IO)) {
		pr_debug("swsusp: Not enough free pages: Have %d\n",
			 nr_free_pages());
		return 0;
	}
	return 1;
}


/**
 *	enough_swap - Make sure we have enough swap to save the image.
 *
 *	Returns TRUE or FALSE after checking the total amount of swap
 *	space avaiable.
 *
 *	FIXME: si_swapinfo(&i) returns all swap devices information.
 *	We should only consider resume_device.
 */

static int enough_swap(void)
{
	struct sysinfo i;

	si_swapinfo(&i);
	if (i.freeswap < (nr_copy_pages + PAGES_FOR_IO))  {
		pr_debug("swsusp: Not enough swap. Need %ld\n",i.freeswap);
		return 0;
	}
	return 1;
}

static int swsusp_alloc(void)
{
	int error;

	pagedir_nosave = NULL;
	nr_copy_pages = calc_nr(nr_copy_pages);
	nr_copy_pages_check = nr_copy_pages;

	pr_debug("suspend: (pages needed: %d + %d free: %d)\n",
		 nr_copy_pages, PAGES_FOR_IO, nr_free_pages());

	if (!enough_free_mem())
		return -ENOMEM;

	if (!enough_swap())
		return -ENOSPC;

	if (MAX_PBES < nr_copy_pages / PBES_PER_PAGE +
	    !!(nr_copy_pages % PBES_PER_PAGE))
		return -ENOSPC;

	if (!(pagedir_save = alloc_pagedir(nr_copy_pages))) {
		printk(KERN_ERR "suspend: Allocating pagedir failed.\n");
		return -ENOMEM;
	}
	create_pbe_list(pagedir_save, nr_copy_pages);
	pagedir_nosave = pagedir_save;
	if ((error = alloc_image_pages())) {
		printk(KERN_ERR "suspend: Allocating image pages failed.\n");
		swsusp_free();
		return error;
	}

	return 0;
}

static int suspend_prepare_image(void)
{
	int error;

	pr_debug("swsusp: critical section: \n");
	if (save_highmem()) {
		printk(KERN_CRIT "Suspend machine: Not enough free pages for highmem\n");
		restore_highmem();
		return -ENOMEM;
	}

	drain_local_pages();
	count_data_pages();
	printk("swsusp: Need to copy %u pages\n", nr_copy_pages);

	error = swsusp_alloc();
	if (error)
		return error;

	/* During allocating of suspend pagedir, new cold pages may appear.
	 * Kill them.
	 */
	drain_local_pages();
	copy_data_pages();

	/*
	 * End of critical section. From now on, we can write to memory,
	 * but we should not touch disk. This specially means we must _not_
	 * touch swap space! Except we must write out our image of course.
	 */

	printk("swsusp: critical section/: done (%d pages copied)\n", nr_copy_pages );
	return 0;
}


/* It is important _NOT_ to umount filesystems at this point. We want
 * them synced (in case something goes wrong) but we DO not want to mark
 * filesystem clean: it is not. (And it does not matter, if we resume
 * correctly, we'll mark system clean, anyway.)
 */
int swsusp_write(void)
{
	int error;
	device_resume();
	lock_swapdevices();
	error = write_suspend_image();
	/* This will unlock ignored swap devices since writing is finished */
	lock_swapdevices();
	return error;

}


extern asmlinkage int swsusp_arch_suspend(void);
extern asmlinkage int swsusp_arch_resume(void);


asmlinkage int swsusp_save(void)
{
	return suspend_prepare_image();
}

int swsusp_suspend(void)
{
	int error;
	if ((error = arch_prepare_suspend()))
		return error;
	local_irq_disable();
	/* At this point, device_suspend() has been called, but *not*
	 * device_power_down(). We *must* device_power_down() now.
	 * Otherwise, drivers for some devices (e.g. interrupt controllers)
	 * become desynchronized with the actual state of the hardware
	 * at resume time, and evil weirdness ensues.
	 */
	if ((error = device_power_down(PMSG_FREEZE))) {
		printk(KERN_ERR "Some devices failed to power down, aborting suspend\n");
		local_irq_enable();
		return error;
	}

	if ((error = swsusp_swap_check())) {
		printk(KERN_ERR "swsusp: cannot find swap device, try swapon -a.\n");
		device_power_up();
		local_irq_enable();
		return error;
	}

	save_processor_state();
	if ((error = swsusp_arch_suspend()))
		printk(KERN_ERR "Error %d suspending\n", error);
	/* Restore control flow magically appears here */
	restore_processor_state();
	BUG_ON (nr_copy_pages_check != nr_copy_pages);
	restore_highmem();
	device_power_up();
	local_irq_enable();
	return error;
}

int swsusp_resume(void)
{
	int error;
	local_irq_disable();
	if (device_power_down(PMSG_FREEZE))
		printk(KERN_ERR "Some devices failed to power down, very bad\n");
	/* We'll ignore saved state, but this gets preempt count (etc) right */
	save_processor_state();
	error = swsusp_arch_resume();
	/* Code below is only ever reached in case of failure. Otherwise
	 * execution continues at place where swsusp_arch_suspend was called
         */
	BUG_ON(!error);
	restore_processor_state();
	restore_highmem();
	touch_softlockup_watchdog();
	device_power_up();
	local_irq_enable();
	return error;
}

/**
 *	On resume, for storing the PBE list and the image,
 *	we can only use memory pages that do not conflict with the pages
 *	which had been used before suspend.
 *
 *	We don't know which pages are usable until we allocate them.
 *
 *	Allocated but unusable (ie eaten) memory pages are linked together
 *	to create a list, so that we can free them easily
 *
 *	We could have used a type other than (void *)
 *	for this purpose, but ...
 */
static void **eaten_memory = NULL;

static inline void eat_page(void *page)
{
	void **c;

	c = eaten_memory;
	eaten_memory = page;
	*eaten_memory = c;
}

static unsigned long get_usable_page(unsigned gfp_mask)
{
	unsigned long m;

	m = get_zeroed_page(gfp_mask);
	while (!PageNosaveFree(virt_to_page(m))) {
		eat_page((void *)m);
		m = get_zeroed_page(gfp_mask);
		if (!m)
			break;
	}
	return m;
}

static void free_eaten_memory(void)
{
	unsigned long m;
	void **c;
	int i = 0;

	c = eaten_memory;
	while (c) {
		m = (unsigned long)c;
		c = *c;
		free_page(m);
		i++;
	}
	eaten_memory = NULL;
	pr_debug("swsusp: %d unused pages freed\n", i);
}

/**
 *	check_pagedir - We ensure here that pages that the PBEs point to
 *	won't collide with pages where we're going to restore from the loaded
 *	pages later
 */

static int check_pagedir(struct pbe *pblist)
{
	struct pbe *p;

	/* This is necessary, so that we can free allocated pages
	 * in case of failure
	 */
	for_each_pbe (p, pblist)
		p->address = 0UL;

	for_each_pbe (p, pblist) {
		p->address = get_usable_page(GFP_ATOMIC);
		if (!p->address)
			return -ENOMEM;
	}
	return 0;
}

/**
 *	swsusp_pagedir_relocate - It is possible, that some memory pages
 *	occupied by the list of PBEs collide with pages where we're going to
 *	restore from the loaded pages later.  We relocate them here.
 */

static struct pbe * swsusp_pagedir_relocate(struct pbe *pblist)
{
	struct zone *zone;
	unsigned long zone_pfn;
	struct pbe *pbpage, *tail, *p;
	void *m;
	int rel = 0, error = 0;

	if (!pblist) /* a sanity check */
		return NULL;

	pr_debug("swsusp: Relocating pagedir (%lu pages to check)\n",
			swsusp_info.pagedir_pages);

	/* Set page flags */

	for_each_zone (zone) {
        	for (zone_pfn = 0; zone_pfn < zone->spanned_pages; ++zone_pfn)
                	SetPageNosaveFree(pfn_to_page(zone_pfn +
					zone->zone_start_pfn));
	}

	/* Clear orig addresses */

	for_each_pbe (p, pblist)
		ClearPageNosaveFree(virt_to_page(p->orig_address));

	tail = pblist + PB_PAGE_SKIP;

	/* Relocate colliding pages */

	for_each_pb_page (pbpage, pblist) {
		if (!PageNosaveFree(virt_to_page((unsigned long)pbpage))) {
			m = (void *)get_usable_page(GFP_ATOMIC | __GFP_COLD);
			if (!m) {
				error = -ENOMEM;
				break;
			}
			memcpy(m, (void *)pbpage, PAGE_SIZE);
			if (pbpage == pblist)
				pblist = (struct pbe *)m;
			else
				tail->next = (struct pbe *)m;

			eat_page((void *)pbpage);
			pbpage = (struct pbe *)m;

			/* We have to link the PBEs again */

			for (p = pbpage; p < pbpage + PB_PAGE_SKIP; p++)
				if (p->next) /* needed to save the end */
					p->next = p + 1;

			rel++;
		}
		tail = pbpage + PB_PAGE_SKIP;
	}

	if (error) {
		printk("\nswsusp: Out of memory\n\n");
		free_pagedir(pblist);
		free_eaten_memory();
		pblist = NULL;
		/* Is this even worth handling? It should never ever happen, and we
		   have just lost user's state, anyway... */
	} else
		printk("swsusp: Relocated %d pages\n", rel);

	return pblist;
}

/*
 *	Using bio to read from swap.
 *	This code requires a bit more work than just using buffer heads
 *	but, it is the recommended way for 2.5/2.6.
 *	The following are to signal the beginning and end of I/O. Bios
 *	finish asynchronously, while we want them to happen synchronously.
 *	A simple atomic_t, and a wait loop take care of this problem.
 */

static atomic_t io_done = ATOMIC_INIT(0);

static int end_io(struct bio * bio, unsigned int num, int err)
{
	if (!test_bit(BIO_UPTODATE, &bio->bi_flags))
		panic("I/O error reading memory image");
	atomic_set(&io_done, 0);
	return 0;
}

static struct block_device * resume_bdev;

/**
 *	submit - submit BIO request.
 *	@rw:	READ or WRITE.
 *	@off	physical offset of page.
 *	@page:	page we're reading or writing.
 *
 *	Straight from the textbook - allocate and initialize the bio.
 *	If we're writing, make sure the page is marked as dirty.
 *	Then submit it and wait.
 */

static int submit(int rw, pgoff_t page_off, void * page)
{
	int error = 0;
	struct bio * bio;

	bio = bio_alloc(GFP_ATOMIC, 1);
	if (!bio)
		return -ENOMEM;
	bio->bi_sector = page_off * (PAGE_SIZE >> 9);
	bio_get(bio);
	bio->bi_bdev = resume_bdev;
	bio->bi_end_io = end_io;

	if (bio_add_page(bio, virt_to_page(page), PAGE_SIZE, 0) < PAGE_SIZE) {
		printk("swsusp: ERROR: adding page to bio at %ld\n",page_off);
		error = -EFAULT;
		goto Done;
	}

	if (rw == WRITE)
		bio_set_pages_dirty(bio);

	atomic_set(&io_done, 1);
	submit_bio(rw | (1 << BIO_RW_SYNC), bio);
	while (atomic_read(&io_done))
		yield();

 Done:
	bio_put(bio);
	return error;
}

static int bio_read_page(pgoff_t page_off, void * page)
{
	return submit(READ, page_off, page);
}

static int bio_write_page(pgoff_t page_off, void * page)
{
	return submit(WRITE, page_off, page);
}

/*
 * Sanity check if this image makes sense with this kernel/swap context
 * I really don't think that it's foolproof but more than nothing..
 */

static const char * sanity_check(void)
{
	dump_info();
	if (swsusp_info.version_code != LINUX_VERSION_CODE)
		return "kernel version";
	if (swsusp_info.num_physpages != num_physpages)
		return "memory size";
	if (strcmp(swsusp_info.uts.sysname,system_utsname.sysname))
		return "system type";
	if (strcmp(swsusp_info.uts.release,system_utsname.release))
		return "kernel release";
	if (strcmp(swsusp_info.uts.version,system_utsname.version))
		return "version";
	if (strcmp(swsusp_info.uts.machine,system_utsname.machine))
		return "machine";
#if 0
	/* We can't use number of online CPUs when we use hotplug to remove them ;-))) */
	if (swsusp_info.cpus != num_possible_cpus())
		return "number of cpus";
#endif
	return NULL;
}


static int check_header(void)
{
	const char * reason = NULL;
	int error;

	if ((error = bio_read_page(swp_offset(swsusp_header.swsusp_info), &swsusp_info)))
		return error;

 	/* Is this same machine? */
	if ((reason = sanity_check())) {
		printk(KERN_ERR "swsusp: Resume mismatch: %s\n",reason);
		return -EPERM;
	}
	nr_copy_pages = swsusp_info.image_pages;
	return error;
}

static int check_sig(void)
{
	int error;

	memset(&swsusp_header, 0, sizeof(swsusp_header));
	if ((error = bio_read_page(0, &swsusp_header)))
		return error;
	if (!memcmp(SWSUSP_SIG, swsusp_header.sig, 10)) {
		memcpy(swsusp_header.sig, swsusp_header.orig_sig, 10);
		memcpy(key_iv, swsusp_header.key_iv, MAXKEY+MAXIV);
		memset(swsusp_header.key_iv, 0, MAXKEY+MAXIV);

		/*
		 * Reset swap signature now.
		 */
		error = bio_write_page(0, &swsusp_header);
	} else { 
		return -EINVAL;
	}
	if (!error)
		pr_debug("swsusp: Signature found, resuming\n");
	return error;
}

/**
 *	data_read - Read image pages from swap.
 *
 *	You do not need to check for overlaps, check_pagedir()
 *	already did that.
 */

static int data_read(struct pbe *pblist)
{
	struct pbe * p;
	int error = 0;
	int i = 0;
	int mod = swsusp_info.image_pages / 100;
	void *tfm;

	if ((error = crypto_init(0, &tfm)))
		return error;

	if (!mod)
		mod = 1;

	printk("swsusp: Reading image data (%lu pages):     ",
			swsusp_info.image_pages);

	for_each_pbe (p, pblist) {
		if (!(i % mod))
			printk("\b\b\b\b%3d%%", i / mod);

		if ((error = crypto_read(p, tfm))) {
			crypto_exit(tfm);
			return error;
		}

		i++;
	}
	printk("\b\b\b\bdone\n");
	crypto_exit(tfm);
	return error;
}

/**
 *	read_pagedir - Read page backup list pages from swap
 */

static int read_pagedir(struct pbe *pblist)
{
	struct pbe *pbpage, *p;
	unsigned i = 0;
	int error;

	if (!pblist)
		return -EFAULT;

	printk("swsusp: Reading pagedir (%lu pages)\n",
			swsusp_info.pagedir_pages);

	for_each_pb_page (pbpage, pblist) {
		unsigned long offset = swp_offset(swsusp_info.pagedir[i++]);

		error = -EFAULT;
		if (offset) {
			p = (pbpage + PB_PAGE_SKIP)->next;
			error = bio_read_page(offset, (void *)pbpage);
			(pbpage + PB_PAGE_SKIP)->next = p;
		}
		if (error)
			break;
	}

	if (error)
		free_pagedir(pblist);
	else
		BUG_ON(i != swsusp_info.pagedir_pages);

	return error;
}


static int check_suspend_image(void)
{
	int error = 0;

	if ((error = check_sig()))
		return error;

	if ((error = check_header()))
		return error;

	return 0;
}

static int read_suspend_image(void)
{
	int error = 0;
	struct pbe *p;

	if (!(p = alloc_pagedir(nr_copy_pages)))
		return -ENOMEM;

	if ((error = read_pagedir(p)))
		return error;

	create_pbe_list(p, nr_copy_pages);

	if (!(pagedir_nosave = swsusp_pagedir_relocate(p)))
		return -ENOMEM;

	/* Allocate memory for the image and read the data from swap */

	error = check_pagedir(pagedir_nosave);
	free_eaten_memory();
	if (!error)
		error = data_read(pagedir_nosave);

	if (error) { /* We fail cleanly */
		for_each_pbe (p, pagedir_nosave)
			if (p->address) {
				free_page(p->address);
				p->address = 0UL;
			}
		free_pagedir(pagedir_nosave);
	}
	return error;
}

/**
 *      swsusp_check - Check for saved image in swap
 */

int swsusp_check(void)
{
	int error;

	resume_bdev = open_by_devnum(swsusp_resume_device, FMODE_READ);
	if (!IS_ERR(resume_bdev)) {
		set_blocksize(resume_bdev, PAGE_SIZE);
		error = check_suspend_image();
		if (error)
		    blkdev_put(resume_bdev);
	} else
		error = PTR_ERR(resume_bdev);

	if (!error)
		pr_debug("swsusp: resume file found\n");
	else
		pr_debug("swsusp: Error %d check for resume file\n", error);
	return error;
}

/**
 *	swsusp_read - Read saved image from swap.
 */

int swsusp_read(void)
{
	int error;

	if (IS_ERR(resume_bdev)) {
		pr_debug("swsusp: block device not initialised\n");
		return PTR_ERR(resume_bdev);
	}

	error = read_suspend_image();
	blkdev_put(resume_bdev);
	memset(key_iv, 0, MAXKEY+MAXIV);

	if (!error)
		pr_debug("swsusp: Reading resume file was successful\n");
	else
		pr_debug("swsusp: Error %d resuming\n", error);
	return error;
}

/**
 *	swsusp_close - close swap device.
 */

void swsusp_close(void)
{
	if (IS_ERR(resume_bdev)) {
		pr_debug("swsusp: block device not initialised\n");
		return;
	}

	blkdev_put(resume_bdev);
}
