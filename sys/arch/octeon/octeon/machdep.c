/*	$OpenBSD: machdep.c,v 1.137 2023/10/24 13:20:10 claudio Exp $ */

/*
 * Copyright (c) 2009, 2010 Miodrag Vallat.
 * Copyright (c) 2019 Visa Hankala.
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
/*
 * Copyright (c) 2003-2004 Opsycon AB  (www.opsycon.se / www.opsycon.com)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/reboot.h>
#include <sys/conf.h>
#include <sys/msgbuf.h>
#include <sys/tty.h>
#include <sys/user.h>
#include <sys/exec.h>
#include <sys/sysctl.h>
#include <sys/mount.h>
#include <sys/syscallargs.h>
#include <sys/exec_elf.h>
#include <sys/timetc.h>
#ifdef SYSVSHM
#include <sys/shm.h>
#endif
#ifdef SYSVSEM
#include <sys/sem.h>
#endif

#include <net/if.h>

#include <uvm/uvm_extern.h>

#include <machine/db_machdep.h>
#include <ddb/db_interface.h>

#include <machine/autoconf.h>
#include <mips64/cache.h>
#include <machine/cpu.h>
#include <mips64/mips_cpu.h>
#include <machine/memconf.h>

#include <dev/cons.h>
#include <dev/ofw/fdt.h>

#include <octeon/dev/cn30xxcorereg.h>
#include <octeon/dev/cn30xxipdreg.h>
#include <octeon/dev/iobusvar.h>
#include <machine/octeonreg.h>
#include <machine/octeonvar.h>
#include <machine/octeon_model.h>

#include "octboot.h"

/* The following is used externally (sysctl_hw) */
char	machine[] = MACHINE;		/* Machine "architecture" */
char	cpu_model[64];

struct uvm_constraint_range  dma_constraint = { 0x0, 0xffffffffUL };
struct uvm_constraint_range *uvm_md_constraints[] = { NULL };

vm_map_t exec_map;
vm_map_t phys_map;

extern struct timecounter cp0_timecounter;
extern uint8_t dt_blob_start[];

enum octeon_board octeon_board;
struct boot_desc *octeon_boot_desc;
struct boot_info *octeon_boot_info;

void		*octeon_fdt;
unsigned int	 octeon_ver;

/*
 * safepri is a safe priority for sleep to set for a spin-wait
 * during autoconfiguration or after a panic.
 */
int   safepri = 0;

caddr_t	msgbufbase;

int	physmem;		/* Max supported memory, changes to actual. */
int	ncpu = 1;		/* At least one CPU in the system. */
struct	user *proc0paddr;

struct cpu_hwinfo bootcpu_hwinfo;

/* Pointers to the start and end of the symbol table. */
caddr_t	ssym;
caddr_t	esym;
caddr_t	ekern;

struct phys_mem_desc mem_layout[MAXMEMSEGS];

void		dumpsys(void);
void		dumpconf(void);
vaddr_t		mips_init(register_t, register_t, register_t, register_t);
int		is_memory_range(paddr_t, psize_t, psize_t);
void		octeon_memory_init(struct boot_info *);
void		octeon_sync_tc(vaddr_t, uint64_t, uint64_t);
int		octeon_cpuspeed(int *);
void		octeon_tlb_init(void);
static void	process_bootargs(void);
static uint64_t	get_ncpusfound(void);
static enum octeon_board get_octeon_board(void);

cons_decl(octuart);
struct consdev uartcons = cons_init(octuart);

u_int		ioclock_get_timecount(struct timecounter *);

struct timecounter ioclock_timecounter = {
	.tc_get_timecount = ioclock_get_timecount,
	.tc_counter_mask = 0xffffffff,	/* truncated to 32 bits */
	.tc_frequency = 0,		/* determined at runtime */
	.tc_name = "ioclock",
	.tc_quality = 0,		/* ioclock can be overridden
					 * by cp0 counter */
	.tc_priv = 0,			/* clock register,
					 * determined at runtime */
	.tc_user = 0,			/* expose to user */
};

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

static struct octeon_bootmem_block *
pa_to_block(paddr_t addr)
{
	return (struct octeon_bootmem_block *)PHYS_TO_XKPHYS(addr, CCA_CACHED);
}

