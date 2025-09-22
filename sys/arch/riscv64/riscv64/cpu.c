/*	$OpenBSD: cpu.c,v 1.21 2024/11/10 06:51:59 jsg Exp $	*/

/*
 * Copyright (c) 2016 Dale Rahn <drahn@dalerahn.com>
 * Copyright (c) 2017 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/atomic.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/sysctl.h>
#include <sys/task.h>

#include <uvm/uvm_extern.h>

#include <machine/cpufunc.h>
#include <machine/elf.h>
#include <machine/fdt.h>
#include <machine/sbi.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_regulator.h>
#include <dev/ofw/ofw_thermal.h>
#include <dev/ofw/fdt.h>

/* CPU Identification */
#define CPU_VENDOR_SIFIVE	0x489
#define CPU_VENDOR_THEAD	0x5b7

/* SiFive */
#define CPU_ARCH_U5		0x0000000000000001
#define CPU_ARCH_U7		0x8000000000000007

/* Architectures */
struct arch {
	uint64_t	id;
	char		*name;
};

struct arch cpu_arch_none[] = {
	{ 0, NULL }
};

struct arch cpu_arch_sifive[] = {
	{ CPU_ARCH_U5, "U5" },
	{ CPU_ARCH_U7, "U7" },
	{ 0, NULL }
};

/* Vendors */
const struct vendor {
	uint32_t	id;
	char		*name;
	struct arch	*archlist;
} cpu_vendors[] = {
	{ CPU_VENDOR_SIFIVE, "SiFive", cpu_arch_sifive },
	{ CPU_VENDOR_THEAD, "T-Head", cpu_arch_none },
	{ 0, NULL }
};

char cpu_model[64];
int cpu_node;

struct cpu_info *cpu_info_list = &cpu_info_primary;

int	cpu_match(struct device *, void *, void *);
void	cpu_attach(struct device *, struct device *, void *);

const struct cfattach cpu_ca = {
	sizeof(struct device), cpu_match, cpu_attach
};

struct cfdriver cpu_cd = {
	NULL, "cpu", DV_DULL
};

int cpu_errata_sifive_cip_1200;

void	cpu_opp_init(struct cpu_info *, uint32_t);

void	thead_dcache_wbinv_range(paddr_t, psize_t);
void	thead_dcache_inv_range(paddr_t, psize_t);
void	thead_dcache_wb_range(paddr_t, psize_t);

size_t	thead_dcache_line_size;

void
cpu_identify(struct cpu_info *ci)
{
	char isa[32];
	uint64_t marchid, mimpid;
	uint32_t mvendorid;
	const char *vendor_name = NULL;
	const char *arch_name = NULL;
	struct arch *archlist = cpu_arch_none;
	int i, len;

	mvendorid = sbi_get_mvendorid();
	marchid = sbi_get_marchid();
	mimpid = sbi_get_mimpid();

	for (i = 0; cpu_vendors[i].name; i++) {
		if (mvendorid == cpu_vendors[i].id) {
			vendor_name = cpu_vendors[i].name;
			archlist = cpu_vendors[i].archlist;
			break;
		}
	}

	for (i = 0; archlist[i].name; i++) {
		if (marchid == archlist[i].id) {
			arch_name = archlist[i].name;
			break;
		}
	}

	if (vendor_name)
		printf(": %s", vendor_name);
	else
		printf(": vendor %x", mvendorid);
	if (arch_name)
		printf(" %s", arch_name);
	else
		printf(" arch %llx", marchid);
	printf(" imp %llx", mimpid);

	len = OF_getprop(ci->ci_node, "riscv,isa", isa, sizeof(isa));
	if (len != -1) {
		printf(" %s", isa);
		strlcpy(cpu_model, isa, sizeof(cpu_model));
	}
	printf("\n");

	/* Handle errata. */
	if (mvendorid == CPU_VENDOR_SIFIVE && marchid == CPU_ARCH_U7)
		cpu_errata_sifive_cip_1200 = 1;
	if (mvendorid == CPU_VENDOR_THEAD && marchid == 0 && mimpid == 0) {
		cpu_dcache_wbinv_range = thead_dcache_wbinv_range;
		cpu_dcache_inv_range = thead_dcache_inv_range;
		cpu_dcache_wb_range = thead_dcache_wb_range;
		thead_dcache_line_size =
		    OF_getpropint(ci->ci_node, "d-cache-block-size", 64);
	}
}

