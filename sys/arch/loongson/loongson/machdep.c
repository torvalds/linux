/*	$OpenBSD: machdep.c,v 1.101 2023/10/24 13:20:10 claudio Exp $ */

/*
 * Copyright (c) 2009, 2010, 2014 Miodrag Vallat.
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
#include <machine/pmon.h>

#ifdef HIBERNATE
#include <machine/hibernate_var.h>
#endif /* HIBERNATE */

#include <dev/cons.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

/* The following is used externally (sysctl_hw) */
char	machine[] = MACHINE;		/* Machine "architecture" */
char	cpu_model[30];
char	pmon_bootp[80];

/*
 * Even though the system is 64bit, 2E- and 2F-based hardware is constrained
 * to up to 2G of contiguous physical memory (direct 2GB DMA area). 2Gq- and
 * 3A-based hardware only supports 32-bit DMA addresses, even though
 * physical memory may exist beyond 4GB.
 */
struct uvm_constraint_range  dma_constraint = { 0x0, 0xffffffffUL };
struct uvm_constraint_range *uvm_md_constraints[] = { NULL };

vm_map_t exec_map;
vm_map_t phys_map;

/*
 * safepri is a safe priority for sleep to set for a spin-wait
 * during autoconfiguration or after a panic.
 */
int   safepri = 0;

caddr_t	msgbufbase;

int	physmem;		/* Max supported memory, changes to actual. */
int	ncpu = 1;		/* At least one CPU in the system. */
int	nnodes = 1;		/* Number of NUMA nodes, only on 3A. */
struct	user *proc0paddr;
int	lid_action = 1;
int	pwr_action = 1;

#ifdef MULTIPROCESSOR
uint64_t cpu_spinup_a0;
uint64_t cpu_spinup_sp;
uint32_t ipi_mask;
#endif

const struct platform *sys_platform;
struct cpu_hwinfo bootcpu_hwinfo;
void *loongson_videobios;
uint loongson_cpumask = 1;
uint loongson_ver;

/* Pointers to the start and end of the symbol table. */
caddr_t	ssym;
caddr_t	esym;
caddr_t	ekern;

struct phys_mem_desc mem_layout[MAXMEMSEGS];
paddr_t	loongson_memlo_alias;

pcitag_t (*pci_make_tag_early)(int, int, int);
pcireg_t (*pci_conf_read_early)(pcitag_t, int);
bus_space_tag_t early_mem_t;
bus_space_tag_t early_io_t;

static u_long atoi(const char *, uint);
static void dobootopts(int);

void	dumpsys(void);
void	dumpconf(void);
extern	void parsepmonbp(void);
const struct platform *loongson_identify(const char *, int);
vaddr_t	mips_init(uint64_t, uint64_t, uint64_t, uint64_t, char *);

extern	void htb_early_setup(void);

extern	void loongson2e_setup(u_long, u_long);
extern	void loongson2f_setup(u_long, u_long);
extern	void loongson3a_setup(u_long, u_long);

cons_decl(pmon);

struct consdev pmoncons = {
	NULL,
	NULL,
	pmoncngetc,
	pmoncnputc,
	nullcnpollc,
	NULL,
	makedev(0, 0),
	CN_DEAD
};

/*
 * List of supported system types, from the ``Version'' environment
 * variable.
 */

struct bonito_flavour {
	const char *prefix;
	const struct platform *platform;
};

extern const struct platform ebenton_platform;
extern const struct platform fuloong_platform;
extern const struct platform gdium_platform;
extern const struct platform generic2e_platform;
extern const struct platform lynloong_platform;
extern const struct platform rs780e_platform;
extern const struct platform yeeloong_platform;

