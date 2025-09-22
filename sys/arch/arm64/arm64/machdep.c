/* $OpenBSD: machdep.c,v 1.96 2025/02/11 22:27:09 kettenis Exp $ */
/*
 * Copyright (c) 2014 Patrick Wildt <patrick@blueri.se>
 * Copyright (c) 2021 Mark Kettenis <kettenis@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sched.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/reboot.h>
#include <sys/mount.h>
#include <sys/exec.h>
#include <sys/user.h>
#include <sys/conf.h>
#include <sys/kcore.h>
#include <sys/core.h>
#include <sys/msgbuf.h>
#include <sys/buf.h>
#include <sys/termios.h>
#include <sys/sensors.h>
#include <sys/malloc.h>

#include <net/if.h>
#include <uvm/uvm_extern.h>
#include <dev/cons.h>
#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>
#include <machine/param.h>
#include <machine/kcore.h>
#include <machine/bootconfig.h>
#include <machine/bus.h>
#include <machine/fpu.h>

#include <machine/db_machdep.h>
#include <ddb/db_extern.h>

#include <dev/efi/efi.h>

#include "softraid.h"
#if NSOFTRAID > 0
#include <dev/softraidvar.h>
#endif

extern vaddr_t virtual_avail;
extern uint64_t esym;

extern char _start[];

char *boot_args = NULL;
uint8_t *bootmac = NULL;

int stdout_node;
int stdout_speed;

void (*cpuresetfn)(void);
void (*powerdownfn)(void);

int cold = 1;
int lid_action = 1;

struct vm_map *exec_map = NULL;
struct vm_map *phys_map = NULL;

int physmem;

struct consdev *cn_tab;

caddr_t msgbufaddr;
paddr_t msgbufphys;

struct user *proc0paddr;

struct uvm_constraint_range  dma_constraint = { 0x0, (paddr_t)-1 };
struct uvm_constraint_range *uvm_md_constraints[] = {
	&dma_constraint,
	NULL,
};

/* the following is used externally (sysctl_hw) */
char    machine[] = MACHINE;            /* from <machine/param.h> */

int safepri = 0;

struct cpu_info cpu_info_primary;
struct cpu_info *cpu_info[MAXCPUS] = { &cpu_info_primary };

struct fdt_reg memreg[VM_PHYSSEG_MAX];
int nmemreg;

void memreg_add(const struct fdt_reg *);
void memreg_remove(const struct fdt_reg *);

static int
atoi(const char *s)
{
	int n, neg;

	n = 0;
	neg = 0;

	while (*s == '-') {
		s++;
		neg = !neg;
	}

	while (*s != '\0') {
		if (*s < '0' || *s > '9')
			break;

		n = (10 * n) + (*s - '0');
		s++;
	}

	return (neg ? -n : n);
}

void *
fdt_find_cons(const char *name)
{
	char *alias = "serial0";
	char buf[128];
	char *stdout = NULL;
	char *p;
	void *node;

	/* First check if "stdout-path" is set. */
	node = fdt_find_node("/chosen");
	if (node) {
		if (fdt_node_property(node, "stdout-path", &stdout) > 0) {
			if (strchr(stdout, ':') != NULL) {
				strlcpy(buf, stdout, sizeof(buf));
				if ((p = strchr(buf, ':')) != NULL) {
					*p++ = '\0';
					stdout_speed = atoi(p);
				}
				stdout = buf;
			}
			if (stdout[0] != '/') {
				/* It's an alias. */
				alias = stdout;
				stdout = NULL;
			}
		}
	}

	/* Perform alias lookup if necessary. */
	if (stdout == NULL) {
		node = fdt_find_node("/aliases");
		if (node)
			fdt_node_property(node, alias, &stdout);
	}

	/* Lookup the physical address of the interface. */
	if (stdout) {
		node = fdt_find_node(stdout);
		if (node && fdt_is_compatible(node, name)) {
			stdout_node = OF_finddevice(stdout);
			return (node);
		}
	}

	return (NULL);
}

void	amluart_init_cons(void);
void	cduart_init_cons(void);
void	com_fdt_init_cons(void);
void	exuart_init_cons(void);
void	imxuart_init_cons(void);
void	mvuart_init_cons(void);
void	pluart_init_cons(void);
void	simplefb_init_cons(bus_space_tag_t);

void
consinit(void)
{
	static int consinit_called = 0;

	if (consinit_called != 0)
		return;

	consinit_called = 1;

	amluart_init_cons();
	cduart_init_cons();
	com_fdt_init_cons();
	exuart_init_cons();
	imxuart_init_cons();
	mvuart_init_cons();
	pluart_init_cons();
	simplefb_init_cons(&arm64_bs_tag);
}

void
cpu_idle_enter(void)
{
	disable_irq_daif();
}

