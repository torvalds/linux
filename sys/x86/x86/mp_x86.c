/*-
 * Copyright (c) 1996, by Steve Passe
 * Copyright (c) 2003, by Peter Wemm
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the developer may NOT be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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

#ifdef __i386__
#include "opt_apic.h"
#endif
#include "opt_cpu.h"
#include "opt_kstack_pages.h"
#include "opt_pmap.h"
#include "opt_sched.h"
#include "opt_smp.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/cons.h>	/* cngetc() */
#include <sys/cpuset.h>
#ifdef GPROF 
#include <sys/gmon.h>
#endif
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/memrange.h>
#include <sys/mutex.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/sysctl.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>
#include <vm/vm_extern.h>
#include <vm/vm_map.h>

#include <x86/apicreg.h>
#include <machine/clock.h>
#include <machine/cpu.h>
#include <machine/cputypes.h>
#include <x86/mca.h>
#include <machine/md_var.h>
#include <machine/pcb.h>
#include <machine/psl.h>
#include <machine/smp.h>
#include <machine/specialreg.h>
#include <x86/ucode.h>

static MALLOC_DEFINE(M_CPUS, "cpus", "CPU items");

/* lock region used by kernel profiling */
int	mcount_lock;

int	mp_naps;		/* # of Applications processors */
int	boot_cpu_id = -1;	/* designated BSP */

/* AP uses this during bootstrap.  Do not staticize.  */
char *bootSTK;
int bootAP;

/* Free these after use */
void *bootstacks[MAXCPU];
void *dpcpu;

struct pcb stoppcbs[MAXCPU];
struct susppcb **susppcbs;

#ifdef COUNT_IPIS
/* Interrupt counts. */
static u_long *ipi_preempt_counts[MAXCPU];
static u_long *ipi_ast_counts[MAXCPU];
u_long *ipi_invltlb_counts[MAXCPU];
u_long *ipi_invlrng_counts[MAXCPU];
u_long *ipi_invlpg_counts[MAXCPU];
u_long *ipi_invlcache_counts[MAXCPU];
u_long *ipi_rendezvous_counts[MAXCPU];
static u_long *ipi_hardclock_counts[MAXCPU];
#endif

/* Default cpu_ops implementation. */
struct cpu_ops cpu_ops;

/*
 * Local data and functions.
 */

static volatile cpuset_t ipi_stop_nmi_pending;

volatile cpuset_t resuming_cpus;
volatile cpuset_t toresume_cpus;

/* used to hold the AP's until we are ready to release them */
struct mtx ap_boot_mtx;

/* Set to 1 once we're ready to let the APs out of the pen. */
volatile int aps_ready = 0;

/*
 * Store data from cpu_add() until later in the boot when we actually setup
 * the APs.
 */
struct cpu_info *cpu_info;
int *apic_cpuids;
int cpu_apic_ids[MAXCPU];
_Static_assert(MAXCPU <= MAX_APIC_ID,
    "MAXCPU cannot be larger that MAX_APIC_ID");
_Static_assert(xAPIC_MAX_APIC_ID <= MAX_APIC_ID,
    "xAPIC_MAX_APIC_ID cannot be larger that MAX_APIC_ID");

/* Holds pending bitmap based IPIs per CPU */
volatile u_int cpu_ipi_pending[MAXCPU];

static void	release_aps(void *dummy);
static void	cpustop_handler_post(u_int cpu);

static int	hyperthreading_allowed = 1;
SYSCTL_INT(_machdep, OID_AUTO, hyperthreading_allowed, CTLFLAG_RDTUN,
	&hyperthreading_allowed, 0, "Use Intel HTT logical CPUs");

static struct topo_node topo_root;

static int pkg_id_shift;
static int node_id_shift;
static int core_id_shift;
static int disabled_cpus;

struct cache_info {
	int	id_shift;
	int	present;
} static caches[MAX_CACHE_LEVELS];

unsigned int boot_address;

#define MiB(v)	(v ## ULL << 20)

void
mem_range_AP_init(void)
{

	if (mem_range_softc.mr_op && mem_range_softc.mr_op->initAP)
		mem_range_softc.mr_op->initAP(&mem_range_softc);
}

/*
 * Round up to the next power of two, if necessary, and then
 * take log2.
 * Returns -1 if argument is zero.
 */
static __inline int
mask_width(u_int x)
{

	return (fls(x << (1 - powerof2(x))) - 1);
}

/*
 * Add a cache level to the cache topology description.
 */
static int
add_deterministic_cache(int type, int level, int share_count)
{

	if (type == 0)
		return (0);
	if (type > 3) {
		printf("unexpected cache type %d\n", type);
		return (1);
	}
	if (type == 2) /* ignore instruction cache */
		return (1);
	if (level == 0 || level > MAX_CACHE_LEVELS) {
		printf("unexpected cache level %d\n", type);
		return (1);
	}

	if (caches[level - 1].present) {
		printf("WARNING: multiple entries for L%u data cache\n", level);
		printf("%u => %u\n", caches[level - 1].id_shift,
		    mask_width(share_count));
	}
	caches[level - 1].id_shift = mask_width(share_count);
	caches[level - 1].present = 1;

	if (caches[level - 1].id_shift > pkg_id_shift) {
		printf("WARNING: L%u data cache covers more "
		    "APIC IDs than a package (%u > %u)\n", level,
		    caches[level - 1].id_shift, pkg_id_shift);
		caches[level - 1].id_shift = pkg_id_shift;
	}
	if (caches[level - 1].id_shift < core_id_shift) {
		printf("WARNING: L%u data cache covers fewer "
		    "APIC IDs than a core (%u < %u)\n", level,
		    caches[level - 1].id_shift, core_id_shift);
		caches[level - 1].id_shift = core_id_shift;
	}

	return (1);
}

/*
 * Determine topology of processing units and caches for AMD CPUs.
 * See:
 *  - AMD CPUID Specification (Publication # 25481)
 *  - BKDG for AMD NPT Family 0Fh Processors (Publication # 32559)
 *  - BKDG For AMD Family 10h Processors (Publication # 31116)
 *  - BKDG For AMD Family 15h Models 00h-0Fh Processors (Publication # 42301)
 *  - BKDG For AMD Family 16h Models 00h-0Fh Processors (Publication # 48751)
 *  - PPR For AMD Family 17h Models 00h-0Fh Processors (Publication # 54945)
 */