const struct bonito_flavour bonito_flavours[] = {
#ifdef CPU_LOONGSON2
	/* eBenton EBT700 netbook */
	{ "EBT700",	&ebenton_platform },	/* prefix added by user */
	/* Lemote Fuloong 2F mini-PC */
	{ "LM6002",	&fuloong_platform },	/* dual Ethernet,
						   prefix added by user */
	{ "LM6003",	&fuloong_platform },
	{ "LM6004",	&fuloong_platform },
	/* EMTEC Gdium Liberty 1000 */
	{ "Gdium",	&gdium_platform },
	/* Lemote Yeeloong 8.9" netbook */
	{ "LM8089",	&yeeloong_platform },
	/* supposedly Lemote Yeeloong 10.1" netbook, but those found so far
	   report themselves as LM8089 */
	{ "LM8101",	&yeeloong_platform },
	/* Lemote Lynloong all-in-one computer */
	{ "LM9001",	&lynloong_platform },
	{ "LM9002",	&lynloong_platform },
	{ "LM9003",	&lynloong_platform },
	/* Lemote Lynloong all-in-one computer, Xueloong edition */
	{ "LM9013",	&lynloong_platform },
#endif
#ifdef CPU_LOONGSON3
	/* Laptops */
	{ "A1004",	&rs780e_platform },	/* 3A */
	{ "A1201",	&rs780e_platform },	/* 2Gq */
	/* Lemote Xinghuo 6100 (mini-ITX PC) */
	{ "A1101",	&rs780e_platform },	/* 3A */
	/* All-in-one PC */
	{ "A1205",	&rs780e_platform },	/* 2Gq */
#endif
	{ NULL }
};

/*
 * Try to figure out what particular machine we run on, depending on the
 * scarce PMON version information and whatever else we can figure.
 */
const struct platform *
loongson_identify(const char *version, int envtype)
{
	const struct bonito_flavour *f;

	switch (envtype) {
#ifdef CPU_LOONGSON3
	case PMON_ENVTYPE_EFI:
		if (loongson_ver == 0x3a || loongson_ver == 0x3b) {
			pcitag_t tag;
			pcireg_t id;

			htb_early_setup();

			/* Determine platform by host bridge. */
			tag = pci_make_tag_early(0, 0, 0);
			id = pci_conf_read_early(tag, PCI_ID_REG);
			switch (id) {
			case PCI_ID_CODE(PCI_VENDOR_AMD,
			    PCI_PRODUCT_AMD_RS780_HB):
				return &rs780e_platform;
			}
		}
		pmon_printf("Unable to figure out model!\n");
		return NULL;
#endif

	default:
	case PMON_ENVTYPE_ENVP:
		if (version == NULL) {
#ifdef CPU_LOONGSON2
			/*
		 	 * If there is no `Version' variable, we expect to be
			 * running on a 2E system, use the generic code and
			 * hope for the best.
		 	 */
			if (loongson_ver == 0x2e)
				return &generic2e_platform;
#endif
			pmon_printf("Unable to figure out model!\n");
			return NULL;
		}

		for (f = bonito_flavours; f->prefix != NULL; f++)
			if (strncmp(version, f->prefix, strlen(f->prefix)) == 0)
				return f->platform;

#ifdef CPU_LOONGSON2
		/*
	 	 * Early Lemote designs shipped without a model prefix.
	 	 *
	 	 * We can reasonably expect these to be close enough to either
		 * the first generation Fuloong 2F design (LM6002), or the 7
		 * inch first netbook model; we can tell them apart by looking
		 * at which video chip they embed.
	 	 *
	 	 * Note that this is only worth doing if the version string is
	 	 * 1.2.something (1.3 onwards are expected to have a model
		 * prefix, and there are currently no reports of 1.1 and
	 	 * below being 2F systems).
	 	 *
	 	 * LM6002 users are encouraged to add the system model prefix to
	 	 * the `Version' variable.
	 	 */
		if (strncmp(version, "1.2.", 4) == 0) {
			const struct platform *p = NULL;
			pcitag_t tag;
			pcireg_t id, class;
			int dev;

			pmon_printf("No model prefix "
			    "in version string \"%s\".\n", version);

			if (loongson_ver == 0x2f)
				for (dev = 0; dev < 32; dev++) {
					tag = pci_make_tag_early(0, dev, 0);
					id = pci_conf_read_early(tag,
					    PCI_ID_REG);
					if (id == 0 || PCI_VENDOR(id) ==
					    PCI_VENDOR_INVALID)
						continue;

					/*
					 * No need to check for
					 * DEVICE_IS_VGA_PCI here, since we
					 * expect a linear framebuffer.
					 */
					class = pci_conf_read_early(tag,
					    PCI_CLASS_REG);
					if (PCI_CLASS(class) !=
					    PCI_CLASS_DISPLAY ||
					    (PCI_SUBCLASS(class) !=
					     PCI_SUBCLASS_DISPLAY_VGA &&
					     PCI_SUBCLASS(class) !=
					     PCI_SUBCLASS_DISPLAY_MISC))
						continue;

					switch (id) {
					case PCI_ID_CODE(PCI_VENDOR_SIS,
				    	    PCI_PRODUCT_SIS_315PRO_VGA):
						p = &fuloong_platform;
						break;
					case PCI_ID_CODE(PCI_VENDOR_SMI,
			    		    PCI_PRODUCT_SMI_SM712):
						p = &ebenton_platform;
						break;
					}
				}

			if (p != NULL) {
				pmon_printf("Attempting to match as "
				    "%s %s\n", p->vendor, p->product);
				return p;
			}
		}
#endif
	}

	pmon_printf("This kernel doesn't support model \"%s\"." "\n", version);
	return NULL;
}

