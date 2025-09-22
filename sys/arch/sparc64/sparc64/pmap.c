/*	$OpenBSD: pmap.c,v 1.124 2025/09/18 14:54:33 kettenis Exp $	*/
/*	$NetBSD: pmap.c,v 1.107 2001/08/31 16:47:41 eeh Exp $	*/
/*
 * 
 * Copyright (C) 1996-1999 Eduardo Horvath.
 * All rights reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR  ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR  BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/atomic.h>
#include <sys/param.h>
#include <sys/queue.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/msgbuf.h>
#include <sys/pool.h>
#include <sys/exec.h>
#include <sys/core.h>
#include <sys/kcore.h>

#include <uvm/uvm.h>

#include <machine/pcb.h>
#include <machine/sparc64.h>
#include <machine/ctlreg.h>
#include <machine/hypervisor.h>
#include <machine/openfirm.h>
#include <machine/kcore.h>
#include <machine/pte.h>

#include <sparc64/sparc64/cache.h>

#ifdef DDB
#include <machine/db_machdep.h>
#include <ddb/db_output.h>
#define db_enter()	__asm volatile("ta 1; nop")
#else
#define db_enter()
#endif

#define	MEG		(1<<20) /* 1MB */
#define	KB		(1<<10)	/* 1KB */

paddr_t cpu0paddr;/* XXXXXXXXXXXXXXXX */

/* These routines are in assembly to allow access thru physical mappings */
extern int64_t pseg_get(struct pmap*, vaddr_t addr);
extern int pseg_set(struct pmap*, vaddr_t addr, int64_t tte, paddr_t spare);

extern void pmap_zero_phys(paddr_t pa);
extern void pmap_copy_phys(paddr_t src, paddr_t dst);

/*
 * Diatribe on ref/mod counting:
 *
 * First of all, ref/mod info must be non-volatile.  Hence we need to keep it
 * in the pv_entry structure for each page.  (We could bypass this for the 
 * vm_page, but that's a long story....)
 * 
 * This architecture has nice, fast traps with lots of space for software bits
 * in the TTE.  To accelerate ref/mod counts we make use of these features.
 *
 * When we map a page initially, we place a TTE in the page table.  It's 
 * inserted with the TLB_W and TLB_ACCESS bits cleared.  If a page is really
 * writeable we set the TLB_REAL_W bit for the trap handler.
 *
 * Whenever we take a TLB miss trap, the trap handler will set the TLB_ACCESS
 * bit in the appropriate TTE in the page table.  Whenever we take a protection
 * fault, if the TLB_REAL_W bit is set then we flip both the TLB_W and TLB_MOD
 * bits to enable writing and mark the page as modified.
 *
 * This means that we may have ref/mod information all over the place.  The
 * pmap routines must traverse the page tables of all pmaps with a given page
 * and collect/clear all the ref/mod information and copy it into the pv_entry.
 */

#define	PV_ALIAS	0x1LL
#define PV_REF		0x2LL
#define PV_MOD		0x4LL
#define PV_MASK		(0x03fLL)
#define PV_VAMASK	(~(NBPG - 1))
#define PV_MATCH(pv,va)	(!((((pv)->pv_va) ^ (va)) & PV_VAMASK))
#define PV_SETVA(pv,va) ((pv)->pv_va = (((va) & PV_VAMASK) | (((pv)->pv_va) & PV_MASK)))

static struct pool pv_pool;
static struct pool pmap_pool;

pv_entry_t pmap_remove_pv(struct pmap *pm, vaddr_t va, paddr_t pa);
pv_entry_t pmap_enter_pv(struct pmap *pm, pv_entry_t, vaddr_t va, paddr_t pa);
void	pmap_page_cache(struct pmap *pm, paddr_t pa, int mode);

void	pmap_bootstrap_cpu(paddr_t);

void	pmap_release(struct pmap *);
pv_entry_t pa_to_pvh(paddr_t);

pv_entry_t
pa_to_pvh(paddr_t pa)
{
	struct vm_page *pg;

	pg = PHYS_TO_VM_PAGE(pa);
	return pg ? &pg->mdpage.pvent : NULL;
}

static __inline u_int
pmap_tte2flags(u_int64_t tte)
{
	if (CPU_ISSUN4V)
		return (((tte & SUN4V_TLB_ACCESS) ? PV_REF : 0) |
		    ((tte & SUN4V_TLB_MODIFY) ? PV_MOD : 0));
	else
		return (((tte & SUN4U_TLB_ACCESS) ? PV_REF : 0) |
		    ((tte & SUN4U_TLB_MODIFY) ? PV_MOD : 0));
}

/*
 * Here's the CPU TSB stuff.  It's allocated in pmap_bootstrap.
 */
pte_t *tsb_dmmu;
pte_t *tsb_immu;
int tsbsize;		/* tsbents = 512 * 2^tsbsize */
#define TSBENTS (512 << tsbsize)
#define	TSBSIZE	(TSBENTS * 16)

/*
 * The invalid tsb tag uses the fact that the last context we have is
 * never allocated.
 */
#define TSB_TAG_INVALID	(~0LL << 48)

#define TSB_DATA(g,sz,pa,priv,write,cache,aliased,valid,ie) \
  (CPU_ISSUN4V ?\
    SUN4V_TSB_DATA(g,sz,pa,priv,write,cache,aliased,valid,ie) : \
    SUN4U_TSB_DATA(g,sz,pa,priv,write,cache,aliased,valid,ie))

/* The same for sun4u and sun4v. */
#define TLB_V		SUN4U_TLB_V

/* Only used for DEBUG. */
#define TLB_NFO		(CPU_ISSUN4V ? SUN4V_TLB_NFO : SUN4U_TLB_NFO)

/*
 * UltraSPARC T1 & T2 implement only a 40-bit real address range, just
 * like older UltraSPARC CPUs.
 */
#define TLB_PA_MASK	SUN4U_TLB_PA_MASK

/* XXX */
#define TLB_TSB_LOCK	(CPU_ISSUN4V ? SUN4V_TLB_TSB_LOCK : SUN4U_TLB_TSB_LOCK)

#ifdef SUN4V
struct tsb_desc *tsb_desc;
#endif

struct pmap kernel_pmap_;

/*
 * Virtual and physical addresses of the start and end of kernel text
 * and data segments.
 */
vaddr_t ktext;
paddr_t ktextp;
vaddr_t ektext;
paddr_t ektextp;
vaddr_t kdata;
paddr_t kdatap;
vaddr_t ekdata;
paddr_t ekdatap;

static struct mem_region memlist[8]; /* Pick a random size here */

vaddr_t	vmmap;			/* one reserved MI vpage for /dev/mem */

struct mem_region *mem, *avail, *orig;
int memsize;

static int memh = 0, vmemh = 0;	/* Handles to OBP devices */

static int ptelookup_va(vaddr_t va); /* sun4u */

static __inline void
tsb_invalidate(int ctx, vaddr_t va)
{
	int i;
	int64_t tag;

	i = ptelookup_va(va);
	tag = TSB_TAG(0, ctx, va);
	if (tsb_dmmu[i].tag == tag)
		atomic_cas_ulong((volatile unsigned long *)&tsb_dmmu[i].tag,
		    tag, TSB_TAG_INVALID);
	if (tsb_immu[i].tag == tag)
		atomic_cas_ulong((volatile unsigned long *)&tsb_immu[i].tag,
		    tag, TSB_TAG_INVALID);
}

struct prom_map *prom_map;
int prom_map_size;

#ifdef DEBUG
#define	PDB_BOOT	0x20000
#define	PDB_BOOT1	0x40000
int	pmapdebug = 0;

#define	BDPRINTF(n, f)	if (pmapdebug & (n)) prom_printf f
#else
#define	BDPRINTF(n, f)
#endif

/*
 *
 * A context is simply a small number that differentiates multiple mappings
 * of the same address.  Contexts on the spitfire are 13 bits, but could
 * be as large as 17 bits.
 *
 * Each context is either free or attached to a pmap.
 *
 * The context table is an array of pointers to psegs.  Just dereference
 * the right pointer and you get to the pmap segment tables.  These are
 * physical addresses, of course.
 *
 */
paddr_t *ctxbusy;	
int numctx;
#define CTXENTRY	(sizeof(paddr_t))
#define CTXSIZE		(numctx * CTXENTRY)

int pmap_get_page(paddr_t *, const char *, struct pmap *);
void pmap_free_page(paddr_t, struct pmap *);

/*
 * Support for big page sizes.  This maps the page size to the
 * page bits.  That is: these are the bits between 8K pages and
 * larger page sizes that cause aliasing.
 */
const struct page_size_map page_size_map[] = {
	{ (4*1024*1024-1) & ~(8*1024-1), PGSZ_4M },
	{ (512*1024-1) & ~(8*1024-1), PGSZ_512K  },
	{ (64*1024-1) & ~(8*1024-1), PGSZ_64K  },
	{ (8*1024-1) & ~(8*1024-1), PGSZ_8K  },
	{ 0, 0  }
};

/*
 * Enter a TTE into the kernel pmap only.  Don't do anything else.
 * 
 * Use only during bootstrapping since it does no locking and 
 * can lose ref/mod info!!!!
 *
 */
static void
pmap_enter_kpage(vaddr_t va, int64_t data)
{
	paddr_t newp;

	newp = 0;
	while (pseg_set(pmap_kernel(), va, data, newp) == 1) {
		newp = 0;
		if (!pmap_get_page(&newp, NULL, pmap_kernel())) {
			prom_printf("pmap_enter_kpage: out of pages\n");
			panic("pmap_enter_kpage");
		}

		BDPRINTF(PDB_BOOT1, 
			 ("pseg_set: pm=%p va=%p data=%lx newp %lx\r\n",
			  pmap_kernel(), va, (long)data, (long)newp));
	}
}

/*
 * Check bootargs to see if we need to enable bootdebug.
 */
#ifdef DEBUG
void
pmap_bootdebug(void) 
{
	int chosen;
	char *cp;
	char buf[128];

	/*
	 * Grab boot args from PROM
	 */
	chosen = OF_finddevice("/chosen");
	/* Setup pointer to boot flags */
	OF_getprop(chosen, "bootargs", buf, sizeof(buf));
	cp = buf;
	while (*cp != '-')
		if (*cp++ == '\0')
			return;
	for (;;) 
		switch (*++cp) {
		case '\0':
			return;
		case 'V':
			pmapdebug |= PDB_BOOT|PDB_BOOT1;
			break;
		case 'D':
			pmapdebug |= PDB_BOOT1;
			break;
		}
}
#endif

/*
 * This is called during bootstrap, before the system is really initialized.
 *
 * It's called with the start and end virtual addresses of the kernel.  We
 * bootstrap the pmap allocator now.  We will allocate the basic structures we
 * need to bootstrap the VM system here: the page frame tables, the TSB, and
 * the free memory lists.
 *
 * Now all this is becoming a bit obsolete.  maxctx is still important, but by
 * separating the kernel text and data segments we really would need to
 * provide the start and end of each segment.  But we can't.  The rodata
 * segment is attached to the end of the kernel segment and has nothing to
 * delimit its end.  We could still pass in the beginning of the kernel and
 * the beginning and end of the data segment but we could also just as easily
 * calculate that all in here.
 *
 * To handle the kernel text, we need to do a reverse mapping of the start of
 * the kernel, then traverse the free memory lists to find out how big it is.
 */

