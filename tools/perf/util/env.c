// SPDX-License-Identifier: GPL-2.0
#include "cpumap.h"
#include "dwarf-regs.h"
#include "debug.h"
#include "env.h"
#include "util/header.h"
#include "util/rwsem.h"
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/rbtree.h>
#include <linux/string.h>
#include <linux/zalloc.h>
#include "cgroup.h"
#include <errno.h>
#include <sys/utsname.h>
#include <stdlib.h>
#include <string.h>
#include "pmu.h"
#include "pmus.h"
#include "strbuf.h"
#include "trace/beauty/beauty.h"

#ifdef HAVE_LIBBPF_SUPPORT
#include "bpf-event.h"
#include "bpf-utils.h"
#include <bpf/libbpf.h>

bool perf_env__insert_bpf_prog_info(struct perf_env *env,
				    struct bpf_prog_info_node *info_node)
{
	bool ret;

	down_write(&env->bpf_progs.lock);
	ret = __perf_env__insert_bpf_prog_info(env, info_node);
	up_write(&env->bpf_progs.lock);

	return ret;
}

bool __perf_env__insert_bpf_prog_info(struct perf_env *env, struct bpf_prog_info_node *info_node)
{
	__u32 prog_id = info_node->info_linear->info.id;
	struct bpf_prog_info_node *node;
	struct rb_node *parent = NULL;
	struct rb_node **p;

	p = &env->bpf_progs.infos.rb_node;

	while (*p != NULL) {
		parent = *p;
		node = rb_entry(parent, struct bpf_prog_info_node, rb_node);
		if (prog_id < node->info_linear->info.id) {
			p = &(*p)->rb_left;
		} else if (prog_id > node->info_linear->info.id) {
			p = &(*p)->rb_right;
		} else {
			pr_debug("duplicated bpf prog info %u\n", prog_id);
			return false;
		}
	}

	rb_link_node(&info_node->rb_node, parent, p);
	rb_insert_color(&info_node->rb_node, &env->bpf_progs.infos);
	env->bpf_progs.infos_cnt++;
	return true;
}

struct bpf_prog_info_node *perf_env__find_bpf_prog_info(struct perf_env *env,
							__u32 prog_id)
{
	struct bpf_prog_info_node *node = NULL;
	struct rb_node *n;

	down_read(&env->bpf_progs.lock);
	n = env->bpf_progs.infos.rb_node;

	while (n) {
		node = rb_entry(n, struct bpf_prog_info_node, rb_node);
		if (prog_id < node->info_linear->info.id)
			n = n->rb_left;
		else if (prog_id > node->info_linear->info.id)
			n = n->rb_right;
		else
			goto out;
	}
	node = NULL;

out:
	up_read(&env->bpf_progs.lock);
	return node;
}

void perf_env__iterate_bpf_prog_info(struct perf_env *env,
				     void (*cb)(struct bpf_prog_info_node *node,
						void *data),
				     void *data)
{
	struct rb_node *first;

	down_read(&env->bpf_progs.lock);
	first = rb_first(&env->bpf_progs.infos);
	for (struct rb_node *node = first; node != NULL; node = rb_next(node))
		(*cb)(rb_entry(node, struct bpf_prog_info_node, rb_node), data);
	up_read(&env->bpf_progs.lock);
}

bool perf_env__insert_btf(struct perf_env *env, struct btf_node *btf_node)
{
	bool ret;

	down_write(&env->bpf_progs.lock);
	ret = __perf_env__insert_btf(env, btf_node);
	up_write(&env->bpf_progs.lock);
	return ret;
}

bool __perf_env__insert_btf(struct perf_env *env, struct btf_node *btf_node)
{
	struct rb_node *parent = NULL;
	__u32 btf_id = btf_node->id;
	struct btf_node *node;
	struct rb_node **p;

	p = &env->bpf_progs.btfs.rb_node;

	while (*p != NULL) {
		parent = *p;
		node = rb_entry(parent, struct btf_node, rb_node);
		if (btf_id < node->id) {
			p = &(*p)->rb_left;
		} else if (btf_id > node->id) {
			p = &(*p)->rb_right;
		} else {
			pr_debug("duplicated btf %u\n", btf_id);
			return false;
		}
	}

	rb_link_node(&btf_node->rb_node, parent, p);
	rb_insert_color(&btf_node->rb_node, &env->bpf_progs.btfs);
	env->bpf_progs.btfs_cnt++;
	return true;
}