void (*cpu_idle_cycle_fcn)(void) = cpu_wfi;

void
cpu_idle_cycle(void)
{
	cpu_idle_cycle_fcn();
	enable_irq_daif();
	disable_irq_daif();
}

void
cpu_idle_leave(void)
{
	enable_irq_daif();
}

/* Dummy trapframe for proc0. */
struct trapframe proc0tf;

void
cpu_startup(void)
{
	u_int loop;
	paddr_t minaddr;
	paddr_t maxaddr;

	proc0.p_addr = proc0paddr;

	/*
	 * Give pmap a chance to set up a few more things now the vm
	 * is initialised
	 */
	pmap_postinit();

	/*
	 * Initialize error message buffer (at end of core).
	 */

	/* msgbufphys was setup during the secondary boot strap */
	for (loop = 0; loop < atop(MSGBUFSIZE); ++loop)
		pmap_kenter_pa((vaddr_t)msgbufaddr + loop * PAGE_SIZE,
		    msgbufphys + loop * PAGE_SIZE, PROT_READ | PROT_WRITE);
	pmap_update(pmap_kernel());
	initmsgbuf(msgbufaddr, round_page(MSGBUFSIZE));

	/*
	 * Identify ourselves for the msgbuf (everything printed earlier will
	 * not be buffered).
	 */
	printf("%s", version);

	printf("real mem  = %lu (%luMB)\n", ptoa(physmem),
	    ptoa(physmem) / 1024 / 1024);

	/*
	 * Allocate a submap for exec arguments.  This map effectively
	 * limits the number of processes exec'ing at any time.
	 */
	minaddr = vm_map_min(kernel_map);
	exec_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr,
	    16 * NCARGS, VM_MAP_PAGEABLE, FALSE, NULL);

	/*
	 * Allocate a submap for physio
	 */
	phys_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr,
	    VM_PHYS_SIZE, 0, FALSE, NULL);

	/*
	 * Set up buffers, so they can be used to read disk labels.
	 */
	bufinit();

	printf("avail mem = %lu (%luMB)\n", ptoa(uvmexp.free),
	    ptoa(uvmexp.free) / 1024 / 1024);

	curpcb = &proc0.p_addr->u_pcb;
	curpcb->pcb_flags = 0;
	curpcb->pcb_tf = &proc0tf;

	if (boothowto & RB_CONFIG) {
#ifdef BOOT_CONFIG
		user_config();
#else
		printf("kernel does not support -c; continuing..\n");
#endif
	}
}

void    cpu_switchto_asm(struct proc *, struct proc *);

void
cpu_switchto(struct proc *old, struct proc *new)
{
	if (old) {
		struct pcb *pcb = &old->p_addr->u_pcb;

		if (pcb->pcb_flags & PCB_FPU)
			fpu_save(old);

		fpu_drop();
	}

	cpu_switchto_asm(old, new);
}

/*
 * machine dependent system variables.
 */

const struct sysctl_bounded_args cpuctl_vars[] = {
	{ CPU_LIDACTION, &lid_action, 0, 2 },
};

int
cpu_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen, struct proc *p)
{
	char *compatible;
	int node, len, error;

	/* all sysctl names at this level are terminal */
	if (namelen != 1)
		return (ENOTDIR);		/* overloaded */

	switch (name[0]) {
	case CPU_COMPATIBLE:
		node = OF_finddevice("/");
		len = OF_getproplen(node, "compatible");
		if (len <= 0)
			return (EOPNOTSUPP);
		compatible = malloc(len, M_TEMP, M_WAITOK | M_ZERO);
		OF_getprop(node, "compatible", compatible, len);
		compatible[len - 1] = 0;
		error = sysctl_rdstring(oldp, oldlenp, newp, compatible);
		free(compatible, M_TEMP, len);
		return error;
	case CPU_ID_AA64ISAR0:
		return sysctl_rdquad(oldp, oldlenp, newp, cpu_id_aa64isar0);
	case CPU_ID_AA64ISAR1:
		return sysctl_rdquad(oldp, oldlenp, newp, cpu_id_aa64isar1);
	case CPU_ID_AA64ISAR2:
		return sysctl_rdquad(oldp, oldlenp, newp, cpu_id_aa64isar2);
	case CPU_ID_AA64PFR0:
		return sysctl_rdquad(oldp, oldlenp, newp, cpu_id_aa64pfr0);
	case CPU_ID_AA64PFR1:
		return sysctl_rdquad(oldp, oldlenp, newp, cpu_id_aa64pfr1);
	case CPU_ID_AA64MMFR0:
		return sysctl_rdquad(oldp, oldlenp, newp, cpu_id_aa64mmfr0);
	case CPU_ID_AA64MMFR1:
		return sysctl_rdquad(oldp, oldlenp, newp, cpu_id_aa64mmfr1);
	case CPU_ID_AA64MMFR2:
		return sysctl_rdquad(oldp, oldlenp, newp, cpu_id_aa64mmfr2);
	case CPU_ID_AA64SMFR0:
		return sysctl_rdquad(oldp, oldlenp, newp, 0);
	case CPU_ID_AA64ZFR0:
		return sysctl_rdquad(oldp, oldlenp, newp, cpu_id_aa64zfr0);
	default:
		return (sysctl_bounded_arr(cpuctl_vars, nitems(cpuctl_vars),
		    name, namelen, oldp, oldlenp, newp, newlen));
	}
	/* NOTREACHED */
}

