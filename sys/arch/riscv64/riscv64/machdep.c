/*	$OpenBSD: machdep.c,v 1.40 2024/11/18 05:32:39 jsg Exp $	*/

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
#include <sys/msgbuf.h>
#include <sys/buf.h>
#include <sys/sensors.h>
#include <sys/malloc.h>
#include <sys/syscallargs.h>

#include <net/if.h>
#include <uvm/uvm_extern.h>
#include <dev/cons.h>
#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>
#include <machine/param.h>
#include <machine/bootconfig.h>
#include <machine/bus.h>
#include <machine/sbi.h>
#include <machine/sysarch.h>

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

struct vm_map *exec_map = NULL;
struct vm_map *phys_map = NULL;

int physmem;

caddr_t msgbufaddr;
paddr_t msgbufphys;

struct user *proc0paddr;

struct uvm_constraint_range  dma_constraint = { 0x0, (paddr_t)-1 };
struct uvm_constraint_range *uvm_md_constraints[] = {
	&dma_constraint,
	NULL,
};

/* the following is used externally (sysctl_hw) */
char    machine[] = MACHINE;		/* from <machine/param.h> */

int safepri = 0;

uint32_t boot_hart;	/* The hart we booted on. */
struct cpu_info cpu_info_primary;
struct cpu_info *cpu_info[MAXCPUS] = { &cpu_info_primary };

uint64_t tb_freq = 1000000;

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

void	com_fdt_init_cons(void);
void	sfuart_init_cons(void);

void
consinit(void)
{
	static int consinit_called = 0;

	if (consinit_called != 0)
		return;

	consinit_called = 1;

	com_fdt_init_cons();
	sfuart_init_cons();
}

void
cpu_idle_cycle(void)
{
	// Enable interrupts
	intr_enable();
	// XXX Data Sync Barrier? (Maybe SFENCE???)
	__asm volatile("wfi");
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

	sbi_print_version();

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

/*
 * Move parts of cpu_switchto into C, too difficult in asm
 */

void    cpu_switchto_asm(struct proc *, struct proc *);

void
cpu_switchto(struct proc *old, struct proc *new)
{
	if (old) {
		struct pcb *pcb = &old->p_addr->u_pcb;
		struct trapframe *tf = pcb->pcb_tf;

		if (pcb->pcb_flags & PCB_FPU)
			fpu_save(old, tf);

		/* drop FPU state */
		tf->tf_sstatus &= ~SSTATUS_FS_MASK;
		tf->tf_sstatus |= SSTATUS_FS_OFF;
	}

	cpu_switchto_asm(old, new);
}

/*
 * machine dependent system variables.
 */

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
	default:
		return (EOPNOTSUPP);
	}
	/* NOTREACHED */
}

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
		printf("no dump so far\n");

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
	struct trapframe *tf = p->p_addr->u_pcb.pcb_tf;
	struct pcb *pcb = &p->p_addr->u_pcb;

	/* If we were using the FPU, forget about it. */
	pcb->pcb_flags &= ~PCB_FPU;
	tf->tf_sstatus &= ~SSTATUS_FS_MASK;
	tf->tf_sstatus |= SSTATUS_FS_OFF;

	memset(tf, 0, sizeof *tf);
	tf->tf_sp = STACKALIGN(stack);
	tf->tf_ra = pack->ep_entry;
	tf->tf_sepc = pack->ep_entry;
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


/*
 * Size of memory segments, before any memory is stolen.
 */
phys_ram_seg_t mem_clusters[VM_PHYSSEG_MAX];
int	mem_cluster_cnt;

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

/*
 * These variables are needed by /sbin/savecore
 */
u_long	dumpmag = 0x8fca0101;	/* magic number */
int	dumpsize = 0;		/* pages */
long	dumplo = 0;		/* blocks */

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

