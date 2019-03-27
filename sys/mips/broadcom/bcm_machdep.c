/*-
 * Copyright (c) 2007 Bruce M. Simpson.
 * Copyright (c) 2016 Michael Zhilin <mizhka@gmail.com>
 * Copyright (c) 2016 Landon Fuller <landonf@FreeBSD.org>
 * Copyright (c) 2017 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Landon Fuller
 * under sponsorship from the FreeBSD Foundation.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/imgact.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/cons.h>
#include <sys/exec.h>
#include <sys/ucontext.h>
#include <sys/proc.h>
#include <sys/kdb.h>
#include <sys/ptrace.h>
#include <sys/reboot.h>
#include <sys/signalvar.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>
#include <sys/user.h>

#include <vm/vm.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>

#include <machine/cache.h>
#include <machine/clock.h>
#include <machine/cpu.h>
#include <machine/cpuinfo.h>
#include <machine/cpufunc.h>
#include <machine/cpuregs.h>
#include <machine/hwfunc.h>
#include <machine/intr_machdep.h>
#include <machine/locore.h>
#include <machine/md_var.h>
#include <machine/pte.h>
#include <machine/sigframe.h>
#include <machine/trap.h>
#include <machine/vmparam.h>

#include <dev/bhnd/bhnd.h>
#include <dev/bhnd/bhndreg.h>
#include <dev/bhnd/bhnd_eromvar.h>

#include <dev/bhnd/bcma/bcma_eromvar.h>

#include <dev/bhnd/siba/sibareg.h>
#include <dev/bhnd/siba/sibavar.h>

#include <dev/bhnd/cores/chipc/chipcreg.h>
#include <dev/bhnd/cores/pmu/bhnd_pmureg.h>

#include "bcm_machdep.h"
#include "bcm_bmips_exts.h"

#ifdef CFE
#include <dev/cfe/cfe_api.h>
#include <dev/cfe/cfe_error.h>
#endif

#if 0
#define	BCM_TRACE(_fmt, ...)	printf(_fmt, ##__VA_ARGS__)
#else
#define	BCM_TRACE(_fmt, ...)
#endif

static int	bcm_init_platform_data(struct bcm_platform *bp);

static int	bcm_find_core(struct bcm_platform *bp,
		    const struct bhnd_core_match *descs, size_t num_descs,
		    struct bhnd_core_info *info, uintptr_t *addr);

static int	bcm_erom_probe_and_attach(bhnd_erom_class_t **erom_cls,
		    kobj_ops_t erom_ops, bhnd_erom_t *erom, size_t esize,
		    struct bhnd_erom_io *eio, struct bhnd_chipid *cid);

extern int	*edata;
extern int	*end;

static struct bcm_platform	 bcm_platform_data;
static bool			 bcm_platform_data_avail = false;

#ifdef CFE
static struct bcm_nvram_iocfe	 bcm_cfe_nvram;
#endif

static const struct bhnd_core_match bcm_chipc_cores[] = {
	{ BHND_MATCH_CORE(BHND_MFGID_BCM,	BHND_COREID_CC)		},
	{ BHND_MATCH_CORE(BHND_MFGID_BCM,	BHND_COREID_4706_CC)	},
};

static const struct bhnd_core_match bcm_cpu0_cores[] = {
	{
		BHND_MATCH_CORE_CLASS(BHND_DEVCLASS_CPU),
		BHND_MATCH_CORE_UNIT(0)
	}
};

static const struct bhnd_core_match bcm_pmu_cores[] = {
	{ BHND_MATCH_CORE(BHND_MFGID_BCM,	BHND_COREID_PMU)	},
};

struct bcm_platform *
bcm_get_platform(void)
{
	if (!bcm_platform_data_avail)
		panic("platform data not available");

	return (&bcm_platform_data);
}

static bus_addr_t
bcm_get_bus_addr(void)
{
	long maddr;

	if (resource_long_value("bhnd", 0, "maddr", &maddr) == 0)
		return ((u_long)maddr);

	return (BHND_DEFAULT_CHIPC_ADDR);
}

static bus_size_t
bcm_get_bus_size(void)
{
	long msize;

	if (resource_long_value("bhnd", 0, "msize", &msize) == 0)
		return ((u_long)msize);

	return (BHND_DEFAULT_ENUM_SIZE);
}

/**
 * Search the device enumeration table for a core matching @p descs,
 * 
 * @param	bp		Platform state containing a valid EROM parser.
 * @param	descs		The core match descriptor table.
 * @param	num_descs	The number of match descriptors in @p descs.
 * @param[out]	info		If non-NULL, will be populated with the core
 *				info.
 * @param[out]	addr		If non-NULL, will be populated with the core's
 *				physical register address.
 */