void dumpsys(void);

int	waittime = -1;

__dead void
boot(int howto)
{
	if ((howto & RB_RESET) != 0)
		goto doreset;

	if (cold) {
		if ((howto & RB_USERREQ) == 0)
			howto |= RB_HALT;
		goto haltsys;
	}

	boothowto = howto;
	if ((howto & RB_NOSYNC) == 0 && waittime < 0) {
		waittime = 0;
		vfs_shutdown(curproc);

		if ((howto & RB_TIMEBAD) == 0) {
			resettodr();
		} else {
			printf("WARNING: not updating battery clock\n");
		}
	}
	if_downall();

	uvm_shutdown();
	splhigh();
	cold = 1;

	if ((howto & RB_DUMP) != 0)
		dumpsys();

haltsys:
	config_suspend_all(DVACT_POWERDOWN);

	if ((howto & RB_HALT) != 0) {
		if ((howto & RB_POWERDOWN) != 0) {
			printf("\nAttempting to power down...\n");
			delay(500000);
			if (powerdownfn)
				(*powerdownfn)();
		}

		printf("\n");
		printf("The operating system has halted.\n");
		printf("Please press any key to reboot.\n\n");
		cngetc();
	}

doreset:
	printf("rebooting...\n");
	delay(500000);
	if (cpuresetfn)
		(*cpuresetfn)();
	printf("reboot failed; spinning\n");
	for (;;)
		continue;
	/* NOTREACHED */
}

void
setregs(struct proc *p, struct exec_package *pack, u_long stack,
    struct ps_strings *arginfo)
{
	struct pmap *pm = p->p_vmspace->vm_map.pmap;
	struct pcb *pcb = &p->p_addr->u_pcb;
	struct trapframe *tf = pcb->pcb_tf;

	if (pack->ep_flags & EXEC_NOBTCFI)
		pm->pm_guarded = 0;
	else
		pm->pm_guarded = ATTR_GP;

	arc4random_buf(&pm->pm_apiakey, sizeof(pm->pm_apiakey));
	arc4random_buf(&pm->pm_apdakey, sizeof(pm->pm_apdakey));
	arc4random_buf(&pm->pm_apibkey, sizeof(pm->pm_apibkey));
	arc4random_buf(&pm->pm_apdbkey, sizeof(pm->pm_apdbkey));
	arc4random_buf(&pm->pm_apgakey, sizeof(pm->pm_apgakey));
	pmap_setpauthkeys(pm);

	/* If we were using the FPU, forget about it. */
	memset(&pcb->pcb_fpstate, 0, sizeof(pcb->pcb_fpstate));
	pcb->pcb_flags &= ~(PCB_FPU | PCB_SVE);
	fpu_drop();

	memset(tf, 0, sizeof *tf);
	tf->tf_sp = stack;
	tf->tf_lr = pack->ep_entry;
	tf->tf_elr = pack->ep_entry; /* ??? */
	tf->tf_spsr = PSR_M_EL0t | PSR_DIT;
}

void
need_resched(struct cpu_info *ci)
{
	ci->ci_want_resched = 1;

	/* There's a risk we'll be called before the idle threads start */
	if (ci->ci_curproc) {
		aston(ci->ci_curproc);
		cpu_kick(ci);
	}
}

int	cpu_dumpsize(void);
u_long	cpu_dump_mempagecnt(void);

paddr_t dumpmem_paddr;
vaddr_t dumpmem_vaddr;
psize_t dumpmem_sz;

/*
 * These variables are needed by /sbin/savecore
 */
u_long	dumpmag = 0x8fca0101;	/* magic number */
int 	dumpsize = 0;		/* pages */
long	dumplo = 0; 		/* blocks */

/*
 * cpu_dump: dump the machine-dependent kernel core dump headers.
 */