static void
topo_probe_amd(void)
{
	u_int p[4];
	uint64_t v;
	int level;
	int nodes_per_socket;
	int share_count;
	int type;
	int i;

	/* No multi-core capability. */
	if ((amd_feature2 & AMDID2_CMP) == 0)
		return;

	/* For families 10h and newer. */
	pkg_id_shift = (cpu_procinfo2 & AMDID_COREID_SIZE) >>
	    AMDID_COREID_SIZE_SHIFT;

	/* For 0Fh family. */
	if (pkg_id_shift == 0)
		pkg_id_shift =
		    mask_width((cpu_procinfo2 & AMDID_CMP_CORES) + 1);

	/*
	 * Families prior to 16h define the following value as
	 * cores per compute unit and we don't really care about the AMD
	 * compute units at the moment.  Perhaps we should treat them as
	 * cores and cores within the compute units as hardware threads,
	 * but that's up for debate.
	 * Later families define the value as threads per compute unit,
	 * so we are following AMD's nomenclature here.
	 */
	if ((amd_feature2 & AMDID2_TOPOLOGY) != 0 &&
	    CPUID_TO_FAMILY(cpu_id) >= 0x16) {
		cpuid_count(0x8000001e, 0, p);
		share_count = ((p[1] >> 8) & 0xff) + 1;
		core_id_shift = mask_width(share_count);

		/*
		 * For Zen (17h), gather Nodes per Processor.  Each node is a
		 * Zeppelin die; TR and EPYC CPUs will have multiple dies per
		 * package.  Communication latency between dies is higher than
		 * within them.
		 */
		nodes_per_socket = ((p[2] >> 8) & 0x7) + 1;
		node_id_shift = pkg_id_shift - mask_width(nodes_per_socket);
	}

	if ((amd_feature2 & AMDID2_TOPOLOGY) != 0) {
		for (i = 0; ; i++) {
			cpuid_count(0x8000001d, i, p);
			type = p[0] & 0x1f;
			level = (p[0] >> 5) & 0x7;
			share_count = 1 + ((p[0] >> 14) & 0xfff);

			if (!add_deterministic_cache(type, level, share_count))
				break;
		}
	} else {
		if (cpu_exthigh >= 0x80000005) {
			cpuid_count(0x80000005, 0, p);
			if (((p[2] >> 24) & 0xff) != 0) {
				caches[0].id_shift = 0;
				caches[0].present = 1;
			}
		}
		if (cpu_exthigh >= 0x80000006) {
			cpuid_count(0x80000006, 0, p);
			if (((p[2] >> 16) & 0xffff) != 0) {
				caches[1].id_shift = 0;
				caches[1].present = 1;
			}
			if (((p[3] >> 18) & 0x3fff) != 0) {
				nodes_per_socket = 1;
				if ((amd_feature2 & AMDID2_NODE_ID) != 0) {
					/*
					 * Handle multi-node processors that
					 * have multiple chips, each with its
					 * own L3 cache, on the same die.
					 */
					v = rdmsr(0xc001100c);
					nodes_per_socket = 1 + ((v >> 3) & 0x7);
				}
				caches[2].id_shift =
				    pkg_id_shift - mask_width(nodes_per_socket);
				caches[2].present = 1;
			}
		}
	}
}

/*
 * Determine topology of processing units for Intel CPUs
 * using CPUID Leaf 1 and Leaf 4, if supported.
 * See:
 *  - Intel 64 Architecture Processor Topology Enumeration
 *  - Intel 64 and IA-32 ArchitecturesSoftware Developer’s Manual,
 *    Volume 3A: System Programming Guide, PROGRAMMING CONSIDERATIONS
 *    FOR HARDWARE MULTI-THREADING CAPABLE PROCESSORS
 */
static void
topo_probe_intel_0x4(void)
{
	u_int p[4];
	int max_cores;
	int max_logical;

	/* Both zero and one here mean one logical processor per package. */
	max_logical = (cpu_feature & CPUID_HTT) != 0 ?
	    (cpu_procinfo & CPUID_HTT_CORES) >> 16 : 1;
	if (max_logical <= 1)
		return;

	if (cpu_high >= 0x4) {
		cpuid_count(0x04, 0, p);
		max_cores = ((p[0] >> 26) & 0x3f) + 1;
	} else
		max_cores = 1;

	core_id_shift = mask_width(max_logical/max_cores);
	KASSERT(core_id_shift >= 0,
	    ("intel topo: max_cores > max_logical\n"));
	pkg_id_shift = core_id_shift + mask_width(max_cores);
}

/*
 * Determine topology of processing units for Intel CPUs
 * using CPUID Leaf 11, if supported.
 * See:
 *  - Intel 64 Architecture Processor Topology Enumeration
 *  - Intel 64 and IA-32 ArchitecturesSoftware Developer’s Manual,
 *    Volume 3A: System Programming Guide, PROGRAMMING CONSIDERATIONS
 *    FOR HARDWARE MULTI-THREADING CAPABLE PROCESSORS
 */
static void
topo_probe_intel_0xb(void)
{
	u_int p[4];
	int bits;
	int type;
	int i;

	/* Fall back if CPU leaf 11 doesn't really exist. */
	cpuid_count(0x0b, 0, p);
	if (p[1] == 0) {
		topo_probe_intel_0x4();
		return;
	}

	/* We only support three levels for now. */
	for (i = 0; ; i++) {
		cpuid_count(0x0b, i, p);

		bits = p[0] & 0x1f;
		type = (p[2] >> 8) & 0xff;

		if (type == 0)
			break;

		/* TODO: check for duplicate (re-)assignment */
		if (type == CPUID_TYPE_SMT)
			core_id_shift = bits;
		else if (type == CPUID_TYPE_CORE)
			pkg_id_shift = bits;
		else
			printf("unknown CPU level type %d\n", type);
	}

	if (pkg_id_shift < core_id_shift) {
		printf("WARNING: core covers more APIC IDs than a package\n");
		core_id_shift = pkg_id_shift;
	}
}

/*
 * Determine topology of caches for Intel CPUs.
 * See:
 *  - Intel 64 Architecture Processor Topology Enumeration
 *  - Intel 64 and IA-32 Architectures Software Developer’s Manual
 *    Volume 2A: Instruction Set Reference, A-M,
 *    CPUID instruction
 */
static void
topo_probe_intel_caches(void)
{
	u_int p[4];
	int level;
	int share_count;
	int type;
	int i;

	if (cpu_high < 0x4) {
		/*
		 * Available cache level and sizes can be determined
		 * via CPUID leaf 2, but that requires a huge table of hardcoded
		 * values, so for now just assume L1 and L2 caches potentially
		 * shared only by HTT processing units, if HTT is present.
		 */
		caches[0].id_shift = pkg_id_shift;
		caches[0].present = 1;
		caches[1].id_shift = pkg_id_shift;
		caches[1].present = 1;
		return;
	}

	for (i = 0; ; i++) {
		cpuid_count(0x4, i, p);
		type = p[0] & 0x1f;
		level = (p[0] >> 5) & 0x7;
		share_count = 1 + ((p[0] >> 14) & 0xfff);

		if (!add_deterministic_cache(type, level, share_count))
			break;
	}
}

/*
 * Determine topology of processing units and caches for Intel CPUs.
 * See:
 *  - Intel 64 Architecture Processor Topology Enumeration
 */
static void
topo_probe_intel(void)
{

	/*
	 * Note that 0x1 <= cpu_high < 4 case should be
	 * compatible with topo_probe_intel_0x4() logic when
	 * CPUID.1:EBX[23:16] > 0 (cpu_cores will be 1)
	 * or it should trigger the fallback otherwise.
	 */
	if (cpu_high >= 0xb)
		topo_probe_intel_0xb();
	else if (cpu_high >= 0x1)
		topo_probe_intel_0x4();

	topo_probe_intel_caches();
}

/*
 * Topology information is queried only on BSP, on which this
 * code runs and for which it can query CPUID information.
 * Then topology is extrapolated on all packages using an
 * assumption that APIC ID to hardware component ID mapping is
 * homogenious.
 * That doesn't necesserily imply that the topology is uniform.
 */