static int
bcm_find_core(struct bcm_platform *bp, const struct bhnd_core_match *descs,
    size_t num_descs, struct bhnd_core_info *info, uintptr_t *addr)
{
	bhnd_addr_t		b_addr;
	bhnd_size_t		b_size;
	int			error;

	/* Fetch core info */
	for (size_t i = 0; i < num_descs; i++) {
		error = bhnd_erom_lookup_core_addr(&bp->erom.obj, &descs[i],
		    BHND_PORT_DEVICE, 0, 0, info, &b_addr, &b_size);

		/* Terminate search on first match */
		if (error == 0)
			break;

		/* Terminate on first error (other than core not found) */
		if (error != ENOENT)
			return (error);

		/* Continue search ... */
	}

	/* Provide the core's base address */
	if (addr != NULL && b_addr > UINTPTR_MAX) {
		BCM_ERR("core address %#jx overflows native address width\n",
		    (uintmax_t)b_addr);
		return (ERANGE);
	}

	if (addr != NULL)
		*addr = b_addr;

	return (0);
}

/**
 * Read a variable directly from NVRAM, decoding as @p type.
 *
 * @param		bp	Platform state.
 * @param		name	The raw name of the variable to be fetched,
 *				including any device path (/pci/1/1/varname) or
 *				alias prefix (0:varname).
 * @param[out]		buf	On success, the requested value will be written
 *				to this buffer. This argment may be NULL if
 *				the value is not desired.
 * @param[in,out]	len	The capacity of @p buf. On success, will be set
 *				to the actual size of the requested value.
 * @param		type	The data type to be written to @p buf.
 *
 * @retval 0		success
 * @retval ENOMEM	If @p buf is non-NULL and a buffer of @p len is too
 *			small to hold the requested value.
 * @retval ENOENT	If @p name is not found.
 * @retval EFTYPE	If the variable data cannot be coerced to @p type.
 * @retval ERANGE	If value coercion would overflow @p type.
 * @retval non-zero	If parsing NVRAM otherwise fails, a regular unix error
 *			code will be returned.
 */
int
bcm_get_nvram(struct bcm_platform *bp, const char *name, void *buf, size_t *len,
    bhnd_nvram_type type)
{
	if (bp->nvram_io == NULL || bp->nvram_cls == NULL)
		return (ENOENT);

	return (bhnd_nvram_data_getvar_direct(bp->nvram_cls, bp->nvram_io, name,
	    buf, len, type));
}

/**
 * Probe and attach a bhnd_erom parser instance for the bhnd bus.
 * 
 * @param[out]	erom_cls	The probed EROM class.
 * @param[out]	erom_ops	The storage to be used when compiling
 *				@p erom_cls.
 * @param[out]	erom		The storage to be used when initializing the
 *				static instance of @p erom_cls.
 * @param	esize		The total available number of bytes allocated
 *				for @p erom. If this is less than is required
 *				by @p erom_cls ENOMEM will be returned.
 * @param	eio		EROM I/O callbacks to be used.
 * @param[out]	cid		On success, the probed chip identification.
 */