struct btf_node *perf_env__find_btf(struct perf_env *env, __u32 btf_id)
{
	struct btf_node *res;

	down_read(&env->bpf_progs.lock);
	res = __perf_env__find_btf(env, btf_id);
	up_read(&env->bpf_progs.lock);
	return res;
}

struct btf_node *__perf_env__find_btf(struct perf_env *env, __u32 btf_id)
{
	struct btf_node *node = NULL;
	struct rb_node *n;

	n = env->bpf_progs.btfs.rb_node;

	while (n) {
		node = rb_entry(n, struct btf_node, rb_node);
		if (btf_id < node->id)
			n = n->rb_left;
		else if (btf_id > node->id)
			n = n->rb_right;
		else
			return node;
	}
	return NULL;
}

/* purge data in bpf_progs.infos tree */
static void perf_env__purge_bpf(struct perf_env *env)
{
	struct rb_root *root;
	struct rb_node *next;

	down_write(&env->bpf_progs.lock);

	root = &env->bpf_progs.infos;
	next = rb_first(root);

	while (next) {
		struct bpf_prog_info_node *node;

		node = rb_entry(next, struct bpf_prog_info_node, rb_node);
		next = rb_next(&node->rb_node);
		rb_erase(&node->rb_node, root);
		zfree(&node->info_linear);
		bpf_metadata_free(node->metadata);
		free(node);
	}

	env->bpf_progs.infos_cnt = 0;

	root = &env->bpf_progs.btfs;
	next = rb_first(root);

	while (next) {
		struct btf_node *node;

		node = rb_entry(next, struct btf_node, rb_node);
		next = rb_next(&node->rb_node);
		rb_erase(&node->rb_node, root);
		free(node);
	}

	env->bpf_progs.btfs_cnt = 0;

	up_write(&env->bpf_progs.lock);
}
#else // HAVE_LIBBPF_SUPPORT
static void perf_env__purge_bpf(struct perf_env *env __maybe_unused)
{
}
#endif // HAVE_LIBBPF_SUPPORT

void free_cpu_domain_info(struct cpu_domain_map **cd_map, u32 schedstat_version, u32 nr)
{
	if (!cd_map)
		return;

	for (u32 i = 0; i < nr; i++) {
		if (!cd_map[i])
			continue;

		for (u32 j = 0; j < cd_map[i]->nr_domains; j++) {
			struct domain_info *d_info = cd_map[i]->domains[j];

			if (!d_info)
				continue;

			if (schedstat_version >= 17)
				zfree(&d_info->dname);

			zfree(&d_info->cpumask);
			zfree(&d_info->cpulist);
			zfree(&d_info);
		}
		zfree(&cd_map[i]->domains);
		zfree(&cd_map[i]);
	}
	zfree(&cd_map);
}

void perf_env__exit(struct perf_env *env)
{
	int i, j;

	mutex_destroy(&env->lock);

	perf_env__purge_bpf(env);
	perf_env__purge_cgroups(env);
	zfree(&env->hostname);
	zfree(&env->os_release);
	zfree(&env->version);
	zfree(&env->arch);
	zfree(&env->cpu_desc);
	zfree(&env->cpuid);
	zfree(&env->cmdline);
	zfree(&env->cmdline_argv);
	zfree(&env->sibling_dies);
	zfree(&env->sibling_cores);
	zfree(&env->sibling_threads);
	zfree(&env->pmu_mappings);
	zfree(&env->cpu);
	for (i = 0; i < env->nr_cpu_pmu_caps; i++)
		zfree(&env->cpu_pmu_caps[i]);
	zfree(&env->cpu_pmu_caps);
	zfree(&env->numa_map);

	for (i = 0; i < env->nr_numa_nodes; i++)
		perf_cpu_map__put(env->numa_nodes[i].map);
	zfree(&env->numa_nodes);

	for (i = 0; i < env->caches_cnt; i++)
		cpu_cache_level__free(&env->caches[i]);
	zfree(&env->caches);

	for (i = 0; i < env->nr_memory_nodes; i++)
		zfree(&env->memory_nodes[i].set);
	zfree(&env->memory_nodes);

	for (i = 0; i < env->nr_hybrid_nodes; i++) {
		zfree(&env->hybrid_nodes[i].pmu_name);
		zfree(&env->hybrid_nodes[i].cpus);
	}
	zfree(&env->hybrid_nodes);

	for (i = 0; i < env->nr_pmus_with_caps; i++) {
		for (j = 0; j < env->pmu_caps[i].nr_caps; j++)
			zfree(&env->pmu_caps[i].caps[j]);
		zfree(&env->pmu_caps[i].caps);
		zfree(&env->pmu_caps[i].pmu_name);
	}
	zfree(&env->pmu_caps);
	free_cpu_domain_info(env->cpu_domain, env->schedstat_version, env->nr_cpus_avail);
}