#ifdef MULTIPROCESSOR
int	cpu_hatch_secondary(struct cpu_info *ci);
#endif
int	cpu_clockspeed(int *);

int
cpu_match(struct device *parent, void *cfdata, void *aux)
{
	struct fdt_attach_args *faa = aux;
	char buf[32];

	if (OF_getprop(faa->fa_node, "device_type", buf, sizeof(buf)) <= 0 ||
	    strcmp(buf, "cpu") != 0)
		return 0;

	if (ncpus < MAXCPUS || faa->fa_reg[0].addr == boot_hart) /* the primary cpu */
		return 1;

	return 0;
}

void
cpu_attach(struct device *parent, struct device *dev, void *aux)
{
	struct fdt_attach_args *faa = aux;
	struct cpu_info *ci;
	int node, level;
	uint32_t opp;

	KASSERT(faa->fa_nreg > 0);

#ifdef MULTIPROCESSOR
	if (faa->fa_reg[0].addr == boot_hart) {
		ci = &cpu_info_primary;
		ci->ci_flags |= CPUF_RUNNING | CPUF_PRESENT | CPUF_PRIMARY;
		csr_set(sie, SIE_SSIE);
	} else {
		ci = malloc(sizeof(*ci), M_DEVBUF, M_WAITOK | M_ZERO);
		cpu_info[dev->dv_unit] = ci;
		ci->ci_next = cpu_info_list->ci_next;
		cpu_info_list->ci_next = ci;
		ci->ci_flags |= CPUF_AP;
		ncpus++;
	}
#else
	ci = &cpu_info_primary;
#endif

	ci->ci_dev = dev;
	ci->ci_cpuid = dev->dv_unit;
	ci->ci_hartid = faa->fa_reg[0].addr;
	ci->ci_node = faa->fa_node;
	ci->ci_self = ci;

#ifdef MULTIPROCESSOR
	if (ci->ci_flags & CPUF_AP) {
		int timeout = 10000;

		clockqueue_init(&ci->ci_queue);
		sched_init_cpu(ci);
		if (cpu_hatch_secondary(ci)) {
			atomic_setbits_int(&ci->ci_flags, CPUF_IDENTIFY);
			membar_producer();

			while ((ci->ci_flags & CPUF_IDENTIFIED) == 0 &&
			    --timeout)
				delay(1000);
			if (timeout == 0) {
				printf(" failed to identify");
				ci->ci_flags = 0;
			}
		} else {
			printf(" failed to spin up");
			ci->ci_flags = 0;
		}
	} else {
#endif
		cpu_identify(ci);

		hwcap |= HWCAP_ISA_G | HWCAP_ISA_C;

		if (OF_getproplen(ci->ci_node, "clocks") > 0) {
			cpu_node = ci->ci_node;
			cpu_cpuspeed = cpu_clockspeed;
		}

		/*
		 * attach cpu's children node, so far there is only the
		 * cpu-embedded interrupt controller
		 */
		struct fdt_attach_args	 fa_intc;
		for (node = OF_child(faa->fa_node); node; node = OF_peer(node)) {
			fa_intc.fa_node = node;
			/* no specifying match func, will call cfdata's match func*/
			config_found(dev, &fa_intc, NULL);
		}

#ifdef MULTIPROCESSOR
	}
#endif

	opp = OF_getpropint(ci->ci_node, "operating-points-v2", 0);
	if (opp)
		cpu_opp_init(ci, opp);

	node = faa->fa_node;

	level = 1;

	while (node) {
		const char *unit = "KB";
		uint32_t line, iline, dline;
		uint32_t size, isize, dsize;
		uint32_t ways, iways, dways;
		uint32_t cache;

		line = OF_getpropint(node, "cache-block-size", 0);
		size = OF_getpropint(node, "cache-size", 0);
		ways = OF_getpropint(node, "cache-sets", 0);
		iline = OF_getpropint(node, "i-cache-block-size", line);
		isize = OF_getpropint(node, "i-cache-size", size) / 1024;
		iways = OF_getpropint(node, "i-cache-sets", ways);
		dline = OF_getpropint(node, "d-cache-block-size", line);
		dsize = OF_getpropint(node, "d-cache-size", size) / 1024;
		dways = OF_getpropint(node, "d-cache-sets", ways);

		if (isize == 0 && dsize == 0)
			break;

		/* Print large cache sizes in MB. */
		if (isize > 4096 && dsize > 4096) {
			unit = "MB";
			isize /= 1024;
			dsize /= 1024;
		}

		printf("%s:", dev->dv_xname);
		
		if (OF_getproplen(node, "cache-unified") == 0) {
			printf(" %d%s %db/line %d-way L%d cache",
			    isize, unit, iline, iways, level);
		} else {
			printf(" %d%s %db/line %d-way L%d I-cache",
			    isize, unit, iline, iways, level);
			printf(", %d%s %db/line %d-way L%d D-cache",
			    dsize, unit, dline, dways, level);
		}

		cache = OF_getpropint(node, "next-level-cache", 0);
		node = OF_getnodebyphandle(cache);
		level++;

		printf("\n");
	}
}