static int
bcm_erom_probe_and_attach(bhnd_erom_class_t **erom_cls, kobj_ops_t erom_ops,
    bhnd_erom_t *erom, size_t esize, struct bhnd_erom_io *eio,
    struct bhnd_chipid *cid)
{
	bhnd_erom_class_t	**clsp;
	bus_addr_t		  bus_addr;
	int			  error, prio, result;

	*erom_cls = NULL;
	prio = 0;

	/* Map our first bus core for the erom probe */
	bus_addr = bcm_get_bus_addr();
	if ((error = bhnd_erom_io_map(eio, bus_addr, BHND_DEFAULT_CORE_SIZE))) {
		BCM_ERR("failed to map first core at %#jx+%#jx: %d\n",
		    (uintmax_t)bus_addr, (uintmax_t)BHND_DEFAULT_CORE_SIZE,
		    error);

		return (error);
	}

	SET_FOREACH(clsp, bhnd_erom_class_set) {
		struct bhnd_chipid	 pcid;
		bhnd_erom_class_t	*cls;
		struct kobj_ops		 kops;

		cls = *clsp;

		/* Compile the class' ops table */
		kobj_class_compile_static(cls, &kops);

		/* Probe the bus address */
		result = bhnd_erom_probe(cls, eio, NULL, &pcid);

		/* Drop pointer to stack allocated ops table */
		cls->ops = NULL;

		/* The parser did not match if an error was returned */
		if (result > 0)
			continue;

		/* Check for a new highest priority match */
		if (*erom_cls == NULL || result > prio) {
			prio = result;

			*cid = pcid;
			*erom_cls = cls;
		}

		/* Terminate immediately on BUS_PROBE_SPECIFIC */
		if (result == BUS_PROBE_SPECIFIC)
			break;
	}

	/* Valid EROM class probed? */
	if (*erom_cls == NULL) {
		BCM_ERR("no erom parser found for root bus at %#jx\n", 
		    (uintmax_t)bus_addr);

		return (ENOENT);
	}

	/* Using the provided storage, recompile the erom class ... */
	kobj_class_compile_static(*erom_cls, erom_ops);

	/* ... and initialize the erom parser instance */
	error = bhnd_erom_init_static(*erom_cls, erom, esize, cid, eio);

	return (error);
}

/**
 * Populate platform configuration data.
 */