void perf_env__init(struct perf_env *env)
{
	memset(env, 0, sizeof(*env));
#ifdef HAVE_LIBBPF_SUPPORT
	env->bpf_progs.infos = RB_ROOT;
	env->bpf_progs.btfs = RB_ROOT;
	init_rwsem(&env->bpf_progs.lock);
#endif
	env->kernel_is_64_bit = -1;
	mutex_init(&env->lock);
}

static void perf_env__init_kernel_mode(struct perf_env *env)
{
	const char *arch = env->arch;

	if (!arch) {
		static struct utsname uts = { .machine[0] = '\0', };

		if (uts.machine[0] == '\0')
			uname(&uts);
		if (uts.machine[0] != '\0')
			arch = uts.machine;
	}

	if (arch) {
		if (strstr(arch, "64") || strstr(arch, "s390x"))
			env->kernel_is_64_bit = 1;
		else
			env->kernel_is_64_bit = 0;
		return;
	}

	/* Fallback if completely unresolvable (assume host-bitness) */
	env->kernel_is_64_bit = (sizeof(void *) == 8) ? 1 : 0;
}

int perf_env__kernel_is_64_bit(struct perf_env *env)
{
	if (env->kernel_is_64_bit == -1)
		perf_env__init_kernel_mode(env);

	return env->kernel_is_64_bit;
}

bool perf_arch_is_big_endian(const char *arch)
{
	if (!arch)
		return __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__;

	if (str_ends_with(arch, "_be") || !strcmp(arch, "sparc") || !strcmp(arch, "sparc64") ||
	    !strcmp(arch, "s390") || !strcmp(arch, "s390x") || !strcmp(arch, "powerpc") ||
	    !strcmp(arch, "ppc") || !strcmp(arch, "ppc64") ||
	    !strcmp(arch, "mips") || !strcmp(arch, "mips64") || !strcmp(arch, "parisc") ||
	    !strcmp(arch, "parisc64") || !strcmp(arch, "m68k") ||
	    !strcmp(arch, "armeb") || !strcmp(arch, "mipseb") || !strcmp(arch, "mips64eb"))
		return true;

	return false;
}

const char *perf_env__os_release(struct perf_env *env)
{
	struct utsname uts;
	int ret;
	const char *release;

	if (!env)
		return perf_version_string;

	mutex_lock(&env->lock);
	if (env->os_release) {
		release = env->os_release;
		goto out;
	}

	/*
	 * If env->arch is set, this is an offline target environment.
	 * If the os_release is not populated in the file, we do not want
	 * to poison it with the host's release which would break guest checks.
	 */
	if (env->arch) {
		release = NULL;
		goto out;
	}

	/*
	 * The os_release is being accessed but wasn't initialized from a data
	 * file, assume this is 'live' mode and use the release from uname. If
	 * uname or strdup fails then use the current perf tool version.
	 */
	ret = uname(&uts);
	env->os_release = strdup(ret < 0 ? perf_version_string : uts.release);
	release = env->os_release ?: perf_version_string;
out:
	mutex_unlock(&env->lock);
	return release;
}