int
cpu_dump(void)
{
	int (*dump)(dev_t, daddr_t, caddr_t, size_t);
	char buf[dbtob(1)];
	kcore_seg_t *segp;
	cpu_kcore_hdr_t *cpuhdrp;
	phys_ram_seg_t *memsegp;
#if 0
	caddr_t va;
	int i;
#endif

	dump = bdevsw[major(dumpdev)].d_dump;

	memset(buf, 0, sizeof buf);
	segp = (kcore_seg_t *)buf;
	cpuhdrp = (cpu_kcore_hdr_t *)&buf[ALIGN(sizeof(*segp))];
	memsegp = (phys_ram_seg_t *)&buf[ALIGN(sizeof(*segp)) +
	    ALIGN(sizeof(*cpuhdrp))];

	/*
	 * Generate a segment header.
	 */
	CORE_SETMAGIC(*segp, KCORE_MAGIC, MID_MACHINE, CORE_CPU);
	segp->c_size = dbtob(1) - ALIGN(sizeof(*segp));

	/*
	 * Add the machine-dependent header info.
	 */
	cpuhdrp->kernelbase = KERNEL_BASE;
	cpuhdrp->kerneloffs = 0;
	cpuhdrp->staticsize = 0;
	cpuhdrp->pmap_kernel_l1 = 0;
	cpuhdrp->pmap_kernel_l2 = 0;

#if 0
	/*
	 * Fill in the memory segment descriptors.
	 */
	for (i = 0; i < mem_cluster_cnt; i++) {
		memsegp[i].start = mem_clusters[i].start;
		memsegp[i].size = mem_clusters[i].size & PMAP_PA_MASK;
	}

	/*
	 * If we have dump memory then assume the kernel stack is in high
	 * memory and bounce
	 */
	if (dumpmem_vaddr != 0) {
		memcpy((char *)dumpmem_vaddr, buf, sizeof(buf));
		va = (caddr_t)dumpmem_vaddr;
	} else {
		va = (caddr_t)buf;
	}
	return (dump(dumpdev, dumplo, va, dbtob(1)));
#else
	return ENOSYS;
#endif
}

/*
 * This is called by main to set dumplo and dumpsize.
 * Dumps always skip the first PAGE_SIZE of disk space
 * in case there might be a disk label stored there.
 * If there is extra space, put dump at the end to
 * reduce the chance that swapping trashes it.
 */
void
dumpconf(void)
{
	int nblks, dumpblks;	/* size of dump area */

	if (dumpdev == NODEV ||
	    (nblks = (bdevsw[major(dumpdev)].d_psize)(dumpdev)) == 0)
		return;
	if (nblks <= ctod(1))
		return;

	dumpblks = cpu_dumpsize();
	if (dumpblks < 0)
		return;
	dumpblks += ctod(cpu_dump_mempagecnt());

	/* If dump won't fit (incl. room for possible label), punt. */
	if (dumpblks > (nblks - ctod(1)))
		return;

	/* Put dump at end of partition */
	dumplo = nblks - dumpblks;

	/* dumpsize is in page units, and doesn't include headers. */
	dumpsize = cpu_dump_mempagecnt();
}

/*
 * Doadump comes here after turning off memory management and
 * getting on the dump stack, either when called above, or by
 * the auto-restart code.
 */
#define BYTES_PER_DUMP  MAXPHYS /* must be a multiple of pagesize */

void
dumpsys(void)
{
	u_long totalbytesleft, bytes, i, n, memseg;
	u_long maddr;
	daddr_t blkno;
	void *va;
	int (*dump)(dev_t, daddr_t, caddr_t, size_t);
	int error;

	if (dumpdev == NODEV)
		return;

	/*
	 * For dumps during autoconfiguration,
	 * if dump device has already configured...
	 */
	if (dumpsize == 0)
		dumpconf();
	if (dumplo <= 0 || dumpsize == 0) {
		printf("\ndump to dev %u,%u not possible\n", major(dumpdev),
		    minor(dumpdev));
		return;
	}
	printf("\ndumping to dev %u,%u offset %ld\n", major(dumpdev),
	    minor(dumpdev), dumplo);

	error = (*bdevsw[major(dumpdev)].d_psize)(dumpdev);
	printf("dump ");
	if (error == -1) {
		printf("area unavailable\n");
		return;
	}

	if ((error = cpu_dump()) != 0)
		goto err;

	totalbytesleft = ptoa(cpu_dump_mempagecnt());
	blkno = dumplo + cpu_dumpsize();
	dump = bdevsw[major(dumpdev)].d_dump;
	error = 0;

	bytes = n = i = memseg = 0;
	maddr = 0;
	va = 0;
#if 0
	for (memseg = 0; memseg < mem_cluster_cnt; memseg++) {
		maddr = mem_clusters[memseg].start;
		bytes = mem_clusters[memseg].size;

		for (i = 0; i < bytes; i += n, totalbytesleft -= n) {
			/* Print out how many MBs we have left to go. */
			if ((totalbytesleft % (1024*1024)) < BYTES_PER_DUMP)
				printf("%ld ", totalbytesleft / (1024 * 1024));

			/* Limit size for next transfer. */
			n = bytes - i;
			if (n > BYTES_PER_DUMP)
				n = BYTES_PER_DUMP;
			if (maddr > 0xffffffff) {
				va = (void *)dumpmem_vaddr;
				if (n > dumpmem_sz)
					n = dumpmem_sz;
				memcpy(va, (void *)PMAP_DIRECT_MAP(maddr), n);
			} else {
				va = (void *)PMAP_DIRECT_MAP(maddr);
			}

			error = (*dump)(dumpdev, blkno, va, n);
			if (error)
				goto err;
			maddr += n;
			blkno += btodb(n);		/* XXX? */

#if 0	/* XXX this doesn't work.  grr. */
			/* operator aborting dump? */
			if (sget() != NULL) {
				error = EINTR;
				break;
			}
#endif
		}
	}
#endif

 err:
	switch (error) {

	case ENXIO:
		printf("device bad\n");
		break;

	case EFAULT:
		printf("device not ready\n");
		break;

	case EINVAL:
		printf("area improper\n");
		break;

	case EIO:
		printf("i/o error\n");
		break;

	case EINTR:
		printf("aborted from console\n");
		break;

	case 0:
		printf("succeeded\n");
		break;

	default:
		printf("error %d\n", error);
		break;
	}
	printf("\n\n");
	delay(5000000);		/* 5 seconds */
}