static int
bcm_init_platform_data(struct bcm_platform *bp)
{
	bus_addr_t		bus_addr, bus_size;
	bus_space_tag_t		erom_bst;
	bus_space_handle_t	erom_bsh;
	bool			aob, pmu;
	int			error;

	bus_addr = bcm_get_bus_addr();
	bus_size = bcm_get_bus_size();

#ifdef CFE
	/* Fetch CFE console handle (if any). Must be initialized before
	 * any calls to printf/early_putc. */
	if ((bp->cfe_console = cfe_getstdhandle(CFE_STDHANDLE_CONSOLE)) < 0)
		bp->cfe_console = -1;

	/* Probe CFE NVRAM sources */
	bp->nvram_io = &bcm_cfe_nvram.io;
	error = bcm_nvram_find_cfedev(&bcm_cfe_nvram, &bp->nvram_cls);
	if (error) {
		bp->nvram_io = NULL;
		bp->nvram_cls = NULL;
	}
#endif /* CFE */

	/* Probe and attach device table provider, populating our
	 * chip identification */
	erom_bst = mips_bus_space_generic;
	erom_bsh = BCM_SOC_BSH(bus_addr, 0);

	error = bhnd_erom_iobus_init(&bp->erom_io, bus_addr, bus_size, erom_bst,
	    erom_bsh);
	if (error) {
		BCM_ERR("failed to initialize erom I/O callbacks: %d\n", error);
		return (error);
	}

	error = bcm_erom_probe_and_attach(&bp->erom_impl, &bp->erom_ops,
	    &bp->erom.obj, sizeof(bp->erom), &bp->erom_io.eio, &bp->cid);
	if (error) {
		BCM_ERR("error attaching erom parser: %d\n", error);
		bhnd_erom_io_fini(&bp->erom_io.eio);
		return (error);
	}

	if (bootverbose)
		bhnd_erom_dump(&bp->erom.obj);

	/* Fetch chipcommon core info */
	error = bcm_find_core(bp, bcm_chipc_cores, nitems(bcm_chipc_cores),
	    &bp->cc_id, &bp->cc_addr);
	if (error) {
		BCM_ERR("error locating chipc core: %d\n", error);
		return (error);
	}

	/* Fetch chipc capability flags */
	bp->cc_caps = BCM_SOC_READ_4(bp->cc_addr, CHIPC_CAPABILITIES);
	bp->cc_caps_ext = 0x0;	

	if (CHIPC_HWREV_HAS_CAP_EXT(bp->cc_id.hwrev))
		bp->cc_caps_ext = BCM_CHIPC_READ_4(bp, CHIPC_CAPABILITIES_EXT);

	/* Fetch PMU info */
	pmu = CHIPC_GET_FLAG(bp->cc_caps, CHIPC_CAP_PMU);
	aob = CHIPC_GET_FLAG(bp->cc_caps_ext, CHIPC_CAP2_AOB);

	if (pmu && aob) {
		/* PMU block mapped to a PMU core on the Always-on-Bus (aob) */
		error = bcm_find_core(bp, bcm_pmu_cores, nitems(bcm_pmu_cores),
		    &bp->pmu_id,  &bp->pmu_addr);
		if (error) {
			BCM_ERR("error locating pmu core: %d\n", error);
			return (error);
		}
	} else if (pmu) {
		/* PMU block mapped to chipc */
		bp->pmu_addr = bp->cc_addr;
		bp->pmu_id = bp->cc_id;
	} else {
		/* No PMU */
		bp->pmu_addr = 0x0;
		memset(&bp->pmu_id, 0, sizeof(bp->pmu_id));
	}

	/* Initialize PMU query state */
	if (pmu) {
		error = bhnd_pmu_query_init(&bp->pmu, NULL, bp->cid,
		    &bcm_pmu_soc_io, bp);
		if (error) {
			BCM_ERR("bhnd_pmu_query_init() failed: %d\n", error);
			return (error);
		}
	}

	/* Find CPU core info */
	error = bcm_find_core(bp, bcm_cpu0_cores, nitems(bcm_cpu0_cores),
	    &bp->cpu_id,  &bp->cpu_addr);
	if (error) {
		BCM_ERR("error locating CPU core: %d\n", error);
		return (error);
	}

	/* Initialize our platform service registry */
	if ((error = bhnd_service_registry_init(&bp->services))) {
		BCM_ERR("error initializing service registry: %d\n", error);
		return (error);
	}

	bcm_platform_data_avail = true;
	return (0);
}

void
platform_cpu_init()
{
	/* Nothing special */
}

static void
mips_init(void)
{
	int i, j;

	printf("entry: mips_init()\n");

#ifdef CFE
	/*
	 * Query DRAM memory map from CFE.
	 */
	physmem = 0;
	for (i = 0; i < 10; i += 2) {
		int result;
		uint64_t addr, len, type;

		result = cfe_enummem(i / 2, 0, &addr, &len, &type);
		if (result < 0) {
			BCM_TRACE("There is no phys memory for: %d\n", i);
			phys_avail[i] = phys_avail[i + 1] = 0;
			break;
		}
		if (type != CFE_MI_AVAILABLE) {
			BCM_TRACE("phys memory is not available: %d\n", i);
			continue;
		}

		phys_avail[i] = addr;
		if (i == 0 && addr == 0) {
			/*
			 * If this is the first physical memory segment probed
			 * from CFE, omit the region at the start of physical
			 * memory where the kernel has been loaded.
			 */
			phys_avail[i] += MIPS_KSEG0_TO_PHYS(kernel_kseg0_end);
		}
		
		BCM_TRACE("phys memory is available for: %d\n", i);
		BCM_TRACE(" => addr =  %jx\n", addr);
		BCM_TRACE(" => len =  %jd\n", len);

		phys_avail[i + 1] = addr + len;
		physmem += len;
	}

	BCM_TRACE("Total phys memory is : %ld\n", physmem);
	realmem = btoc(physmem);
#endif

	for (j = 0; j < i; j++)
		dump_avail[j] = phys_avail[j];

	physmem = realmem;

	init_param1();
	init_param2(physmem);
	mips_cpu_init();
	pmap_bootstrap();
	mips_proc0_init();
	mutex_init();
	kdb_init();
#ifdef KDB
	if (boothowto & RB_KDB)
		kdb_enter(KDB_WHY_BOOTFLAGS, "Boot flags requested debugger");
#endif
}