void
pmap_bootstrap(u_long kernelstart, u_long kernelend, u_int maxctx, u_int numcpus)
{
	extern int data_start[], end[];	/* start of data segment */
	extern int msgbufmapped;
	struct mem_region *mp, *mp1;
	int msgbufsiz;
	int pcnt;
	size_t s, sz;
	int i, j;
	int64_t data;
	vaddr_t va;
	u_int64_t phys_msgbuf;
	paddr_t newkp;
	vaddr_t newkv, firstaddr, intstk;
	vsize_t kdsize, ktsize;

#ifdef DEBUG
	pmap_bootdebug();
#endif

	BDPRINTF(PDB_BOOT, ("Entered pmap_bootstrap.\r\n"));
	/*
	 * set machine page size
	 */
	uvmexp.pagesize = NBPG;
	uvm_setpagesize();

	/*
	 * Find out how big the kernel's virtual address
	 * space is.  The *$#@$ prom loses this info
	 */
	if ((vmemh = OF_finddevice("/virtual-memory")) == -1) {
		prom_printf("no virtual-memory?");
		OF_exit();
	}
	bzero((caddr_t)memlist, sizeof(memlist));
	if (OF_getprop(vmemh, "available", memlist, sizeof(memlist)) <= 0) {
		prom_printf("no vmemory avail?");
		OF_exit();
	}

#ifdef DEBUG
	if (pmapdebug & PDB_BOOT) {
		/* print out mem list */
		prom_printf("Available virtual memory:\r\n");
		for (mp = memlist; mp->size; mp++) {
			prom_printf("memlist start %p size %lx\r\n", 
				    (void *)(u_long)mp->start,
				    (u_long)mp->size);
		}
		prom_printf("End of available virtual memory\r\n");
	}
#endif
	/* 
	 * Get hold or the message buffer.
	 */
	msgbufp = (struct msgbuf *)(vaddr_t)MSGBUF_VA;
/* XXXXX -- increase msgbufsiz for uvmhist printing */
	msgbufsiz = 4*NBPG /* round_page(sizeof(struct msgbuf)) */;
	BDPRINTF(PDB_BOOT, ("Trying to allocate msgbuf at %lx, size %lx\r\n", 
			    (long)msgbufp, (long)msgbufsiz));
	if ((long)msgbufp !=
	    (long)(phys_msgbuf = prom_claim_virt((vaddr_t)msgbufp, msgbufsiz)))
		prom_printf(
		    "cannot get msgbuf VA, msgbufp=%p, phys_msgbuf=%lx\r\n", 
		    (void *)msgbufp, (long)phys_msgbuf);
	phys_msgbuf = prom_get_msgbuf(msgbufsiz, MMU_PAGE_ALIGN);
	BDPRINTF(PDB_BOOT, 
		("We should have the memory at %lx, let's map it in\r\n",
			phys_msgbuf));
	if (prom_map_phys(phys_msgbuf, msgbufsiz, (vaddr_t)msgbufp, 
			  -1/* sunos does this */) == -1)
		prom_printf("Failed to map msgbuf\r\n");
	else
		BDPRINTF(PDB_BOOT, ("msgbuf mapped at %p\r\n", 
			(void *)msgbufp));
	msgbufmapped = 1;	/* enable message buffer */
	initmsgbuf((caddr_t)msgbufp, msgbufsiz);

	/* 
	 * Record kernel mapping -- we will map these with a permanent 4MB
	 * TLB entry when we initialize the CPU later.
	 */
	BDPRINTF(PDB_BOOT, ("translating kernelstart %p\r\n", 
		(void *)kernelstart));
	ktext = kernelstart;
	ktextp = prom_vtop(kernelstart);

	kdata = (vaddr_t)data_start;
	kdatap = prom_vtop(kdata);
	ekdata = (vaddr_t)end;

	/*
	 * Find the real size of the kernel.  Locate the smallest starting
	 * address > kernelstart.
	 */
	for (mp1 = mp = memlist; mp->size; mp++) {
		/*
		 * Check whether this region is at the end of the kernel.
		 */
		if (mp->start >= ekdata && (mp1->start < ekdata || 
						mp1->start > mp->start))
			mp1 = mp;
	}
	if (mp1->start < kdata)
		prom_printf("Kernel at end of vmem???\r\n");

	BDPRINTF(PDB_BOOT1, 
		("Kernel data is mapped at %lx, next free seg: %lx, %lx\r\n",
			(long)kdata, (u_long)mp1->start, (u_long)mp1->size));

	/*
	 * We save where we can start allocating memory.
	 */
	firstaddr = (ekdata + 07) & ~ 07;	/* Longword align */

	/*
	 * We reserve 100K to grow.
	 */
	ekdata += 100*KB;

	/*
	 * And set the end of the data segment to the end of what our
	 * bootloader allocated for us, if we still fit in there.
	 */
	if (ekdata < mp1->start)
		ekdata = mp1->start;

#define	valloc(name, type, num) (name) = (type *)firstaddr; firstaddr += (num)

	/*
	 * Since we can't always give the loader the hint to align us on a 4MB
	 * boundary, we will need to do the alignment ourselves.  First
	 * allocate a new 4MB aligned segment for the kernel, then map it
	 * in, copy the kernel over, swap mappings, then finally, free the
	 * old kernel.  Then we can continue with this.
	 *
	 * We'll do the data segment up here since we know how big it is.
	 * We'll do the text segment after we've read in the PROM translations
	 * so we can figure out its size.
	 *
	 * The ctxbusy table takes about 64KB, the TSB up to 32KB, and the
	 * rest should be less than 1K, so 100KB extra should be plenty.
	 */
	kdsize = round_page(ekdata - kdata);
	BDPRINTF(PDB_BOOT1, ("Kernel data size is %lx\r\n", (long)kdsize));

	if ((kdatap & (4*MEG-1)) == 0) {
		/* We were at a 4MB boundary -- claim the rest */
		psize_t szdiff = (4*MEG - kdsize) & (4*MEG - 1);

		BDPRINTF(PDB_BOOT1, ("Need to extend dseg by %lx\r\n",
			(long)szdiff));
		if (szdiff) {
			/* Claim the rest of the physical page. */
			newkp = kdatap + kdsize;
			newkv = kdata + kdsize;
			if (newkp != prom_claim_phys(newkp, szdiff)) {
				prom_printf("pmap_bootstrap: could not claim "
					"physical dseg extension "
					"at %lx size %lx\r\n",
					newkp, szdiff);
				goto remap_data;
			}

			/* And the rest of the virtual page. */
			if (prom_claim_virt(newkv, szdiff) != newkv)
				prom_printf("pmap_bootstrap: could not claim "
				    "virtual dseg extension "
				    "at size %lx\r\n", newkv, szdiff);

			/* Make sure all 4MB are mapped */
			prom_map_phys(newkp, szdiff, newkv, -1);
		}
	} else {
		psize_t sz;
remap_data:
		/* 
		 * Either we're not at a 4MB boundary or we can't get the rest
		 * of the 4MB extension.  We need to move the data segment.
		 * Leave 1MB of extra fiddle space in the calculations.
		 */

		sz = (kdsize + 4*MEG - 1) & ~(4*MEG-1);
		BDPRINTF(PDB_BOOT1, 
			 ("Allocating new %lx kernel data at 4MB boundary\r\n",
			  (u_long)sz));
		if ((newkp = prom_alloc_phys(sz, 4*MEG)) == (paddr_t)-1 ) {
			prom_printf("Cannot allocate new kernel\r\n");
			OF_exit();
		}
		BDPRINTF(PDB_BOOT1, ("Allocating new va for buffer at %llx\r\n",
				     (u_int64_t)newkp));
		if ((newkv = (vaddr_t)prom_alloc_virt(sz, 8)) ==
		    (vaddr_t)-1) {
			prom_printf("Cannot allocate new kernel va\r\n");
			OF_exit();
		}
		BDPRINTF(PDB_BOOT1, ("Mapping in buffer %llx at %llx\r\n",
		    (u_int64_t)newkp, (u_int64_t)newkv));
		prom_map_phys(newkp, sz, (vaddr_t)newkv, -1); 
		BDPRINTF(PDB_BOOT1, ("Copying %ld bytes kernel data...",
			kdsize));
		bzero((void *)newkv, sz);
		bcopy((void *)kdata, (void *)newkv, kdsize);
		BDPRINTF(PDB_BOOT1, ("done.  Swapping maps..unmap new\r\n"));
		prom_unmap_virt((vaddr_t)newkv, sz);
		BDPRINTF(PDB_BOOT, ("remap old "));
#if 0
		/*
		 * calling the prom will probably require reading part of the
		 * data segment so we can't do this.  */
		prom_unmap_virt((vaddr_t)kdatap, kdsize);
#endif
		prom_map_phys(newkp, sz, kdata, -1); 
		/*
		 * we will map in 4MB, more than we allocated, to allow
		 * further allocation
		 */
		BDPRINTF(PDB_BOOT1, ("free old\r\n"));
		prom_free_phys(kdatap, kdsize);
		kdatap = newkp;
		BDPRINTF(PDB_BOOT1,
			 ("pmap_bootstrap: firstaddr is %lx virt (%lx phys)"
			  "avail for kernel\r\n", (u_long)firstaddr,
			  (u_long)prom_vtop(firstaddr)));
	}

	/*
	 * Find out how much RAM we have installed.
	 */
	BDPRINTF(PDB_BOOT, ("pmap_bootstrap: getting phys installed\r\n"));
	if ((memh = OF_finddevice("/memory")) == -1) {
		prom_printf("no memory?");
		OF_exit();
	}
	memsize = OF_getproplen(memh, "reg") + 2 * sizeof(struct mem_region);
	valloc(mem, struct mem_region, memsize);
	bzero((caddr_t)mem, memsize);
	if (OF_getprop(memh, "reg", mem, memsize) <= 0) {
		prom_printf("no memory installed?");
		OF_exit();
	}

#ifdef DEBUG
	if (pmapdebug & PDB_BOOT1) {
		/* print out mem list */
		prom_printf("Installed physical memory:\r\n");
		for (mp = mem; mp->size; mp++) {
			prom_printf("memlist start %lx size %lx\r\n",
				    (u_long)mp->start, (u_long)mp->size);
		}
	}
#endif
	BDPRINTF(PDB_BOOT1, ("Calculating physmem:"));

	for (mp = mem; mp->size; mp++)
		physmem += atop(mp->size);
	BDPRINTF(PDB_BOOT1, (" result %x or %d pages\r\n", 
			     (int)physmem, (int)physmem));

	/* 
	 * Calculate approx TSB size.
	 */
	tsbsize = 0;
#ifdef SMALL_KERNEL
	while ((physmem >> tsbsize) > atop(64 * MEG) && tsbsize < 2)
#else
	while ((physmem >> tsbsize) > atop(64 * MEG) && tsbsize < 7)
#endif
		tsbsize++;

	/*
	 * Save the prom translations
	 */
	sz = OF_getproplen(vmemh, "translations");
	valloc(prom_map, struct prom_map, sz);
	if (OF_getprop(vmemh, "translations", (void *)prom_map, sz) <= 0) {
		prom_printf("no translations installed?");
		OF_exit();
	}
	prom_map_size = sz / sizeof(struct prom_map);
#ifdef DEBUG
	if (pmapdebug & PDB_BOOT) {
		/* print out mem list */
		prom_printf("Prom xlations:\r\n");
		for (i = 0; i < prom_map_size; i++) {
			prom_printf("start %016lx size %016lx tte %016lx\r\n", 
				    (u_long)prom_map[i].vstart, 
				    (u_long)prom_map[i].vsize,
				    (u_long)prom_map[i].tte);
		}
		prom_printf("End of prom xlations\r\n");
	}
#endif
	/*
	 * Hunt for the kernel text segment and figure out it size and
	 * alignment.  
	 */
	ktsize = 0;
	for (i = 0; i < prom_map_size; i++) 
		if (prom_map[i].vstart == ktext + ktsize)
			ktsize += prom_map[i].vsize;
	if (ktsize == 0)
		panic("No kernel text segment!");
	ektext = ktext + ktsize;

	if (ktextp & (4*MEG-1)) {
		/* Kernel text is not 4MB aligned -- need to fix that */
		BDPRINTF(PDB_BOOT1, 
			 ("Allocating new %lx kernel text at 4MB boundary\r\n",
			  (u_long)ktsize));
		if ((newkp = prom_alloc_phys(ktsize, 4*MEG)) == 0 ) {
			prom_printf("Cannot allocate new kernel text\r\n");
			OF_exit();
		}
		BDPRINTF(PDB_BOOT1, ("Allocating new va for buffer at %llx\r\n",
				     (u_int64_t)newkp));
		if ((newkv = (vaddr_t)prom_alloc_virt(ktsize, 8)) ==
		    (vaddr_t)-1) {
			prom_printf("Cannot allocate new kernel text va\r\n");
			OF_exit();
		}
		BDPRINTF(PDB_BOOT1, ("Mapping in buffer %lx at %lx\r\n",
				     (u_long)newkp, (u_long)newkv));
		prom_map_phys(newkp, ktsize, (vaddr_t)newkv, -1); 
		BDPRINTF(PDB_BOOT1, ("Copying %ld bytes kernel text...",
			ktsize));
		bcopy((void *)ktext, (void *)newkv,
		    ktsize);
		BDPRINTF(PDB_BOOT1, ("done.  Swapping maps..unmap new\r\n"));
		prom_unmap_virt((vaddr_t)newkv, 4*MEG);
		BDPRINTF(PDB_BOOT, ("remap old "));
#if 0
		/*
		 * calling the prom will probably require reading part of the
		 * text segment so we can't do this.  
		 */
		prom_unmap_virt((vaddr_t)ktextp, ktsize);
#endif
		prom_map_phys(newkp, ktsize, ktext, -1); 
		/*
		 * we will map in 4MB, more than we allocated, to allow
		 * further allocation
		 */
		BDPRINTF(PDB_BOOT1, ("free old\r\n"));
		prom_free_phys(ktextp, ktsize);
		ktextp = newkp;
		
		BDPRINTF(PDB_BOOT1, 
			 ("pmap_bootstrap: firstaddr is %lx virt (%lx phys)"
			  "avail for kernel\r\n", (u_long)firstaddr,
			  (u_long)prom_vtop(firstaddr)));

		/*
		 * Re-fetch translations -- they've certainly changed.
		 */
		if (OF_getprop(vmemh, "translations", (void *)prom_map, sz) <=
			0) {
			prom_printf("no translations installed?");
			OF_exit();
		}
#ifdef DEBUG
		if (pmapdebug & PDB_BOOT) {
			/* print out mem list */
			prom_printf("New prom xlations:\r\n");
			for (i = 0; i < prom_map_size; i++) {
				prom_printf("start %016lx size %016lx tte %016lx\r\n",
					    (u_long)prom_map[i].vstart, 
					    (u_long)prom_map[i].vsize,
					    (u_long)prom_map[i].tte);
			}
			prom_printf("End of prom xlations\r\n");
		}
#endif
	} 
	ektextp = ktextp + ktsize;

	/*
	 * Here's a quick in-lined reverse bubble sort.  It gets rid of
	 * any translations inside the kernel data VA range.
	 */
	for(i = 0; i < prom_map_size; i++) {
		if (prom_map[i].vstart >= kdata &&
		    prom_map[i].vstart <= firstaddr) {
			prom_map[i].vstart = 0;
			prom_map[i].vsize = 0;
		}
		if (prom_map[i].vstart >= ktext &&
		    prom_map[i].vstart <= ektext) {
			prom_map[i].vstart = 0;
			prom_map[i].vsize = 0;
		}
		for(j = i; j < prom_map_size; j++) {
			if (prom_map[j].vstart >= kdata &&
			    prom_map[j].vstart <= firstaddr)
				continue;	/* this is inside the kernel */
			if (prom_map[j].vstart >= ktext &&
			    prom_map[j].vstart <= ektext)
				continue;	/* this is inside the kernel */
			if (prom_map[j].vstart > prom_map[i].vstart) {
				struct prom_map tmp;
				tmp = prom_map[i];
				prom_map[i] = prom_map[j];
				prom_map[j] = tmp;
			}
		}
	}
#ifdef DEBUG
	if (pmapdebug & PDB_BOOT) {
		/* print out mem list */
		prom_printf("Prom xlations:\r\n");
		for (i = 0; i < prom_map_size; i++) {
			prom_printf("start %016lx size %016lx tte %016lx\r\n", 
				    (u_long)prom_map[i].vstart, 
				    (u_long)prom_map[i].vsize,
				    (u_long)prom_map[i].tte);
		}
		prom_printf("End of prom xlations\r\n");
	}
#endif

	/*
	 * Allocate a 64KB page for the cpu_info structure now.
	 */
	if ((cpu0paddr = prom_alloc_phys(numcpus * 8*NBPG, 8*NBPG)) == 0 ) {
		prom_printf("Cannot allocate new cpu_info\r\n");
		OF_exit();
	}

	/*
	 * Now the kernel text segment is in its final location we can try to
	 * find out how much memory really is free.  
	 */
	sz = OF_getproplen(memh, "available") + sizeof(struct mem_region);
	valloc(orig, struct mem_region, sz);
	bzero((caddr_t)orig, sz);
	if (OF_getprop(memh, "available", orig, sz) <= 0) {
		prom_printf("no available RAM?");
		OF_exit();
	}
#ifdef DEBUG
	if (pmapdebug & PDB_BOOT1) {
		/* print out mem list */
		prom_printf("Available physical memory:\r\n");
		for (mp = orig; mp->size; mp++) {
			prom_printf("memlist start %lx size %lx\r\n",
				    (u_long)mp->start, (u_long)mp->size);
		}
		prom_printf("End of available physical memory\r\n");
	}
#endif
	valloc(avail, struct mem_region, sz);
	bzero((caddr_t)avail, sz);
	for (pcnt = 0, mp = orig, mp1 = avail; (mp1->size = mp->size);
	    mp++, mp1++) {
		mp1->start = mp->start;
		pcnt++;
	}

	/*
	 * Allocate and initialize a context table
	 */
	numctx = maxctx;
	valloc(ctxbusy, paddr_t, CTXSIZE);
	bzero((caddr_t)ctxbusy, CTXSIZE);

	/*
	 * Allocate our TSB.
	 *
	 * We will use the left over space to flesh out the kernel pmap.
	 */
	BDPRINTF(PDB_BOOT1, ("firstaddr before TSB=%lx\r\n", 
		(u_long)firstaddr));
	firstaddr = ((firstaddr + TSBSIZE - 1) & ~(TSBSIZE-1)); 
#ifdef DEBUG
	i = (firstaddr + (NBPG-1)) & ~(NBPG-1);	/* First, page align */
	if ((int)firstaddr < i) {
		prom_printf("TSB alloc fixup failed\r\n");
		prom_printf("frobbed i, firstaddr before TSB=%x, %lx\r\n",
		    (int)i, (u_long)firstaddr);
		panic("TSB alloc");
		OF_exit();
	}
#endif
	BDPRINTF(PDB_BOOT, ("frobbed i, firstaddr before TSB=%x, %lx\r\n", 
			    (int)i, (u_long)firstaddr));
	valloc(tsb_dmmu, pte_t, TSBSIZE);
	bzero(tsb_dmmu, TSBSIZE);
	valloc(tsb_immu, pte_t, TSBSIZE);
	bzero(tsb_immu, TSBSIZE);

	BDPRINTF(PDB_BOOT1, ("firstaddr after TSB=%lx\r\n", (u_long)firstaddr));
	BDPRINTF(PDB_BOOT1, ("TSB allocated at %p size %08x\r\n", (void *)tsb_dmmu,
	    (int)TSBSIZE));

#ifdef SUN4V
	if (CPU_ISSUN4V) {
		valloc(tsb_desc, struct tsb_desc, sizeof(struct tsb_desc));
		bzero(tsb_desc, sizeof(struct tsb_desc));
		tsb_desc->td_idxpgsz = 0;
		tsb_desc->td_assoc = 1;
		tsb_desc->td_size = TSBENTS;
		tsb_desc->td_ctxidx = -1;
		tsb_desc->td_pgsz = 0xf;
		tsb_desc->td_pa = (paddr_t)tsb_dmmu + kdatap - kdata;
	}
#endif

	BDPRINTF(PDB_BOOT1, ("firstaddr after pmap=%08lx\r\n", 
		(u_long)firstaddr));

	/*
	 * Page align all regions.  
	 * Non-page memory isn't very interesting to us.
	 * Also, sort the entries for ascending addresses.
	 * 
	 * And convert from virtual to physical addresses.
	 */
	
	BDPRINTF(PDB_BOOT, ("kernel virtual size %08lx - %08lx\r\n",
			    (u_long)kernelstart, (u_long)firstaddr));
	kdata = kdata & ~PGOFSET;
	ekdata = firstaddr;
	ekdata = (ekdata + PGOFSET) & ~PGOFSET;
	BDPRINTF(PDB_BOOT1, ("kernel virtual size %08lx - %08lx\r\n",
			     (u_long)kernelstart, (u_long)kernelend));
	ekdatap = ekdata - kdata + kdatap;
	/* Switch from vaddrs to paddrs */
	if(ekdatap > (kdatap + 4*MEG)) {
		prom_printf("Kernel size exceeds 4MB\r\n");
	}

#ifdef DEBUG
	if (pmapdebug & PDB_BOOT1) {
		/* print out mem list */
		prom_printf("Available %lx physical memory before cleanup:\r\n",
			    (u_long)avail);
		for (mp = avail; mp->size; mp++) {
			prom_printf("memlist start %lx size %lx\r\n", 
				    (u_long)mp->start, 
				    (u_long)mp->size);
		}
		prom_printf("End of available physical memory before cleanup\r\n");
		prom_printf("kernel physical text size %08lx - %08lx\r\n",
			    (u_long)ktextp, (u_long)ektextp);
		prom_printf("kernel physical data size %08lx - %08lx\r\n",
			    (u_long)kdatap, (u_long)ekdatap);
	}
#endif
	/*
	 * Here's a another quick in-lined bubble sort.
	 */
	for (i = 0; i < pcnt; i++) {
		for (j = i; j < pcnt; j++) {
			if (avail[j].start < avail[i].start) {
				struct mem_region tmp;
				tmp = avail[i];
				avail[i] = avail[j];
				avail[j] = tmp;
			}
		}
	}

	/* Throw away page zero if we have it. */
	if (avail->start == 0) {
		avail->start += NBPG;
		avail->size -= NBPG;
	}
	/*
	 * Now we need to remove the area we valloc'ed from the available
	 * memory lists.  (NB: we may have already alloc'ed the entire space).
	 */
	for (mp = avail; mp->size; mp++) {
		/*
		 * Check whether this region holds all of the kernel.
		 */
		s = mp->start + mp->size;
		if (mp->start < kdatap && s > roundup(ekdatap, 4*MEG)) {
			avail[pcnt].start = roundup(ekdatap, 4*MEG);
			avail[pcnt++].size = s - kdatap;
			mp->size = kdatap - mp->start;
		}
		/*
		 * Look whether this regions starts within the kernel.
		 */
		if (mp->start >= kdatap && 
			mp->start < roundup(ekdatap, 4*MEG)) {
			s = ekdatap - mp->start;
			if (mp->size > s)
				mp->size -= s;
			else
				mp->size = 0;
			mp->start = roundup(ekdatap, 4*MEG);
		}
		/*
		 * Now look whether this region ends within the kernel.
		 */
		s = mp->start + mp->size;
		if (s > kdatap && s < roundup(ekdatap, 4*MEG))
			mp->size -= s - kdatap;
		/*
		 * Now page align the start of the region.
		 */
		s = mp->start % NBPG;
		if (mp->size >= s) {
			mp->size -= s;
			mp->start += s;
		}
		/*
		 * And now align the size of the region.
		 */
		mp->size -= mp->size % NBPG;
		/*
		 * Check whether some memory is left here.
		 */
		if (mp->size == 0) {
			bcopy(mp + 1, mp,
			      (pcnt - (mp - avail)) * sizeof *mp);
			pcnt--;
			mp--;
			continue;
		}
		s = mp->start;
		sz = mp->size;
		for (mp1 = avail; mp1 < mp; mp1++)
			if (s < mp1->start)
				break;
		if (mp1 < mp) {
			bcopy(mp1, mp1 + 1, (char *)mp - (char *)mp1);
			mp1->start = s;
			mp1->size = sz;
		}
		/* 
		 * In future we should be able to specify both allocated
		 * and free.
		 */
		uvm_page_physload(
			atop(mp->start),
			atop(mp->start+mp->size),
			atop(mp->start),
			atop(mp->start+mp->size), 0);
	}

#if 0
	/* finally, free up any space that valloc did not use */
	prom_unmap_virt((vaddr_t)ekdata, roundup(ekdata, 4*MEG) - ekdata);
	if (ekdatap < roundup(kdatap, 4*MEG))) {
		uvm_page_physload(atop(ekdatap), 
			atop(roundup(ekdatap, (4*MEG))),
			atop(ekdatap), 
			atop(roundup(ekdatap, (4*MEG))), 0);
	}