/*
 * Size of memory segments, before any memory is stolen.
 */
phys_ram_seg_t mem_clusters[VM_PHYSSEG_MAX];
int     mem_cluster_cnt;

/*
 * cpu_dumpsize: calculate size of machine-dependent kernel core dump headers.
 */
int
cpu_dumpsize(void)
{
	int size;

	size = ALIGN(sizeof(kcore_seg_t)) +
	    ALIGN(mem_cluster_cnt * sizeof(phys_ram_seg_t));
	if (roundup(size, dbtob(1)) != dbtob(1))
		return (-1);

	return (1);
}

u_long
cpu_dump_mempagecnt(void)
{
	return 0;
}

int64_t dcache_line_size;	/* The minimum D cache line size */
int64_t icache_line_size;	/* The minimum I cache line size */
int64_t idcache_line_size;	/* The minimum cache line size */
int64_t dczva_line_size;	/* The size of cache line the dc zva zeroes */

void
cache_setup(void)
{
	int dcache_line_shift, icache_line_shift, dczva_line_shift;
	uint32_t ctr_el0;
	uint32_t dczid_el0;

	ctr_el0 = READ_SPECIALREG(ctr_el0);

	/* Read the log2 words in each D cache line */
	dcache_line_shift = CTR_DLINE_SIZE(ctr_el0);
	/* Get the D cache line size */
	dcache_line_size = sizeof(int) << dcache_line_shift;

	/* And the same for the I cache */
	icache_line_shift = CTR_ILINE_SIZE(ctr_el0);
	icache_line_size = sizeof(int) << icache_line_shift;

	idcache_line_size = MIN(dcache_line_size, icache_line_size);

	dczid_el0 = READ_SPECIALREG(dczid_el0);

	/* Check if dc zva is not prohibited */
	if (dczid_el0 & DCZID_DZP)
		dczva_line_size = 0;
	else {
		/* Same as with above calculations */
		dczva_line_shift = DCZID_BS_SIZE(dczid_el0);
		dczva_line_size = sizeof(int) << dczva_line_shift;
	}
}

uint64_t mmap_start;
uint32_t mmap_size;
uint32_t mmap_desc_size;
uint32_t mmap_desc_ver;

EFI_MEMORY_DESCRIPTOR *mmap;

void	collect_kernel_args(const char *);
void	process_kernel_args(void);

int	pmap_bootstrap_bs_map(bus_space_tag_t, bus_addr_t,
	    bus_size_t, int, bus_space_handle_t *);