void
platform_reset(void)
{
	struct bcm_platform	*bp;
	bool			 bcm4785war;

	printf("bcm::platform_reset()\n");
	intr_disable();

#ifdef CFE
	/* Fall back on CFE if reset requested during platform
	 * data initialization */
	if (!bcm_platform_data_avail) {
		cfe_exit(0, 0);
		while (1);
	}
#endif

	bp = bcm_get_platform();
	bcm4785war = false;

	/* Handle BCM4785-specific behavior */
	if (bp->cid.chip_id == BHND_CHIPID_BCM4785) {
		bcm4785war = true;

		/* Switch to async mode */
		bcm_bmips_wr_pllcfg3(BMIPS_BCMCFG_PLLCFG3_SM);
	}

	/* Set watchdog (PMU or ChipCommon) */
	if (bp->pmu_addr != 0x0) {
		BCM_PMU_WRITE_4(bp, BHND_PMU_WATCHDOG, 1);
	} else
		BCM_CHIPC_WRITE_4(bp, CHIPC_WATCHDOG, 1);

	/* BCM4785 */
	if (bcm4785war) {
		mips_sync();
		__asm __volatile("wait");
	}

	while (1);
}

void
platform_start(__register_t a0, __register_t a1, __register_t a2,
	       __register_t a3)
{
	vm_offset_t 		 kernend;
	uint64_t		 platform_counter_freq;
	int			 error;

	/* clear the BSS and SBSS segments */
	kernend = (vm_offset_t)&end;
	memset(&edata, 0, kernend - (vm_offset_t)(&edata));

	mips_postboot_fixup();

	/* Initialize pcpu stuff */
	mips_pcpu0_init();

#ifdef CFE
	/*
	 * Initialize CFE firmware trampolines. This must be done
	 * before any CFE APIs are called, including writing
	 * to the CFE console.
	 *
	 * CFE passes the following values in registers:
	 * a0: firmware handle
	 * a2: firmware entry point
	 * a3: entry point seal
	 */
	if (a3 == CFE_EPTSEAL)
		cfe_init(a0, a2);
#endif

	/* Init BCM platform data */
	if ((error = bcm_init_platform_data(&bcm_platform_data)))
		panic("bcm_init_platform_data() failed: %d", error);

	platform_counter_freq = bcm_get_cpufreq(bcm_get_platform());

	/* CP0 ticks every two cycles */
	mips_timer_early_init(platform_counter_freq / 2);

	cninit();

	mips_init();

	mips_timer_init_params(platform_counter_freq, 1);
}

/*
 * CFE-based EARLY_PRINTF support. To use, add the following to the kernel
 * config:
 *	option EARLY_PRINTF
 *	option CFE
 *	device cfe
 */
#if defined(EARLY_PRINTF) && defined(CFE)
static void
bcm_cfe_eputc(int c)
{
	unsigned char	ch;
	int		handle;

	ch = (unsigned char) c;

	/* bcm_get_platform() cannot be used here, as we may be called
	 * from bcm_init_platform_data(). */
	if ((handle = bcm_platform_data.cfe_console) < 0)
		return;

	if (ch == '\n')
		early_putc('\r');

	while ((cfe_write(handle, &ch, 1)) == 0)
		continue;
}

early_putc_t *early_putc = bcm_cfe_eputc;
#endif /* EARLY_PRINTF */