void
octeon_memory_init(struct boot_info *boot_info)
{
	struct octeon_bootmem_block *block;
	struct octeon_bootmem_desc *memdesc;
	paddr_t blockaddr;
	uint64_t fp, lp;
	int i;

	physmem = atop((uint64_t)boot_info->dram_size << 20);

	if (boot_info->phys_mem_desc_addr == 0)
		panic("bootmem desc is missing");
	memdesc = (struct octeon_bootmem_desc *)PHYS_TO_XKPHYS(
	    boot_info->phys_mem_desc_addr, CCA_CACHED);
	printf("bootmem desc 0x%x version %d.%d\n",
	    boot_info->phys_mem_desc_addr, memdesc->major_version,
	    memdesc->minor_version);
	if (memdesc->major_version > 3)
		panic("unhandled bootmem desc version %d.%d",
		    memdesc->major_version, memdesc->minor_version);

	blockaddr = memdesc->head_addr;
	if (blockaddr == 0)
		panic("bootmem list is empty");
	for (i = 0; i < MAXMEMSEGS && blockaddr != 0; blockaddr = block->next) {
		block = pa_to_block(blockaddr);
		printf("avail phys mem 0x%016lx - 0x%016lx\n", blockaddr,
		    (paddr_t)(blockaddr + block->size));

#if NOCTBOOT > 0
		/*
		 * Reserve the physical memory below the boot kernel
		 * for loading the actual kernel.
		 */
		extern char start[];
		if (blockaddr < CKSEG_SIZE &&
		    PHYS_TO_CKSEG0(blockaddr) < (vaddr_t)start) {
			printf("skipped\n");
			continue;
		}
#endif

		fp = atop(round_page(blockaddr));
		lp = atop(trunc_page(blockaddr + block->size));

		/* Clamp to the range of the pmap. */
		if (fp > atop(pfn_to_pad(PG_FRAME)))
			continue;
		if (lp > atop(pfn_to_pad(PG_FRAME)) + 1)
			lp = atop(pfn_to_pad(PG_FRAME)) + 1;
		if (fp >= lp)
			continue;

		/* Skip small fragments. */
		if (lp - fp < atop(1u << 20))
			continue;

		mem_layout[i].mem_first_page = fp;
		mem_layout[i].mem_last_page = lp;
		i++;
	}

	printf("Total DRAM Size 0x%016llX\n",
	    (uint64_t)boot_info->dram_size << 20);

	for (i = 0; mem_layout[i].mem_last_page; i++) {
		printf("mem_layout[%d] page 0x%016llX -> 0x%016llX\n", i,
		    mem_layout[i].mem_first_page, mem_layout[i].mem_last_page);

#if NOCTBOOT > 0
		fp = mem_layout[i].mem_first_page;
		lp = mem_layout[i].mem_last_page;
		if (bootmem_alloc_region(ptoa(fp), ptoa(lp) - ptoa(fp)) != 0)
			panic("%s: bootmem allocation failed", __func__);
#endif
	}
}

/*
 * Do all the stuff that locore normally does before calling main().
 * Reset mapping and set up mapping to hardware and init "wired" reg.
 */