void
topo_probe(void)
{
	static int cpu_topo_probed = 0;
	struct x86_topo_layer {
		int type;
		int subtype;
		int id_shift;
	} topo_layers[MAX_CACHE_LEVELS + 4];
	struct topo_node *parent;
	struct topo_node *node;
	int layer;
	int nlayers;
	int node_id;
	int i;

	if (cpu_topo_probed)
		return;

	CPU_ZERO(&logical_cpus_mask);

	if (mp_ncpus <= 1)
		; /* nothing */
	else if (cpu_vendor_id == CPU_VENDOR_AMD)
		topo_probe_amd();
	else if (cpu_vendor_id == CPU_VENDOR_INTEL)
		topo_probe_intel();

	KASSERT(pkg_id_shift >= core_id_shift,
	    ("bug in APIC topology discovery"));

	nlayers = 0;
	bzero(topo_layers, sizeof(topo_layers));

	topo_layers[nlayers].type = TOPO_TYPE_PKG;
	topo_layers[nlayers].id_shift = pkg_id_shift;
	if (bootverbose)
		printf("Package ID shift: %u\n", topo_layers[nlayers].id_shift);
	nlayers++;

	if (pkg_id_shift > node_id_shift && node_id_shift != 0) {
		topo_layers[nlayers].type = TOPO_TYPE_GROUP;
		topo_layers[nlayers].id_shift = node_id_shift;
		if (bootverbose)
			printf("Node ID shift: %u\n",
			    topo_layers[nlayers].id_shift);
		nlayers++;
	}

	/*
	 * Consider all caches to be within a package/chip
	 * and "in front" of all sub-components like
	 * cores and hardware threads.
	 */
	for (i = MAX_CACHE_LEVELS - 1; i >= 0; --i) {
		if (caches[i].present) {
			if (node_id_shift != 0)
				KASSERT(caches[i].id_shift <= node_id_shift,
					("bug in APIC topology discovery"));
			KASSERT(caches[i].id_shift <= pkg_id_shift,
				("bug in APIC topology discovery"));
			KASSERT(caches[i].id_shift >= core_id_shift,
				("bug in APIC topology discovery"));

			topo_layers[nlayers].type = TOPO_TYPE_CACHE;
			topo_layers[nlayers].subtype = i + 1;
			topo_layers[nlayers].id_shift = caches[i].id_shift;
			if (bootverbose)
				printf("L%u cache ID shift: %u\n",
				    topo_layers[nlayers].subtype,
				    topo_layers[nlayers].id_shift);
			nlayers++;
		}
	}

	if (pkg_id_shift > core_id_shift) {
		topo_layers[nlayers].type = TOPO_TYPE_CORE;
		topo_layers[nlayers].id_shift = core_id_shift;
		if (bootverbose)
			printf("Core ID shift: %u\n",
			    topo_layers[nlayers].id_shift);
		nlayers++;
	}

	topo_layers[nlayers].type = TOPO_TYPE_PU;
	topo_layers[nlayers].id_shift = 0;
	nlayers++;

	topo_init_root(&topo_root);
	for (i = 0; i <= max_apic_id; ++i) {
		if (!cpu_info[i].cpu_present)
			continue;

		parent = &topo_root;
		for (layer = 0; layer < nlayers; ++layer) {
			node_id = i >> topo_layers[layer].id_shift;
			parent = topo_add_node_by_hwid(parent, node_id,
			    topo_layers[layer].type,
			    topo_layers[layer].subtype);
		}
	}

	parent = &topo_root;
	for (layer = 0; layer < nlayers; ++layer) {
		node_id = boot_cpu_id >> topo_layers[layer].id_shift;
		node = topo_find_node_by_hwid(parent, node_id,
		    topo_layers[layer].type,
		    topo_layers[layer].subtype);
		topo_promote_child(node);
		parent = node;
	}

	cpu_topo_probed = 1;
}

/*
 * Assign logical CPU IDs to local APICs.
 */
void
assign_cpu_ids(void)
{
	struct topo_node *node;
	u_int smt_mask;
	int nhyper;

	smt_mask = (1u << core_id_shift) - 1;

	/*
	 * Assign CPU IDs to local APIC IDs and disable any CPUs
	 * beyond MAXCPU.  CPU 0 is always assigned to the BSP.
	 */
	mp_ncpus = 0;
	nhyper = 0;
	TOPO_FOREACH(node, &topo_root) {
		if (node->type != TOPO_TYPE_PU)
			continue;

		if ((node->hwid & smt_mask) != (boot_cpu_id & smt_mask))
			cpu_info[node->hwid].cpu_hyperthread = 1;

		if (resource_disabled("lapic", node->hwid)) {
			if (node->hwid != boot_cpu_id)
				cpu_info[node->hwid].cpu_disabled = 1;
			else
				printf("Cannot disable BSP, APIC ID = %d\n",
				    node->hwid);
		}

		if (!hyperthreading_allowed &&
		    cpu_info[node->hwid].cpu_hyperthread)
			cpu_info[node->hwid].cpu_disabled = 1;

		if (mp_ncpus >= MAXCPU)
			cpu_info[node->hwid].cpu_disabled = 1;

		if (cpu_info[node->hwid].cpu_disabled) {
			disabled_cpus++;
			continue;
		}

		if (cpu_info[node->hwid].cpu_hyperthread)
			nhyper++;

		cpu_apic_ids[mp_ncpus] = node->hwid;
		apic_cpuids[node->hwid] = mp_ncpus;
		topo_set_pu_id(node, mp_ncpus);
		mp_ncpus++;
	}

	KASSERT(mp_maxid >= mp_ncpus - 1,
	    ("%s: counters out of sync: max %d, count %d", __func__, mp_maxid,
	    mp_ncpus));

	mp_ncores = mp_ncpus - nhyper;
	smp_threads_per_core = mp_ncpus / mp_ncores;
}

/*
 * Print various information about the SMP system hardware and setup.
 */