int perf_env__set_cmdline(struct perf_env *env, int argc, const char *argv[])
{
	int i;

	/* do not include NULL termination */
	env->cmdline_argv = calloc(argc, sizeof(char *));
	if (env->cmdline_argv == NULL)
		goto out_enomem;

	/*
	 * Must copy argv contents because it gets moved around during option
	 * parsing:
	 */
	for (i = 0; i < argc ; i++) {
		env->cmdline_argv[i] = argv[i];
		if (env->cmdline_argv[i] == NULL)
			goto out_free;
	}

	env->nr_cmdline = argc;

	return 0;
out_free:
	zfree(&env->cmdline_argv);
out_enomem:
	return -ENOMEM;
}

int perf_env__read_cpu_topology_map(struct perf_env *env)
{
	int idx, nr_cpus;

	if (env->cpu != NULL)
		return 0;

	if (env->nr_cpus_avail == 0)
		env->nr_cpus_avail = cpu__max_present_cpu().cpu;

	nr_cpus = env->nr_cpus_avail;
	if (nr_cpus == -1)
		return -EINVAL;

	env->cpu = calloc(nr_cpus, sizeof(env->cpu[0]));
	if (env->cpu == NULL)
		return -ENOMEM;

	for (idx = 0; idx < nr_cpus; ++idx) {
		struct perf_cpu cpu = { .cpu = idx };
		int core_id   = cpu__get_core_id(cpu);
		int socket_id = cpu__get_socket_id(cpu);
		int die_id    = cpu__get_die_id(cpu);

		env->cpu[idx].core_id	= core_id >= 0 ? core_id : -1;
		env->cpu[idx].socket_id	= socket_id >= 0 ? socket_id : -1;
		env->cpu[idx].die_id	= die_id >= 0 ? die_id : -1;
	}

	env->nr_cpus_avail = nr_cpus;
	return 0;
}

int perf_env__read_pmu_mappings(struct perf_env *env)
{
	struct perf_pmu *pmu = NULL;
	u32 pmu_num = 0;
	struct strbuf sb;

	while ((pmu = perf_pmus__scan(pmu)))
		pmu_num++;

	if (!pmu_num) {
		pr_debug("pmu mappings not available\n");
		return -ENOENT;
	}
	env->nr_pmu_mappings = pmu_num;

	if (strbuf_init(&sb, 128 * pmu_num) < 0)
		return -ENOMEM;

	while ((pmu = perf_pmus__scan(pmu))) {
		if (strbuf_addf(&sb, "%u:%s", pmu->type, pmu->name) < 0)
			goto error;
		/* include a NULL character at the end */
		if (strbuf_add(&sb, "", 1) < 0)
			goto error;
	}

	env->pmu_mappings = strbuf_detach(&sb, NULL);

	return 0;

error:
	strbuf_release(&sb);
	return -1;
}

int perf_env__read_cpuid(struct perf_env *env)
{
	char cpuid[128];
	struct perf_cpu cpu = {-1};
	int err = get_cpuid(cpuid, sizeof(cpuid), cpu);

	if (err)
		return err;

	free(env->cpuid);
	env->cpuid = strdup(cpuid);
	if (env->cpuid == NULL)
		return ENOMEM;
	return 0;
}

static int perf_env__read_nr_cpus_avail(struct perf_env *env)
{
	if (env->nr_cpus_avail == 0)
		env->nr_cpus_avail = cpu__max_present_cpu().cpu;

	return env->nr_cpus_avail ? 0 : -ENOENT;
}

static int __perf_env__read_core_pmu_caps(const struct perf_pmu *pmu,
					  int *nr_caps, char ***caps,
					  unsigned int *max_branches,
					  unsigned int *br_cntr_nr,
					  unsigned int *br_cntr_width)
{
	struct perf_pmu_caps *pcaps = NULL;
	char *ptr, **tmp;
	int ret = 0;

	*nr_caps = 0;
	*caps = NULL;

	if (!pmu->nr_caps)
		return 0;

	*caps = calloc(pmu->nr_caps, sizeof(char *));
	if (!*caps)
		return -ENOMEM;

	tmp = *caps;
	list_for_each_entry(pcaps, &pmu->caps, list) {
		if (asprintf(&ptr, "%s=%s", pcaps->name, pcaps->value) < 0) {
			ret = -ENOMEM;
			goto error;
		}

		*tmp++ = ptr;

		if (!strcmp(pcaps->name, "branches"))
			*max_branches = atoi(pcaps->value);
		else if (!strcmp(pcaps->name, "branch_counter_nr"))
			*br_cntr_nr = atoi(pcaps->value);
		else if (!strcmp(pcaps->name, "branch_counter_width"))
			*br_cntr_width = atoi(pcaps->value);
	}
	*nr_caps = pmu->nr_caps;
	return 0;
error:
	while (tmp-- != *caps)
		zfree(tmp);
	zfree(caps);
	*nr_caps = 0;
	return ret;
}

