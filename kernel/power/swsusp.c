/*
 * linux/kernel/power/swsusp.c
 *
 * This file provides code to write suspend image to swap and read it back.
 *
 * Copyright (C) 1998-2001 Gabor Kuti <seasons@fornax.hu>
 * Copyright (C) 1998,2001-2005 Pavel Machek <pavel@suse.cz>
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
#include <linux/bitops.h>
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
#include <linux/highmem.h>
#include <linux/bio.h>

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

extern char resume_file[];

/* Local variables that should not be affected by save */
unsigned int nr_copy_pages __nosavedata = 0;

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
suspend_pagedir_t *pagedir_save;

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

/**
 *	enough_swap - Make sure we have enough swap to save the image.
 *
 *	Returns TRUE or FALSE after checking the total amount of swap
 *	space avaiable.
 *
 *	FIXME: si_swapinfo(&i) returns all swap devices information.
 *	We should only consider resume_device.
 */

int enough_swap(unsigned nr_pages)
{
	struct sysinfo i;

	si_swapinfo(&i);
	pr_debug("swsusp: available swap: %lu pages\n", i.freeswap);
	return i.freeswap > (nr_pages + PAGES_FOR_IO +
		(nr_pages + PBES_PER_PAGE - 1) / PBES_PER_PAGE);
}


/* It is important _NOT_ to umount filesystems at this point. We want
 * them synced (in case something goes wrong) but we DO not want to mark
 * filesystem clean: it is not. (And it does not matter, if we resume
 * correctly, we'll mark system clean, anyway.)
 */
int swsusp_write(void)
{
	int error;

	lock_swapdevices();
	error = write_suspend_image();
	/* This will unlock ignored swap devices since writing is finished */
	lock_swapdevices();
	return error;

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
	/* The only reason why swsusp_arch_resume() can fail is memory being
	 * very tight, so we have to free it as soon as we can to avoid
	 * subsequent failures
	 */
	swsusp_free();
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
 *	Allocated but unusable (ie eaten) memory pages are marked so that
 *	swsusp_free() can release them
 */

unsigned long get_safe_page(gfp_t gfp_mask)
{
	unsigned long m;

	do {
		m = get_zeroed_page(gfp_mask);
		if (m && PageNosaveFree(virt_to_page(m)))
			/* This is for swsusp_free() */
			SetPageNosave(virt_to_page(m));
	} while (m && PageNosaveFree(virt_to_page(m)));
	if (m) {
		/* This is for swsusp_free() */
		SetPageNosave(virt_to_page(m));
		SetPageNosaveFree(virt_to_page(m));
	}
	return m;
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
		p->address = get_safe_page(GFP_ATOMIC);
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
	int rel = 0;

	if (!pblist) /* a sanity check */
		return NULL;

	pr_debug("swsusp: Relocating pagedir (%lu pages to check)\n",
			swsusp_info.pagedir_pages);

	/* Clear page flags */

	for_each_zone (zone) {
        	for (zone_pfn = 0; zone_pfn < zone->spanned_pages; ++zone_pfn)
        		if (pfn_valid(zone_pfn + zone->zone_start_pfn))
                		ClearPageNosaveFree(pfn_to_page(zone_pfn +
					zone->zone_start_pfn));
	}

	/* Mark orig addresses */

	for_each_pbe (p, pblist)
		SetPageNosaveFree(virt_to_page(p->orig_address));

	tail = pblist + PB_PAGE_SKIP;

	/* Relocate colliding pages */

	for_each_pb_page (pbpage, pblist) {
		if (PageNosaveFree(virt_to_page((unsigned long)pbpage))) {
			m = (void *)get_safe_page(GFP_ATOMIC | __GFP_COLD);
			if (!m)
				return NULL;
			memcpy(m, (void *)pbpage, PAGE_SIZE);
			if (pbpage == pblist)
				pblist = (struct pbe *)m;
			else
				tail->next = (struct pbe *)m;
			pbpage = (struct pbe *)m;

			/* We have to link the PBEs again */
			for (p = pbpage; p < pbpage + PB_PAGE_SKIP; p++)
				if (p->next) /* needed to save the end */
					p->next = p + 1;

			rel++;
		}
		tail = pbpage + PB_PAGE_SKIP;
	}

	/* This is for swsusp_free() */
	for_each_pb_page (pbpage, pblist) {
		SetPageNosave(virt_to_page(pbpage));
		SetPageNosaveFree(virt_to_page(pbpage));
	}

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

	if (!error)
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

	if (!error)
		error = data_read(pagedir_nosave);

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