#endif

#ifdef DEBUG
	if (pmapdebug & PDB_BOOT) {
		/* print out mem list */
		prom_printf("Available physical memory after cleanup:\r\n");
		for (mp = avail; mp->size; mp++) {
			prom_printf("avail start %lx size %lx\r\n", 
				    (long)mp->start, (long)mp->size);
		}
		prom_printf("End of available physical memory after cleanup\r\n");
	}
#endif
	/*
	 * Allocate and clear out pmap_kernel()->pm_segs[]
	 */
	mtx_init(&pmap_kernel()->pm_mtx, IPL_VM);
	pmap_kernel()->pm_refs = 1;
	pmap_kernel()->pm_ctx = 0;
	{
		paddr_t newp;

		do {
			pmap_get_page(&newp, NULL, pmap_kernel());
		} while (!newp); /* Throw away page zero */
		pmap_kernel()->pm_segs=(int64_t *)(u_long)newp;
		pmap_kernel()->pm_physaddr = newp;
		/* mark kernel context as busy */
		((paddr_t*)ctxbusy)[0] = pmap_kernel()->pm_physaddr;
	}
	/*
	 * finish filling out kernel pmap.
	 */

	BDPRINTF(PDB_BOOT, ("pmap_kernel()->pm_physaddr = %lx\r\n",
	    (long)pmap_kernel()->pm_physaddr));
	/*
	 * Tell pmap about our mesgbuf -- Hope this works already
	 */
#ifdef DEBUG
	BDPRINTF(PDB_BOOT1, ("Calling consinit()\r\n"));
	if (pmapdebug & PDB_BOOT1) consinit();
	BDPRINTF(PDB_BOOT1, ("Inserting mesgbuf into pmap_kernel()\r\n"));