int perf_env__read_core_pmu_caps(struct perf_env *env)
{
	struct pmu_caps *pmu_caps;
	struct perf_pmu *pmu = NULL;
	int nr_pmu, i = 0, j;
	int ret;

	nr_pmu = perf_pmus__num_core_pmus();

	if (!nr_pmu)
		return -ENODEV;

	if (nr_pmu == 1) {
		pmu = perf_pmus__find_core_pmu();
		if (!pmu)
			return -ENODEV;
		ret = perf_pmu__caps_parse(pmu);
		if (ret < 0)
			return ret;
		return __perf_env__read_core_pmu_caps(pmu, &env->nr_cpu_pmu_caps,
						      &env->cpu_pmu_caps,
						      &env->max_branches,
						      &env->br_cntr_nr,
						      &env->br_cntr_width);
	}

	pmu_caps = calloc(nr_pmu, sizeof(*pmu_caps));
	if (!pmu_caps)
		return -ENOMEM;

	while ((pmu = perf_pmus__scan_core(pmu)) != NULL) {
		if (perf_pmu__caps_parse(pmu) <= 0)
			continue;
		ret = __perf_env__read_core_pmu_caps(pmu, &pmu_caps[i].nr_caps,
						     &pmu_caps[i].caps,
						     &pmu_caps[i].max_branches,
						     &pmu_caps[i].br_cntr_nr,
						     &pmu_caps[i].br_cntr_width);
		if (ret)
			goto error;

		pmu_caps[i].pmu_name = strdup(pmu->name);
		if (!pmu_caps[i].pmu_name) {
			ret = -ENOMEM;
			goto error;
		}
		i++;
	}

	env->nr_pmus_with_caps = nr_pmu;
	env->pmu_caps = pmu_caps;

	return 0;
error:
	for (i = 0; i < nr_pmu; i++) {
		for (j = 0; j < pmu_caps[i].nr_caps; j++)
			zfree(&pmu_caps[i].caps[j]);
		zfree(&pmu_caps[i].caps);
		zfree(&pmu_caps[i].pmu_name);
	}
	zfree(&pmu_caps);
	return ret;
}

int perf_env__nr_cpus_avail(struct perf_env *env)
{
	return env && !perf_env__read_nr_cpus_avail(env) ? env->nr_cpus_avail : 0;
}

void cpu_cache_level__free(struct cpu_cache_level *cache)
{
	zfree(&cache->type);
	zfree(&cache->map);
	zfree(&cache->size);
}

struct arch_to_e_machine {
	const char *prefix;
	uint16_t e_machine;
};

/*
 * A mapping from an arch prefix string to an ELF machine that can be used in a
 * bsearch. Some arch prefixes are shared an need additional processing as
 * marked next to the architecture. The prefixes handle both perf's architecture
 * naming and those from uname.
 */
static const struct arch_to_e_machine prefix_to_e_machine[] = {
	{"aarch64", EM_AARCH64},
	{"alpha", EM_ALPHA},
	{"arc", EM_ARC},
	{"arm", EM_ARM}, /* Check also for EM_AARCH64. */
	{"avr", EM_AVR},  /* Check also for EM_AVR32. */
	{"bfin", EM_BLACKFIN},
	{"blackfin", EM_BLACKFIN},
	{"cris", EM_CRIS},
	{"csky", EM_CSKY},
	{"hppa", EM_PARISC},
	{"i386", EM_386},
	{"i486", EM_386},
	{"i586", EM_386},
	{"i686", EM_386},
	{"loongarch", EM_LOONGARCH},
	{"m32r", EM_M32R},
	{"m68k", EM_68K},
	{"microblaze", EM_MICROBLAZE},
	{"mips", EM_MIPS},
	{"msp430", EM_MSP430},
	{"parisc", EM_PARISC},
	{"powerpc", EM_PPC}, /* Check also for EM_PPC64. */
	{"ppc", EM_PPC}, /* Check also for EM_PPC64. */
	{"riscv", EM_RISCV},
	{"s390", EM_S390},
	{"sa110", EM_ARM},
	{"sh", EM_SH},
	{"sparc", EM_SPARC}, /* Check also for EM_SPARCV9. */
	{"sun4u", EM_SPARC},
	{"x86", EM_X86_64}, /* Check also for EM_386. */
	{"xtensa", EM_XTENSA},
};

