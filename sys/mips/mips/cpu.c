/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Juli Mallett.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/stdint.h>

#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include <vm/vm.h>
#include <vm/vm_page.h>

#include <machine/cache.h>
#include <machine/cpufunc.h>
#include <machine/cpuinfo.h>
#include <machine/cpuregs.h>
#include <machine/intr_machdep.h>
#include <machine/locore.h>
#include <machine/pte.h>
#include <machine/tlb.h>
#include <machine/hwfunc.h>
#include <machine/mips_opcode.h>
#include <machine/regnum.h>
#include <machine/tls.h>

#if defined(CPU_CNMIPS)
#include <contrib/octeon-sdk/cvmx.h>
#include <contrib/octeon-sdk/octeon-model.h>
#endif

static void cpu_identify(void);

struct mips_cpuinfo cpuinfo;

#define _ENCODE_INSN(a,b,c,d,e) \
    ((uint32_t)(((a) << 26)|((b) << 21)|((c) << 16)|((d) << 11)|(e)))

#if defined(__mips_n64)

#   define	_LOAD_T0_MDTLS_A1 \
    _ENCODE_INSN(OP_LD, A1, T0, 0, offsetof(struct thread, td_md.md_tls))

#   define	_LOAD_T0_MDTLS_TCV_OFFSET_A1 \
    _ENCODE_INSN(OP_LD, A1, T1, 0, \
    offsetof(struct thread, td_md.md_tls_tcb_offset))

#   define	_ADDU_V0_T0_T1 \
    _ENCODE_INSN(0, T0, T1, V0, OP_DADDU)

#else /* mips 32 */

#   define	_LOAD_T0_MDTLS_A1 \
    _ENCODE_INSN(OP_LW, A1, T0, 0, offsetof(struct thread, td_md.md_tls))

#   define	_LOAD_T0_MDTLS_TCV_OFFSET_A1 \
    _ENCODE_INSN(OP_LW, A1, T1, 0, \
    offsetof(struct thread, td_md.md_tls_tcb_offset))

#   define	_ADDU_V0_T0_T1 \
    _ENCODE_INSN(0, T0, T1, V0, OP_ADDU)

#endif /* ! __mips_n64 */

#if defined(__mips_n64) || defined(__mips_n32)

#   define _MTC0_V0_USERLOCAL \
    _ENCODE_INSN(OP_COP0, OP_DMT, V0, 4, 2)

#else /* mips o32 */

#   define _MTC0_V0_USERLOCAL \
    _ENCODE_INSN(OP_COP0, OP_MT, V0, 4, 2)

#endif /* ! (__mips_n64 || __mipsn32) */

#define	_JR_RA	_ENCODE_INSN(OP_SPECIAL, RA, 0, 0, OP_JR)
#define	_NOP	0

/*
 * Patch cpu_switch() by removing the UserLocal register code at the end.
 * For MIPS hardware that don't support UserLocal Register Implementation
 * we remove the instructions that update this register which may cause a
 * reserved instruction exception in the kernel.
 */
static void
remove_userlocal_code(uint32_t *cpu_switch_code)
{
	uint32_t *instructp;

	for (instructp = cpu_switch_code;; instructp++) {
		if (instructp[0] == _JR_RA)
			panic("%s: Unable to patch cpu_switch().", __func__);
		if (instructp[0] == _LOAD_T0_MDTLS_A1 &&
		    instructp[1] == _LOAD_T0_MDTLS_TCV_OFFSET_A1 &&
		    instructp[2] == _ADDU_V0_T0_T1 &&
		    instructp[3] == _MTC0_V0_USERLOCAL) {
			instructp[0] = _JR_RA;
			instructp[1] = _NOP;
			break;
		}
	}
}

/*
 * Attempt to identify the MIPS CPU as much as possible.
 *
 * XXX: Assumes the CPU is MIPS{32,64}{,r2} compliant.
 * XXX: For now, skip config register selections 2 and 3
 * as we don't currently use L2/L3 cache or additional
 * MIPS32 processor features.
 */