void
initarm(struct arm64_bootparams *abp)
{
	long kernbase = (long)_start & ~PAGE_MASK;
	long kvo = abp->kern_delta;
	paddr_t memstart, memend;
	paddr_t startpa, endpa, pa;
	vaddr_t vstart, va;
	struct fdt_head *fh;
	void *config = abp->arg2;
	void *fdt = NULL;
	struct fdt_reg reg;
	void *node;
	EFI_PHYSICAL_ADDRESS system_table = 0;
	int (*map_func_save)(bus_space_tag_t, bus_addr_t, bus_size_t, int,
	    bus_space_handle_t *);
	int i;

	/*
	 * Set the per-CPU pointer with a backup in tpidr_el1 to be
	 * loaded when entering the kernel from userland.
	 */
	__asm volatile("mov x18, %0\n"
	    "msr tpidr_el1, %0" :: "r"(&cpu_info_primary));

	cache_setup();

	/* The bootloader has loaded us into a 64MB block. */
	memstart = KERNBASE + kvo;
	memend = memstart + 64 * 1024 * 1024;

	/* Bootstrap enough of pmap to enter the kernel proper. */
	vstart = pmap_bootstrap(kvo, abp->kern_l1pt,
	    kernbase, esym, memstart, memend);

	/* Map the FDT header to determine its size. */
	va = vstart;
	startpa = trunc_page((paddr_t)config);
	endpa = round_page((paddr_t)config + sizeof(struct fdt_head));
	for (pa = startpa; pa < endpa; pa += PAGE_SIZE, va += PAGE_SIZE)
		pmap_kenter_cache(va, pa, PROT_READ | PROT_WRITE, PMAP_CACHE_WB);
	fh = (void *)(vstart + ((paddr_t)config - startpa));
	if (betoh32(fh->fh_magic) != FDT_MAGIC || betoh32(fh->fh_size) == 0)
		panic("%s: no FDT", __func__);

	/* Map the remainder of the FDT. */
	endpa = round_page((paddr_t)config + betoh32(fh->fh_size));
	for (; pa < endpa; pa += PAGE_SIZE, va += PAGE_SIZE)
		pmap_kenter_cache(va, pa, PROT_READ | PROT_WRITE, PMAP_CACHE_WB);
	config = (void *)(vstart + ((paddr_t)config - startpa));
	vstart = va;

	if (!fdt_init(config))
		panic("%s: corrupt FDT", __func__);

	node = fdt_find_node("/chosen");
	if (node != NULL) {
		char *prop;
		int len;
		static uint8_t lladdr[6];

		len = fdt_node_property(node, "bootargs", &prop);
		if (len > 0)
			collect_kernel_args(prop);

		len = fdt_node_property(node, "openbsd,boothowto", &prop);
		if (len == sizeof(boothowto))
			boothowto = bemtoh32((uint32_t *)prop);

		len = fdt_node_property(node, "openbsd,bootduid", &prop);
		if (len == sizeof(bootduid))
			memcpy(bootduid, prop, sizeof(bootduid));

		len = fdt_node_property(node, "openbsd,bootmac", &prop);
		if (len == sizeof(lladdr)) {
			memcpy(lladdr, prop, sizeof(lladdr));
			bootmac = lladdr;
		}

		len = fdt_node_property(node, "openbsd,sr-bootuuid", &prop);
#if NSOFTRAID > 0
		if (len == sizeof(sr_bootuuid))
			memcpy(&sr_bootuuid, prop, sizeof(sr_bootuuid));
#endif
		if (len > 0)
			explicit_bzero(prop, len);

		len = fdt_node_property(node, "openbsd,sr-bootkey", &prop);
#if NSOFTRAID > 0
		if (len == sizeof(sr_bootkey))
			memcpy(&sr_bootkey, prop, sizeof(sr_bootkey));
#endif
		if (len > 0)
			explicit_bzero(prop, len);

		len = fdt_node_property(node, "openbsd,uefi-mmap-start", &prop);
		if (len == sizeof(mmap_start))
			mmap_start = bemtoh64((uint64_t *)prop);
		len = fdt_node_property(node, "openbsd,uefi-mmap-size", &prop);
		if (len == sizeof(mmap_size))
			mmap_size = bemtoh32((uint32_t *)prop);
		len = fdt_node_property(node, "openbsd,uefi-mmap-desc-size", &prop);
		if (len == sizeof(mmap_desc_size))
			mmap_desc_size = bemtoh32((uint32_t *)prop);
		len = fdt_node_property(node, "openbsd,uefi-mmap-desc-ver", &prop);
		if (len == sizeof(mmap_desc_ver))
			mmap_desc_ver = bemtoh32((uint32_t *)prop);

		len = fdt_node_property(node, "openbsd,uefi-system-table", &prop);
		if (len == sizeof(system_table))
			system_table = bemtoh64((uint64_t *)prop);

		len = fdt_node_property(node, "openbsd,dma-constraint", &prop);
		if (len == sizeof(dma_constraint)) {
			dma_constraint.ucr_low = bemtoh64((uint64_t *)prop);
			dma_constraint.ucr_high = bemtoh64((uint64_t *)prop + 1);
		}
	}

	process_kernel_args();

	proc0paddr = (struct user *)abp->kern_stack;

	msgbufaddr = (caddr_t)vstart;
	msgbufphys = pmap_steal_avail(round_page(MSGBUFSIZE), PAGE_SIZE, NULL);
	vstart += round_page(MSGBUFSIZE);

	zero_page = vstart;
	vstart += MAXCPUS * PAGE_SIZE;
	copy_src_page = vstart;
	vstart += MAXCPUS * PAGE_SIZE;
	copy_dst_page = vstart;
	vstart += MAXCPUS * PAGE_SIZE;

	/* Relocate the FDT to safe memory. */
	if (fdt_get_size(config) != 0) {
		uint32_t csize, size = round_page(fdt_get_size(config));
		paddr_t pa;
		vaddr_t va;

		pa = pmap_steal_avail(size, PAGE_SIZE, NULL);
		memcpy((void *)pa, config, size); /* copy to physical */
		for (va = vstart, csize = size; csize > 0;
		    csize -= PAGE_SIZE, va += PAGE_SIZE, pa += PAGE_SIZE)
			pmap_kenter_cache(va, pa, PROT_READ, PMAP_CACHE_WB);

		fdt = (void *)vstart;
		vstart += size;
	}

	/* Relocate the EFI memory map too. */
	if (mmap_start != 0) {
		uint32_t csize, size = round_page(mmap_size);
		paddr_t pa, startpa, endpa;
		vaddr_t va;

		startpa = trunc_page(mmap_start);
		endpa = round_page(mmap_start + mmap_size);
		for (pa = startpa, va = vstart; pa < endpa;
		    pa += PAGE_SIZE, va += PAGE_SIZE)
			pmap_kenter_cache(va, pa, PROT_READ, PMAP_CACHE_WB);
		pa = pmap_steal_avail(size, PAGE_SIZE, NULL);
		memcpy((void *)pa, (caddr_t)vstart + (mmap_start - startpa),
		    mmap_size); /* copy to physical */
		pmap_kremove(vstart, endpa - startpa);

		for (va = vstart, csize = size; csize > 0;
		    csize -= PAGE_SIZE, va += PAGE_SIZE, pa += PAGE_SIZE)
			pmap_kenter_cache(va, pa, PROT_READ | PROT_WRITE, PMAP_CACHE_WB);

		mmap = (void *)vstart;
		vstart += size;
	}

	/* No more KVA stealing after this point. */
	virtual_avail = vstart;

	/* Now we can reinit the FDT, using the virtual address. */
	if (fdt)
		fdt_init(fdt);

	map_func_save = arm64_bs_tag._space_map;
	arm64_bs_tag._space_map = pmap_bootstrap_bs_map;

	consinit();

	arm64_bs_tag._space_map = map_func_save;

	pmap_avail_fixup();

	uvmexp.pagesize = PAGE_SIZE;
	uvm_setpagesize();

	/* Make what's left of the initial 64MB block available to UVM. */
	pmap_physload_avail();

	/* Make all other physical memory available to UVM. */
	if (mmap && mmap_desc_ver == EFI_MEMORY_DESCRIPTOR_VERSION) {
		EFI_MEMORY_DESCRIPTOR *desc = mmap;

		/*
		 * Load all memory marked as EfiConventionalMemory,
		 * EfiBootServicesCode or EfiBootServicesData.
		 * The initial 64MB memory block should be marked as
		 * EfiLoaderData so it won't be added here.
		 */
		for (i = 0; i < mmap_size / mmap_desc_size; i++) {
#ifdef MMAP_DEBUG
			printf("type 0x%x pa 0x%llx va 0x%llx pages 0x%llx attr 0x%llx\n",
			    desc->Type, desc->PhysicalStart,
			    desc->VirtualStart, desc->NumberOfPages,
			    desc->Attribute);
#endif
			if (desc->Type == EfiConventionalMemory ||
			    desc->Type == EfiBootServicesCode ||
			    desc->Type == EfiBootServicesData) {
				reg.addr = desc->PhysicalStart;
				reg.size = ptoa(desc->NumberOfPages);
				memreg_add(&reg);
			}
			desc = NextMemoryDescriptor(desc, mmap_desc_size);
		}
	} else {
		node = fdt_find_node("/memory");
		if (node == NULL)
			panic("%s: no memory specified", __func__);

		for (i = 0; nmemreg < nitems(memreg); i++) {
			if (fdt_get_reg(node, i, &reg))
				break;
			if (reg.size == 0)
				continue;
			memreg_add(&reg);
		}
	}

	/* Remove reserved memory. */
	node = fdt_find_node("/reserved-memory");
	if (node) {
		for (node = fdt_child_node(node); node;
		    node = fdt_next_node(node)) {
			char *no_map;
			if (fdt_node_property(node, "no-map", &no_map) < 0)
				continue;
			if (fdt_get_reg(node, 0, &reg))
				continue;
			if (reg.size == 0)
				continue;
			memreg_remove(&reg);
		}
	}

	/* Remove the initial 64MB block. */
	reg.addr = memstart;
	reg.size = memend - memstart;
	memreg_remove(&reg);

	for (i = 0; i < nmemreg; i++) {
		paddr_t start = memreg[i].addr;
		paddr_t end = start + memreg[i].size;

		uvm_page_physload(atop(start), atop(end),
		    atop(start), atop(end), 0);
		physmem += atop(end - start);
	}

	kmeminit_nkmempages();

	/*
	 * Make sure that we have enough KVA to initialize UVM.  In
	 * particular, we need enough KVA to be able to allocate the
	 * vm_page structures and nkmempages for malloc(9).
	 */
	pmap_growkernel(VM_MIN_KERNEL_ADDRESS + 1024 * 1024 * 1024 +
	    physmem * sizeof(struct vm_page) + ptoa(nkmempages));

#ifdef DDB
	db_machine_init();

	/* Firmware doesn't load symbols. */
	ddb_init();

	if (boothowto & RB_KDB)
		db_enter();
#endif

	softintr_init();
	splraise(IPL_IPI);
}