static int compare_prefix(const void *key, const void *element)
{
	const char *search_key = key;
	const struct arch_to_e_machine *map_element = element;
	size_t prefix_len = strlen(map_element->prefix);

	return strncmp(search_key, map_element->prefix, prefix_len);
}

static uint16_t perf_arch_to_e_machine(const char *perf_arch, int is_64_bit)
{
	/* Binary search for a matching prefix. */
	const struct arch_to_e_machine *result;

	if (!perf_arch)
		return EM_HOST;

	result = bsearch(perf_arch,
			 prefix_to_e_machine, ARRAY_SIZE(prefix_to_e_machine),
			 sizeof(prefix_to_e_machine[0]),
			 compare_prefix);

	if (!result) {
		pr_debug("Unknown perf arch for ELF machine mapping: %s\n", perf_arch);
		return EM_NONE;
	}

	/*
	 * Handle conflicting prefixes. If the is_64_bit is unknown (-1) then
	 * assume 64-bit. We can't use perf_env__kernel_is_64_bit as that
	 * depends on the arch string.
	 */
	switch (result->e_machine) {
	case EM_ARM:
		return !strcmp(perf_arch, "arm64") || !strcmp(perf_arch, "aarch64")
			? EM_AARCH64 : EM_ARM;
	case EM_AVR:
		return !strcmp(perf_arch, "avr32") ? EM_AVR32 : EM_AVR;
	case EM_PPC:
		if (is_64_bit == 1)
			return EM_PPC64;
		if (is_64_bit == 0)
			return EM_PPC;
		return strstarts(perf_arch, "ppc64") ? EM_PPC64 : EM_PPC;
	case EM_SPARC:
		if (is_64_bit == 1)
			return EM_SPARCV9;
		if (is_64_bit == 0)
			return EM_SPARC;
		return !strcmp(perf_arch, "sparc64") || !strcmp(perf_arch, "sun4u")
			? EM_SPARCV9 : EM_SPARC;
	case EM_X86_64:
		if (is_64_bit == 1)
			return EM_X86_64;
		if (is_64_bit == 0)
			return EM_386;
		return !strcmp(perf_arch, "x86_64") || !strcmp(perf_arch, "x86")
			? EM_X86_64 : EM_386;
	default:
		return result->e_machine;
	}
}

static const char *e_machine_to_perf_arch(uint16_t e_machine)
{
	/*
	 * Table for if either the perf arch string differs from uname or there
	 * are >1 ELF machine with the prefix.
	 */
	static const struct arch_to_e_machine extras[] = {
		{"arm64", EM_AARCH64},
		{"avr32", EM_AVR32},
		{"powerpc", EM_PPC},
		{"powerpc", EM_PPC64},
		{"sparc", EM_SPARCV9},
		{"x86", EM_386},
		{"x86", EM_X86_64},
		{"none", EM_NONE},
	};

	for (size_t i = 0; i < ARRAY_SIZE(extras); i++) {
		if (extras[i].e_machine == e_machine)
			return extras[i].prefix;
	}

	for (size_t i = 0; i < ARRAY_SIZE(prefix_to_e_machine); i++) {
		if (prefix_to_e_machine[i].e_machine == e_machine)
			return prefix_to_e_machine[i].prefix;

	}
	return "unknown";
}

uint16_t perf_env__e_machine_nocache(struct perf_env *env, uint32_t *e_flags)
{
	uint16_t e_machine = EM_NONE;
	const char *arch = NULL;
	int is_64_bit = -1;

	if (e_flags)
		*e_flags = 0;

	if (env) {
		arch = env->arch;
		is_64_bit = env->kernel_is_64_bit;
	}

	if (!arch) {
		static struct utsname uts = { .machine[0] = '\0', };

		if (uts.machine[0] == '\0')
			uname(&uts);
		if (uts.machine[0] != '\0')
			arch = uts.machine;
	}

	e_machine = perf_arch_to_e_machine(arch, is_64_bit);

	if (e_flags)
		*e_flags = (e_machine == EM_HOST) ? EF_HOST : 0;

	return e_machine;
}