/*
 * Figure out machine parameters using the 'EFI-like' interface.
 */
int
loongson_efi_setup(void)
{
	struct pmon_env_mem_entry entry;
	const struct pmon_env_cpu *cpuenv;
	const struct pmon_env_mem *mem;
	paddr_t fp, lp;
	uint32_t i, ncpus, seg = 0;

	cpuenv = pmon_get_env_cpu();
	bootcpu_hwinfo.clock = cpuenv->speed;

	/*
	 * Get available CPUs.
	 */

	ncpus = cpuenv->ncpus;
	if (ncpus > LOONGSON_MAXCPUS)
		ncpus = LOONGSON_MAXCPUS;

	loongson_cpumask = (1u << ncpus) - 1;
	loongson_cpumask &= ~(uint)cpuenv->reserved_cores;

	ncpusfound = 0;
	for (i = 0; i < ncpus; i++) {
		if (ISSET(loongson_cpumask, 1u << i))
			ncpusfound++;
	}

	/*
	 * Get free memory segments.
	 */

	mem = pmon_get_env_mem();
	physmem = 0;
	for (i = 0; i < mem->nentries && seg < MAXMEMSEGS; i++) {
		memcpy(&entry, &mem->mem_map[i], sizeof(entry));
		if (entry.node != 0 ||
		    (entry.type != PMON_MEM_SYSTEM_LOW &&
		     entry.type != PMON_MEM_SYSTEM_HIGH))
			continue;
		fp = atop(entry.address);
		lp = atop(entry.address + ((uint64_t)entry.size << 20));
		if (lp > atop(pfn_to_pad(PG_FRAME)) + 1)
			lp = atop(pfn_to_pad(PG_FRAME)) + 1;
		if (fp >= lp)
			continue;
		physmem += lp - fp;
		mem_layout[seg].mem_first_page = fp;
		mem_layout[seg].mem_last_page = lp;
		seg++;
	}

	return 0;
}

/*
 * Figure out machine parameters using the PMON interface.
 */