void
cpu_mp_announce(void)
{
	struct topo_node *node;
	const char *hyperthread;
	struct topo_analysis topology;

	printf("FreeBSD/SMP: ");
	if (topo_analyze(&topo_root, 1, &topology)) {
		printf("%d package(s)", topology.entities[TOPO_LEVEL_PKG]);
		if (topology.entities[TOPO_LEVEL_GROUP] > 1)
			printf(" x %d groups",
			    topology.entities[TOPO_LEVEL_GROUP]);
		if (topology.entities[TOPO_LEVEL_CACHEGROUP] > 1)
			printf(" x %d cache groups",
			    topology.entities[TOPO_LEVEL_CACHEGROUP]);
		if (topology.entities[TOPO_LEVEL_CORE] > 0)
			printf(" x %d core(s)",
			    topology.entities[TOPO_LEVEL_CORE]);
		if (topology.entities[TOPO_LEVEL_THREAD] > 1)
			printf(" x %d hardware threads",
			    topology.entities[TOPO_LEVEL_THREAD]);
	} else {
		printf("Non-uniform topology");
	}
	printf("\n");

	if (disabled_cpus) {
		printf("FreeBSD/SMP Online: ");
		if (topo_analyze(&topo_root, 0, &topology)) {
			printf("%d package(s)",
			    topology.entities[TOPO_LEVEL_PKG]);
			if (topology.entities[TOPO_LEVEL_GROUP] > 1)
				printf(" x %d groups",
				    topology.entities[TOPO_LEVEL_GROUP]);
			if (topology.entities[TOPO_LEVEL_CACHEGROUP] > 1)
				printf(" x %d cache groups",
				    topology.entities[TOPO_LEVEL_CACHEGROUP]);
			if (topology.entities[TOPO_LEVEL_CORE] > 0)
				printf(" x %d core(s)",
				    topology.entities[TOPO_LEVEL_CORE]);
			if (topology.entities[TOPO_LEVEL_THREAD] > 1)
				printf(" x %d hardware threads",
				    topology.entities[TOPO_LEVEL_THREAD]);
		} else {
			printf("Non-uniform topology");
		}
		printf("\n");
	}

	if (!bootverbose)
		return;

	TOPO_FOREACH(node, &topo_root) {
		switch (node->type) {
		case TOPO_TYPE_PKG:
			printf("Package HW ID = %u\n", node->hwid);
			break;
		case TOPO_TYPE_CORE:
			printf("\tCore HW ID = %u\n", node->hwid);
			break;
		case TOPO_TYPE_PU:
			if (cpu_info[node->hwid].cpu_hyperthread)
				hyperthread = "/HT";
			else
				hyperthread = "";

			if (node->subtype == 0)
				printf("\t\tCPU (AP%s): APIC ID: %u"
				    "(disabled)\n", hyperthread, node->hwid);
			else if (node->id == 0)
				printf("\t\tCPU0 (BSP): APIC ID: %u\n",
				    node->hwid);
			else
				printf("\t\tCPU%u (AP%s): APIC ID: %u\n",
				    node->id, hyperthread, node->hwid);
			break;
		default:
			/* ignored */
			break;
		}
	}
}

/*
 * Add a scheduling group, a group of logical processors sharing
 * a particular cache (and, thus having an affinity), to the scheduling
 * topology.
 * This function recursively works on lower level caches.
 */
static void
x86topo_add_sched_group(struct topo_node *root, struct cpu_group *cg_root)
{
	struct topo_node *node;
	int nchildren;
	int ncores;
	int i;

	KASSERT(root->type == TOPO_TYPE_SYSTEM || root->type == TOPO_TYPE_CACHE ||
	    root->type == TOPO_TYPE_GROUP,
	    ("x86topo_add_sched_group: bad type: %u", root->type));
	CPU_COPY(&root->cpuset, &cg_root->cg_mask);
	cg_root->cg_count = root->cpu_count;
	if (root->type == TOPO_TYPE_SYSTEM)
		cg_root->cg_level = CG_SHARE_NONE;
	else
		cg_root->cg_level = root->subtype;

	/*
	 * Check how many core nodes we have under the given root node.
	 * If we have multiple logical processors, but not multiple
	 * cores, then those processors must be hardware threads.
	 */
	ncores = 0;
	node = root;
	while (node != NULL) {
		if (node->type != TOPO_TYPE_CORE) {
			node = topo_next_node(root, node);
			continue;
		}

		ncores++;
		node = topo_next_nonchild_node(root, node);
	}

	if (cg_root->cg_level != CG_SHARE_NONE &&
	    root->cpu_count > 1 && ncores < 2)
		cg_root->cg_flags = CG_FLAG_SMT;

	/*
	 * Find out how many cache nodes we have under the given root node.
	 * We ignore cache nodes that cover all the same processors as the
	 * root node.  Also, we do not descend below found cache nodes.
	 * That is, we count top-level "non-redundant" caches under the root
	 * node.
	 */
	nchildren = 0;
	node = root;
	while (node != NULL) {
		if ((node->type != TOPO_TYPE_GROUP &&
		    node->type != TOPO_TYPE_CACHE) ||
		    (root->type != TOPO_TYPE_SYSTEM &&
		    CPU_CMP(&node->cpuset, &root->cpuset) == 0)) {
			node = topo_next_node(root, node);
			continue;
		}
		nchildren++;
		node = topo_next_nonchild_node(root, node);
	}

	cg_root->cg_child = smp_topo_alloc(nchildren);
	cg_root->cg_children = nchildren;

	/*
	 * Now find again the same cache nodes as above and recursively
	 * build scheduling topologies for them.
	 */
	node = root;
	i = 0;
	while (node != NULL) {
		if ((node->type != TOPO_TYPE_GROUP &&
		    node->type != TOPO_TYPE_CACHE) ||
		    (root->type != TOPO_TYPE_SYSTEM &&
		    CPU_CMP(&node->cpuset, &root->cpuset) == 0)) {
			node = topo_next_node(root, node);
			continue;
		}
		cg_root->cg_child[i].cg_parent = cg_root;
		x86topo_add_sched_group(node, &cg_root->cg_child[i]);
		i++;
		node = topo_next_nonchild_node(root, node);
	}
}

/*
 * Build the MI scheduling topology from the discovered hardware topology.
 */
struct cpu_group *
cpu_topo(void)
{
	struct cpu_group *cg_root;

	if (mp_ncpus <= 1)
		return (smp_topo_none());

	cg_root = smp_topo_alloc(1);
	x86topo_add_sched_group(&topo_root, cg_root);
	return (cg_root);
}

static void
cpu_alloc(void *dummy __unused)
{
	/*
	 * Dynamically allocate the arrays that depend on the
	 * maximum APIC ID.
	 */
	cpu_info = malloc(sizeof(*cpu_info) * (max_apic_id + 1), M_CPUS,
	    M_WAITOK | M_ZERO);
	apic_cpuids = malloc(sizeof(*apic_cpuids) * (max_apic_id + 1), M_CPUS,
	    M_WAITOK | M_ZERO);
}
SYSINIT(cpu_alloc, SI_SUB_CPU, SI_ORDER_FIRST, cpu_alloc, NULL);

/*
 * Add a logical CPU to the topology.
 */
void
cpu_add(u_int apic_id, char boot_cpu)
{

	if (apic_id > max_apic_id) {
		panic("SMP: APIC ID %d too high", apic_id);
		return;
	}
	KASSERT(cpu_info[apic_id].cpu_present == 0, ("CPU %u added twice",
	    apic_id));
	cpu_info[apic_id].cpu_present = 1;
	if (boot_cpu) {
		KASSERT(boot_cpu_id == -1,
		    ("CPU %u claims to be BSP, but CPU %u already is", apic_id,
		    boot_cpu_id));
		boot_cpu_id = apic_id;
		cpu_info[apic_id].cpu_bsp = 1;
	}
	if (bootverbose)
		printf("SMP: Added CPU %u (%s)\n", apic_id, boot_cpu ? "BSP" :
		    "AP");
}

void
cpu_mp_setmaxid(void)
{

	/*
	 * mp_ncpus and mp_maxid should be already set by calls to cpu_add().
	 * If there were no calls to cpu_add() assume this is a UP system.
	 */
	if (mp_ncpus == 0)
		mp_ncpus = 1;
}

int
cpu_mp_probe(void)
{

	/*
	 * Always record BSP in CPU map so that the mbuf init code works
	 * correctly.
	 */
	CPU_SETOF(0, &all_cpus);
	return (mp_ncpus > 1);
}