uint16_t perf_env__e_machine(struct perf_env *env, uint32_t *e_flags)
{
	uint16_t e_machine;
	uint32_t local_e_flags = 0;

	if (env && env->e_machine != EM_NONE) {
		if (e_flags)
			*e_flags = env->e_flags;

		return env->e_machine;
	}
	e_machine = perf_env__e_machine_nocache(env, &local_e_flags);
	/*
	 * Only cache the e_machine in perf_env if env->arch is not NULL.
	 * If env->arch is NULL, the e_machine is just a fallback to EM_HOST.
	 * Caching it permanently would prevent dynamic, more accurate
	 * thread-based session e_machine scanning later in
	 * perf_session__e_machine().
	 */
	if (env && env->arch) {
		env->e_machine = e_machine;
		env->e_flags = local_e_flags;
	}
	if (e_flags)
		*e_flags = local_e_flags;

	return e_machine;
}

const char *perf_env__arch(struct perf_env *env)
{
	uint16_t e_machine;
	const char *arch;

	if (!env) {
		static struct utsname uts = { .machine[0] = '\0', };
		uint16_t host_e_machine;

		if (uts.machine[0] == '\0')
			uname(&uts);
		if (uts.machine[0] != '\0') {
			host_e_machine = perf_arch_to_e_machine(uts.machine, -1);
			return e_machine_to_perf_arch(host_e_machine);
		}
		return e_machine_to_perf_arch(EM_HOST);
	}

	/*
	 * Lazily compute/allocate arch. The e_machine may have been
	 * read from a data file and so may not be EM_HOST.
	 */
	e_machine = perf_env__e_machine(env, /*e_flags=*/NULL);
	arch = e_machine_to_perf_arch(e_machine);

	if (e_machine == EM_RISCV && perf_env__kernel_is_64_bit(env) == 1)
		arch = "riscv64";
	else if (e_machine == EM_MIPS && perf_env__kernel_is_64_bit(env) == 1)
		arch = "mips64";
	else if (e_machine == EM_PARISC && perf_env__kernel_is_64_bit(env) == 1)
		arch = "parisc64";

	return arch;
}

const char *arch_syscalls__strerrno(uint16_t e_machine, int err);

const char *perf_env__arch_strerrno(uint16_t e_machine, int err)
{
	return arch_syscalls__strerrno(e_machine, err);
}

const char *perf_env__cpuid(struct perf_env *env)
{
	int status;

	if (!env->cpuid) { /* Assume local operation */
		status = perf_env__read_cpuid(env);
		if (status)
			return NULL;
	}

	return env->cpuid;
}

int perf_env__nr_pmu_mappings(struct perf_env *env)
{
	int status;

	if (!env->nr_pmu_mappings) { /* Assume local operation */
		status = perf_env__read_pmu_mappings(env);
		if (status)
			return 0;
	}

	return env->nr_pmu_mappings;
}

const char *perf_env__pmu_mappings(struct perf_env *env)
{
	int status;

	if (!env->pmu_mappings) { /* Assume local operation */
		status = perf_env__read_pmu_mappings(env);
		if (status)
			return NULL;
	}

	return env->pmu_mappings;
}

int perf_env__numa_node(struct perf_env *env, struct perf_cpu cpu)
{
	if (!env->nr_numa_map) {
		struct numa_node *nn;
		int i, nr = 0;

		for (i = 0; i < env->nr_numa_nodes; i++) {
			nn = &env->numa_nodes[i];
			nr = max(nr, (int)perf_cpu_map__max(nn->map).cpu);
		}

		nr++;

		/*
		 * We initialize the numa_map array to prepare
		 * it for missing cpus, which return node -1
		 */
		env->numa_map = malloc(nr * sizeof(int));
		if (!env->numa_map)
			return -1;

		for (i = 0; i < nr; i++)
			env->numa_map[i] = -1;

		env->nr_numa_map = nr;

		for (i = 0; i < env->nr_numa_nodes; i++) {
			struct perf_cpu tmp;
			unsigned int j;

			nn = &env->numa_nodes[i];
			perf_cpu_map__for_each_cpu(tmp, j, nn->map)
				env->numa_map[tmp.cpu] = i;
		}
	}

	return cpu.cpu >= 0 && cpu.cpu < env->nr_numa_map ? env->numa_map[cpu.cpu] : -1;
}