int
loongson_envp_setup(void)
{
	const char *envvar;
	u_long cpuspeed, memlo, memhi;

	/*
	 * Figure out processor clock speed.
	 * Hopefully the processor speed, in Hertz, will not overflow
	 * uint32_t...
	 */

	cpuspeed = 0;
	envvar = pmon_getenv("cpuclock");
	if (envvar != NULL)
		cpuspeed = atoi(envvar, 10);	/* speed in Hz */
	if (cpuspeed < 100 * 1000000)
		cpuspeed = 797000000;  /* Reasonable default */
	bootcpu_hwinfo.clock = cpuspeed;

	/*
	 * Guess the available CPUs.
	 */

	switch (loongson_ver) {
#ifdef CPU_LOONGSON3
	case 0x3a:
		loongson_cpumask = 0x0f;
		ncpusfound = 4;
		break;
	case 0x3b:
		loongson_cpumask = 0xff;
		ncpusfound = 8;
		break;
#endif
	}

	/*
	 * Figure out memory information.
	 * PMON reports it in two chunks, the memory under the 256MB
	 * CKSEG limit, and memory above that limit.  We need to do the
	 * math ourselves.
	 */

	envvar = pmon_getenv("memsize");
	if (envvar == NULL) {
		pmon_printf("Could not get memory information"
		    " from the firmware\n");
		return -1;
	}
	memlo = atoi(envvar, 10);	/* size in MB */
	if (memlo < 0 || memlo > 256) {
		pmon_printf("Incorrect low memory size `%s'\n", envvar);
		return -1;
	}

	/* 3A PMON only reports up to 240MB as low memory */
	if (memlo >= 240) {
		envvar = pmon_getenv("highmemsize");
		if (envvar == NULL)
			memhi = 0;
		else
			memhi = atoi(envvar, 10);	/* size in MB */
		if (memhi < 0 || memhi > (64 * 1024) - 256) {
			pmon_printf("Incorrect high memory size `%s'\n",
			    envvar);
			/* better expose the problem than limit to 256MB */
			return -1;
		}
	} else
		memhi = 0;

	switch (loongson_ver) {
	default:
#ifdef CPU_LOONGSON2
	case 0x2e:
		loongson2e_setup(memlo, memhi);
		break;
	case 0x2f:
		loongson2f_setup(memlo, memhi);
		break;
#endif
#ifdef CPU_LOONGSON3
	case 0x3a:
		loongson3a_setup(memlo, memhi);
		break;
#endif
	}

	return 0;
}

/*
 * Do all the stuff that locore normally does before calling main().
 * Reset mapping and set up mapping to hardware and init "wired" reg.
 */