static void
mips_get_identity(struct mips_cpuinfo *cpuinfo)
{
	u_int32_t prid;
	u_int32_t cfg0;
	u_int32_t cfg1;
	u_int32_t cfg2;
	u_int32_t cfg3;
#if defined(CPU_CNMIPS)
	u_int32_t cfg4;
#endif
	u_int32_t tmp;

	memset(cpuinfo, 0, sizeof(struct mips_cpuinfo));

	/* Read and store the PrID ID for CPU identification. */
	prid = mips_rd_prid();
	cpuinfo->cpu_vendor = MIPS_PRID_CID(prid);
	cpuinfo->cpu_rev = MIPS_PRID_REV(prid);
	cpuinfo->cpu_impl = MIPS_PRID_IMPL(prid);

	/* Read config register selection 0 to learn TLB type. */
	cfg0 = mips_rd_config();

	cpuinfo->tlb_type = 
	    ((cfg0 & MIPS_CONFIG0_MT_MASK) >> MIPS_CONFIG0_MT_SHIFT);
	cpuinfo->icache_virtual = cfg0 & MIPS_CONFIG0_VI;

	/* If config register selection 1 does not exist, return. */
	if (!(cfg0 & MIPS_CONFIG0_M))
		return;

	/* Learn TLB size and L1 cache geometry. */
	cfg1 = mips_rd_config1();

	/* Get the Config2 and Config3 registers as well. */
	cfg2 = 0;
	cfg3 = 0;
	if (cfg1 & MIPS_CONFIG1_M) {
		cfg2 = mips_rd_config2();
		if (cfg2 & MIPS_CONFIG2_M)
			cfg3 = mips_rd_config3();
	}

	/* Save FP implementation revision if FP is present. */
	if (cfg1 & MIPS_CONFIG1_FP)
		cpuinfo->fpu_id = MipsFPID();

	/* Check to see if UserLocal register is implemented. */
	if (cfg3 & MIPS_CONFIG3_ULR) {
		/* UserLocal register is implemented, enable it. */
		cpuinfo->userlocal_reg = true;
		tmp = mips_rd_hwrena();
		mips_wr_hwrena(tmp | MIPS_HWRENA_UL);
	} else {
		/*
		 * UserLocal register is not implemented. Patch
		 * cpu_switch() and remove unsupported code.
		 */
		cpuinfo->userlocal_reg = false;
		remove_userlocal_code((uint32_t *)cpu_switch);
	}


#if defined(CPU_NLM)
	/* Account for Extended TLB entries in XLP */
	tmp = mips_rd_config6();
	cpuinfo->tlb_nentries = ((tmp >> 16) & 0xffff) + 1;
#elif defined(BERI_LARGE_TLB)
	/* Check if we support extended TLB entries and if so activate. */
	tmp = mips_rd_config5();
#define	BERI_CP5_LTLB_SUPPORTED	0x1
	if (tmp & BERI_CP5_LTLB_SUPPORTED) {
		/* See how many extra TLB entries we have. */
		tmp = mips_rd_config6();
		cpuinfo->tlb_nentries = (tmp >> 16) + 1;
		/* Activate the extended entries. */
		mips_wr_config6(tmp|0x4);
	} else
#endif
#if !defined(CPU_NLM)
	cpuinfo->tlb_nentries = 
	    ((cfg1 & MIPS_CONFIG1_TLBSZ_MASK) >> MIPS_CONFIG1_TLBSZ_SHIFT) + 1;
#endif
#if defined(CPU_CNMIPS)
	/* Add extended TLB size information from config4.  */
	cfg4 = mips_rd_config4();
	if ((cfg4 & MIPS_CONFIG4_MMUEXTDEF) == MIPS_CONFIG4_MMUEXTDEF_MMUSIZEEXT)
		cpuinfo->tlb_nentries += (cfg4 & MIPS_CONFIG4_MMUSIZEEXT) * 0x40;
#endif

	/* L1 instruction cache. */
#ifdef MIPS_DISABLE_L1_CACHE
	cpuinfo->l1.ic_linesize = 0;
#else
	tmp = (cfg1 & MIPS_CONFIG1_IL_MASK) >> MIPS_CONFIG1_IL_SHIFT;
	if (tmp != 0) {
		cpuinfo->l1.ic_linesize = 1 << (tmp + 1);
		cpuinfo->l1.ic_nways = (((cfg1 & MIPS_CONFIG1_IA_MASK) >> MIPS_CONFIG1_IA_SHIFT)) + 1;
		cpuinfo->l1.ic_nsets = 
	    		1 << (((cfg1 & MIPS_CONFIG1_IS_MASK) >> MIPS_CONFIG1_IS_SHIFT) + 6);
	}
#endif

	/* L1 data cache. */
#ifdef MIPS_DISABLE_L1_CACHE
	cpuinfo->l1.dc_linesize = 0;
#else
#ifndef CPU_CNMIPS
	tmp = (cfg1 & MIPS_CONFIG1_DL_MASK) >> MIPS_CONFIG1_DL_SHIFT;
	if (tmp != 0) {
		cpuinfo->l1.dc_linesize = 1 << (tmp + 1);
		cpuinfo->l1.dc_nways = 
		    (((cfg1 & MIPS_CONFIG1_DA_MASK) >> MIPS_CONFIG1_DA_SHIFT)) + 1;
		cpuinfo->l1.dc_nsets = 
		    1 << (((cfg1 & MIPS_CONFIG1_DS_MASK) >> MIPS_CONFIG1_DS_SHIFT) + 6);
	}
#else
	/*
	 * Some Octeon cache configuration parameters are by model family, not
	 * config1.
	 */
	if (OCTEON_IS_MODEL(OCTEON_CN3XXX)) {
		/* Octeon and Octeon XL.  */
		cpuinfo->l1.dc_nsets = 1;
		cpuinfo->l1.dc_nways = 64;
	} else if (OCTEON_IS_MODEL(OCTEON_CN5XXX)) {
		/* Octeon Plus.  */
		cpuinfo->l1.dc_nsets = 2;
		cpuinfo->l1.dc_nways = 64;
	} else if (OCTEON_IS_MODEL(OCTEON_CN6XXX)) {
		/* Octeon II.  */
		cpuinfo->l1.dc_nsets = 8;
		cpuinfo->l1.dc_nways = 32;

		cpuinfo->l1.ic_nsets = 8;
		cpuinfo->l1.ic_nways = 37;
	} else {
		panic("%s: unsupported Cavium Networks CPU.", __func__);
	}

	/* All Octeon models use 128 byte line size.  */
	cpuinfo->l1.dc_linesize = 128;
#endif
#endif

	cpuinfo->l1.ic_size = cpuinfo->l1.ic_linesize
	    * cpuinfo->l1.ic_nsets * cpuinfo->l1.ic_nways;
	cpuinfo->l1.dc_size = cpuinfo->l1.dc_linesize 
	    * cpuinfo->l1.dc_nsets * cpuinfo->l1.dc_nways;

	/*
	 * Probe PageMask register to see what sizes of pages are supported
	 * by writing all one's and then reading it back.
	 */
	mips_wr_pagemask(~0);
	cpuinfo->tlb_pgmask = mips_rd_pagemask();
	mips_wr_pagemask(MIPS3_PGMASK_4K);

#ifndef CPU_CNMIPS
	/* L2 cache */
	if (!(cfg1 & MIPS_CONFIG_CM)) {
		/* We don't have valid cfg2 register */
		return;
	}

	cfg2 = mips_rd_config2();

	tmp = (cfg2 >> MIPS_CONFIG2_SL_SHIFT) & MIPS_CONFIG2_SL_MASK;
	if (0 < tmp && tmp <= 7)
		cpuinfo->l2.dc_linesize = 2 << tmp;

	tmp = (cfg2 >> MIPS_CONFIG2_SS_SHIFT) & MIPS_CONFIG2_SS_MASK;
	if (0 <= tmp && tmp <= 7)
		cpuinfo->l2.dc_nsets = 64 << tmp;

	tmp = (cfg2 >> MIPS_CONFIG2_SA_SHIFT) & MIPS_CONFIG2_SA_MASK;
	if (0 <= tmp && tmp <= 7)
		cpuinfo->l2.dc_nways = tmp + 1;

	cpuinfo->l2.dc_size = cpuinfo->l2.dc_linesize
	    * cpuinfo->l2.dc_nsets * cpuinfo->l2.dc_nways;
#endif
}