#endif
	/* it's not safe to call pmap_enter so we need to do this ourselves */
	va = (vaddr_t)msgbufp;
	prom_map_phys(phys_msgbuf, msgbufsiz, (vaddr_t)msgbufp, -1);
	while (msgbufsiz) {
		data = TSB_DATA(0 /* global */, 
			PGSZ_8K,
			phys_msgbuf,
			1 /* priv */,
			1 /* Write */,
			1 /* Cacheable */,
			0 /* ALIAS -- Disable D$ */,
			1 /* valid */,
			0 /* IE */);
		pmap_enter_kpage(va, data);
		va += PAGE_SIZE;
		msgbufsiz -= PAGE_SIZE;
		phys_msgbuf += PAGE_SIZE;
	}
	BDPRINTF(PDB_BOOT1, ("Done inserting mesgbuf into pmap_kernel()\r\n"));
	
	BDPRINTF(PDB_BOOT1, ("Inserting PROM mappings into pmap_kernel()\r\n"));
	data = (CPU_ISSUN4V ? SUN4V_TLB_EXEC : SUN4U_TLB_EXEC);
	for (i = 0; i < prom_map_size; i++) {
		if (prom_map[i].vstart && ((prom_map[i].vstart>>32) == 0)) {
			for (j = 0; j < prom_map[i].vsize; j += NBPG) {
				int k;
				uint64_t tte;
				
				for (k = 0; page_size_map[k].mask; k++) {
					if (((prom_map[i].vstart |
					      prom_map[i].tte) &
					      page_size_map[k].mask) == 0 &&
					      page_size_map[k].mask <
					      prom_map[i].vsize)
						break;
				}
				/* Enter PROM map into pmap_kernel() */
				tte = prom_map[i].tte;
				if (CPU_ISSUN4V)
					tte &= ~SUN4V_TLB_SOFT_MASK;
				else
					tte &= ~(SUN4U_TLB_SOFT2_MASK |
					    SUN4U_TLB_SOFT_MASK);
				pmap_enter_kpage(prom_map[i].vstart + j,
				    (tte + j) | data | page_size_map[k].code);
			}
		}
	}
	BDPRINTF(PDB_BOOT1, ("Done inserting PROM mappings into pmap_kernel()\r\n"));

	/*
	 * Fix up start of kernel heap.
	 */
	vmmap = (vaddr_t)roundup(ekdata, 4*MEG);
	/* Let's keep 1 page of redzone after the kernel */
	vmmap += NBPG;
	{ 
		extern vaddr_t u0[2];
		extern struct pcb *proc0paddr;
		extern void main(void);
		paddr_t pa;

		/* Initialize all the pointers to u0 */
		u0[0] = vmmap;
		/* Allocate some VAs for u0 */
		u0[1] = vmmap + 2*USPACE;

		BDPRINTF(PDB_BOOT1, 
			("Inserting stack 0 into pmap_kernel() at %p\r\n",
				vmmap));

		while (vmmap < u0[1]) {
			int64_t data;

			pmap_get_page(&pa, NULL, pmap_kernel());
			prom_map_phys(pa, NBPG, vmmap, -1);
			data = TSB_DATA(0 /* global */,
				PGSZ_8K,
				pa,
				1 /* priv */,
				1 /* Write */,
				1 /* Cacheable */,
				0 /* ALIAS -- Disable D$ */,
				1 /* valid */,
				0 /* IE */);
			pmap_enter_kpage(vmmap, data);
			vmmap += NBPG;
		}
		BDPRINTF(PDB_BOOT1, 
			 ("Done inserting stack 0 into pmap_kernel()\r\n"));

		/* Now map in and initialize our cpu_info structure */
#ifdef DIAGNOSTIC
		vmmap += NBPG; /* redzone -- XXXX do we need one? */
#endif
		intstk = vmmap = roundup(vmmap, 64*KB);
		cpus = (struct cpu_info *)(intstk + CPUINFO_VA - INTSTACK);

		BDPRINTF(PDB_BOOT1,
			("Inserting cpu_info into pmap_kernel() at %p\r\n",
				 cpus));
		/* Now map in all 8 pages of cpu_info */
		pa = cpu0paddr;
		prom_map_phys(pa, 64*KB, vmmap, -1);
		/* 
		 * Also map it in as the interrupt stack.
		 * This lets the PROM see this if needed.
		 *
		 * XXXX locore.s does not flush these mappings
		 * before installing the locked TTE.
		 */
		prom_map_phys(pa, 64*KB, CPUINFO_VA, -1);
		for (i=0; i<8; i++) {
			int64_t data;

			data = TSB_DATA(0 /* global */,
				PGSZ_8K,
				pa,
				1 /* priv */,
				1 /* Write */,
				1 /* Cacheable */,
				0 /* ALIAS -- Disable D$ */,
				1 /* valid */,
				0 /* IE */);
			pmap_enter_kpage(vmmap, data);
			vmmap += NBPG;
			pa += NBPG;
		}
		BDPRINTF(PDB_BOOT1, ("Initializing cpu_info\r\n"));

		/* Initialize our cpu_info structure */
		bzero((void *)intstk, 8*NBPG);
		cpus->ci_self = cpus;
		cpus->ci_next = NULL; /* Redundant, I know. */
		cpus->ci_curproc = &proc0;
		cpus->ci_cpcb = (struct pcb *)u0[0]; /* Need better source */
		cpus->ci_cpcbpaddr = pseg_get(pmap_kernel(), u0[0]) &
		    TLB_PA_MASK;
		cpus->ci_upaid = cpu_myid();
		cpus->ci_cpuid = 0;
		cpus->ci_flags = CPUF_RUNNING;
		cpus->ci_fpproc = NULL;
		cpus->ci_spinup = main; /* Call main when we're running. */
		cpus->ci_initstack = (void *)u0[1];
		cpus->ci_paddr = cpu0paddr;
#ifdef SUN4V
		cpus->ci_mmfsa = cpu0paddr;
#endif
		proc0paddr = cpus->ci_cpcb;

		cpu0paddr += 64 * KB;

		/* The rest will be done at CPU attach time. */
		BDPRINTF(PDB_BOOT1, 
			 ("Done inserting cpu_info into pmap_kernel()\r\n"));
	}

	vmmap = (vaddr_t)reserve_dumppages((caddr_t)(u_long)vmmap);
	BDPRINTF(PDB_BOOT1, ("Finished pmap_bootstrap()\r\n"));

	pmap_bootstrap_cpu(cpus->ci_paddr);
}

void sun4u_bootstrap_cpu(paddr_t);
void sun4v_bootstrap_cpu(paddr_t);

void
pmap_bootstrap_cpu(paddr_t intstack)
{
	if (CPU_ISSUN4V)
		sun4v_bootstrap_cpu(intstack);
	else
		sun4u_bootstrap_cpu(intstack);
}

extern void sun4u_set_tsbs(void);

void
sun4u_bootstrap_cpu(paddr_t intstack)
{
	u_int64_t data;
	paddr_t pa;
	vaddr_t va;
	int index;
	int impl;

	impl = (getver() & VER_IMPL) >> VER_IMPL_SHIFT;

	/*
	 * Establish the 4MB locked mappings for kernel data and text.
	 *
	 * The text segment needs to be mapped into the DTLB too,
	 * because of .rodata.
	 */

	index = 15; /* XXX */
	for (va = ktext, pa = ktextp; va < ektext; va += 4*MEG, pa += 4*MEG) {
		data = SUN4U_TSB_DATA(0, PGSZ_4M, pa, 1, 0, 1, 0, 1, 0);
		data |= SUN4U_TLB_L;
		prom_itlb_load(index, data, va);
		prom_dtlb_load(index, data, va);
		index--;
	}

	for (va = kdata, pa = kdatap; va < ekdata; va += 4*MEG, pa += 4*MEG) {
		data = SUN4U_TSB_DATA(0, PGSZ_4M, pa, 1, 1, 1, 0, 1, 0);
		data |= SUN4U_TLB_L;
		prom_dtlb_load(index, data, va);
		index--;
	}

#ifdef MULTIPROCESSOR
	if (impl >= IMPL_OLYMPUS_C && impl <= IMPL_JUPITER) {
		/*
		 * On SPARC64-VI and SPARC64-VII processors, the MMU is
		 * shared between threads, so we can't establish a locked
		 * mapping for the interrupt stack since the mappings would
		 * conflict.  Instead we stick the address in a scratch
		 * register, like we do for sun4v.
		 */
		pa = intstack + (CPUINFO_VA - INTSTACK);
		pa += offsetof(struct cpu_info, ci_self);
		va = ldxa(pa, ASI_PHYS_CACHED);
		stxa(0x00, ASI_SCRATCH, va);

		if ((CPU_JUPITERID % 2) == 1)
			index--;

		data = SUN4U_TSB_DATA(0, PGSZ_64K, intstack, 1, 1, 1, 0, 1, 0);
		data |= SUN4U_TLB_L;
		prom_dtlb_load(index, data, va - (CPUINFO_VA - INTSTACK));

		sun4u_set_tsbs();
		return;
	}
#endif

	/*
	 * Establish the 64KB locked mapping for the interrupt stack.
	 */

	data = SUN4U_TSB_DATA(0, PGSZ_64K, intstack, 1, 1, 1, 0, 1, 0);
	data |= SUN4U_TLB_L;
	prom_dtlb_load(index, data, INTSTACK);

	sun4u_set_tsbs();
}

void
sun4v_bootstrap_cpu(paddr_t intstack)
{
#ifdef SUN4V
	u_int64_t data;
	paddr_t pa;
	vaddr_t va;
	int err;

	/*
	 * Establish the 4MB locked mappings for kernel data and text.
	 *
	 * The text segment needs to be mapped into the DTLB too,
	 * because of .rodata.
	 */

	for (va = ktext, pa = ktextp; va < ektext; va += 4*MEG, pa += 4*MEG) {
		data = SUN4V_TSB_DATA(0, PGSZ_4M, pa, 1, 0, 1, 0, 1, 0);
		data |= SUN4V_TLB_X;
		err = hv_mmu_map_perm_addr(va, data, MAP_ITLB|MAP_DTLB);
		if (err != H_EOK)
			prom_printf("err: %d\r\n", err);
	}

	for (va = kdata, pa = kdatap; va < ekdata; va += 4*MEG, pa += 4*MEG) {
		data = SUN4V_TSB_DATA(0, PGSZ_4M, pa, 1, 1, 1, 0, 1, 0);
		err = hv_mmu_map_perm_addr(va, data, MAP_DTLB);
		if (err != H_EOK)
			prom_printf("err: %d\r\n", err);
	}

#ifndef MULTIPROCESSOR
	/*
	 * Establish the 64KB locked mapping for the interrupt stack.
	 */
	data = SUN4V_TSB_DATA(0, PGSZ_64K, intstack, 1, 1, 1, 0, 1, 0);
	err = hv_mmu_map_perm_addr(INTSTACK, data, MAP_DTLB);
	if (err != H_EOK)
		prom_printf("err: %d\r\n", err);
#else
	pa = intstack + (CPUINFO_VA - INTSTACK);
	pa += offsetof(struct cpu_info, ci_self);
	stxa(0x00, ASI_SCRATCHPAD, ldxa(pa, ASI_PHYS_CACHED));
#endif

	stxa(0x10, ASI_SCRATCHPAD, intstack + (CPUINFO_VA - INTSTACK));

	err = hv_mmu_tsb_ctx0(1, (paddr_t)tsb_desc + kdatap - kdata);
	if (err != H_EOK)
		prom_printf("err: %d\r\n", err);
	err = hv_mmu_tsb_ctxnon0(1, (paddr_t)tsb_desc + kdatap - kdata);
	if (err != H_EOK)
		prom_printf("err: %d\r\n", err);
#endif
}

/*
 * Initialize anything else for pmap handling.
 * Called during uvm_init().
 */
void
pmap_init(void)
{
	BDPRINTF(PDB_BOOT1, ("pmap_init()\r\n"));
	if (PAGE_SIZE != NBPG)
		panic("pmap_init: CLSIZE!=1");

	/* Setup a pool for additional pvlist structures */
	pool_init(&pv_pool, sizeof(struct pv_entry), 0, IPL_VM, 0,
	    "pv_entry", NULL);
	pool_init(&pmap_pool, sizeof(struct pmap), 0, IPL_NONE, 0,
	    "pmappl", NULL);
}

/* Start of non-cachable physical memory on UltraSPARC-III. */
#define VM_MAXPHYS_ADDRESS	((vaddr_t)0x0000040000000000L)

static vaddr_t kbreak; /* End of kernel VA */

/*
 * How much virtual space is available to the kernel?
 */