/* Allocate memory for the AP trampoline. */
void
alloc_ap_trampoline(vm_paddr_t *physmap, unsigned int *physmap_idx)
{
	unsigned int i;
	bool allocated;

	allocated = false;
	for (i = *physmap_idx; i <= *physmap_idx; i -= 2) {
		/*
		 * Find a memory region big enough and below the 1MB boundary
		 * for the trampoline code.
		 * NB: needs to be page aligned.
		 */
		if (physmap[i] >= MiB(1) ||
		    (trunc_page(physmap[i + 1]) - round_page(physmap[i])) <
		    round_page(bootMP_size))
			continue;

		allocated = true;
		/*
		 * Try to steal from the end of the region to mimic previous
		 * behaviour, else fallback to steal from the start.
		 */
		if (physmap[i + 1] < MiB(1)) {
			boot_address = trunc_page(physmap[i + 1]);
			if ((physmap[i + 1] - boot_address) < bootMP_size)
				boot_address -= round_page(bootMP_size);
			physmap[i + 1] = boot_address;
		} else {
			boot_address = round_page(physmap[i]);
			physmap[i] = boot_address + round_page(bootMP_size);
		}
		if (physmap[i] == physmap[i + 1] && *physmap_idx != 0) {
			memmove(&physmap[i], &physmap[i + 2],
			    sizeof(*physmap) * (*physmap_idx - i + 2));
			*physmap_idx -= 2;
		}
		break;
	}

	if (!allocated) {
		boot_address = basemem * 1024 - bootMP_size;
		if (bootverbose)
			printf(
"Cannot find enough space for the boot trampoline, placing it at %#x",
			    boot_address);
	}
}

/*
 * AP CPU's call this to initialize themselves.
 */
void
init_secondary_tail(void)
{
	u_int cpuid;

	pmap_activate_boot(vmspace_pmap(proc0.p_vmspace));

	/*
	 * On real hardware, switch to x2apic mode if possible.  Do it
	 * after aps_ready was signalled, to avoid manipulating the
	 * mode while BSP might still want to send some IPI to us
	 * (second startup IPI is ignored on modern hardware etc).
	 */
	lapic_xapic_mode();

	/* Initialize the PAT MSR. */
	pmap_init_pat();

	/* set up CPU registers and state */
	cpu_setregs();

	/* set up SSE/NX */
	initializecpu();

	/* set up FPU state on the AP */
#ifdef __amd64__
	fpuinit();
#else
	npxinit(false);
#endif

	if (cpu_ops.cpu_init)
		cpu_ops.cpu_init();

	/* A quick check from sanity claus */
	cpuid = PCPU_GET(cpuid);
	if (PCPU_GET(apic_id) != lapic_id()) {
		printf("SMP: cpuid = %d\n", cpuid);
		printf("SMP: actual apic_id = %d\n", lapic_id());
		printf("SMP: correct apic_id = %d\n", PCPU_GET(apic_id));
		panic("cpuid mismatch! boom!!");
	}

	/* Initialize curthread. */
	KASSERT(PCPU_GET(idlethread) != NULL, ("no idle thread"));
	PCPU_SET(curthread, PCPU_GET(idlethread));

	mtx_lock_spin(&ap_boot_mtx);

	mca_init();

	/* Init local apic for irq's */
	lapic_setup(1);

	/* Set memory range attributes for this CPU to match the BSP */
	mem_range_AP_init();

	smp_cpus++;

	CTR1(KTR_SMP, "SMP: AP CPU #%d Launched", cpuid);
	if (bootverbose)
		printf("SMP: AP CPU #%d Launched!\n", cpuid);
	else
		printf("%s%d%s", smp_cpus == 2 ? "Launching APs: " : "",
		    cpuid, smp_cpus == mp_ncpus ? "\n" : " ");

	/* Determine if we are a logical CPU. */
	if (cpu_info[PCPU_GET(apic_id)].cpu_hyperthread)
		CPU_SET(cpuid, &logical_cpus_mask);

	if (bootverbose)
		lapic_dump("AP");

	if (smp_cpus == mp_ncpus) {
		/* enable IPI's, tlb shootdown, freezes etc */
		atomic_store_rel_int(&smp_started, 1);
	}

#ifdef __amd64__
	/*
	 * Enable global pages TLB extension
	 * This also implicitly flushes the TLB 
	 */
	load_cr4(rcr4() | CR4_PGE);
	if (pmap_pcid_enabled)
		load_cr4(rcr4() | CR4_PCIDE);
	load_ds(_udatasel);
	load_es(_udatasel);
	load_fs(_ufssel);
#endif

	mtx_unlock_spin(&ap_boot_mtx);

	/* Wait until all the AP's are up. */
	while (atomic_load_acq_int(&smp_started) == 0)
		ia32_pause();

#ifndef EARLY_AP_STARTUP
	/* Start per-CPU event timers. */
	cpu_initclocks_ap();
#endif

	sched_throw(NULL);

	panic("scheduler returned us to %s", __func__);
	/* NOTREACHED */
}

static void
smp_after_idle_runnable(void *arg __unused)
{
	struct thread *idle_td;
	int cpu;

	for (cpu = 1; cpu < mp_ncpus; cpu++) {
		idle_td = pcpu_find(cpu)->pc_idlethread;
		while (atomic_load_int(&idle_td->td_lastcpu) == NOCPU &&
		    atomic_load_int(&idle_td->td_oncpu) == NOCPU)
			cpu_spinwait();
		kmem_free((vm_offset_t)bootstacks[cpu], kstack_pages *
		    PAGE_SIZE);
	}
}
SYSINIT(smp_after_idle_runnable, SI_SUB_SMP, SI_ORDER_ANY,
    smp_after_idle_runnable, NULL);

/*
 * We tell the I/O APIC code about all the CPUs we want to receive
 * interrupts.  If we don't want certain CPUs to receive IRQs we
 * can simply not tell the I/O APIC code about them in this function.
 * We also do not tell it about the BSP since it tells itself about
 * the BSP internally to work with UP kernels and on UP machines.
 */
void
set_interrupt_apic_ids(void)
{
	u_int i, apic_id;

	for (i = 0; i < MAXCPU; i++) {
		apic_id = cpu_apic_ids[i];
		if (apic_id == -1)
			continue;
		if (cpu_info[apic_id].cpu_bsp)
			continue;
		if (cpu_info[apic_id].cpu_disabled)
			continue;

		/* Don't let hyperthreads service interrupts. */
		if (cpu_info[apic_id].cpu_hyperthread)
			continue;

		intr_add_cpu(i);
	}
}


#ifdef COUNT_XINVLTLB_HITS
u_int xhits_gbl[MAXCPU];
u_int xhits_pg[MAXCPU];
u_int xhits_rng[MAXCPU];
static SYSCTL_NODE(_debug, OID_AUTO, xhits, CTLFLAG_RW, 0, "");
SYSCTL_OPAQUE(_debug_xhits, OID_AUTO, global, CTLFLAG_RW, &xhits_gbl,
    sizeof(xhits_gbl), "IU", "");
SYSCTL_OPAQUE(_debug_xhits, OID_AUTO, page, CTLFLAG_RW, &xhits_pg,
    sizeof(xhits_pg), "IU", "");
SYSCTL_OPAQUE(_debug_xhits, OID_AUTO, range, CTLFLAG_RW, &xhits_rng,
    sizeof(xhits_rng), "IU", "");