vaddr_t
mips_init(uint64_t argc, uint64_t argv, uint64_t envp, uint64_t cv,
    char *boot_esym)
{
	uint32_t prid;
	vaddr_t xtlb_handler;
	const char *envvar;
	int i;

	extern char start[], edata[], end[];
	extern char exception[], e_exception[];
	extern void xtlb_miss;

#ifdef MULTIPROCESSOR
	/*
	 * Set curcpu address on primary processor.
	 */
	setcurcpu(&cpu_info_primary);
#endif

	/*
	 * Make sure we can access the extended address space.
	 * This is not necessary on real hardware, but some emulators
	 * are not aware of this.
	 */
	setsr(getsr() | SR_KX | SR_UX);

	/*
	 * Clear the compiled BSS segment in OpenBSD code.
	 * PMON is supposed to have done this, though.
	 */

	bzero(edata, end - edata);

	/*
	 * Set up early console output.
	 */

	prid = cp0_get_prid();
	pmon_init((int32_t)argc, (int32_t)argv, (int32_t)envp, (int32_t)cv,
	    prid);
	cn_tab = &pmoncons;

	/*
	 * Reserve space for the symbol table, if it exists.
	 */

	/* Attempt to locate ELF header and symbol table after kernel. */
	if (end[0] == ELFMAG0 && end[1] == ELFMAG1 &&
	    end[2] == ELFMAG2 && end[3] == ELFMAG3) {
		/* ELF header exists directly after kernel. */
		ssym = end;
		esym = boot_esym;
		ekern = esym;
	} else {
		ssym = (char *)(vaddr_t)*(int32_t *)end;
		if (((long)ssym - (long)end) >= 0 &&
		    ((long)ssym - (long)end) <= 0x1000 &&
		    ssym[0] == ELFMAG0 && ssym[1] == ELFMAG1 &&
		    ssym[2] == ELFMAG2 && ssym[3] == ELFMAG3) {
			/* Pointers exist directly after kernel. */
			esym = (char *)(vaddr_t)*((int32_t *)end + 1);
			ekern = esym;
		} else {
			/* Pointers aren't setup either... */
			ssym = NULL;
			esym = NULL;
			ekern = end;
		}
	}

	/*
	 * While the kernel supports other processor types than Loongson,
	 * we are currently not expecting to run on a system with a
	 * different processor.  Just to be on the safe side, refuse to
	 * run on non-Loongson processors for now.
	 */

	switch ((prid >> 8) & 0xff) {
	case MIPS_LOONGSON2:
		switch (prid & 0xff) {
#ifdef CPU_LOONGSON2
#ifdef CPU_LOONGSON2C
		case 0x00:
			loongson_ver = 0x2c;
			break;
#endif
		case 0x02:
			loongson_ver = 0x2e;
			break;
		case 0x03:
			loongson_ver = 0x2f;
			break;
#endif
#ifdef CPU_LOONGSON3
		case 0x05:
		case 0x08:
			loongson_ver = 0x3a;
			break;
#endif
		default:
			break;
		}
	}
	if (loongson_ver == 0) {
		pmon_printf("This kernel doesn't support processor type 0x%x"
		    ", version %d.%d.\n",
		    (prid >> 8) & 0xff, (prid >> 4) & 0x0f, prid & 0x0f);
		goto unsupported;
	}

	/*
	 * Try and figure out what kind of hardware we are.
	 */

	switch (pmon_getenvtype()) {
	default:
		pmon_printf("Unable to figure out "
		    "firmware environment information!\n");
		goto unsupported;

	case PMON_ENVTYPE_EFI:
		break;

	case PMON_ENVTYPE_ENVP:
		envvar = pmon_getenv("systype");
		if (envvar == NULL) {
			pmon_printf("Unable to figure out system type!\n");
			goto unsupported;
		}
		if (strcmp(envvar, "Bonito") != 0) {
			pmon_printf("This kernel doesn't support system type \"%s\".\n",
		    	envvar);
			goto unsupported;
		}
	}

	/*
	 * Try to figure out what particular machine we run on, depending
	 * on the PMON version information.
	 */

	if ((sys_platform = loongson_identify(pmon_getenv("Version"),
	    pmon_getenvtype())) == NULL)
		goto unsupported;

	hw_vendor = sys_platform->vendor;
	hw_prod = sys_platform->product;
	pmon_printf("Found %s %s, setting up.\n", hw_vendor, hw_prod);

	snprintf(cpu_model, sizeof cpu_model, "Loongson %X", loongson_ver);

	/*
	 * Look at arguments passed to us and compute boothowto.
	 */

	boothowto = RB_AUTOBOOT;
	dobootopts(argc);

	switch (pmon_getenvtype()) {
	case PMON_ENVTYPE_EFI:
		if (loongson_efi_setup() != 0)
			goto unsupported;
		break;

	case PMON_ENVTYPE_ENVP:
		if (loongson_envp_setup() != 0)
			goto unsupported;
		break;
	}

	if (sys_platform->setup != NULL)
		(*(sys_platform->setup))();

	/*
	 * PMON functions should no longer be used from now on.
	 */

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
#ifdef HIBERNATE
		firstkernpage -= HIBERNATE_RESERVED_PAGES;
#endif
		lastkernpage = atop(round_page(lastkernpa));

		if (loongson_memlo_alias != 0) {
			firstkernpage += atop(loongson_memlo_alias);
			lastkernpage += atop(loongson_memlo_alias);
		}

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
	/* FPU reports itself as type 5, version 0.1... */
	bootcpu_hwinfo.c1prid = bootcpu_hwinfo.c0prid;

	/*
	 * Configure cache and tlb.
	 */

	switch (loongson_ver) {
	default:
#ifdef CPU_LOONGSON2
#ifdef CPU_LOONGSON2C
	case 0x2c:
#endif
	case 0x2e:
	case 0x2f:
		bootcpu_hwinfo.tlbsize = 64;
		Loongson2_ConfigCache(curcpu());
		Loongson2_SyncCache(curcpu());
		break;
#endif
#ifdef CPU_LOONGSON3
	case 0x3a:
		bootcpu_hwinfo.tlbsize =
		    1 + ((cp0_get_config_1() & CONFIG1_MMUSize1) >>
		    CONFIG1_MMUSize1_SHIFT);
		Loongson3_ConfigCache(curcpu());
		Loongson3_SyncCache(curcpu());
		break;
#endif
	}

	tlb_init(bootcpu_hwinfo.tlbsize);

	/*
	 * Get a console, very early but after initial mapping setup.
	 */

	consinit();
	printf("Initial setup done, switching console.\n");

	/*
	 * Init message buffer. This is similar to pmap_steal_memory(), but
	 * without zeroing the area, to keep the message buffer from the
	 * previous kernel run intact, if any.
	 */
	for (i = 0; i < vm_nphysseg; i++) {
		struct vm_physseg *vps = &vm_physmem[i];
		uint npg = atop(round_page(MSGBUFSIZE));
		int j;

		if (vps->avail_start != vps->start ||
		    vps->avail_start >= vps->avail_end) {
			continue;
		}

		if ((vps->avail_end - vps->avail_start) < npg)
			continue;

		msgbufbase = (caddr_t)PHYS_TO_XKPHYS(ptoa(vps->avail_start),
		    CCA_CACHED);
		vps->avail_start += npg;
		vps->start += npg;

		if (vps->avail_start == vps->end) {
			/* don't bother panicing if nphysseg becomes zero, */
			/* the next pmap_steal_memory() call will. */
			vm_nphysseg--;
			for (j = i; j < vm_nphysseg; j++)
				vm_physmem[j] = vm_physmem[j + 1];
		}

		break;
	}
	if (msgbufbase == NULL)
		panic("not enough contiguous memory for message buffer");
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
	 *
	 * On Loongson 2F, the XTLB refill exception actually uses
	 * the TLB refill vector.
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

	/*
	 * Return the new kernel stack pointer.
	 */

	return ((vaddr_t)proc0paddr + USPACE - 64);

unsupported:
	pmon_printf("Halting system.\nPress enter to return to PMON\n");
	cngetc();
	return 0;	/* causes us to return to pmon */
}