void
pmap_virtual_space(vaddr_t *start, vaddr_t *end)
{
	/*
	 * Make sure virtual memory and physical memory don't overlap
	 * to avoid problems with ASI_PHYS_CACHED on UltraSPARC-III.
	 */
	if (vmmap < VM_MAXPHYS_ADDRESS)
		vmmap = VM_MAXPHYS_ADDRESS;

	/* Reserve two pages for pmap_copy_page && /dev/mem */
	*start = kbreak = (vaddr_t)(vmmap + 2*NBPG);
	*end = VM_MAX_KERNEL_ADDRESS;
	BDPRINTF(PDB_BOOT1, ("pmap_virtual_space: %x-%x\r\n", *start, *end));
}

/*
 * Preallocate kernel page tables to a specified VA.
 * This simply loops through the first TTE for each
 * page table from the beginning of the kernel pmap, 
 * reads the entry, and if the result is
 * zero (either invalid entry or no page table) it stores
 * a zero there, populating page tables in the process.
 * This is not the most efficient technique but i don't
 * expect it to be called that often.
 */
vaddr_t 
pmap_growkernel(vaddr_t maxkvaddr)
{
	paddr_t pg;
	struct pmap *pm = pmap_kernel();
	
	if (maxkvaddr >= VM_MAX_KERNEL_ADDRESS) {
		printf("WARNING: cannot extend kernel pmap beyond %p to %p\n",
		       (void *)VM_MAX_KERNEL_ADDRESS, (void *)maxkvaddr);
		return (kbreak);
	}

	/* Align with the start of a page table */
	for (kbreak &= (-1<<PDSHIFT); kbreak < maxkvaddr;
	     kbreak += (1<<PDSHIFT)) {
		if (pseg_get(pm, kbreak))
			continue;

		pg = 0;
		while (pseg_set(pm, kbreak, 0, pg) == 1) {
			pg = 0;
			pmap_get_page(&pg, "growk", pm);
		}
		
	}

	return (kbreak);
}

/*
 * Create and return a physical map.
 */
struct pmap *
pmap_create(void)
{
	struct pmap *pm;

	pm = pool_get(&pmap_pool, PR_WAITOK | PR_ZERO);

	mtx_init(&pm->pm_mtx, IPL_VM);
	pm->pm_refs = 1;
	pmap_get_page(&pm->pm_physaddr, "pmap_create", pm);
	pm->pm_segs = (int64_t *)(u_long)pm->pm_physaddr;
	ctx_alloc(pm);

	return (pm);
}

/*
 * Add a reference to the given pmap.
 */
void
pmap_reference(struct pmap *pm)
{
	atomic_inc_int(&pm->pm_refs);
}

/*
 * Retire the given pmap from service.
 * Should only be called if the map contains no valid mappings.
 */
void
pmap_destroy(struct pmap *pm)
{
	if (atomic_dec_int_nv(&pm->pm_refs) == 0) {
		pmap_release(pm);
		pool_put(&pmap_pool, pm);
	}
}

/*
 * Release any resources held by the given physical map.
 * Called when a pmap initialized by pmap_create is being released.
 */
void
pmap_release(struct pmap *pm)
{
	int i, j, k;
	paddr_t *pdir, *ptbl, tmp;

#ifdef DIAGNOSTIC
	if(pm == pmap_kernel())
		panic("pmap_release: releasing pmap_kernel()");
#endif

	mtx_enter(&pm->pm_mtx);
	for(i=0; i<STSZ; i++) {
		paddr_t psegentp = (paddr_t)(u_long)&pm->pm_segs[i];
		if((pdir = (paddr_t *)(u_long)ldxa((vaddr_t)psegentp,
		    ASI_PHYS_CACHED))) {
			for (k=0; k<PDSZ; k++) {
				paddr_t pdirentp = (paddr_t)(u_long)&pdir[k];
				if ((ptbl = (paddr_t *)(u_long)ldxa(
					(vaddr_t)pdirentp, ASI_PHYS_CACHED))) {
					for (j=0; j<PTSZ; j++) {
						int64_t data;
						paddr_t pa;
						pv_entry_t pv;

						data  = ldxa((vaddr_t)&ptbl[j],
							ASI_PHYS_CACHED);
						if (!(data & TLB_V))
							continue;
						pa = data & TLB_PA_MASK;
						pv = pa_to_pvh(pa);
						if (pv != NULL) {
							printf("pmap_release: pm=%p page %llx still in use\n", pm, 
							       (unsigned long long)(((u_int64_t)i<<STSHIFT)|((u_int64_t)k<<PDSHIFT)|((u_int64_t)j<<PTSHIFT)));
							db_enter();
						}
					}
					stxa(pdirentp, ASI_PHYS_CACHED, 0);
					pmap_free_page((paddr_t)ptbl, pm);
				}
			}
			stxa(psegentp, ASI_PHYS_CACHED, 0);
			pmap_free_page((paddr_t)pdir, pm);
		}
	}
	tmp = (paddr_t)(u_long)pm->pm_segs;
	pm->pm_segs = NULL;
	pmap_free_page(tmp, pm);
	mtx_leave(&pm->pm_mtx);
	ctx_free(pm);
}

void
pmap_zero_page(struct vm_page *pg)
{
	pmap_zero_phys(VM_PAGE_TO_PHYS(pg));
}

void
pmap_copy_page(struct vm_page *srcpg, struct vm_page *dstpg)
{
	paddr_t src = VM_PAGE_TO_PHYS(srcpg);
	paddr_t dst = VM_PAGE_TO_PHYS(dstpg);

	pmap_copy_phys(src, dst);
}

/*
 * Activate the address space for the specified process.  If the
 * process is the current process, load the new MMU context.
 */
void
pmap_activate(struct proc *p)
{
	struct pmap *pmap = p->p_vmspace->vm_map.pmap;
	int s;

	/*
	 * This is essentially the same thing that happens in cpu_switch()
	 * when the newly selected process is about to run, except that we
	 * have to make sure to clean the register windows before we set
	 * the new context.
	 */

	s = splvm();
	if (p == curproc) {
		write_user_windows();
		if (pmap->pm_ctx == 0)
			ctx_alloc(pmap);
		if (CPU_ISSUN4V)
			stxa(CTX_SECONDARY, ASI_MMU_CONTEXTID, pmap->pm_ctx);
		else
			stxa(CTX_SECONDARY, ASI_DMMU, pmap->pm_ctx);
	}
	splx(s);
}

/*
 * Deactivate the address space of the specified process.
 */
void
pmap_deactivate(struct proc *p)
{
}

void
pmap_purge(struct proc *p)
{
	/*
	 * Write out the user windows before we tear down the userland
	 * mappings.
	 */
	write_user_windows();
}

/*
 * pmap_kenter_pa:		[ INTERFACE ]
 *
 *	Enter a va -> pa mapping into the kernel pmap without any
 *	physical->virtual tracking.
 *
 *	Note: no locking is necessary in this function.
 */
void
pmap_kenter_pa(vaddr_t va, paddr_t pa, vm_prot_t prot)
{
	struct pmap *pm = pmap_kernel();
	pte_t tte;

	KDASSERT(va < INTSTACK || va > EINTSTACK);
	KDASSERT(va < kdata || va > ekdata);

#ifdef DIAGNOSTIC
	if (pa & (PMAP_NVC|PMAP_NC|PMAP_LITTLE))
		panic("%s: illegal cache flags 0x%lx", __func__, pa);
#endif

	/*
	 * Construct the TTE.
	 */
	tte.tag = TSB_TAG(0, pm->pm_ctx,va);
	if (CPU_ISSUN4V) {
		tte.data = SUN4V_TSB_DATA(0, PGSZ_8K, pa, 1 /* Privileged */,
		    (PROT_WRITE & prot), 1, 0, 1, 0);
		/*
		 * We don't track modification on kenter mappings.
		 */
		if (prot & PROT_WRITE)
			tte.data |= SUN4V_TLB_REAL_W|SUN4V_TLB_W;
		if (prot & PROT_EXEC)
			tte.data |= SUN4V_TLB_EXEC;
		tte.data |= SUN4V_TLB_TSB_LOCK;	/* wired */
	} else {
		tte.data = SUN4U_TSB_DATA(0, PGSZ_8K, pa, 1 /* Privileged */,
		    (PROT_WRITE & prot), 1, 0, 1, 0);
		/*
		 * We don't track modification on kenter mappings.
		 */
		if (prot & PROT_WRITE)
			tte.data |= SUN4U_TLB_REAL_W|SUN4U_TLB_W;
		if (prot & PROT_EXEC)
			tte.data |= SUN4U_TLB_EXEC;
		if (prot == PROT_EXEC)
			tte.data |= SUN4U_TLB_EXEC_ONLY;
		tte.data |= SUN4U_TLB_TSB_LOCK;	/* wired */
	}
	KDASSERT((tte.data & TLB_NFO) == 0);

	/* Kernel page tables are pre-allocated. */
	if (pseg_set(pm, va, tte.data, 0) != 0)
		panic("%s: no pseg", __func__);

	/* this is correct */
	dcache_flush_page(pa);
}

/*
 * pmap_kremove:		[ INTERFACE ]
 *
 *	Remove a mapping entered with pmap_kenter_pa() starting at va,
 *	for size bytes (assumed to be page rounded).
 */
void
pmap_kremove(vaddr_t va, vsize_t size)
{
	struct pmap *pm = pmap_kernel();

	KDASSERT(va < INTSTACK || va > EINTSTACK);
	KDASSERT(va < kdata || va > ekdata);

	while (size >= NBPG) {
		/*
		 * Is this part of the permanent 4MB mapping?
		 */
#ifdef DIAGNOSTIC
		if (pm == pmap_kernel() && 
		    (va >= ktext && va < roundup(ekdata, 4*MEG)))
			panic("%s: va=0x%lx in locked TLB", __func__, va);
#endif
		/* Shouldn't need to do this if the entry's not valid. */
		if (pseg_get(pm, va)) {
			/* We need to flip the valid bit and clear the access statistics. */
			if (pseg_set(pm, va, 0, 0)) {
				printf("pmap_kremove: gotten pseg empty!\n");
				db_enter();
				/* panic? */
			}

			tsb_invalidate(pm->pm_ctx, va);
			/* Here we assume nothing can get into the TLB unless it has a PTE */
			tlb_flush_pte(va, pm->pm_ctx);
		}
		va += NBPG;
		size -= NBPG;
	}
}

/*
 * Insert physical page at pa into the given pmap at virtual address va.
 * Supports 64-bit pa so we can map I/O space.
 */
int
pmap_enter(struct pmap *pm, vaddr_t va, paddr_t pa, vm_prot_t prot, int flags)
{
	pte_t tte;
	paddr_t pg;
	int aliased = 0;
	pv_entry_t pv, npv;
	int size = 0; /* PMAP_SZ_TO_TTE(pa); */
	boolean_t wired = (flags & PMAP_WIRED) != 0;

	/*
	 * Is this part of the permanent mappings?
	 */
	KDASSERT(pm != pmap_kernel() || va < INTSTACK || va > EINTSTACK);
	KDASSERT(pm != pmap_kernel() || va < kdata || va > ekdata);

	npv = pool_get(&pv_pool, PR_NOWAIT);
	if (npv == NULL && (flags & PMAP_CANFAIL))
		return (ENOMEM);

	/*
	 * XXXX If a mapping at this address already exists, remove it.
	 */
	mtx_enter(&pm->pm_mtx);
	tte.data = pseg_get(pm, va);
	if (tte.data & TLB_V) {
		mtx_leave(&pm->pm_mtx);
		pmap_remove(pm, va, va + NBPG-1);
		mtx_enter(&pm->pm_mtx);
		tte.data = pseg_get(pm, va);
	}

	/*
	 * Construct the TTE.
	 */
	pv = pa_to_pvh(pa);
	if (pv != NULL) {
		struct vm_page *pg = PHYS_TO_VM_PAGE(pa);

		mtx_enter(&pg->mdpage.pvmtx);
		aliased = (pv->pv_va & PV_ALIAS);
#ifdef DIAGNOSTIC
		if ((flags & PROT_MASK) & ~prot)
			panic("pmap_enter: access_type exceeds prot");
#endif
		/* If we don't have the traphandler do it, set the ref/mod bits now */
		if (flags & PROT_MASK)
			pv->pv_va |= PV_REF;
		if (flags & PROT_WRITE)
			pv->pv_va |= PV_MOD;
		pv->pv_va |= pmap_tte2flags(tte.data);
		mtx_leave(&pg->mdpage.pvmtx);
	} else {
		aliased = 0;
	}
	if (pa & PMAP_NVC)
		aliased = 1;
	if (CPU_ISSUN4V) {
		tte.data = SUN4V_TSB_DATA(0, size, pa, pm == pmap_kernel(),
		    (flags & PROT_WRITE), (!(pa & PMAP_NC)), 
		    aliased, 1, (pa & PMAP_LITTLE));
		if (prot & PROT_WRITE)
			tte.data |= SUN4V_TLB_REAL_W;
		if (prot & PROT_EXEC)
			tte.data |= SUN4V_TLB_EXEC;
		if (wired)
			tte.data |= SUN4V_TLB_TSB_LOCK;
	} else {
		tte.data = SUN4U_TSB_DATA(0, size, pa, pm == pmap_kernel(),
		    (flags & PROT_WRITE), (!(pa & PMAP_NC)), 
		    aliased, 1, (pa & PMAP_LITTLE));
		if (prot & PROT_WRITE)
			tte.data |= SUN4U_TLB_REAL_W;
		if (prot & PROT_EXEC)
			tte.data |= SUN4U_TLB_EXEC;
		if (prot == PROT_EXEC)
			tte.data |= SUN4U_TLB_EXEC_ONLY;
		if (wired)
			tte.data |= SUN4U_TLB_TSB_LOCK;
	}
	KDASSERT((tte.data & TLB_NFO) == 0);

	pg = 0;
	while (pseg_set(pm, va, tte.data, pg) == 1) {
		pg = 0;
		if (!pmap_get_page(&pg, NULL, pm)) {
			if ((flags & PMAP_CANFAIL) == 0)
				panic("pmap_enter: no memory");
			mtx_leave(&pm->pm_mtx);
			if (npv != NULL)
				pool_put(&pv_pool, npv);
			return (ENOMEM);
		}
	}

	if (pv != NULL)
		npv = pmap_enter_pv(pm, npv, va, pa);
	atomic_inc_long(&pm->pm_stats.resident_count);
	mtx_leave(&pm->pm_mtx);
	if (pm->pm_ctx || pm == pmap_kernel()) {
		tsb_invalidate(pm->pm_ctx, va);

		/* Force reload -- protections may be changed */
		tlb_flush_pte(va, pm->pm_ctx);	
	}
	/* this is correct */
	dcache_flush_page(pa);

	if (npv != NULL)
		pool_put(&pv_pool, npv);

	/* We will let the fast mmu miss interrupt load the new translation */
	return 0;
}