int
cpu_clockspeed(int *freq)
{
	*freq = clock_get_frequency(cpu_node, NULL) / 1000000;
	return 0;
}

void
cpu_cache_nop_range(paddr_t pa, psize_t len)
{
}

void
thead_dcache_wbinv_range(paddr_t pa, psize_t len)
{
	paddr_t end, mask;

	mask = thead_dcache_line_size - 1;
	end = (pa + len + mask) & ~mask;
	pa &= ~mask;

	while (pa != end) {
		/* th.dcache.cipa a0 */
		__asm volatile ("mv a0, %0; .long 0x02b5000b" :: "r"(pa)
		    : "a0", "memory");
		pa += thead_dcache_line_size;
	}
	/* th.sync.s */
	__asm volatile (".long 0x0190000b" ::: "memory");
}

void
thead_dcache_inv_range(paddr_t pa, psize_t len)
{
	paddr_t end, mask;

	mask = thead_dcache_line_size - 1;
	end = (pa + len + mask) & ~mask;
	pa &= ~mask;

	while (pa != end) {
		/* th.dcache.ipa a0 */
		__asm volatile ("mv a0, %0; .long 0x02a5000b" :: "r"(pa)
		    : "a0", "memory");
		pa += thead_dcache_line_size;
	}
	/* th.sync.s */
	__asm volatile (".long 0x0190000b" ::: "memory");
}

void
thead_dcache_wb_range(paddr_t pa, psize_t len)
{
	paddr_t end, mask;

	mask = thead_dcache_line_size - 1;
	end = (pa + len + mask) & ~mask;
	pa &= ~mask;

	while (pa != end) {
		/* th.dcache.cpa a0 */
		__asm volatile ("mv a0, %0; .long 0x0295000b" :: "r"(pa)
		    : "a0", "memory");
		pa += thead_dcache_line_size;
	}
	/* th.sync.s */
	__asm volatile (".long 0x0190000b" ::: "memory");
}

void (*cpu_dcache_wbinv_range)(paddr_t, psize_t) = cpu_cache_nop_range;
void (*cpu_dcache_inv_range)(paddr_t, psize_t) = cpu_cache_nop_range;
void (*cpu_dcache_wb_range)(paddr_t, psize_t) = cpu_cache_nop_range;

#ifdef MULTIPROCESSOR

void	cpu_hatch(void);

void
cpu_boot_secondary(struct cpu_info *ci)
{
	atomic_setbits_int(&ci->ci_flags, CPUF_GO);
	membar_producer();

	while ((ci->ci_flags & CPUF_RUNNING) == 0)
		CPU_BUSY_CYCLE();
}

void
cpu_boot_secondary_processors(void)
{
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;

	CPU_INFO_FOREACH(cii, ci) {
		if ((ci->ci_flags & CPUF_AP) == 0)
			continue;
		if (ci->ci_flags & CPUF_PRIMARY)
			continue;

		ci->ci_randseed = (arc4random() & 0x7fffffff) + 1;
		cpu_boot_secondary(ci);
	}
}