int
sys_sysarch(struct proc *p, void *v, register_t *retval)
{
	struct sys_sysarch_args /* {
		syscallarg(int) op;
		syscallarg(void *) parms;
	} */ *uap = v;
	struct riscv_sync_icache_args args;
	int error = 0;

	switch (SCARG(uap, op)) {
	case RISCV_SYNC_ICACHE:
		if (SCARG(uap, parms) != NULL)
			error = copyin(SCARG(uap, parms), &args, sizeof(args));
		if (error)
			break;
		/*
		 * XXX Validate args.addr and args.len before using them.
		 */
		pmap_proc_iflush(p->p_p, (vaddr_t)args.addr, args.len);
		break;
	default:
		error = EINVAL;
		break;
	}

	return (error);
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
initriscv(struct riscv_bootparams *rbp)
{
	paddr_t memstart, memend;
	paddr_t startpa, endpa, pa;
	vaddr_t vstart, va;
	struct fdt_head *fh;
	void *config = (void *)rbp->dtbp_phys;
	void *fdt = NULL;
	struct fdt_reg reg;
	void *node;
	EFI_PHYSICAL_ADDRESS system_table = 0;
	int (*map_func_save)(bus_space_tag_t, bus_addr_t, bus_size_t, int,
	    bus_space_handle_t *);
	int i;

	/* Set the per-CPU pointer. */
	__asm volatile("mv tp, %0" :: "r"(&cpu_info_primary));

	sbi_init();

	/* The bootloader has loaded us into a 64MB block. */
	memstart = rbp->kern_phys;
	memend = memstart + 64 * 1024 * 1024;

	/* Bootstrap enough of pmap to enter the kernel proper. */
	vstart = pmap_bootstrap(rbp->kern_phys - KERNBASE, rbp->kern_l1pt,
	    KERNBASE, esym, memstart, memend);

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

	node = fdt_find_node("/cpus");
	if (node != NULL) {
		char *prop;
		int len;

		len = fdt_node_property(node, "timebase-frequency", &prop);
		if (len == sizeof(uint32_t))
			tb_freq = bemtoh32((uint32_t *)prop);
	}

	node = fdt_find_node("/chosen");
	if (node != NULL) {
		char *prop;
		int len;
		static uint8_t lladdr[6];

		len = fdt_node_property(node, "boot-hartid", &prop);
		if (len == sizeof(boot_hart))
			boot_hart = bemtoh32((uint32_t *)prop);

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

	proc0paddr = (struct user *)rbp->kern_stack;

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
		memcpy((void *)PHYS_TO_DMAP(pa), config, size);
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
		memcpy((void *)PHYS_TO_DMAP(pa),
		    (caddr_t)vstart + (mmap_start - startpa), mmap_size);
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

	map_func_save = riscv64_bs_tag._space_map;
	riscv64_bs_tag._space_map = pmap_bootstrap_bs_map;

	consinit();

	riscv64_bs_tag._space_map = map_func_save;

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
		 * Don't bother with blocks smaller than 64KB.  The
		 * initial 64MB memory block should be marked as
		 * EfiLoaderData so it won't be added here.
		 */
		for (i = 0; i < mmap_size / mmap_desc_size; i++) {
			printf("type 0x%x pa 0x%llx va 0x%llx pages 0x%llx attr 0x%llx\n",
			    desc->Type, desc->PhysicalStart,
			    desc->VirtualStart, desc->NumberOfPages,
			    desc->Attribute);
			if ((desc->Type == EfiConventionalMemory ||
			     desc->Type == EfiBootServicesCode ||
			     desc->Type == EfiBootServicesData) &&
			    desc->NumberOfPages >= 16) {
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
	}

	/*
	 * Determine physical RAM size from the /memory nodes in the
	 * FDT.  There can be multiple nodes and each node can contain
	 * multiple ranges.
	 */
	node = fdt_find_node("/memory");
	if (node == NULL)
		panic("%s: no memory specified", __func__);
	while (node) {
		const char *s = fdt_node_name(node);
		if (strncmp(s, "memory", 6) == 0 &&
		    (s[6] == '\0' || s[6] == '@')) {
			for (i = 0; i < VM_PHYSSEG_MAX; i++) {
				if (fdt_get_reg(node, i, &reg))
					break;
				if (reg.size == 0)
					continue;
				physmem += atop(reg.size);
			}
		}

		node = fdt_next_node(node);
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

	while (*++cp != 0) {
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

	va = virtual_avail;	/* steal memory from virtual avail. */

	startpa = trunc_page(bpa);
	endpa = round_page((bpa + size));

	*bshp = (bus_space_handle_t)(va + (bpa - startpa));

	for (pa = startpa; pa < endpa; pa += PAGE_SIZE, va += PAGE_SIZE)
		pmap_kenter_cache(va, pa, PROT_READ | PROT_WRITE,
		    PMAP_CACHE_DEV);

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