/*
 * Remove the given range of mapping entries.
 */
void
pmap_remove(struct pmap *pm, vaddr_t va, vaddr_t endva)
{
	pv_entry_t pv, freepvs = NULL;
	int flush = 0;
	int64_t data;
	vaddr_t flushva = va;

	/* 
	 * In here we should check each pseg and if there are no more entries,
	 * free it.  It's just that linear scans of 8K pages gets expensive.
	 */

	KDASSERT(pm != pmap_kernel() || endva < INTSTACK || va > EINTSTACK);
	KDASSERT(pm != pmap_kernel() || endva < kdata || va > ekdata);

	mtx_enter(&pm->pm_mtx);

	/* Now do the real work */
	while (va < endva) {
		/*
		 * Is this part of the permanent 4MB mapping?
		 */
#ifdef DIAGNOSTIC
		if (pm == pmap_kernel() && va >= ktext && 
			va < roundup(ekdata, 4*MEG))
			panic("pmap_remove: va=%08x in locked TLB", (u_int)va);
#endif
		/* We don't really need to do this if the valid bit is not set... */
		if ((data = pseg_get(pm, va)) && (data & TLB_V) != 0) {
			paddr_t entry;
			
			flush |= 1;
			/* First remove it from the pv_table */
			entry = (data & TLB_PA_MASK);
			pv = pa_to_pvh(entry);
			if (pv != NULL) {
				pv = pmap_remove_pv(pm, va, entry);
				if (pv != NULL) {
					pv->pv_next = freepvs;
					freepvs = pv;
				}
			}
			/* We need to flip the valid bit and clear the access statistics. */
			if (pseg_set(pm, va, 0, 0)) {
				printf("pmap_remove: gotten pseg empty!\n");
				db_enter();
				/* panic? */
			}
			atomic_dec_long(&pm->pm_stats.resident_count);
			if (!pm->pm_ctx && pm != pmap_kernel())
				continue;
			tsb_invalidate(pm->pm_ctx, va);
			/* Here we assume nothing can get into the TLB unless it has a PTE */
			tlb_flush_pte(va, pm->pm_ctx);
		}
		va += NBPG;
	}

	mtx_leave(&pm->pm_mtx);

	while ((pv = freepvs) != NULL) {
		freepvs = pv->pv_next;
		pool_put(&pv_pool, pv);
	}

	if (flush)
		cache_flush_virt(flushva, endva - flushva);
}

/*
 * Change the protection on the specified range of this pmap.
 */
void
pmap_protect(struct pmap *pm, vaddr_t sva, vaddr_t eva, vm_prot_t prot)
{
	paddr_t pa;
	pv_entry_t pv;
	int64_t data;
	
	KDASSERT(pm != pmap_kernel() || eva < INTSTACK || sva > EINTSTACK);
	KDASSERT(pm != pmap_kernel() || eva < kdata || sva > ekdata);

	if ((prot & (PROT_WRITE | PROT_EXEC)) == (PROT_WRITE | PROT_EXEC))
		return;

	if (prot == PROT_NONE) {
		pmap_remove(pm, sva, eva);
		return;
	}
		
	mtx_enter(&pm->pm_mtx);
	sva = sva & ~PGOFSET;
	while (sva < eva) {
		/*
		 * Is this part of the permanent 4MB mapping?
		 */
		if (pm == pmap_kernel() && sva >= ktext && 
			sva < roundup(ekdata, 4*MEG)) {
			prom_printf("pmap_protect: va=%08x in locked TLB\r\n", sva);
			OF_enter();
			mtx_leave(&pm->pm_mtx);
			return;
		}

		if (((data = pseg_get(pm, sva))&TLB_V) /*&& ((data&TLB_TSB_LOCK) == 0)*/) {
			pa = data & TLB_PA_MASK;
			pv = pa_to_pvh(pa);
			if (pv != NULL) {
				struct vm_page *pg = PHYS_TO_VM_PAGE(pa);

				/* Save REF/MOD info */
				mtx_enter(&pg->mdpage.pvmtx);
				pv->pv_va |= pmap_tte2flags(data);
				mtx_leave(&pg->mdpage.pvmtx);
			}
			/* Just do the pmap and TSB, not the pv_list */
			if (CPU_ISSUN4V) {
				if ((prot & PROT_WRITE) == 0)
					data &= ~(SUN4V_TLB_W|SUN4V_TLB_REAL_W);
				if ((prot & PROT_EXEC) == 0)
					data &= ~(SUN4V_TLB_EXEC);
			} else {
				if ((prot & PROT_WRITE) == 0)
					data &= ~(SUN4U_TLB_W|SUN4U_TLB_REAL_W);
				if ((prot & PROT_EXEC) == 0)
					data &= ~(SUN4U_TLB_EXEC | SUN4U_TLB_EXEC_ONLY);
			}
			KDASSERT((data & TLB_NFO) == 0);
			if (pseg_set(pm, sva, data, 0)) {
				printf("pmap_protect: gotten pseg empty!\n");
				db_enter();
				/* panic? */
			}
			
			if (!pm->pm_ctx && pm != pmap_kernel())
				continue;
			tsb_invalidate(pm->pm_ctx, sva);
			tlb_flush_pte(sva, pm->pm_ctx);
		}
		sva += NBPG;
	}
	mtx_leave(&pm->pm_mtx);
}

/*
 * Extract the physical page address associated
 * with the given map/virtual_address pair.
 */
boolean_t
pmap_extract(struct pmap *pm, vaddr_t va, paddr_t *pap)
{
	paddr_t pa;

	if (pm == pmap_kernel()) {
		if (va >= kdata && va < roundup(ekdata, 4*MEG)) {
			/* Need to deal w/locked TLB entry specially. */
			pa = (paddr_t)(kdatap - kdata + va);
		} else if (va >= ktext && va < ektext) {
			/* Need to deal w/locked TLB entry specially. */
			pa = (paddr_t)(ktextp - ktext + va);
		} else if (va >= INTSTACK && va < EINTSTACK) {
			pa = curcpu()->ci_paddr + va - INTSTACK;
		} else {
			goto check_pseg;
		}
	} else {
check_pseg:
		mtx_enter(&pm->pm_mtx);
		pa = pseg_get(pm, va) & TLB_PA_MASK;
		mtx_leave(&pm->pm_mtx);
		if (pa == 0)
			return FALSE;
		pa |= va & PAGE_MASK;
	}
	if (pap != NULL)
		*pap = pa;
	return TRUE;
}

/*
 * Return the number bytes that pmap_dumpmmu() will dump.
 */
int
pmap_dumpsize(void)
{
	int	sz;

	sz = ALIGN(sizeof(kcore_seg_t)) + ALIGN(sizeof(cpu_kcore_hdr_t));
	sz += memsize * sizeof(phys_ram_seg_t);

	return btodb(sz + DEV_BSIZE - 1);
}

/*
 * Write the mmu contents to the dump device.
 * This gets appended to the end of a crash dump since
 * there is no in-core copy of kernel memory mappings on a 4/4c machine.
 *
 * Write the core dump headers and MD data to the dump device.
 * We dump the following items:
 * 
 *	kcore_seg_t		 MI header defined in <sys/kcore.h>)
 *	cpu_kcore_hdr_t		 MD header defined in <machine/kcore.h>)
 *	phys_ram_seg_t[memsize]  physical memory segments
 */
int
pmap_dumpmmu(int (*dump)(dev_t, daddr_t, caddr_t, size_t), daddr_t blkno)
{
	kcore_seg_t	*kseg;
	cpu_kcore_hdr_t	*kcpu;
	phys_ram_seg_t	memseg;
	register int	error = 0;
	register int	i, memsegoffset;
	int		buffer[dbtob(1) / sizeof(int)];
	int		*bp, *ep;

#define EXPEDITE(p,n) do {						\
	int *sp = (int *)(p);						\
	int sz = (n);							\
	while (sz > 0) {						\
		*bp++ = *sp++;						\
		if (bp >= ep) {						\
			error = (*dump)(dumpdev, blkno,			\
					(caddr_t)buffer, dbtob(1));	\
			if (error != 0)					\
				return (error);				\
			++blkno;					\
			bp = buffer;					\
		}							\
		sz -= 4;						\
	}								\
} while (0)

	/* Setup bookkeeping pointers */
	bp = buffer;
	ep = &buffer[sizeof(buffer) / sizeof(buffer[0])];

	/* Fill in MI segment header */
	kseg = (kcore_seg_t *)bp;
	CORE_SETMAGIC(*kseg, KCORE_MAGIC, MID_MACHINE, CORE_CPU);
	kseg->c_size = dbtob(pmap_dumpsize()) - ALIGN(sizeof(kcore_seg_t));

	/* Fill in MD segment header (interpreted by MD part of libkvm) */
	kcpu = (cpu_kcore_hdr_t *)((long)bp + ALIGN(sizeof(kcore_seg_t)));
	kcpu->cputype = CPU_SUN4U;
	kcpu->kernbase = (u_int64_t)KERNBASE;
	kcpu->cpubase = (u_int64_t)CPUINFO_VA;

	/* Describe the locked text segment */
	kcpu->ktextbase = (u_int64_t)ktext;
	kcpu->ktextp = (u_int64_t)ktextp;
	kcpu->ktextsz = (u_int64_t)(roundup(ektextp, 4*MEG) - ktextp);

	/* Describe locked data segment */
	kcpu->kdatabase = (u_int64_t)kdata;
	kcpu->kdatap = (u_int64_t)kdatap;
	kcpu->kdatasz = (u_int64_t)(roundup(ekdatap, 4*MEG) - kdatap);

	/* Now the memsegs */
	kcpu->nmemseg = memsize;
	kcpu->memsegoffset = memsegoffset = ALIGN(sizeof(cpu_kcore_hdr_t));

	/* Now we need to point this at our kernel pmap. */
	kcpu->nsegmap = STSZ;
	kcpu->segmapoffset = (u_int64_t)pmap_kernel()->pm_physaddr;

	/* Note: we have assumed everything fits in buffer[] so far... */
	bp = (int *)((long)kcpu + ALIGN(sizeof(cpu_kcore_hdr_t)));

	for (i = 0; i < memsize; i++) {
		memseg.start = mem[i].start;
		memseg.size = mem[i].size;
		EXPEDITE(&memseg, sizeof(phys_ram_seg_t));
	}

	if (bp != buffer)
		error = (*dump)(dumpdev, blkno++, (caddr_t)buffer, dbtob(1));

	return (error);
}

/*
 * Determine (non)existence of physical page
 */
int
pmap_pa_exists(paddr_t pa)
{
	struct mem_region *mp;

	/* Just go through physical memory list & see if we're there */
	for (mp = mem; mp->size && mp->start <= pa; mp++)
		if (mp->start <= pa && mp->start + mp->size >= pa)
			return 1;
	return 0;
}

/*
 * Lookup the appropriate TSB entry.
 *
 * Here is the full official pseudo code:
 *
 */