u_int ipi_global;
u_int ipi_page;
u_int ipi_range;
u_int ipi_range_size;
SYSCTL_INT(_debug_xhits, OID_AUTO, ipi_global, CTLFLAG_RW, &ipi_global, 0, "");
SYSCTL_INT(_debug_xhits, OID_AUTO, ipi_page, CTLFLAG_RW, &ipi_page, 0, "");
SYSCTL_INT(_debug_xhits, OID_AUTO, ipi_range, CTLFLAG_RW, &ipi_range, 0, "");
SYSCTL_INT(_debug_xhits, OID_AUTO, ipi_range_size, CTLFLAG_RW, &ipi_range_size,
    0, "");
#endif /* COUNT_XINVLTLB_HITS */

/*
 * Init and startup IPI.
 */
void
ipi_startup(int apic_id, int vector)
{

	/*
	 * This attempts to follow the algorithm described in the
	 * Intel Multiprocessor Specification v1.4 in section B.4.
	 * For each IPI, we allow the local APIC ~20us to deliver the
	 * IPI.  If that times out, we panic.
	 */

	/*
	 * first we do an INIT IPI: this INIT IPI might be run, resetting
	 * and running the target CPU. OR this INIT IPI might be latched (P5
	 * bug), CPU waiting for STARTUP IPI. OR this INIT IPI might be
	 * ignored.
	 */
	lapic_ipi_raw(APIC_DEST_DESTFLD | APIC_TRIGMOD_LEVEL |
	    APIC_LEVEL_ASSERT | APIC_DESTMODE_PHY | APIC_DELMODE_INIT, apic_id);
	lapic_ipi_wait(100);

	/* Explicitly deassert the INIT IPI. */
	lapic_ipi_raw(APIC_DEST_DESTFLD | APIC_TRIGMOD_LEVEL |
	    APIC_LEVEL_DEASSERT | APIC_DESTMODE_PHY | APIC_DELMODE_INIT,
	    apic_id);

	DELAY(10000);		/* wait ~10mS */

	/*
	 * next we do a STARTUP IPI: the previous INIT IPI might still be
	 * latched, (P5 bug) this 1st STARTUP would then terminate
	 * immediately, and the previously started INIT IPI would continue. OR
	 * the previous INIT IPI has already run. and this STARTUP IPI will
	 * run. OR the previous INIT IPI was ignored. and this STARTUP IPI
	 * will run.
	 */
	lapic_ipi_raw(APIC_DEST_DESTFLD | APIC_TRIGMOD_EDGE |
	    APIC_LEVEL_ASSERT | APIC_DESTMODE_PHY | APIC_DELMODE_STARTUP |
	    vector, apic_id);
	if (!lapic_ipi_wait(100))
		panic("Failed to deliver first STARTUP IPI to APIC %d",
		    apic_id);
	DELAY(200);		/* wait ~200uS */

	/*
	 * finally we do a 2nd STARTUP IPI: this 2nd STARTUP IPI should run IF
	 * the previous STARTUP IPI was cancelled by a latched INIT IPI. OR
	 * this STARTUP IPI will be ignored, as only ONE STARTUP IPI is
	 * recognized after hardware RESET or INIT IPI.
	 */
	lapic_ipi_raw(APIC_DEST_DESTFLD | APIC_TRIGMOD_EDGE |
	    APIC_LEVEL_ASSERT | APIC_DESTMODE_PHY | APIC_DELMODE_STARTUP |
	    vector, apic_id);
	if (!lapic_ipi_wait(100))
		panic("Failed to deliver second STARTUP IPI to APIC %d",
		    apic_id);

	DELAY(200);		/* wait ~200uS */
}

/*
 * Send an IPI to specified CPU handling the bitmap logic.
 */
void
ipi_send_cpu(int cpu, u_int ipi)
{
	u_int bitmap, old_pending, new_pending;

	KASSERT(cpu_apic_ids[cpu] != -1, ("IPI to non-existent CPU %d", cpu));

	if (IPI_IS_BITMAPED(ipi)) {
		bitmap = 1 << ipi;
		ipi = IPI_BITMAP_VECTOR;
		do {
			old_pending = cpu_ipi_pending[cpu];
			new_pending = old_pending | bitmap;
		} while  (!atomic_cmpset_int(&cpu_ipi_pending[cpu],
		    old_pending, new_pending));	
		if (old_pending)
			return;
	}
	lapic_ipi_vectored(ipi, cpu_apic_ids[cpu]);
}

void
ipi_bitmap_handler(struct trapframe frame)
{
	struct trapframe *oldframe;
	struct thread *td;
	int cpu = PCPU_GET(cpuid);
	u_int ipi_bitmap;

	critical_enter();
	td = curthread;
	td->td_intr_nesting_level++;
	oldframe = td->td_intr_frame;
	td->td_intr_frame = &frame;
	ipi_bitmap = atomic_readandclear_int(&cpu_ipi_pending[cpu]);
	if (ipi_bitmap & (1 << IPI_PREEMPT)) {
#ifdef COUNT_IPIS
		(*ipi_preempt_counts[cpu])++;
#endif
		sched_preempt(td);
	}
	if (ipi_bitmap & (1 << IPI_AST)) {
#ifdef COUNT_IPIS
		(*ipi_ast_counts[cpu])++;
#endif
		/* Nothing to do for AST */
	}
	if (ipi_bitmap & (1 << IPI_HARDCLOCK)) {
#ifdef COUNT_IPIS
		(*ipi_hardclock_counts[cpu])++;
#endif
		hardclockintr();
	}
	td->td_intr_frame = oldframe;
	td->td_intr_nesting_level--;
	critical_exit();
}

/*
 * send an IPI to a set of cpus.
 */
void
ipi_selected(cpuset_t cpus, u_int ipi)
{
	int cpu;

	/*
	 * IPI_STOP_HARD maps to a NMI and the trap handler needs a bit
	 * of help in order to understand what is the source.
	 * Set the mask of receiving CPUs for this purpose.
	 */
	if (ipi == IPI_STOP_HARD)
		CPU_OR_ATOMIC(&ipi_stop_nmi_pending, &cpus);

	while ((cpu = CPU_FFS(&cpus)) != 0) {
		cpu--;
		CPU_CLR(cpu, &cpus);
		CTR3(KTR_SMP, "%s: cpu: %d ipi: %x", __func__, cpu, ipi);
		ipi_send_cpu(cpu, ipi);
	}
}

/*
 * send an IPI to a specific CPU.
 */
void
ipi_cpu(int cpu, u_int ipi)
{

	/*
	 * IPI_STOP_HARD maps to a NMI and the trap handler needs a bit
	 * of help in order to understand what is the source.
	 * Set the mask of receiving CPUs for this purpose.
	 */
	if (ipi == IPI_STOP_HARD)
		CPU_SET_ATOMIC(cpu, &ipi_stop_nmi_pending);

	CTR3(KTR_SMP, "%s: cpu: %d ipi: %x", __func__, cpu, ipi);
	ipi_send_cpu(cpu, ipi);
}

/*
 * send an IPI to all CPUs EXCEPT myself
 */