/*
 * Decode boot options.
 */
static void
dobootopts(int argc)
{
	const char *arg;
	const char *cp;
	int ignore = 1;
	int i;

	/*
	 * Parse the boot command line.
	 *
	 * It should be of the form `boot [flags] filename [args]', so we
	 * need to ignore flags to the boot command.
	 * To achieve this, we ignore argc[0], which is the `boot' command
	 * itself, and ignore arguments starting with dashes until the
	 * boot file has been found.
	 */

	if (argc != 0) {
		arg = pmon_getarg(0);
		if (arg == NULL)
			return;
		/* if `go', not `boot', then no path and command options */
		if (*arg == 'g')
			ignore = 0;
	}
	for (i = 1; i < argc; i++) {
		arg = pmon_getarg(i);
		if (arg == NULL)
			continue;

		/* device path */
		if (*arg == '/' || strncmp(arg, "bootduid=", 9) == 0 ||
		    strncmp(arg, "tftp://", 7) == 0) {
			if (*pmon_bootp == '\0') {
				strlcpy(pmon_bootp, arg, sizeof pmon_bootp);
				parsepmonbp();
			}
			ignore = 0;	/* further options are for the kernel */
			continue;
		}

		/* not an option, or not a kernel option */
		if (*arg != '-' || ignore)
			continue;

		for (cp = arg + 1; *cp != '\0'; cp++)
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
			case 'g':
				boothowto |= RB_GOODRANDOM;
				break;
			default:
				pmon_printf("unrecognized option `%c'", *cp);
				break;
			}
	}

	/*
	 * Consider parsing the `karg' environment variable here too?
	 */
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
		cn_tab = NULL;
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

const struct sysctl_bounded_args cpuctl_vars[] = {
	{ CPU_LIDACTION, &lid_action, 0, 2 },
};

/*
 * Machine dependent system variables.
 */
int
cpu_sysctl(int *name, u_int namelen, void *oldp, size_t *oldlenp, void *newp,
    size_t newlen, struct proc *p)
{
	return (sysctl_bounded_arr(cpuctl_vars, nitems(cpuctl_vars),
	    name, namelen, oldp, oldlenp, newp, newlen));
}

int	waittime = -1;