int
cpu_hatch_secondary(struct cpu_info *ci)
{
	paddr_t start_addr, a1;
	void *kstack;
	int error;

	kstack = km_alloc(USPACE, &kv_any, &kp_zero, &kd_waitok);
	ci->ci_initstack_end = (vaddr_t)kstack + USPACE - 16;

	pmap_extract(pmap_kernel(), (vaddr_t)cpu_hatch, &start_addr);
	pmap_extract(pmap_kernel(), (vaddr_t)ci, &a1);

	ci->ci_satp = pmap_kernel()->pm_satp;

	error = sbi_hsm_hart_start(ci->ci_hartid, start_addr, a1);
	return (error == SBI_SUCCESS);
}

void cpu_startclock(void);

void
cpu_start_secondary(void)
{
	struct cpu_info *ci = curcpu();
	int s;

	ci->ci_flags |= CPUF_PRESENT;
	membar_producer();

	while ((ci->ci_flags & CPUF_IDENTIFY) == 0)
		membar_consumer();

	cpu_identify(ci);

	atomic_setbits_int(&ci->ci_flags, CPUF_IDENTIFIED);
	membar_producer();

	while ((ci->ci_flags & CPUF_GO) == 0)
		membar_consumer();

	s = splhigh();
	riscv_intr_cpu_enable();
	cpu_startclock();

	csr_clear(sstatus, SSTATUS_FS_MASK);
	csr_set(sie, SIE_SSIE);

	atomic_setbits_int(&ci->ci_flags, CPUF_RUNNING);
	membar_producer();

	spllower(IPL_NONE);
	intr_enable();

	sched_toidle();
}

void
cpu_kick(struct cpu_info *ci)
{
	if (ci != curcpu())
		intr_send_ipi(ci, IPI_NOP);
}

void
cpu_unidle(struct cpu_info *ci)
{
	if (ci != curcpu())
		intr_send_ipi(ci, IPI_NOP);
}

#endif

/*
 * Dynamic voltage and frequency scaling implementation.
 */

extern int perflevel;

struct opp {
	uint64_t opp_hz;
	uint32_t opp_microvolt;
};

struct opp_table {
	LIST_ENTRY(opp_table) ot_list;
	uint32_t ot_phandle;

	struct opp *ot_opp;
	u_int ot_nopp;
	uint64_t ot_opp_hz_min;
	uint64_t ot_opp_hz_max;

	struct cpu_info *ot_master;
};

LIST_HEAD(, opp_table) opp_tables = LIST_HEAD_INITIALIZER(opp_tables);
struct task cpu_opp_task;

void	cpu_opp_mountroot(struct device *);
void	cpu_opp_dotask(void *);
void	cpu_opp_setperf(int);

uint32_t cpu_opp_get_cooling_level(void *, uint32_t *);
void	cpu_opp_set_cooling_level(void *, uint32_t *, uint32_t);