void
ipi_all_but_self(u_int ipi)
{
	cpuset_t other_cpus;

	other_cpus = all_cpus;
	CPU_CLR(PCPU_GET(cpuid), &other_cpus);
	if (IPI_IS_BITMAPED(ipi)) {
		ipi_selected(other_cpus, ipi);
		return;
	}

	/*
	 * IPI_STOP_HARD maps to a NMI and the trap handler needs a bit
	 * of help in order to understand what is the source.
	 * Set the mask of receiving CPUs for this purpose.
	 */
	if (ipi == IPI_STOP_HARD)
		CPU_OR_ATOMIC(&ipi_stop_nmi_pending, &other_cpus);

	CTR2(KTR_SMP, "%s: ipi: %x", __func__, ipi);
	lapic_ipi_vectored(ipi, APIC_IPI_DEST_OTHERS);
}

int
ipi_nmi_handler(void)
{
	u_int cpuid;

	/*
	 * As long as there is not a simple way to know about a NMI's
	 * source, if the bitmask for the current CPU is present in
	 * the global pending bitword an IPI_STOP_HARD has been issued
	 * and should be handled.
	 */
	cpuid = PCPU_GET(cpuid);
	if (!CPU_ISSET(cpuid, &ipi_stop_nmi_pending))
		return (1);

	CPU_CLR_ATOMIC(cpuid, &ipi_stop_nmi_pending);
	cpustop_handler();
	return (0);
}

int nmi_kdb_lock;

void
nmi_call_kdb_smp(u_int type, struct trapframe *frame)
{
	int cpu;
	bool call_post;

	cpu = PCPU_GET(cpuid);
	if (atomic_cmpset_acq_int(&nmi_kdb_lock, 0, 1)) {
		nmi_call_kdb(cpu, type, frame);
		call_post = false;
	} else {
		savectx(&stoppcbs[cpu]);
		CPU_SET_ATOMIC(cpu, &stopped_cpus);
		while (!atomic_cmpset_acq_int(&nmi_kdb_lock, 0, 1))
			ia32_pause();
		call_post = true;
	}
	atomic_store_rel_int(&nmi_kdb_lock, 0);
	if (call_post)
		cpustop_handler_post(cpu);
}

/*
 * Handle an IPI_STOP by saving our current context and spinning until we
 * are resumed.
 */
void
cpustop_handler(void)
{
	u_int cpu;

	cpu = PCPU_GET(cpuid);

	savectx(&stoppcbs[cpu]);

	/* Indicate that we are stopped */
	CPU_SET_ATOMIC(cpu, &stopped_cpus);

	/* Wait for restart */
	while (!CPU_ISSET(cpu, &started_cpus))
	    ia32_pause();

	cpustop_handler_post(cpu);
}

static void
cpustop_handler_post(u_int cpu)
{

	CPU_CLR_ATOMIC(cpu, &started_cpus);
	CPU_CLR_ATOMIC(cpu, &stopped_cpus);

	/*
	 * We don't broadcast TLB invalidations to other CPUs when they are
	 * stopped. Hence, we clear the TLB before resuming.
	 */
	invltlb_glob();

#if defined(__amd64__) && defined(DDB)
	amd64_db_resume_dbreg();
#endif

	if (cpu == 0 && cpustop_restartfunc != NULL) {
		cpustop_restartfunc();
		cpustop_restartfunc = NULL;
	}
}

/*
 * Handle an IPI_SUSPEND by saving our current context and spinning until we
 * are resumed.
 */
void
cpususpend_handler(void)
{
	u_int cpu;

	mtx_assert(&smp_ipi_mtx, MA_NOTOWNED);

	cpu = PCPU_GET(cpuid);
	if (savectx(&susppcbs[cpu]->sp_pcb)) {
#ifdef __amd64__
		fpususpend(susppcbs[cpu]->sp_fpususpend);
#else
		npxsuspend(susppcbs[cpu]->sp_fpususpend);
#endif
		/*
		 * suspended_cpus is cleared shortly after each AP is restarted
		 * by a Startup IPI, so that the BSP can proceed to restarting
		 * the next AP.
		 *
		 * resuming_cpus gets cleared when the AP completes
		 * initialization after having been released by the BSP.
		 * resuming_cpus is probably not the best name for the
		 * variable, because it is actually a set of processors that
		 * haven't resumed yet and haven't necessarily started resuming.
		 *
		 * Note that suspended_cpus is meaningful only for ACPI suspend
		 * as it's not really used for Xen suspend since the APs are
		 * automatically restored to the running state and the correct
		 * context.  For the same reason resumectx is never called in
		 * that case.
		 */
		CPU_SET_ATOMIC(cpu, &suspended_cpus);
		CPU_SET_ATOMIC(cpu, &resuming_cpus);

		/*
		 * Invalidate the cache after setting the global status bits.
		 * The last AP to set its bit may end up being an Owner of the
		 * corresponding cache line in MOESI protocol.  The AP may be
		 * stopped before the cache line is written to the main memory.
		 */
		wbinvd();
	} else {
#ifdef __amd64__
		fpuresume(susppcbs[cpu]->sp_fpususpend);
#else
		npxresume(susppcbs[cpu]->sp_fpususpend);
#endif
		pmap_init_pat();
		initializecpu();
		PCPU_SET(switchtime, 0);
		PCPU_SET(switchticks, ticks);

		/* Indicate that we have restarted and restored the context. */
		CPU_CLR_ATOMIC(cpu, &suspended_cpus);
	}

	/* Wait for resume directive */
	while (!CPU_ISSET(cpu, &toresume_cpus))
		ia32_pause();

	/* Re-apply microcode updates. */
	ucode_reload();

#ifdef __i386__
	/* Finish removing the identity mapping of low memory for this AP. */
	invltlb_glob();
#endif

	if (cpu_ops.cpu_resume)
		cpu_ops.cpu_resume();
#ifdef __amd64__
	if (vmm_resume_p)
		vmm_resume_p();
#endif

	/* Resume MCA and local APIC */
	lapic_xapic_mode();
	mca_resume();
	lapic_setup(0);

	/* Indicate that we are resumed */
	CPU_CLR_ATOMIC(cpu, &resuming_cpus);
	CPU_CLR_ATOMIC(cpu, &suspended_cpus);
	CPU_CLR_ATOMIC(cpu, &toresume_cpus);
}


void
invlcache_handler(void)
{
	uint32_t generation;

#ifdef COUNT_IPIS
	(*ipi_invlcache_counts[PCPU_GET(cpuid)])++;
#endif /* COUNT_IPIS */

	/*
	 * Reading the generation here allows greater parallelism
	 * since wbinvd is a serializing instruction.  Without the
	 * temporary, we'd wait for wbinvd to complete, then the read
	 * would execute, then the dependent write, which must then
	 * complete before return from interrupt.
	 */
	generation = smp_tlb_generation;
	wbinvd();
	PCPU_SET(smp_tlb_done, generation);
}

/*
 * This is called once the rest of the system is up and running and we're
 * ready to let the AP's out of the pen.
 */
static void
release_aps(void *dummy __unused)
{

	if (mp_ncpus == 1) 
		return;
	atomic_store_rel_int(&aps_ready, 1);
	while (smp_started == 0)
		ia32_pause();
}
SYSINIT(start_aps, SI_SUB_SMP, SI_ORDER_FIRST, release_aps, NULL);

#ifdef COUNT_IPIS
/*
 * Setup interrupt counters for IPI handlers.
 */