__dead void
boot(int howto)
{
	void (*__reset)(void) = (void (*)(void))RESET_EXC_VEC;

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
	pci_dopm = 0;
	config_suspend_all(DVACT_POWERDOWN);

	if ((howto & RB_HALT) != 0) {
		if ((howto & RB_POWERDOWN) != 0) {
			if (sys_platform->powerdown != NULL) {
				printf("System Power Down.\n");
				(*(sys_platform->powerdown))();
			} else {
				printf("System Power Down not supported,"
				    " halting system.\n");
			}
		} else
			printf("System Halt.\n");
	} else {
doreset:
		printf("System restart.\n");
		if (sys_platform->reset != NULL)
			(*(sys_platform->reset))();
		(void)disableintr();
		tlb_set_wired(0);
		tlb_flush(bootcpu_hwinfo.tlbsize);
		__reset();
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

/*
 * Convert an ASCII string into an integer.
 */
static u_long
atoi(const char *s, uint b)
{
	int c;
	uint base = b, d;
	int neg = 0;
	u_long val = 0;

	if (s == NULL || *s == '\0')
		return 0;

	/* Skip spaces if any. */
	do {
		c = *s++;
	} while (c == ' ' || c == '\t');

	/* Parse sign, allow more than one (compat). */
	while (c == '-') {
		neg = !neg;
		c = *s++;
	}

	/* Parse base specification, if any. */
	if (base == 0 && c == '0') {
		c = *s++;
		switch (c) {
		case 'X':
		case 'x':
			base = 16;
			c = *s++;
			break;
		case 'B':
		case 'b':
			base = 2;
			c = *s++;
			break;
		default:
			base = 8;
			break;
		}
	}

	/* Parse number proper. */
	for (;;) {
		if (c >= '0' && c <= '9')
			d = c - '0';
		else if (c >= 'a' && c <= 'z')
			d = c - 'a' + 10;
		else if (c >= 'A' && c <= 'Z')
			d = c - 'A' + 10;
		else
			break;
		if (d >= base)
			break;
		val *= base;
		val += d;
		c = *s++;
	}

	return neg ? -val : val;
}

/*
 * Early console through pmon routines.
 */

int
pmoncngetc(dev_t dev)
{
	/*
	 * PMON does not give us a getc routine.  So try to get a whole line
	 * and return it char by char, trying not to lose the \n.  Kind
	 * of ugly but should work.
	 *
	 * Note that one could theoretically use pmon_read(STDIN, &c, 1)
	 * but the value of STDIN within PMON is not a constant and there
	 * does not seem to be a way of letting us know which value to use.
	 */
	static char buf[1 + PMON_MAXLN];
	static char *bufpos = buf;
	int c;

	if (*bufpos == '\0') {
		bufpos = buf;
		if (pmon_gets(buf) == NULL) {
			/* either an empty line or EOF. assume the former */
			return (int)'\n';
		} else {
			/* put back the \n sign */
			buf[strlen(buf)] = '\n';
		}
	}

	c = (int)*bufpos++;
	if (bufpos - buf > PMON_MAXLN) {
		bufpos = buf;
		*bufpos = '\0';
	}

	return c;
}

void
pmoncnputc(dev_t dev, int c)
{
	if (c == '\n')
		pmon_printf("\n");
	else
		pmon_printf("%c", c);
}

void
intr_barrier(void *cookie)
{
	sched_barrier(NULL);
}

#ifdef MULTIPROCESSOR

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

	tlb_init(ci->ci_hw.tlbsize);
	tlb_set_pid(0);

	/*
	 * Turn off bootstrap exception vectors.
	 */
	setsr(getsr() & ~SR_BOOT_EXC_VEC);

	/*
	 * Clear out the I and D caches.
	 */
	switch (loongson_ver) {
#ifdef CPU_LOONGSON3
	case 0x3a:
	case 0x3b:
		Loongson3_ConfigCache(ci);
		Loongson3_SyncCache(ci);
		break;
#endif
	default:
		panic("%s: unhandled Loongson version %x", __func__,
		    loongson_ver);
	}

	(*md_startclock)(ci);

	mips64_ipi_init();

	ci->ci_flags |= CPUF_RUNNING;
	membar_sync();

	ncpus++;

	spl0();
	(void)updateimask(0);

	sched_toidle();
}

void
hw_cpu_boot_secondary(struct cpu_info *ci)
{
	sys_platform->boot_secondary_cpu(ci);
}

int
hw_ipi_intr_establish(int (*func)(void *), u_long cpuid)
{
	if (sys_platform->ipi_establish != NULL)
		return sys_platform->ipi_establish(func, cpuid);
	else
		return 0;
}

void
hw_ipi_intr_set(u_long cpuid)
{
	sys_platform->ipi_set(cpuid);
}

void
hw_ipi_intr_clear(u_long cpuid)
{
	if (sys_platform->ipi_clear != NULL)
		sys_platform->ipi_clear(cpuid);
}

#endif /* MULTIPROCESSOR */