vaddr_t
mips_init(register_t a0, register_t a1, register_t a2, register_t a3)
{
	uint prid;
	vaddr_t xtlb_handler;
	size_t len;
	int i;
	struct boot_desc *boot_desc;
	struct boot_info *boot_info;
	int32_t *symptr;
	uint32_t config4;

	extern char start[], end[];
	extern char exception[], e_exception[];
	extern void xtlb_miss;

	boot_desc = (struct boot_desc *)a3;
	boot_info = (struct boot_info *)
	    PHYS_TO_XKPHYS(boot_desc->boot_info_addr, CCA_CACHED);

	/*
	 * Save the pointers for future reference.
	 * The descriptors are located outside the free memory,
	 * and the kernel should preserve them.
	 */
	octeon_boot_desc = boot_desc;
	octeon_boot_info = boot_info;

#ifdef MULTIPROCESSOR
	/*
	 * Set curcpu address on primary processor.
	 */
	setcurcpu(&cpu_info_primary);
#endif

	/*
	 * Set up early console output.
	 */
	cn_tab = &uartcons;

	/*
	 * Reserve space for the symbol table, if it exists.
	 */
	symptr = (int32_t *)roundup((vaddr_t)end, BOOTMEM_BLOCK_ALIGN);
	ssym = (char *)(vaddr_t)symptr[0];
	if (((long)ssym - (long)end) >= 0 &&
	    ((long)ssym - (long)end) <= 0x1000 &&
	    ssym[0] == ELFMAG0 && ssym[1] == ELFMAG1 &&
	    ssym[2] == ELFMAG2 && ssym[3] == ELFMAG3) {
		/* Pointers exist directly after kernel. */
		esym = (char *)(vaddr_t)symptr[1];
		ekern = esym;
	} else {
		/* Pointers aren't setup either... */
		ssym = NULL;
		esym = NULL;
		ekern = end;
	}

	prid = cp0_get_prid();

	bootcpu_hwinfo.clock = boot_desc->eclock;

	switch (octeon_model_family(prid)) {
	default:
		octeon_ver = OCTEON_1;
		break;
	case OCTEON_MODEL_FAMILY_CN50XX:
		octeon_ver = OCTEON_PLUS;
		break;
	case OCTEON_MODEL_FAMILY_CN61XX:
	case OCTEON_MODEL_FAMILY_CN63XX:
	case OCTEON_MODEL_FAMILY_CN66XX:
	case OCTEON_MODEL_FAMILY_CN68XX:
		octeon_ver = OCTEON_2;
		break;
	case OCTEON_MODEL_FAMILY_CN71XX:
	case OCTEON_MODEL_FAMILY_CN73XX:
	case OCTEON_MODEL_FAMILY_CN78XX:
		octeon_ver = OCTEON_3;
		break;
	}

	/*
	 * Look at arguments passed to us and compute boothowto.
	 */
	boothowto = RB_AUTOBOOT;

	octeon_memory_init(boot_info);

	/*
	 * Set pagesize to enable use of page macros and functions.
	 * Commit available memory to UVM system.
	 */

	uvmexp.pagesize = PAGE_SIZE;
	uvm_setpagesize();

	for (i = 0; i < MAXMEMSEGS && mem_layout[i].mem_last_page != 0; i++) {
		uint64_t fp, lp;
		uint64_t firstkernpage, lastkernpage;
		paddr_t firstkernpa, lastkernpa;

		/* kernel is linked in CKSEG0 */
		firstkernpa = CKSEG0_TO_PHYS((vaddr_t)start);
		lastkernpa = CKSEG0_TO_PHYS((vaddr_t)ekern);

		firstkernpage = atop(trunc_page(firstkernpa));
		lastkernpage = atop(round_page(lastkernpa));

		fp = mem_layout[i].mem_first_page;
		lp = mem_layout[i].mem_last_page;

		/* Account for kernel and kernel symbol table. */
		if (fp >= firstkernpage && lp < lastkernpage)
			continue;	/* In kernel. */

		if (lp < firstkernpage || fp > lastkernpage) {
			uvm_page_physload(fp, lp, fp, lp, 0);
			continue;	/* Outside kernel. */
		}

		if (fp >= firstkernpage)
			fp = lastkernpage;
		else if (lp < lastkernpage)
			lp = firstkernpage;
		else { /* Need to split! */
			uint64_t xp = firstkernpage;
			uvm_page_physload(fp, xp, fp, xp, 0);
			fp = lastkernpage;
		}
		if (lp > fp) {
			uvm_page_physload(fp, lp, fp, lp, 0);
		}
	}

	bootcpu_hwinfo.c0prid = prid;
	bootcpu_hwinfo.type = (prid >> 8) & 0xff;
	if (cp0_get_config_1() & CONFIG1_FP)
		bootcpu_hwinfo.c1prid = cp1_get_prid();
	else
		bootcpu_hwinfo.c1prid = 0;

	bootcpu_hwinfo.tlbsize = 1 + ((cp0_get_config_1() & CONFIG1_MMUSize1)
	    >> CONFIG1_MMUSize1_SHIFT);
	if (cp0_get_config_3() & CONFIG3_M) {
		config4 = cp0_get_config_4();
		if (((config4 & CONFIG4_MMUExtDef) >>
		    CONFIG4_MMUExtDef_SHIFT) == 1)
			bootcpu_hwinfo.tlbsize +=
			    (config4 & CONFIG4_MMUSizeExt) << 6;
	}

	bcopy(&bootcpu_hwinfo, &curcpu()->ci_hw, sizeof(struct cpu_hwinfo));

	/*
	 * Configure cache.
	 */

	Octeon_ConfigCache(curcpu());
	Octeon_SyncCache(curcpu());

	octeon_tlb_init();

	snprintf(cpu_model, sizeof(cpu_model), "Cavium OCTEON (rev %d.%d) @ %d MHz",
		 (bootcpu_hwinfo.c0prid >> 4) & 0x0f,
		 bootcpu_hwinfo.c0prid & 0x0f,
		 bootcpu_hwinfo.clock / 1000000);

	cpu_cpuspeed = octeon_cpuspeed;
	ncpusfound = get_ncpusfound();
	octeon_board = get_octeon_board();

	process_bootargs();

	/*
	 * Save the FDT and let the system use it.
	 */
	if (octeon_boot_info->ver_minor >= 3 &&
	    octeon_boot_info->fdt_addr != 0) {
		void *fdt;
		size_t fdt_size;

		fdt = (void *)PHYS_TO_XKPHYS(octeon_boot_info->fdt_addr,
		    CCA_CACHED);
		if (fdt_init(fdt) != 0 && (fdt_size = fdt_get_size(fdt)) != 0) {
			octeon_fdt = (void *)pmap_steal_memory(fdt_size, NULL,
			    NULL);
			memcpy(octeon_fdt, fdt, fdt_size);
			fdt_init(octeon_fdt);
		}
	} else
		fdt_init(dt_blob_start);

	/*
	 * Get a console, very early but after initial mapping setup.
	 */

	consinit();
	printf("Initial setup done, switching console.\n");

#define DUMP_BOOT_DESC(field, format) \
	printf("boot_desc->" #field ":" #format "\n", boot_desc->field)
#define DUMP_BOOT_INFO(field, format) \
	printf("boot_info->" #field ":" #format "\n", boot_info->field)

	DUMP_BOOT_DESC(desc_ver, %d);
	DUMP_BOOT_DESC(desc_size, %d);
	DUMP_BOOT_DESC(stack_top, %llx);
	DUMP_BOOT_DESC(heap_start, %llx);
	DUMP_BOOT_DESC(heap_end, %llx);
	DUMP_BOOT_DESC(argc, %d);
	DUMP_BOOT_DESC(flags, %#x);
	DUMP_BOOT_DESC(core_mask, %#x);
	DUMP_BOOT_DESC(dram_size, %d);
	DUMP_BOOT_DESC(phy_mem_desc_addr, %#x);
	DUMP_BOOT_DESC(debugger_flag_addr, %#x);
	DUMP_BOOT_DESC(eclock, %d);
	DUMP_BOOT_DESC(boot_info_addr, %#llx);

	DUMP_BOOT_INFO(ver_major, %d);
	DUMP_BOOT_INFO(ver_minor, %d);
	DUMP_BOOT_INFO(stack_top, %llx);
	DUMP_BOOT_INFO(heap_start, %llx);
	DUMP_BOOT_INFO(heap_end, %llx);
	DUMP_BOOT_INFO(boot_desc_addr, %#llx);
	DUMP_BOOT_INFO(exception_base_addr, %#x);
	DUMP_BOOT_INFO(stack_size, %d);
	DUMP_BOOT_INFO(flags, %#x);
	DUMP_BOOT_INFO(core_mask, %#x);
	DUMP_BOOT_INFO(dram_size, %d);
	DUMP_BOOT_INFO(phys_mem_desc_addr, %#x);
	DUMP_BOOT_INFO(debugger_flags_addr, %#x);
	DUMP_BOOT_INFO(eclock, %d);
	DUMP_BOOT_INFO(dclock, %d);
	DUMP_BOOT_INFO(board_type, %d);
	DUMP_BOOT_INFO(board_rev_major, %d);
	DUMP_BOOT_INFO(board_rev_minor, %d);
	DUMP_BOOT_INFO(mac_addr_count, %d);
	DUMP_BOOT_INFO(cf_common_addr, %#llx);
	DUMP_BOOT_INFO(cf_attr_addr, %#llx);
	DUMP_BOOT_INFO(led_display_addr, %#llx);
	DUMP_BOOT_INFO(dfaclock, %d);
	DUMP_BOOT_INFO(config_flags, %#x);
	if (octeon_boot_info->ver_minor >= 3)
		DUMP_BOOT_INFO(fdt_addr, %#llx);

	/*
	 * It is possible to launch the kernel from the bootloader without
	 * physical CPU 0. That does not really work, however, because of the
	 * way how the kernel assigns and uses cpuids. Moreover, cnmac(4) is
	 * hard coded to use CPU 0 for packet reception.
	 */
	if (!(octeon_boot_info->core_mask & 1))
		panic("cannot run without physical CPU 0");

	/*
	 * Use bits of board information to improve initial entropy.
	 */
	enqueue_randomness((octeon_boot_info->board_type << 16) |
	    (octeon_boot_info->board_rev_major << 8) |
	    octeon_boot_info->board_rev_minor);
	len = strnlen(octeon_boot_info->board_serial,
	    sizeof(octeon_boot_info->board_serial));
	for (i = 0; i < len; i++)
		enqueue_randomness(octeon_boot_info->board_serial[i]);

	/*
	 * Init message buffer.
	 */
	msgbufbase = (caddr_t)pmap_steal_memory(MSGBUFSIZE, NULL,NULL);
	initmsgbuf(msgbufbase, MSGBUFSIZE);

	/*
	 * Allocate U page(s) for proc[0], pm_tlbpid 1.
	 */

	proc0.p_addr = proc0paddr = curcpu()->ci_curprocpaddr =
	    (struct user *)pmap_steal_memory(USPACE, NULL, NULL);
	proc0.p_md.md_regs = (struct trapframe *)&proc0paddr->u_pcb.pcb_regs;
	tlb_set_pid(MIN_USER_ASID);

	/*
	 * Bootstrap VM system.
	 */

	pmap_bootstrap();

	/*
	 * Copy down exception vector code.
	 */

	bcopy(exception, (char *)CACHE_ERR_EXC_VEC, e_exception - exception);
	bcopy(exception, (char *)GEN_EXC_VEC, e_exception - exception);

	/*
	 * Build proper TLB refill handler trampolines.
	 */

	xtlb_handler = (vaddr_t)&xtlb_miss;
	build_trampoline(TLB_MISS_EXC_VEC, xtlb_handler);
	build_trampoline(XTLB_MISS_EXC_VEC, xtlb_handler);

	/*
	 * Turn off bootstrap exception vectors.
	 * (this is done by PMON already, but it doesn't hurt to be safe)
	 */

	setsr(getsr() & ~SR_BOOT_EXC_VEC);
	proc0.p_md.md_regs->sr = getsr();

#ifdef DDB
	db_machine_init();
	if (boothowto & RB_KDB)
		db_enter();
#endif

	switch (octeon_model_family(prid)) {
	case OCTEON_MODEL_FAMILY_CN73XX:
	case OCTEON_MODEL_FAMILY_CN78XX:
		ioclock_timecounter.tc_priv = (void *)FPA3_CLK_COUNT;
		break;
	default:
		ioclock_timecounter.tc_priv = (void *)IPD_CLK_COUNT;
		break;
	}
	ioclock_timecounter.tc_frequency = octeon_ioclock_speed();
	tc_init(&ioclock_timecounter);

	cpu_has_synced_cp0_count = 1;
	cp0_timecounter.tc_quality = 1000;
	cp0_timecounter.tc_user = TC_CP0_COUNT;

	/*
	 * Return the new kernel stack pointer.
	 */
	return ((vaddr_t)proc0paddr + USPACE - 64);
}

/*
 * Console initialization: called early on from main, before vm init or startup.
 * Do enough configuration to choose and initialize a console.
 */
void
consinit()
{
	static int console_ok = 0;

	if (console_ok == 0) {
		com_fdt_init_cons();
		cninit();
		console_ok = 1;
	}
}

/*
 * cpu_startup: allocate memory for variable-sized tables, initialize CPU, and
 * do auto-configuration.
 */
void
cpu_startup()
{
	vaddr_t minaddr, maxaddr;

	/*
	 * Good {morning,afternoon,evening,night}.
	 */
	printf("%s", version);
	printf("real mem = %lu (%luMB)\n", ptoa((psize_t)physmem),
	    ptoa((psize_t)physmem)/1024/1024);

	/*
	 * Allocate a submap for exec arguments. This map effectively
	 * limits the number of processes exec'ing at any time.
	 */
	minaddr = vm_map_min(kernel_map);
	exec_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr,
	    16 * NCARGS, VM_MAP_PAGEABLE, FALSE, NULL);
	/* Allocate a submap for physio. */
	phys_map = uvm_km_suballoc(kernel_map, &minaddr, &maxaddr,
	    VM_PHYS_SIZE, 0, FALSE, NULL);

	printf("avail mem = %lu (%luMB)\n", ptoa(uvmexp.free),
	    ptoa(uvmexp.free)/1024/1024);

	/*
	 * Set up buffers, so they can be used to read disk labels.
	 */
	bufinit();

	/*
	 * Configure the system.
	 */
	if (boothowto & RB_CONFIG) {
#ifdef BOOT_CONFIG
		user_config();
#else
		printf("kernel does not support -c; continuing..\n");
#endif
	}
}

int
octeon_cpuspeed(int *freq)
{
	*freq = octeon_boot_info->eclock / 1000000;
	return (0);
}

int
octeon_ioclock_speed(void)
{
	u_int64_t mio_rst_boot, rst_boot;

	switch (octeon_ver) {
	case OCTEON_2:
		mio_rst_boot = octeon_xkphys_read_8(MIO_RST_BOOT);
		return OCTEON_IO_REF_CLOCK * ((mio_rst_boot >>
		    MIO_RST_BOOT_PNR_MUL_SHIFT) & MIO_RST_BOOT_PNR_MUL_MASK);
	case OCTEON_3:
		rst_boot = octeon_xkphys_read_8(RST_BOOT);
		return OCTEON_IO_REF_CLOCK * ((rst_boot >>
		    RST_BOOT_PNR_MUL_SHIFT) & RST_BOOT_PNR_MUL_MASK);
	default:
		return octeon_boot_info->eclock;
	}
}

void
octeon_tlb_init(void)
{
	uint64_t clk_reg, cvmmemctl, frac, cmul, imul, val;
	uint32_t hwrena = 0;
	uint32_t pgrain = 0;
	int chipid;

	chipid = octeon_get_chipid();
	switch (octeon_model_family(chipid)) {
	case OCTEON_MODEL_FAMILY_CN73XX:
		/* Enable LMTDMA/LMTST transactions. */
		cvmmemctl = octeon_get_cvmmemctl();
		cvmmemctl |= COP_0_CVMMEMCTL_LMTENA;
		cvmmemctl &= ~COP_0_CVMMEMCTL_LMTLINE_M;
		cvmmemctl |= 2ull << COP_0_CVMMEMCTL_LMTLINE_S;
		octeon_set_cvmmemctl(cvmmemctl);
		break;
	}

	/*
	 * Make sure Coprocessor 2 is disabled.
	 */
	setsr(getsr() & ~SR_COP_2_BIT);

	/*
	 * Synchronize this core's cycle counter with the system-wide
	 * IO clock counter.
	 *
	 * The IO clock counter's value has to be scaled from the IO clock
	 * frequency domain to the core clock frequency domain:
	 *
	 * cclk / cmul = iclk / imul
	 * cclk = iclk * cmul / imul
	 *
	 * Division is very slow and possibly variable-time on the system,
	 * so the synchronization routine uses multiplication:
	 *
	 * cclk = iclk * cmul * frac / 2^64,
	 *
	 * where frac = 2^64 / imul is precomputed.
	 */
	switch (octeon_model_family(chipid)) {
	case OCTEON_MODEL_FAMILY_CN73XX:
	case OCTEON_MODEL_FAMILY_CN78XX:
		clk_reg = FPA3_CLK_COUNT;
		break;
	default:
		clk_reg = IPD_CLK_COUNT;
		break;
	}
	switch (octeon_ver) {
	case OCTEON_2:
		val = octeon_xkphys_read_8(MIO_RST_BOOT);
		cmul = (val >> MIO_RST_BOOT_C_MUL_SHIFT) &
		    MIO_RST_BOOT_C_MUL_MASK;
		imul = (val >> MIO_RST_BOOT_PNR_MUL_SHIFT) &
		    MIO_RST_BOOT_PNR_MUL_MASK;
		break;
	case OCTEON_3:
		val = octeon_xkphys_read_8(RST_BOOT);
		cmul = (val >> RST_BOOT_C_MUL_SHIFT) &
		    RST_BOOT_C_MUL_MASK;
		imul = (val >> RST_BOOT_PNR_MUL_SHIFT) &
		    RST_BOOT_PNR_MUL_MASK;
		break;
	default:
		cmul = 1;
		imul = 1;
		break;
	}
	frac = ((1ULL << 63) / imul) * 2;
	octeon_sync_tc(PHYS_TO_XKPHYS(clk_reg, CCA_NC), cmul, frac);

	/* Let userspace access the cycle counter. */
	hwrena |= HWRENA_CC;

	/*
	 * If the UserLocal register is available, let userspace
	 * access it using the RDHWR instruction.
	 */
	if (cp0_get_config_3() & CONFIG3_ULRI) {
		cp0_set_userlocal(NULL);
		hwrena |= HWRENA_ULR;
		cpu_has_userlocal = 1;
	}
	cp0_set_hwrena(hwrena);

#ifdef MIPS_PTE64
	pgrain |= PGRAIN_ELPA;
#endif
	if (cp0_get_config_3() & CONFIG3_RXI)
		pgrain |= (PGRAIN_RIE | PGRAIN_XIE);
	cp0_set_pagegrain(pgrain);

	tlb_init(bootcpu_hwinfo.tlbsize);
}

static u_int64_t
get_ncpusfound(void)
{
	uint64_t core_mask;
	uint64_t i, ncpus = 0;
	int chipid;

	chipid = octeon_get_chipid();
	switch (octeon_model_family(chipid)) {
	case OCTEON_MODEL_FAMILY_CN73XX:
	case OCTEON_MODEL_FAMILY_CN78XX:
		core_mask = octeon_xkphys_read_8(OCTEON_CIU3_BASE + CIU3_FUSE);
		break;
	default:
		core_mask = octeon_xkphys_read_8(OCTEON_CIU_BASE + CIU_FUSE);
		break;
	}

	/* There has to be 1-to-1 mapping between cpuids and coreids. */
	for (i = 0; i < OCTEON_MAXCPUS && (core_mask & (1ul << i)) != 0; i++)
		ncpus++;

	return ncpus;
}

static enum octeon_board
get_octeon_board(void)
{
	switch (octeon_boot_info->board_type) {
	case 11:
		return BOARD_CN3010_EVB_HS5;
	case 20002:
		return BOARD_UBIQUITI_E100;
	case 20003:
		return BOARD_UBIQUITI_E200;
	case 20004:
		/* E120 has two cores, whereas UTM25 has one core. */
		if (ncpusfound == 1)
			return BOARD_NETGEAR_UTM25;
		return BOARD_UBIQUITI_E120;
	case 20005:
		return BOARD_UBIQUITI_E220;
	case 20010:
		return BOARD_UBIQUITI_E1000;
	case 20011:
		return BOARD_CHECKPOINT_N100;
	case 20012:
		return BOARD_RHINOLABS_UTM8;
	case 20015:
		return BOARD_DLINK_DSR_500;
	case 20300:
		return BOARD_UBIQUITI_E300;
	default:
		break;
	}

	return BOARD_UNKNOWN;
}

static void
process_bootargs(void)
{
	const char *cp;
	int i;

	/*
	 * U-Boot doesn't pass us anything by default, we need to explicitly
	 * pass the rootdevice.
	 */
	for (i = 0; i < octeon_boot_desc->argc; i++ ) {
		const char *arg = (const char*)
		    PHYS_TO_XKPHYS(octeon_boot_desc->argv[i], CCA_CACHED);

		if (octeon_boot_desc->argv[i] == 0)
			continue;

#ifdef DEBUG
		printf("boot_desc->argv[%d] = %s\n", i, arg);
#endif

		if (strncmp(arg, "boothowto=", 10) == 0) {
			boothowto = atoi(arg + 10);
			continue;
		}

		if (strncmp(arg, "rootdev=", 8) == 0) {
			parse_uboot_root(arg + 8);
			continue;
		}

		if (*arg != '-')
			continue;

		for (cp = arg + 1; *cp != '\0'; cp++) {
			switch (*cp) {
			case '-':
				break;
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
				printf("unrecognized option `%c'", *cp);
				break;
			}
		}
	}
}

/*
 * Machine dependent system variables.
 */
int
cpu_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen, struct proc *p)
{
	/* All sysctl names at this level are terminal. */
	if (namelen != 1)
		return ENOTDIR;		/* Overloaded */

	switch (name[0]) {
	default:
		return EOPNOTSUPP;
	}
}

int	waittime = -1;

__dead void
boot(int howto)
{
	if ((howto & RB_RESET) != 0)
		goto doreset;

	if (curproc)
		savectx(curproc->p_addr, 0);

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
		if ((howto & RB_POWERDOWN) != 0)
			printf("System Power Down not supported,"
			" halting system.\n");
		else
			printf("System Halt.\n");
	} else {
doreset:
		printf("System restart.\n");
		(void)disableintr();
		tlb_set_wired(0);
		tlb_flush(bootcpu_hwinfo.tlbsize);

		if (octeon_ver == OCTEON_3)
			octeon_xkphys_write_8(RST_SOFT_RST, 1);
		else
			octeon_xkphys_write_8(OCTEON_CIU_BASE +
			    CIU_SOFT_RST, 1);
	}

	for (;;)
		continue;
	/* NOTREACHED */
}

u_long	dumpmag = 0x8fca0101;	/* Magic number for savecore. */
int	dumpsize = 0;			/* Also for savecore. */
long	dumplo = 0;

void
dumpconf(void)
{
	int nblks;

	if (dumpdev == NODEV ||
	    (nblks = (bdevsw[major(dumpdev)].d_psize)(dumpdev)) == 0)
		return;
	if (nblks <= ctod(1))
		return;

	dumpsize = ptoa(physmem);
	if (dumpsize > atop(round_page(dbtob(nblks - dumplo))))
		dumpsize = atop(round_page(dbtob(nblks - dumplo)));
	else if (dumplo == 0)
		dumplo = nblks - btodb(ptoa(physmem));

	/*
	 * Don't dump on the first page in case the dump device includes a 
	 * disk label.
	 */
	if (dumplo < btodb(PAGE_SIZE))
		dumplo = btodb(PAGE_SIZE);
}

void
dumpsys()
{
	/* XXX TBD */
}

int
is_memory_range(paddr_t pa, psize_t len, psize_t limit)
{
	extern char start[];
	struct phys_mem_desc *seg;
	uint64_t fp, lp;
	int i;

	fp = atop(pa);
	lp = atop(round_page(pa + len));

	if (limit != 0 && lp > atop(limit))
		return 0;

	/* The kernel is linked in CKSEG0. */
	if (fp >= atop(trunc_page(CKSEG0_TO_PHYS((vaddr_t)start))) &&
	    lp <= atop(round_page(CKSEG0_TO_PHYS((vaddr_t)ekern))))
		return 1;

	for (i = 0, seg = mem_layout; i < MAXMEMSEGS; i++, seg++)
		if (fp >= seg->mem_first_page && lp <= seg->mem_last_page)
			return 1;

	return 0;
}

u_int
ioclock_get_timecount(struct timecounter *tc)
{
	uint64_t reg = (uint64_t)tc->tc_priv;

	return octeon_xkphys_read_8(reg);
}

#if NOCTBOOT > 0
static uint64_t
size_trunc(uint64_t size)
{
	return (size & ~BOOTMEM_BLOCK_MASK);
}

void
bootmem_dump(void)
{
	struct octeon_bootmem_desc *memdesc = (struct octeon_bootmem_desc *)
	    PHYS_TO_XKPHYS(octeon_boot_info->phys_mem_desc_addr, CCA_CACHED);
	struct octeon_bootmem_block *block;
	paddr_t pa;

	pa = memdesc->head_addr;
	while (pa != 0) {
		block = pa_to_block(pa);
		printf("free 0x%lx - 0x%lx\n", pa, pa + (size_t)block->size);
		pa = block->next;
	}
}

/*
 * Allocate the given region from the free memory list.
 */
int
bootmem_alloc_region(paddr_t pa, size_t size)
{
	struct octeon_bootmem_desc *memdesc = (struct octeon_bootmem_desc *)
	    PHYS_TO_XKPHYS(octeon_boot_info->phys_mem_desc_addr, CCA_CACHED);
	struct octeon_bootmem_block *block, *next, nblock;
	paddr_t bpa;

	if (pa == 0 || size < BOOTMEM_BLOCK_MIN_SIZE ||
	    (pa & BOOTMEM_BLOCK_MASK) != 0 ||
	    (size & BOOTMEM_BLOCK_MASK) != 0)
		return EINVAL;

	if (memdesc->head_addr == 0 || pa < memdesc->head_addr)
		return ENOMEM;

	/* Check if the region is at the head of the free list. */
	if (pa == memdesc->head_addr) {
		block = pa_to_block(memdesc->head_addr);
		if (block->size < size)
			return ENOMEM;
		if (size_trunc(block->size) == size) {
			memdesc->head_addr = block->next;
		} else {
			KASSERT(block->size > size);
			nblock.next = block->next;
			nblock.size = block->size - size;
			KASSERT(nblock.size >= BOOTMEM_BLOCK_MIN_SIZE);
			memdesc->head_addr += size;
			*pa_to_block(memdesc->head_addr) = nblock;
		}
		return 0;
	}

	/* Find the block that immediately precedes or is at `pa'. */
	bpa = memdesc->head_addr;
	block = pa_to_block(bpa);
	while (block->next != 0 && block->next < pa) {
		bpa = block->next;
		block = pa_to_block(bpa);
	}

	/* Refuse to play if the block is not properly aligned. */
	if ((bpa & BOOTMEM_BLOCK_MASK) != 0)
		return ENOMEM;

	if (block->next == pa) {
		next = pa_to_block(block->next);
		if (next->size < size)
			return ENOMEM;
		if (size_trunc(next->size) == size) {
			block->next = next->next;
		} else {
			KASSERT(next->size > size);
			nblock.next = next->next;
			nblock.size = next->size - size;
			KASSERT(nblock.size >= BOOTMEM_BLOCK_MIN_SIZE);
			block->next += size;
			*pa_to_block(block->next) = nblock;
		}
	} else {
		KASSERT(bpa < pa);
		KASSERT(block->next == 0 || block->next > pa);

		if (bpa + block->size < pa + size)
			return ENOMEM;
		if (bpa + size_trunc(block->size) == pa + size) {
			block->size = pa - bpa;
		} else {
			KASSERT(bpa + block->size > pa + size);
			nblock.next = block->next;
			nblock.size = block->size - (pa - bpa) - size;
			KASSERT(nblock.size >= BOOTMEM_BLOCK_MIN_SIZE);
			block->next = pa + size;
			block->size = pa - bpa;
			*pa_to_block(block->next) = nblock;
		}
	}

	return 0;
}

/*
 * Release the given region to the free memory list.
 */
void
bootmem_free(paddr_t pa, size_t size)
{
	struct octeon_bootmem_desc *memdesc = (struct octeon_bootmem_desc *)
	    PHYS_TO_XKPHYS(octeon_boot_info->phys_mem_desc_addr, CCA_CACHED);
	struct octeon_bootmem_block *block, *next, *prev;
	paddr_t prevpa;

	if (pa == 0 || size < BOOTMEM_BLOCK_MIN_SIZE ||
	    (pa & BOOTMEM_BLOCK_MASK) != 0 ||
	    (size & BOOTMEM_BLOCK_MASK) != 0)
		panic("%s: invalid block 0x%lx @ 0x%lx", __func__, size, pa);

	/* If the list is empty, insert at the head. */
	if (memdesc->head_addr == 0) {
		block = pa_to_block(pa);
		block->next = 0;
		block->size = size;
		memdesc->head_addr = pa;
		return;
	}

	/* If the block precedes the current head, insert before, or merge. */
	if (pa <= memdesc->head_addr) {
		block = pa_to_block(pa);
		if (pa + size < memdesc->head_addr) {
			block->next = memdesc->head_addr;
			block->size = size;
			memdesc->head_addr = pa;
		} else if (pa + size == memdesc->head_addr) {
			next = pa_to_block(memdesc->head_addr);
			block->next = next->next;
			block->size = next->size + size;
			memdesc->head_addr = pa;
		} else {
			panic("%s: overlap 1: 0x%lx @ 0x%lx / 0x%llx @ 0x%llx",
			    __func__, size, pa,
			    pa_to_block(memdesc->head_addr)->size,
			    memdesc->head_addr);
		}
		return;
	}

	/* Find the immediate predecessor. */
	prevpa = memdesc->head_addr;
	prev = pa_to_block(prevpa);
	while (prev->next != 0 && prev->next < pa) {
		prevpa = prev->next;
		prev = pa_to_block(prevpa);
	}
	if (prevpa + prev->size > pa) {
		panic("%s: overlap 2: 0x%llx @ 0x%lx / 0x%lx @ 0x%lx",
		    __func__, prev->size, prevpa, size, pa);
	}

	/* Merge with or insert after the predecessor. */
	if (prevpa + prev->size == pa) {
		if (prev->next == 0) {
			prev->size += size;
			return;
		}
		next = pa_to_block(prev->next);
		if (prevpa + prev->size + size < prev->next) {
			prev->size += size;
		} else if (prevpa + prev->size + size == prev->next) {
			prev->next = next->next;
			prev->size += size + next->size;
		} else {
			panic("%s: overlap 3: 0x%llx @ 0x%lx / 0x%lx @ 0x%lx / "
			    "0x%llx @ 0x%llx", __func__,
			    prev->size, prevpa, size, pa,
			    next->size, prev->next);
		}
	} else {
		/* The block is disjoint with prev. */
		KASSERT(prevpa + prev->size < pa);

		block = pa_to_block(pa);
		if (pa + size < prev->next || prev->next == 0) {
			block->next = prev->next;
			block->size = size;
			prev->next = pa;
		} else if (pa + size == prev->next) {
			next = pa_to_block(prev->next);
			block->next = next->next;
			block->size = next->size + size;
			prev->next = pa;
		} else {
			next = pa_to_block(prev->next);
			panic("%s: overlap 4: 0x%llx @ 0x%lx / "
			    "0x%lx @ 0x%lx / 0x%llx @ 0x%llx",
			    __func__, prev->size, prevpa, size, pa,
			    next->size, prev->next);
		}
	}
}
#endif /* NOCTBOOT > 0 */

#ifdef MULTIPROCESSOR
uint32_t cpu_spinup_mask = 0;
uint64_t cpu_spinup_a0, cpu_spinup_sp;

void
hw_cpu_boot_secondary(struct cpu_info *ci)
{
	vaddr_t kstack;

	kstack = alloc_contiguous_pages(USPACE);
	if (kstack == 0)
		panic("unable to allocate idle stack");
	ci->ci_curprocpaddr = (void *)kstack;

	cpu_spinup_a0 = (uint64_t)ci;
	cpu_spinup_sp = (uint64_t)(kstack + USPACE);
	mips_sync();

	cpu_spinup_mask = (uint32_t)ci->ci_cpuid;

	while (!CPU_IS_RUNNING(ci))
		membar_sync();
}

void
hw_cpu_hatch(struct cpu_info *ci)
{
	/*
	 * Set curcpu address on this processor.
	 */
	setcurcpu(ci);

	/*
	 * Make sure we can access the extended address space.
	 */
	setsr(getsr() | SR_KX | SR_UX);

	octeon_tlb_init();
	tlb_set_pid(0);

	/*
	 * Turn off bootstrap exception vectors.
	 */
	setsr(getsr() & ~SR_BOOT_EXC_VEC);

	/*
	 * Clear out the I and D caches.
	 */
	Octeon_ConfigCache(ci);
	Mips_SyncCache(ci);

	(*md_startclock)(ci);

	octeon_intr_init();
	mips64_ipi_init();

	ci->ci_flags |= CPUF_RUNNING;
	membar_sync();

	ncpus++;

	spl0();
	(void)updateimask(0);

	sched_toidle();
}
#endif /* MULTIPROCESSOR */