static void
mp_ipi_intrcnt(void *dummy)
{
	char buf[64];
	int i;

	CPU_FOREACH(i) {
		snprintf(buf, sizeof(buf), "cpu%d:invltlb", i);
		intrcnt_add(buf, &ipi_invltlb_counts[i]);
		snprintf(buf, sizeof(buf), "cpu%d:invlrng", i);
		intrcnt_add(buf, &ipi_invlrng_counts[i]);
		snprintf(buf, sizeof(buf), "cpu%d:invlpg", i);
		intrcnt_add(buf, &ipi_invlpg_counts[i]);
		snprintf(buf, sizeof(buf), "cpu%d:invlcache", i);
		intrcnt_add(buf, &ipi_invlcache_counts[i]);
		snprintf(buf, sizeof(buf), "cpu%d:preempt", i);
		intrcnt_add(buf, &ipi_preempt_counts[i]);
		snprintf(buf, sizeof(buf), "cpu%d:ast", i);
		intrcnt_add(buf, &ipi_ast_counts[i]);
		snprintf(buf, sizeof(buf), "cpu%d:rendezvous", i);
		intrcnt_add(buf, &ipi_rendezvous_counts[i]);
		snprintf(buf, sizeof(buf), "cpu%d:hardclock", i);
		intrcnt_add(buf, &ipi_hardclock_counts[i]);
	}		
}
SYSINIT(mp_ipi_intrcnt, SI_SUB_INTR, SI_ORDER_MIDDLE, mp_ipi_intrcnt, NULL);
#endif

/*
 * Flush the TLB on other CPU's
 */

/* Variables needed for SMP tlb shootdown. */
vm_offset_t smp_tlb_addr1, smp_tlb_addr2;
pmap_t smp_tlb_pmap;
volatile uint32_t smp_tlb_generation;

#ifdef __amd64__
#define	read_eflags() read_rflags()
#endif

static void
smp_targeted_tlb_shootdown(cpuset_t mask, u_int vector, pmap_t pmap,
    vm_offset_t addr1, vm_offset_t addr2)
{
	cpuset_t other_cpus;
	volatile uint32_t *p_cpudone;
	uint32_t generation;
	int cpu;

	/* It is not necessary to signal other CPUs while in the debugger. */
	if (kdb_active || panicstr != NULL)
		return;

	/*
	 * Check for other cpus.  Return if none.
	 */
	if (CPU_ISFULLSET(&mask)) {
		if (mp_ncpus <= 1)
			return;
	} else {
		CPU_CLR(PCPU_GET(cpuid), &mask);
		if (CPU_EMPTY(&mask))
			return;
	}

	if (!(read_eflags() & PSL_I))
		panic("%s: interrupts disabled", __func__);
	mtx_lock_spin(&smp_ipi_mtx);
	smp_tlb_addr1 = addr1;
	smp_tlb_addr2 = addr2;
	smp_tlb_pmap = pmap;
	generation = ++smp_tlb_generation;
	if (CPU_ISFULLSET(&mask)) {
		ipi_all_but_self(vector);
		other_cpus = all_cpus;
		CPU_CLR(PCPU_GET(cpuid), &other_cpus);
	} else {
		other_cpus = mask;
		while ((cpu = CPU_FFS(&mask)) != 0) {
			cpu--;
			CPU_CLR(cpu, &mask);
			CTR3(KTR_SMP, "%s: cpu: %d ipi: %x", __func__,
			    cpu, vector);
			ipi_send_cpu(cpu, vector);
		}
	}
	while ((cpu = CPU_FFS(&other_cpus)) != 0) {
		cpu--;
		CPU_CLR(cpu, &other_cpus);
		p_cpudone = &cpuid_to_pcpu[cpu]->pc_smp_tlb_done;
		while (*p_cpudone != generation)
			ia32_pause();
	}
	mtx_unlock_spin(&smp_ipi_mtx);
}

void
smp_masked_invltlb(cpuset_t mask, pmap_t pmap)
{

	if (smp_started) {
		smp_targeted_tlb_shootdown(mask, IPI_INVLTLB, pmap, 0, 0);
#ifdef COUNT_XINVLTLB_HITS
		ipi_global++;
#endif
	}
}

void
smp_masked_invlpg(cpuset_t mask, vm_offset_t addr, pmap_t pmap)
{

	if (smp_started) {
		smp_targeted_tlb_shootdown(mask, IPI_INVLPG, pmap, addr, 0);
#ifdef COUNT_XINVLTLB_HITS
		ipi_page++;
#endif
	}
}

void
smp_masked_invlpg_range(cpuset_t mask, vm_offset_t addr1, vm_offset_t addr2,
    pmap_t pmap)
{

	if (smp_started) {
		smp_targeted_tlb_shootdown(mask, IPI_INVLRNG, pmap,
		    addr1, addr2);
#ifdef COUNT_XINVLTLB_HITS
		ipi_range++;
		ipi_range_size += (addr2 - addr1) / PAGE_SIZE;
#endif
	}
}

void
smp_cache_flush(void)
{

	if (smp_started) {
		smp_targeted_tlb_shootdown(all_cpus, IPI_INVLCACHE, NULL,
		    0, 0);
	}
}

/*
 * Handlers for TLB related IPIs
 */
void
invltlb_handler(void)
{
	uint32_t generation;
  
#ifdef COUNT_XINVLTLB_HITS
	xhits_gbl[PCPU_GET(cpuid)]++;
#endif /* COUNT_XINVLTLB_HITS */
#ifdef COUNT_IPIS
	(*ipi_invltlb_counts[PCPU_GET(cpuid)])++;
#endif /* COUNT_IPIS */

	/*
	 * Reading the generation here allows greater parallelism
	 * since invalidating the TLB is a serializing operation.
	 */
	generation = smp_tlb_generation;
	if (smp_tlb_pmap == kernel_pmap)
		invltlb_glob();
#ifdef __amd64__
	else
		invltlb();
#endif
	PCPU_SET(smp_tlb_done, generation);
}

void
invlpg_handler(void)
{
	uint32_t generation;

#ifdef COUNT_XINVLTLB_HITS
	xhits_pg[PCPU_GET(cpuid)]++;
#endif /* COUNT_XINVLTLB_HITS */
#ifdef COUNT_IPIS
	(*ipi_invlpg_counts[PCPU_GET(cpuid)])++;
#endif /* COUNT_IPIS */

	generation = smp_tlb_generation;	/* Overlap with serialization */
#ifdef __i386__
	if (smp_tlb_pmap == kernel_pmap)
#endif
		invlpg(smp_tlb_addr1);
	PCPU_SET(smp_tlb_done, generation);
}

void
invlrng_handler(void)
{
	vm_offset_t addr, addr2;
	uint32_t generation;

#ifdef COUNT_XINVLTLB_HITS
	xhits_rng[PCPU_GET(cpuid)]++;
#endif /* COUNT_XINVLTLB_HITS */
#ifdef COUNT_IPIS
	(*ipi_invlrng_counts[PCPU_GET(cpuid)])++;
#endif /* COUNT_IPIS */

	addr = smp_tlb_addr1;
	addr2 = smp_tlb_addr2;
	generation = smp_tlb_generation;	/* Overlap with serialization */
#ifdef __i386__
	if (smp_tlb_pmap == kernel_pmap)
#endif
		do {
			invlpg(addr);
			addr += PAGE_SIZE;
		} while (addr < addr2);

	PCPU_SET(smp_tlb_done, generation);
}