#ifdef NOTYET
int64 GenerateTSBPointer(
 	int64 va,		/* Missing VA			*/
 	PointerType type,	/* 8K_POINTER or 16K_POINTER	*/
 	int64 TSBBase,		/* TSB Register[63:13] << 13	*/
 	Boolean split,		/* TSB Register[12]		*/
 	int TSBSize)		/* TSB Register[2:0]		*/
{
 	int64 vaPortion;
 	int64 TSBBaseMask;
 	int64 splitMask;
 
	/* TSBBaseMask marks the bits from TSB Base Reg		*/
	TSBBaseMask = 0xffffffffffffe000 <<
		(split? (TSBsize + 1) : TSBsize);

	/* Shift va towards lsb appropriately and		*/
	/* zero out the original va page offset			*/
	vaPortion = (va >> ((type == 8K_POINTER)? 9: 12)) &
		0xfffffffffffffff0;
	
	if (split) {
		/* There's only one bit in question for split	*/
		splitMask = 1 << (13 + TSBsize);
		if (type == 8K_POINTER)
			/* Make sure we're in the lower half	*/
			vaPortion &= ~splitMask;
		else
			/* Make sure we're in the upper half	*/
			vaPortion |= splitMask;
	}
	return (TSBBase & TSBBaseMask) | (vaPortion & ~TSBBaseMask);
}
#endif
/*
 * Of course, since we are not using a split TSB or variable page sizes,
 * we can optimize this a bit.  
 *
 * The following only works for a unified 8K TSB.  It will find the slot
 * for that particular va and return it.  IT MAY BE FOR ANOTHER MAPPING!
 */
int
ptelookup_va(vaddr_t va)
{
	long tsbptr;
#define TSBBASEMASK	(0xffffffffffffe000LL<<tsbsize)

	tsbptr = (((va >> 9) & 0xfffffffffffffff0LL) & ~TSBBASEMASK );
	return (tsbptr/sizeof(pte_t));
}

/*
 * Do whatever is needed to sync the MOD/REF flags
 */

boolean_t
pmap_clear_modify(struct vm_page *pg)
{
	paddr_t pa = VM_PAGE_TO_PHYS(pg);
	int changed = 0;
	pv_entry_t pv;
	
	/* Clear all mappings */
	mtx_enter(&pg->mdpage.pvmtx);
	pv = pa_to_pvh(pa);
	if (pv->pv_va & PV_MOD) {
		changed |= 1;
		pv->pv_va &= ~PV_MOD;
	}
	if (pv->pv_pmap != NULL) {
		for (; pv; pv = pv->pv_next) {
			int64_t data;

			/* First clear the mod bit in the PTE and make it R/O */
			data = pseg_get(pv->pv_pmap, pv->pv_va & PV_VAMASK);

			/* Need to both clear the modify and write bits */
			if (CPU_ISSUN4V) {
				if (data & (SUN4V_TLB_MODIFY))
					changed |= 1;
				data &= ~(SUN4V_TLB_MODIFY|SUN4V_TLB_W);
			} else {
				if (data & (SUN4U_TLB_MODIFY))
					changed |= 1;
				data &= ~(SUN4U_TLB_MODIFY|SUN4U_TLB_W);
			}
			KDASSERT((data & TLB_NFO) == 0);
			if (pseg_set(pv->pv_pmap, pv->pv_va & PV_VAMASK, data, 0)) {
				printf("pmap_clear_modify: gotten pseg empty!\n");
				db_enter();
				/* panic? */
			}
			if (pv->pv_pmap->pm_ctx || pv->pv_pmap == pmap_kernel()) {
				tsb_invalidate(pv->pv_pmap->pm_ctx,
				    (pv->pv_va & PV_VAMASK));
				tlb_flush_pte((pv->pv_va & PV_VAMASK),
				    pv->pv_pmap->pm_ctx);
			}
			/* Then clear the mod bit in the pv */
			if (pv->pv_va & PV_MOD) {
				changed |= 1;
				pv->pv_va &= ~PV_MOD;
			}
			dcache_flush_page(pa);
		}
	}
	mtx_leave(&pg->mdpage.pvmtx);

	return (changed);
}

boolean_t
pmap_clear_reference(struct vm_page *pg)
{
	paddr_t pa = VM_PAGE_TO_PHYS(pg);
	int changed = 0;
	pv_entry_t pv;

	/* Clear all references */
	mtx_enter(&pg->mdpage.pvmtx);
	pv = pa_to_pvh(pa);
	if (pv->pv_va & PV_REF) {
		changed = 1;
		pv->pv_va &= ~PV_REF;
	}
	if (pv->pv_pmap != NULL) {
		for (; pv; pv = pv->pv_next) {
			int64_t data;

			data = pseg_get(pv->pv_pmap, pv->pv_va & PV_VAMASK);
			if (CPU_ISSUN4V) {
				if (data & SUN4V_TLB_ACCESS)
					changed = 1;
				data &= ~SUN4V_TLB_ACCESS;
			} else {
				if (data & SUN4U_TLB_ACCESS)
					changed = 1;
				data &= ~SUN4U_TLB_ACCESS;
			}
			KDASSERT((data & TLB_NFO) == 0);
			if (pseg_set(pv->pv_pmap, pv->pv_va & PV_VAMASK, data, 0)) {
				printf("pmap_clear_reference: gotten pseg empty!\n");
				db_enter();
				/* panic? */
			}
			if (pv->pv_pmap->pm_ctx || pv->pv_pmap == pmap_kernel()) {
				tsb_invalidate(pv->pv_pmap->pm_ctx,
				    (pv->pv_va & PV_VAMASK));
/*
				tlb_flush_pte(pv->pv_va & PV_VAMASK, 
					pv->pv_pmap->pm_ctx);
*/
			}
			if (pv->pv_va & PV_REF) {
				changed = 1;
				pv->pv_va &= ~PV_REF;
			}
		}
	}
	/* Stupid here will take a cache hit even on unmapped pages 8^( */
	dcache_flush_page(VM_PAGE_TO_PHYS(pg));
	mtx_leave(&pg->mdpage.pvmtx);

	return (changed);
}

boolean_t
pmap_is_modified(struct vm_page *pg)
{
	pv_entry_t pv, npv;
	int mod = 0;

	/* Check if any mapping has been modified */
	mtx_enter(&pg->mdpage.pvmtx);
	pv = &pg->mdpage.pvent;
	if (pv->pv_va & PV_MOD)
		mod = 1;
	if (!mod && (pv->pv_pmap != NULL)) {
		for (npv = pv; mod == 0 && npv && npv->pv_pmap; npv = npv->pv_next) {
			int64_t data;
			
			data = pseg_get(npv->pv_pmap, npv->pv_va & PV_VAMASK);
			if (pmap_tte2flags(data) & PV_MOD)
				mod = 1;
			/* Migrate modify info to head pv */
			if (npv->pv_va & PV_MOD) {
				mod = 1;
				npv->pv_va &= ~PV_MOD;
			}
		}
	}
	/* Save modify info */
	if (mod)
		pv->pv_va |= PV_MOD;
	mtx_leave(&pg->mdpage.pvmtx);

	return (mod);
}

boolean_t
pmap_is_referenced(struct vm_page *pg)
{
	pv_entry_t pv, npv;
	int ref = 0;

	/* Check if any mapping has been referenced */
	mtx_enter(&pg->mdpage.pvmtx);
	pv = &pg->mdpage.pvent;
	if (pv->pv_va & PV_REF)
		ref = 1;
	if (!ref && (pv->pv_pmap != NULL)) {
		for (npv = pv; npv; npv = npv->pv_next) {
			int64_t data;
			
			data = pseg_get(npv->pv_pmap, npv->pv_va & PV_VAMASK);
			if (pmap_tte2flags(data) & PV_REF)
				ref = 1;
			/* Migrate modify info to head pv */
			if (npv->pv_va & PV_REF) {
				ref = 1;
				npv->pv_va &= ~PV_REF;
			}
		}
	}
	/* Save ref info */
	if (ref)
		pv->pv_va |= PV_REF;
	mtx_leave(&pg->mdpage.pvmtx);

	return (ref);
}

/*
 *	Routine:	pmap_unwire
 *	Function:	Clear the wired attribute for a map/virtual-address
 *			pair.
 *	In/out conditions:
 *			The mapping must already exist in the pmap.
 */
void
pmap_unwire(struct pmap *pmap, vaddr_t va)
{
	int64_t data;

	if (pmap == NULL)
		return;

	/*
	 * Is this part of the permanent 4MB mapping?
	 */
	if (pmap == pmap_kernel() && va >= ktext && 
		va < roundup(ekdata, 4*MEG)) {
		prom_printf("pmap_unwire: va=%08x in locked TLB\r\n", va);
		OF_enter();
		return;
	}
	mtx_enter(&pmap->pm_mtx);
	data = pseg_get(pmap, va & PV_VAMASK);

	if (CPU_ISSUN4V)
		data &= ~SUN4V_TLB_TSB_LOCK;
	else
		data &= ~SUN4U_TLB_TSB_LOCK;

	if (pseg_set(pmap, va & PV_VAMASK, data, 0)) {
		printf("pmap_unwire: gotten pseg empty!\n");
		db_enter();
		/* panic? */
	}
	mtx_leave(&pmap->pm_mtx);
}

/*
 * Lower the protection on the specified physical page.
 *
 * Never enable writing as it will break COW
 */
void
pmap_page_protect(struct vm_page *pg, vm_prot_t prot)
{
	paddr_t pa = VM_PAGE_TO_PHYS(pg);
	pv_entry_t pv, freepvs = NULL;
	int64_t data, clear, set;

	if (prot & PROT_WRITE)
		return;

	if (prot & (PROT_READ | PROT_EXEC)) {
		/* copy_on_write */

		set = TLB_V;
		if (CPU_ISSUN4V) {
			clear = SUN4V_TLB_REAL_W|SUN4V_TLB_W;
			if (PROT_EXEC & prot)
				set |= SUN4V_TLB_EXEC;
			else
				clear |= SUN4V_TLB_EXEC;
		} else {
			clear = SUN4U_TLB_REAL_W|SUN4U_TLB_W;
			if (PROT_EXEC & prot)
				set |= SUN4U_TLB_EXEC;
			else
				clear |= SUN4U_TLB_EXEC;
			if (PROT_EXEC == prot)
				set |= SUN4U_TLB_EXEC_ONLY;
			else
				clear |= SUN4U_TLB_EXEC_ONLY;
		}

		pv = pa_to_pvh(pa);
		mtx_enter(&pg->mdpage.pvmtx);
		if (pv->pv_pmap != NULL) {
			for (; pv; pv = pv->pv_next) {
				data = pseg_get(pv->pv_pmap, pv->pv_va & PV_VAMASK);

				/* Save REF/MOD info */
				pv->pv_va |= pmap_tte2flags(data);

				data &= ~(clear);
				data |= (set);
				KDASSERT((data & TLB_NFO) == 0);
				if (pseg_set(pv->pv_pmap, pv->pv_va & PV_VAMASK, data, 0)) {
					printf("pmap_page_protect: gotten pseg empty!\n");
					db_enter();
					/* panic? */
				}
				if (pv->pv_pmap->pm_ctx || pv->pv_pmap == pmap_kernel()) {
					tsb_invalidate(pv->pv_pmap->pm_ctx,
					    (pv->pv_va & PV_VAMASK));
					tlb_flush_pte(pv->pv_va & PV_VAMASK, pv->pv_pmap->pm_ctx);
				}
			}
		}
		mtx_leave(&pg->mdpage.pvmtx);
	} else {
		pv_entry_t firstpv;
		/* remove mappings */

		firstpv = pa_to_pvh(pa);
		mtx_enter(&pg->mdpage.pvmtx);

		/* First remove the entire list of continuation pv's*/
		while ((pv = firstpv->pv_next) != NULL) {
			data = pseg_get(pv->pv_pmap, pv->pv_va & PV_VAMASK);

			/* Save REF/MOD info */
			firstpv->pv_va |= pmap_tte2flags(data);

			/* Clear mapping */
			if (pseg_set(pv->pv_pmap, pv->pv_va & PV_VAMASK, 0, 0)) {
				printf("pmap_page_protect: gotten pseg empty!\n");
				db_enter();
				/* panic? */
			}
			if (pv->pv_pmap->pm_ctx || pv->pv_pmap == pmap_kernel()) {
				tsb_invalidate(pv->pv_pmap->pm_ctx,
				    (pv->pv_va & PV_VAMASK));
				tlb_flush_pte(pv->pv_va & PV_VAMASK, pv->pv_pmap->pm_ctx);
			}
			atomic_dec_long(&pv->pv_pmap->pm_stats.resident_count);

			firstpv->pv_next = pv->pv_next;
			pv->pv_next = freepvs;
			freepvs = pv;
		}

		pv = firstpv;

		/* Then remove the primary pv */
		if (pv->pv_pmap != NULL) {
			data = pseg_get(pv->pv_pmap, pv->pv_va & PV_VAMASK);

			/* Save REF/MOD info */
			pv->pv_va |= pmap_tte2flags(data);
			if (pseg_set(pv->pv_pmap, pv->pv_va & PV_VAMASK, 0, 0)) {
				printf("pmap_page_protect: gotten pseg empty!\n");
				db_enter();
				/* panic? */
			}
			if (pv->pv_pmap->pm_ctx || pv->pv_pmap == pmap_kernel()) {
				tsb_invalidate(pv->pv_pmap->pm_ctx,
				    (pv->pv_va & PV_VAMASK));
				tlb_flush_pte(pv->pv_va & PV_VAMASK,
				    pv->pv_pmap->pm_ctx);
			}
			atomic_dec_long(&pv->pv_pmap->pm_stats.resident_count);

			KASSERT(pv->pv_next == NULL);
			/* dump the first pv */
			pv->pv_pmap = NULL;
		}
		dcache_flush_page(pa);
		mtx_leave(&pg->mdpage.pvmtx);

		while ((pv = freepvs) != NULL) {
			freepvs = pv->pv_next;
			pool_put(&pv_pool, pv);
		}
	}
	/* We should really only flush the pages we demapped. */
}