char bootargs[256];

void
collect_kernel_args(const char *args)
{
	/* Make a local copy of the bootargs */
	strlcpy(bootargs, args, sizeof(bootargs));
}

void
process_kernel_args(void)
{
	char *cp = bootargs;

	if (*cp == 0)
		return;

	/* Skip the kernel image filename */
	while (*cp != ' ' && *cp != 0)
		cp++;

	if (*cp != 0)
		*cp++ = 0;

	while (*cp == ' ')
		cp++;

	boot_args = cp;

	printf("bootargs: %s\n", boot_args);

	/* Setup pointer to boot flags */
	while (*cp != '-')
		if (*cp++ == '\0')
			return;

	while (*cp != 0) {
		switch (*cp) {
		case 'a':
			boothowto |= RB_ASKNAME;
			break;
		case 'c':
			boothowto |= RB_CONFIG;
			break;
		case 'd':
			boothowto |= RB_KDB;
			break;
		case 's':
			boothowto |= RB_SINGLE;
			break;
		default:
			printf("unknown option `%c'\n", *cp);
			break;
		}
		cp++;
	}
}

/*
 * Allow bootstrap to steal KVA after machdep has given it back to pmap.
 */
int
pmap_bootstrap_bs_map(bus_space_tag_t t, bus_addr_t bpa, bus_size_t size,
    int flags, bus_space_handle_t *bshp)
{
	u_long startpa, pa, endpa;
	vaddr_t va;
	int cache = PMAP_CACHE_DEV_NGNRNE;

	if (flags & BUS_SPACE_MAP_PREFETCHABLE)
		cache = PMAP_CACHE_CI;

	va = virtual_avail;	/* steal memory from virtual avail. */

	startpa = trunc_page(bpa);
	endpa = round_page((bpa + size));

	*bshp = (bus_space_handle_t)(va + (bpa - startpa));

	for (pa = startpa; pa < endpa; pa += PAGE_SIZE, va += PAGE_SIZE)
		pmap_kenter_cache(va, pa, PROT_READ | PROT_WRITE, cache);

	virtual_avail = va;

	return 0;
}