void
cpu_opp_init(struct cpu_info *ci, uint32_t phandle)
{
	struct opp_table *ot;
	struct cooling_device *cd;
	int count, node, child;
	uint32_t opp_hz, opp_microvolt;
	uint32_t values[3];
	int i, j, len;

	LIST_FOREACH(ot, &opp_tables, ot_list) {
		if (ot->ot_phandle == phandle) {
			ci->ci_opp_table = ot;
			return;
		}
	}

	node = OF_getnodebyphandle(phandle);
	if (node == 0)
		return;

	if (!OF_is_compatible(node, "operating-points-v2"))
		return;

	count = 0;
	for (child = OF_child(node); child != 0; child = OF_peer(child)) {
		if (OF_getproplen(child, "turbo-mode") == 0)
			continue;
		count++;
	}
	if (count == 0)
		return;

	ot = malloc(sizeof(struct opp_table), M_DEVBUF, M_ZERO | M_WAITOK);
	ot->ot_phandle = phandle;
	ot->ot_opp = mallocarray(count, sizeof(struct opp),
	    M_DEVBUF, M_ZERO | M_WAITOK);
	ot->ot_nopp = count;

	count = 0;
	for (child = OF_child(node); child != 0; child = OF_peer(child)) {
		if (OF_getproplen(child, "turbo-mode") == 0)
			continue;
		opp_hz = OF_getpropint64(child, "opp-hz", 0);
		len = OF_getpropintarray(child, "opp-microvolt",
		    values, sizeof(values));
		opp_microvolt = 0;
		if (len == sizeof(uint32_t) || len == 3 * sizeof(uint32_t))
			opp_microvolt = values[0];

		/* Insert into the array, keeping things sorted. */
		for (i = 0; i < count; i++) {
			if (opp_hz < ot->ot_opp[i].opp_hz)
				break;
		}
		for (j = count; j > i; j--)
			ot->ot_opp[j] = ot->ot_opp[j - 1];
		ot->ot_opp[i].opp_hz = opp_hz;
		ot->ot_opp[i].opp_microvolt = opp_microvolt;
		count++;
	}

	ot->ot_opp_hz_min = ot->ot_opp[0].opp_hz;
	ot->ot_opp_hz_max = ot->ot_opp[count - 1].opp_hz;

	if (OF_getproplen(node, "opp-shared") == 0)
		ot->ot_master = ci;

	LIST_INSERT_HEAD(&opp_tables, ot, ot_list);

	ci->ci_opp_table = ot;
	ci->ci_opp_idx = -1;
	ci->ci_opp_max = ot->ot_nopp - 1;
	ci->ci_cpu_supply = OF_getpropint(ci->ci_node, "cpu-supply", 0);

	cd = malloc(sizeof(struct cooling_device), M_DEVBUF, M_ZERO | M_WAITOK);
	cd->cd_node = ci->ci_node;
	cd->cd_cookie = ci;
	cd->cd_get_level = cpu_opp_get_cooling_level;
	cd->cd_set_level = cpu_opp_set_cooling_level;
	cooling_device_register(cd);

	/*
	 * Do additional checks at mountroot when all the clocks and
	 * regulators are available.
	 */
	config_mountroot(ci->ci_dev, cpu_opp_mountroot);
}

void
cpu_opp_mountroot(struct device *self)
{
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;
	int count = 0;
	int level = 0;

	if (cpu_setperf)
		return;

	CPU_INFO_FOREACH(cii, ci) {
		struct opp_table *ot = ci->ci_opp_table;
		uint64_t curr_hz;
		uint32_t curr_microvolt;
		int error;

		if (ot == NULL)
			continue;

		/* Skip if this table is shared and we're not the master. */
		if (ot->ot_master && ot->ot_master != ci)
			continue;

		/* PWM regulators may need to be explicitly enabled. */
		regulator_enable(ci->ci_cpu_supply);

		curr_hz = clock_get_frequency(ci->ci_node, NULL);
		curr_microvolt = regulator_get_voltage(ci->ci_cpu_supply);

		/* Disable if clock isn't implemented. */
		error = ENODEV;
		if (curr_hz != 0)
			error = clock_set_frequency(ci->ci_node, NULL, curr_hz);
		if (error) {
			ci->ci_opp_table = NULL;
			printf("%s: clock not implemented\n",
			       ci->ci_dev->dv_xname);
			continue;
		}

		/* Disable if regulator isn't implemented. */
		error = ci->ci_cpu_supply ? ENODEV : 0;
		if (ci->ci_cpu_supply && curr_microvolt != 0)
			error = regulator_set_voltage(ci->ci_cpu_supply,
			    curr_microvolt);
		if (error) {
			ci->ci_opp_table = NULL;
			printf("%s: regulator not implemented\n",
			    ci->ci_dev->dv_xname);
			continue;
		}

		/*
		 * Initialize performance level based on the current
		 * speed of the first CPU that supports DVFS.
		 */
		if (level == 0) {
			uint64_t min, max;
			uint64_t level_hz;

			min = ot->ot_opp_hz_min;
			max = ot->ot_opp_hz_max;
			level_hz = clock_get_frequency(ci->ci_node, NULL);
			if (level_hz < min)
				level_hz = min;
			if (level_hz > max)
				level_hz = max;
			level = howmany(100 * (level_hz - min), (max - min));
		}

		count++;
	}

	if (count > 0) {
		task_set(&cpu_opp_task, cpu_opp_dotask, NULL);
		cpu_setperf = cpu_opp_setperf;

		perflevel = (level > 0) ? level : 0;
		cpu_setperf(perflevel);
	}
}