/*
 * Allocate a context.  If necessary, steal one from someone else.
 * Changes hardware context number and loads segment map.
 *
 * This routine is only ever called from locore.s just after it has
 * saved away the previous process, so there are no active user windows.
 *
 * The new context is flushed from the TLB before returning.
 */
int
ctx_alloc(struct pmap *pm)
{
	int s, cnum;
	static int next = 0;

	if (pm == pmap_kernel()) {
#ifdef DIAGNOSTIC
		printf("ctx_alloc: kernel pmap!\n");
#endif
		return (0);
	}
	s = splvm();
	cnum = next;
	do {
		/*
		 * We use the last context as an "invalid" context in
		 * TSB tags. Never allocate (or bad things will happen).
		 */
		if (cnum >= numctx - 2)
			cnum = 0;
	} while (ctxbusy[++cnum] != 0 && cnum != next);
	if (cnum==0) cnum++; /* Never steal ctx 0 */
	if (ctxbusy[cnum]) {
		int i;
		/* We gotta steal this context */
		for (i = 0; i < TSBENTS; i++) {
			if (TSB_TAG_CTX(tsb_dmmu[i].tag) == cnum)
				tsb_dmmu[i].tag = TSB_TAG_INVALID;
			if (TSB_TAG_CTX(tsb_immu[i].tag) == cnum)
				tsb_immu[i].tag = TSB_TAG_INVALID;
		}
		tlb_flush_ctx(cnum);
	}
	ctxbusy[cnum] = pm->pm_physaddr;
	next = cnum;
	splx(s);
	pm->pm_ctx = cnum;
	return cnum;
}

/*
 * Give away a context.
 */
void
ctx_free(struct pmap *pm)
{
	int oldctx;
	
	oldctx = pm->pm_ctx;

	if (oldctx == 0)
		panic("ctx_free: freeing kernel context");
#ifdef DIAGNOSTIC
	if (ctxbusy[oldctx] == 0)
		printf("ctx_free: freeing free context %d\n", oldctx);
	if (ctxbusy[oldctx] != pm->pm_physaddr) {
		printf("ctx_free: freeing someone else's context\n "
		       "ctxbusy[%d] = %p, pm(%p)->pm_ctx = %p\n", 
		       oldctx, (void *)(u_long)ctxbusy[oldctx], pm,
		       (void *)(u_long)pm->pm_physaddr);
		db_enter();
	}
#endif
	/* We should verify it has not been stolen and reallocated... */
	ctxbusy[oldctx] = 0;
}

/*
 * Enter the pmap and virtual address into the
 * physical to virtual map table.
 */
pv_entry_t
pmap_enter_pv(struct pmap *pmap, pv_entry_t npv, vaddr_t va, paddr_t pa)
{
	struct vm_page *pg = PHYS_TO_VM_PAGE(pa);
	pv_entry_t pv = &pg->mdpage.pvent;

	mtx_enter(&pg->mdpage.pvmtx);

	if (pv->pv_pmap == NULL) {
		/*
		 * No entries yet, use header as the first entry
		 */
		PV_SETVA(pv, va);
		pv->pv_pmap = pmap;
		pv->pv_next = NULL;

		mtx_leave(&pg->mdpage.pvmtx);
		return (npv);
	}

	if (npv == NULL)
		panic("%s: no pv entries available", __func__);

	if (!(pv->pv_va & PV_ALIAS)) {
		/*
		 * There is at least one other VA mapping this page.
		 * Check if they are cache index compatible. If not
		 * remove all mappings, flush the cache and set page
		 * to be mapped uncached. Caching will be restored
		 * when pages are mapped compatible again.
		 */
		if ((pv->pv_va ^ va) & VA_ALIAS_MASK) {
			pv->pv_va |= PV_ALIAS;
			pmap_page_cache(pmap, pa, 0);
		}
	}

	/*
	 * There is at least one other VA mapping this page.
	 * Place this entry after the header.
	 */
	npv->pv_va = va & PV_VAMASK;
	npv->pv_pmap = pmap;
	npv->pv_next = pv->pv_next;
	pv->pv_next = npv;

	mtx_leave(&pg->mdpage.pvmtx);
	return (NULL);
}

/*
 * Remove a physical to virtual address translation.
 */
pv_entry_t
pmap_remove_pv(struct pmap *pmap, vaddr_t va, paddr_t pa)
{
	pv_entry_t pv, opv, npv = NULL;
	struct vm_page *pg = PHYS_TO_VM_PAGE(pa);
	int64_t data = 0LL;
	int alias;

	opv = pv = &pg->mdpage.pvent;
	mtx_enter(&pg->mdpage.pvmtx);

	/*
	 * If it is the first entry on the list, it is actually
	 * in the header and we must copy the following entry up
	 * to the header.  Otherwise we must search the list for
	 * the entry.
	 */
	if (pmap == pv->pv_pmap && PV_MATCH(pv, va)) {
		/* Save modified/ref bits */
		data = pseg_get(pv->pv_pmap, pv->pv_va & PV_VAMASK);
		npv = pv->pv_next;
		if (npv) {
			/* First save mod/ref bits */
			pv->pv_va = (pv->pv_va & PV_MASK) | npv->pv_va;
			pv->pv_next = npv->pv_next;
			pv->pv_pmap = npv->pv_pmap;
		} else {
			pv->pv_pmap = NULL;
			pv->pv_next = NULL;
			pv->pv_va &= (PV_REF|PV_MOD); /* Only save ref/mod bits */
		}
	} else {
		for (npv = pv->pv_next; npv; pv = npv, npv = npv->pv_next) {
			if (pmap == npv->pv_pmap && PV_MATCH(npv, va))
				goto found;
		}

		/* 
		 * Sometimes UVM gets confused and calls pmap_remove() instead
		 * of pmap_kremove() 
		 */
		mtx_leave(&pg->mdpage.pvmtx);
		return (NULL);
found:
		pv->pv_next = npv->pv_next;

		/*
		 * move any referenced/modified info to the base pv
		 */
		data = pseg_get(npv->pv_pmap, npv->pv_va & PV_VAMASK);

		/* 
		 * Here, if this page was aliased, we should try clear out any
		 * alias that may have occurred.  However, that's a complicated
		 * operation involving multiple scans of the pv list. 
		 */
	}

	/* Save REF/MOD info */
	opv->pv_va |= pmap_tte2flags(data);

	/* Check to see if the alias went away */
	if (opv->pv_va & PV_ALIAS) {
		alias = 0;
		for (pv = opv; pv; pv = pv->pv_next) {
			if ((pv->pv_va ^ opv->pv_va) & VA_ALIAS_MASK) {
				alias = 1;
				break;
			}
		}
		if (alias == 0) {
			opv->pv_va &= ~PV_ALIAS;
			pmap_page_cache(pmap, pa, 1);
		}
	}

	mtx_leave(&pg->mdpage.pvmtx);
	return (npv);
}

/*
 *	pmap_page_cache:
 *
 *	Change all mappings of a page to cached/uncached.
 */
void
pmap_page_cache(struct pmap *pm, paddr_t pa, int mode)
{
	pv_entry_t pv;
	struct vm_page *pg = PHYS_TO_VM_PAGE(pa);

	if (CPU_ISSUN4US || CPU_ISSUN4V)
		return;

	pv = &pg->mdpage.pvent;
	if (pv == NULL)
		return;

	MUTEX_ASSERT_LOCKED(&pg->mdpage.pvmtx);

	while (pv) {
		vaddr_t va;

		va = (pv->pv_va & PV_VAMASK);
		if (mode) {
			/* Enable caching */
			if (pseg_set(pv->pv_pmap, va,
			    pseg_get(pv->pv_pmap, va) | SUN4U_TLB_CV, 0)) {
				printf("pmap_page_cache: aliased pseg empty!\n");
				db_enter();
				/* panic? */
			}
		} else {
			/* Disable caching */
			if (pseg_set(pv->pv_pmap, va,
			    pseg_get(pv->pv_pmap, va) & ~SUN4U_TLB_CV, 0)) {
				printf("pmap_page_cache: aliased pseg empty!\n");
				db_enter();
				/* panic? */
			}
		}
		if (pv->pv_pmap->pm_ctx || pv->pv_pmap == pmap_kernel()) {
			tsb_invalidate(pv->pv_pmap->pm_ctx, va);
			/* Force reload -- protections may be changed */
			tlb_flush_pte(va, pv->pv_pmap->pm_ctx);	
		}

		pv = pv->pv_next;
	}
}

int
pmap_get_page(paddr_t *pa, const char *wait, struct pmap *pm)
{
	int reserve = pm == pmap_kernel() ? UVM_PGA_USERESERVE : 0;

	if (uvm.page_init_done) {
		struct vm_page *pg;

		while ((pg = uvm_pagealloc(NULL, 0, NULL,
		    UVM_PGA_ZERO|reserve)) == NULL) {
			if (wait == NULL)
				return 0;
			uvm_wait(wait);
		}
		pg->wire_count++;
		atomic_clearbits_int(&pg->pg_flags, PG_BUSY);
		*pa = VM_PAGE_TO_PHYS(pg);
	} else {
		uvm_page_physget(pa);
		prom_claim_phys(*pa, PAGE_SIZE);
		pmap_zero_phys(*pa);
	}

	return (1);
}

void
pmap_free_page(paddr_t pa, struct pmap *pm)
{
	struct vm_page *pg = PHYS_TO_VM_PAGE(pa);

	pg->wire_count = 0;
	uvm_pagefree(pg);
}

void
pmap_remove_holes(struct vmspace *vm)
{
	vaddr_t shole, ehole;
	struct vm_map *map = &vm->vm_map;

	/*
	 * Although the hardware only supports 44-bit virtual addresses
	 * (and thus a hole from 1 << 43 to -1 << 43), this pmap
	 * implementation itself only supports 43-bit virtual addresses,
	 * so we have to narrow the hole a bit more.
	 */
	shole = 1L << (HOLESHIFT - 1);
	ehole = -1L << (HOLESHIFT - 1);

	shole = ulmax(vm_map_min(map), shole);
	ehole = ulmin(vm_map_max(map), ehole);

	if (ehole <= shole)
		return;

	(void)uvm_map(map, &shole, ehole - shole, NULL, UVM_UNKNOWN_OFFSET, 0,
	    UVM_MAPFLAG(PROT_NONE, PROT_NONE, MAP_INHERIT_SHARE, MADV_RANDOM,
	      UVM_FLAG_NOMERGE | UVM_FLAG_HOLE | UVM_FLAG_FIXED));
}

#ifdef DDB

void
db_dump_pv(db_expr_t addr, int have_addr, db_expr_t count, char *modif)
{
	struct pv_entry *pv;

	if (!have_addr) {
		db_printf("Need addr for pv\n");
		return;
	}

	for (pv = pa_to_pvh(addr); pv; pv = pv->pv_next)
		db_printf("pv@%p: next=%p pmap=%p va=0x%llx\n",
			  pv, pv->pv_next, pv->pv_pmap,
			  (unsigned long long)pv->pv_va);
	
}

#endif

/*
 * Read an instruction from a given virtual memory address.
 * EXEC_ONLY mappings are bypassed.
 */
int
pmap_copyinsn(pmap_t pmap, vaddr_t va, uint32_t *insn)
{
	paddr_t pa;

	if (pmap == pmap_kernel())
		return EINVAL;

	mtx_enter(&pmap->pm_mtx);
	/* inline pmap_extract */
	pa = pseg_get(pmap, va) & TLB_PA_MASK;
	if (pa != 0)
		*insn = lduwa(pa | (va & PAGE_MASK), ASI_PHYS_CACHED);
	mtx_leave(&pmap->pm_mtx);

	return pa == 0 ? EFAULT : 0;
}