void
mips_cpu_init(void)
{
	platform_cpu_init();
	mips_get_identity(&cpuinfo);
	num_tlbentries = cpuinfo.tlb_nentries;
	mips_wr_wired(0);
	tlb_invalidate_all();
	mips_wr_wired(VMWIRED_ENTRIES);
	mips_config_cache(&cpuinfo);
	mips_vector_init();

	mips_icache_sync_all();
	mips_dcache_wbinv_all();
	/* Print some info about CPU */
	cpu_identify();
}

static void
cpu_identify(void)
{
	uint32_t cfg0, cfg1, cfg2, cfg3;
#if defined(CPU_MIPS1004K) || defined (CPU_MIPS74K) || defined (CPU_MIPS24K)
	uint32_t cfg7;
#endif
	printf("cpu%d: ", 0);   /* XXX per-cpu */
	switch (cpuinfo.cpu_vendor) {
	case MIPS_PRID_CID_MTI:
		printf("MIPS Technologies");
		break;
	case MIPS_PRID_CID_BROADCOM:
	case MIPS_PRID_CID_SIBYTE:
		printf("Broadcom");
		break;
	case MIPS_PRID_CID_ALCHEMY:
		printf("AMD");
		break;
	case MIPS_PRID_CID_SANDCRAFT:
		printf("Sandcraft");
		break;
	case MIPS_PRID_CID_PHILIPS:
		printf("Philips");
		break;
	case MIPS_PRID_CID_TOSHIBA:
		printf("Toshiba");
		break;
	case MIPS_PRID_CID_LSI:
		printf("LSI");
		break;
	case MIPS_PRID_CID_LEXRA:
		printf("Lexra");
		break;
	case MIPS_PRID_CID_RMI:
		printf("RMI");
		break;
	case MIPS_PRID_CID_CAVIUM:
		printf("Cavium");
		break;
	case MIPS_PRID_CID_INGENIC:
	case MIPS_PRID_CID_INGENIC2:
		printf("Ingenic XBurst");
		break;
	case MIPS_PRID_CID_PREHISTORIC:
	default:
		printf("Unknown cid %#x", cpuinfo.cpu_vendor);
		break;
	}
	printf(" processor v%d.%d\n", cpuinfo.cpu_rev, cpuinfo.cpu_impl);

	printf("  MMU: ");
	if (cpuinfo.tlb_type == MIPS_MMU_NONE) {
		printf("none present\n");
	} else {
		if (cpuinfo.tlb_type == MIPS_MMU_TLB) {
			printf("Standard TLB");
		} else if (cpuinfo.tlb_type == MIPS_MMU_BAT) {
			printf("Standard BAT");
		} else if (cpuinfo.tlb_type == MIPS_MMU_FIXED) {
			printf("Fixed mapping");
		}
		printf(", %d entries ", cpuinfo.tlb_nentries);
	}

	if (cpuinfo.tlb_pgmask) {
		printf("(");
		if (cpuinfo.tlb_pgmask & MIPS3_PGMASK_MASKX)
			printf("1K ");
		printf("4K ");
		if (cpuinfo.tlb_pgmask & MIPS3_PGMASK_16K)
			printf("16K ");
		if (cpuinfo.tlb_pgmask & MIPS3_PGMASK_64K)
			printf("64K ");
		if (cpuinfo.tlb_pgmask & MIPS3_PGMASK_256K)
			printf("256K ");
		if (cpuinfo.tlb_pgmask & MIPS3_PGMASK_1M)
			printf("1M ");
		if (cpuinfo.tlb_pgmask & MIPS3_PGMASK_16M)
			printf("16M ");
		if (cpuinfo.tlb_pgmask & MIPS3_PGMASK_64M)
			printf("64M ");
		if (cpuinfo.tlb_pgmask & MIPS3_PGMASK_256M)
			printf("256M ");
		printf("pg sizes)");
	}
	printf("\n");

	printf("  L1 i-cache: ");
	if (cpuinfo.l1.ic_linesize == 0) {
		printf("disabled");
	} else {
		if (cpuinfo.l1.ic_nways == 1) {
			printf("direct-mapped with");
		} else {
			printf ("%d ways of", cpuinfo.l1.ic_nways);
		}
		printf(" %d sets, %d bytes per line\n", 
		    cpuinfo.l1.ic_nsets, cpuinfo.l1.ic_linesize);
	}

	printf("  L1 d-cache: ");
	if (cpuinfo.l1.dc_linesize == 0) {
		printf("disabled");
	} else {
		if (cpuinfo.l1.dc_nways == 1) {
			printf("direct-mapped with");
		} else {
			printf ("%d ways of", cpuinfo.l1.dc_nways);
		}
		printf(" %d sets, %d bytes per line\n", 
		    cpuinfo.l1.dc_nsets, cpuinfo.l1.dc_linesize);
	}

	printf("  L2 cache: ");
	if (cpuinfo.l2.dc_linesize == 0) {
		printf("disabled\n");
	} else {
		printf("%d ways of %d sets, %d bytes per line, "
		    "%d KiB total size\n",
		    cpuinfo.l2.dc_nways,
		    cpuinfo.l2.dc_nsets,
		    cpuinfo.l2.dc_linesize,
		    cpuinfo.l2.dc_size / 1024);
	}

	cfg0 = mips_rd_config();
	/* If config register selection 1 does not exist, exit. */
	if (!(cfg0 & MIPS_CONFIG_CM))
		return;

	cfg1 = mips_rd_config1();
	printf("  Config1=0x%b\n", cfg1, 
	    "\20\7COP2\6MDMX\5PerfCount\4WatchRegs\3MIPS16\2EJTAG\1FPU");

	if (cpuinfo.fpu_id != 0)
		printf("  FPU ID=0x%b\n", cpuinfo.fpu_id,
		    "\020"
		    "\020S"
		    "\021D"
		    "\022PS"
		    "\0233D"
		    "\024W"
		    "\025L"
		    "\026F64"
		    "\0272008"
		    "\034UFRP");

	/* If config register selection 2 does not exist, exit. */
	if (!(cfg1 & MIPS_CONFIG_CM))
		return;
	cfg2 = mips_rd_config2();
	/* 
	 * Config2 contains no useful information other then Config3 
	 * existence flag
	 */
	printf("  Config2=0x%08x\n", cfg2);

	/* If config register selection 3 does not exist, exit. */
	if (!(cfg2 & MIPS_CONFIG_CM))
		return;
	cfg3 = mips_rd_config3();

	/* Print Config3 if it contains any useful info */
	if (cfg3 & ~(0x80000000))
		printf("  Config3=0x%b\n", cfg3, "\20\16ULRI\2SmartMIPS\1TraceLogic");

#if defined(CPU_MIPS1004K) || defined (CPU_MIPS74K) || defined (CPU_MIPS24K)
	cfg7 = mips_rd_config7();
	printf("  Config7=0x%b\n", cfg7, "\20\40WII\21AR");
#endif
}