void
cpu_opp_dotask(void *arg)
{
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;

	CPU_INFO_FOREACH(cii, ci) {
		struct opp_table *ot = ci->ci_opp_table;
		uint64_t curr_hz, opp_hz;
		uint32_t curr_microvolt, opp_microvolt;
		int opp_idx;
		int error = 0;

		if (ot == NULL)
			continue;

		/* Skip if this table is shared and we're not the master. */
		if (ot->ot_master && ot->ot_master != ci)
			continue;

		opp_idx = MIN(ci->ci_opp_idx, ci->ci_opp_max);
		opp_hz = ot->ot_opp[opp_idx].opp_hz;
		opp_microvolt = ot->ot_opp[opp_idx].opp_microvolt;

		curr_hz = clock_get_frequency(ci->ci_node, NULL);
		curr_microvolt = regulator_get_voltage(ci->ci_cpu_supply);

		if (error == 0 && opp_hz < curr_hz)
			error = clock_set_frequency(ci->ci_node, NULL, opp_hz);
		if (error == 0 && ci->ci_cpu_supply &&
		    opp_microvolt != 0 && opp_microvolt != curr_microvolt) {
			error = regulator_set_voltage(ci->ci_cpu_supply,
			    opp_microvolt);
		}
		if (error == 0 && opp_hz > curr_hz)
			error = clock_set_frequency(ci->ci_node, NULL, opp_hz);

		if (error)
			printf("%s: DVFS failed\n", ci->ci_dev->dv_xname);
	}
}

void
cpu_opp_setperf(int level)
{
	struct cpu_info *ci;
	CPU_INFO_ITERATOR cii;
	int update = 0;

	CPU_INFO_FOREACH(cii, ci) {
		struct opp_table *ot = ci->ci_opp_table;
		uint64_t min, max;
		uint64_t level_hz, opp_hz;
		int opp_idx = -1;
		int i;

		if (ot == NULL)
			continue;

		/* Skip if this table is shared and we're not the master. */
		if (ot->ot_master && ot->ot_master != ci)
			continue;

		min = ot->ot_opp_hz_min;
		max = ot->ot_opp_hz_max;
		level_hz = min + (level * (max - min)) / 100;
		opp_hz = min;
		for (i = 0; i < ot->ot_nopp; i++) {
			if (ot->ot_opp[i].opp_hz <= level_hz &&
			    ot->ot_opp[i].opp_hz >= opp_hz)
				opp_hz = ot->ot_opp[i].opp_hz;
		}

		/* Find index of selected operating point. */
		for (i = 0; i < ot->ot_nopp; i++) {
			if (ot->ot_opp[i].opp_hz == opp_hz) {
				opp_idx = i;
				break;
			}
		}
		KASSERT(opp_idx >= 0);

		if (ci->ci_opp_idx != opp_idx) {
			ci->ci_opp_idx = opp_idx;
			update = 1;
		}
	}

	/*
	 * Update the hardware from a task since setting the
	 * regulators might need process context.
	 */
	if (update)
		task_add(systq, &cpu_opp_task);
}

uint32_t
cpu_opp_get_cooling_level(void *cookie, uint32_t *cells)
{
	struct cpu_info *ci = cookie;
	struct opp_table *ot = ci->ci_opp_table;
	
	return ot->ot_nopp - ci->ci_opp_max - 1;
}

void
cpu_opp_set_cooling_level(void *cookie, uint32_t *cells, uint32_t level)
{
	struct cpu_info *ci = cookie;
	struct opp_table *ot = ci->ci_opp_table;
	int opp_max;

	if (level > (ot->ot_nopp - 1))
		level = ot->ot_nopp - 1;

	opp_max = (ot->ot_nopp - level - 1);
	if (ci->ci_opp_max != opp_max) {
		ci->ci_opp_max = opp_max;
		task_add(systq, &cpu_opp_task);
	}
}