bool perf_env__has_pmu_mapping(struct perf_env *env, const char *pmu_name)
{
	char *pmu_mapping = env->pmu_mappings, *colon;

	for (int i = 0; i < env->nr_pmu_mappings; ++i) {
		if (strtoul(pmu_mapping, &colon, 0) == ULONG_MAX || *colon != ':')
			goto out_error;

		pmu_mapping = colon + 1;
		if (strcmp(pmu_mapping, pmu_name) == 0)
			return true;

		pmu_mapping += strlen(pmu_mapping) + 1;
	}
out_error:
	return false;
}

char *perf_env__find_pmu_cap(struct perf_env *env, const char *pmu_name,
			     const char *cap)
{
	char *cap_eq;
	int cap_size;
	char **ptr;
	int i, j;

	if (!pmu_name || !cap)
		return NULL;

	cap_size = strlen(cap);
	cap_eq = zalloc(cap_size + 2);
	if (!cap_eq)
		return NULL;

	memcpy(cap_eq, cap, cap_size);
	cap_eq[cap_size] = '=';

	if (!strcmp(pmu_name, "cpu")) {
		for (i = 0; i < env->nr_cpu_pmu_caps; i++) {
			if (!strncmp(env->cpu_pmu_caps[i], cap_eq, cap_size + 1)) {
				free(cap_eq);
				return &env->cpu_pmu_caps[i][cap_size + 1];
			}
		}
		goto out;
	}

	for (i = 0; i < env->nr_pmus_with_caps; i++) {
		if (strcmp(env->pmu_caps[i].pmu_name, pmu_name))
			continue;

		ptr = env->pmu_caps[i].caps;

		for (j = 0; j < env->pmu_caps[i].nr_caps; j++) {
			if (!strncmp(ptr[j], cap_eq, cap_size + 1)) {
				free(cap_eq);
				return &ptr[j][cap_size + 1];
			}
		}
	}

out:
	free(cap_eq);
	return NULL;
}

void perf_env__find_br_cntr_info(struct perf_env *env,
				 unsigned int *nr,
				 unsigned int *width)
{
	if (nr) {
		*nr = env->cpu_pmu_caps ? env->br_cntr_nr :
					  env->pmu_caps->br_cntr_nr;
	}

	if (width) {
		*width = env->cpu_pmu_caps ? env->br_cntr_width :
					     env->pmu_caps->br_cntr_width;
	}
}

bool perf_env__is_x86_amd_cpu(struct perf_env *env)
{
	static int is_amd; /* 0: Uninitialized, 1: Yes, -1: No */

	if (is_amd == 0)
		is_amd = env->cpuid && strstarts(env->cpuid, "AuthenticAMD") ? 1 : -1;

	return is_amd >= 1 ? true : false;
}

bool x86__is_amd_cpu(void)
{
	struct perf_env env = { .total_mem = 0, };
	bool is_amd;

	perf_env__init(&env);
	perf_env__cpuid(&env);
	is_amd = perf_env__is_x86_amd_cpu(&env);
	perf_env__exit(&env);

	return is_amd;
}

bool perf_env__is_x86_intel_cpu(struct perf_env *env)
{
	static int is_intel; /* 0: Uninitialized, 1: Yes, -1: No */

	if (is_intel == 0)
		is_intel = env->cpuid && strstarts(env->cpuid, "GenuineIntel") ? 1 : -1;

	return is_intel >= 1 ? true : false;
}

bool x86__is_intel_cpu(void)
{
	struct perf_env env = { .total_mem = 0, };
	bool is_intel;

	perf_env__init(&env);
	perf_env__cpuid(&env);
	is_intel = perf_env__is_x86_intel_cpu(&env);
	perf_env__exit(&env);

	return is_intel;
}