void
memreg_add(const struct fdt_reg *reg)
{
	int i;

	for (i = 0; i < nmemreg; i++) {
		if (reg->addr == memreg[i].addr + memreg[i].size) {
			memreg[i].size += reg->size;
			return;
		}
		if (reg->addr + reg->size == memreg[i].addr) {
			memreg[i].addr = reg->addr;
			memreg[i].size += reg->size;
			return;
		}
	}

	if (nmemreg >= nitems(memreg))
		return;

	memreg[nmemreg++] = *reg;
}

void
memreg_remove(const struct fdt_reg *reg)
{
	uint64_t start = reg->addr;
	uint64_t end = reg->addr + reg->size;
	int i, j;

	for (i = 0; i < nmemreg; i++) {
		uint64_t memstart = memreg[i].addr;
		uint64_t memend = memreg[i].addr + memreg[i].size;

		if (end <= memstart)
			continue;
		if (start >= memend)
			continue;

		if (start <= memstart)
			memstart = MIN(end, memend);
		if (end >= memend)
			memend = MAX(start, memstart);

		if (start > memstart && end < memend) {
			if (nmemreg < nitems(memreg)) {
				memreg[nmemreg].addr = end;
				memreg[nmemreg].size = memend - end;
				nmemreg++;
			}
			memend = start;
		}
		memreg[i].addr = memstart;
		memreg[i].size = memend - memstart;
	}

	/* Remove empty slots. */
	for (i = nmemreg - 1; i >= 0; i--) {
		if (memreg[i].size == 0) {
			for (j = i; (j + 1) < nmemreg; j++)
				memreg[j] = memreg[j + 1];
			nmemreg--;
		}
	}
}