static struct rman cpu_hardirq_rman;

static devclass_t cpu_devclass;

/*
 * Device methods
 */
static int cpu_probe(device_t);
static int cpu_attach(device_t);
static struct resource *cpu_alloc_resource(device_t, device_t, int, int *,
					   rman_res_t, rman_res_t, rman_res_t,
					   u_int);
static int cpu_setup_intr(device_t, device_t, struct resource *, int,
			  driver_filter_t *f, driver_intr_t *, void *, 
			  void **);

static device_method_t cpu_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		cpu_probe),
	DEVMETHOD(device_attach,	cpu_attach),
	DEVMETHOD(device_detach,	bus_generic_detach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),

	/* Bus interface */
	DEVMETHOD(bus_alloc_resource,	cpu_alloc_resource),
	DEVMETHOD(bus_setup_intr,	cpu_setup_intr),
	DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),

	{ 0, 0 }
};

static driver_t cpu_driver = {
	"cpu", cpu_methods, 1
};

static int
cpu_probe(device_t dev)
{

	return (0);
}

static int
cpu_attach(device_t dev)
{
	int error;
#ifdef notyet
	device_t clock;
#endif

	cpu_hardirq_rman.rm_start = 0;
	cpu_hardirq_rman.rm_end = 5;
	cpu_hardirq_rman.rm_type = RMAN_ARRAY;
	cpu_hardirq_rman.rm_descr = "CPU Hard Interrupts";

	error = rman_init(&cpu_hardirq_rman);
	if (error != 0) {
		device_printf(dev, "failed to initialize irq resources\n");
		return (error);
	}
	/* XXX rman_manage_all. */
	error = rman_manage_region(&cpu_hardirq_rman,
				   cpu_hardirq_rman.rm_start,
				   cpu_hardirq_rman.rm_end);
	if (error != 0) {
		device_printf(dev, "failed to manage irq resources\n");
		return (error);
	}

	if (device_get_unit(dev) != 0)
		panic("can't attach more cpus");
	device_set_desc(dev, "MIPS32 processor");

#ifdef notyet
	clock = device_add_child(dev, "clock", device_get_unit(dev));
	if (clock == NULL)
		device_printf(dev, "clock failed to attach");
#endif

	return (bus_generic_attach(dev));
}

static struct resource *
cpu_alloc_resource(device_t dev, device_t child, int type, int *rid,
		   rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct resource *res;

	if (type != SYS_RES_IRQ)
		return (NULL);
	res = rman_reserve_resource(&cpu_hardirq_rman, start, end, count, 0,
				    child);
	return (res);
}

static int
cpu_setup_intr(device_t dev, device_t child, struct resource *res, int flags,
	       driver_filter_t *filt, driver_intr_t *handler, void *arg, 
	       void **cookiep)
{
	int error;
	int intr;

	error = rman_activate_resource(res);
	if (error != 0) {
		device_printf(child, "could not activate irq\n");
		return (error);
	}

	intr = rman_get_start(res);

	cpu_establish_hardintr(device_get_nameunit(child), filt, handler, arg, 
	    intr, flags, cookiep);
	device_printf(child, "established CPU interrupt %d\n", intr);
	return (0);
}

DRIVER_MODULE(cpu, root, cpu_driver, cpu_devclass, 0, 0);
