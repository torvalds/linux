// SPDX-License-Identifier: GPL-2.0
#include "cpumap.h"
#include "debug.h"
#include "env.h"
#include "util/header.h"
#include <linux/ctype.h>
#include <linux/zalloc.h>
#include "cgroup.h"
#include <errno.h>
#include <sys/utsname.h>
#include <stdlib.h>
#include <string.h>
#include "strbuf.h"

struct perf_env perf_env;

#ifdef HAVE_LIBBPF_SUPPORT
#include "bpf-event.h"
#include "bpf-utils.h"
#include <bpf/libbpf.h>

void perf_env__insert_bpf_prog_info(struct perf_env *env,
				    struct bpf_prog_info_node *info_node)
{
	__u32 prog_id = info_node->info_linear->info.id;
	struct bpf_prog_info_node *node;
	struct rb_node *parent = NULL;
	struct rb_node **p;

	down_write(&env->bpf_progs.lock);
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
			goto out;
		}
	}

	rb_link_node(&info_node->rb_node, parent, p);
	rb_insert_color(&info_node->rb_node, &env->bpf_progs.infos);
	env->bpf_progs.infos_cnt++;
out:
	up_write(&env->bpf_progs.lock);
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

bool perf_env__insert_btf(struct perf_env *env, struct btf_node *btf_node)
{
	struct rb_node *parent = NULL;
	__u32 btf_id = btf_node->id;
	struct btf_node *node;
	struct rb_node **p;
	bool ret = true;

	down_write(&env->bpf_progs.lock);
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
			ret = false;
			goto out;
		}
	}

	rb_link_node(&btf_node->rb_node, parent, p);
	rb_insert_color(&btf_node->rb_node, &env->bpf_progs.btfs);
	env->bpf_progs.btfs_cnt++;
out:
	up_write(&env->bpf_progs.lock);
	return ret;
}

struct btf_node *perf_env__find_btf(struct perf_env *env, __u32 btf_id)
{
	struct btf_node *node = NULL;
	struct rb_node *n;

	down_read(&env->bpf_progs.lock);
	n = env->bpf_progs.btfs.rb_node;

	while (n) {
		node = rb_entry(n, struct btf_node, rb_node);
		if (btf_id < node->id)
			n = n->rb_left;
		else if (btf_id > node->id)
			n = n->rb_right;
		else
			goto out;
	}
	node = NULL;

out:
	up_read(&env->bpf_progs.lock);
	return node;
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
		free(node->info_linear);
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

void perf_env__exit(struct perf_env *env)
{
	int i;

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

	for (i = 0; i < env->nr_hybrid_cpc_nodes; i++) {
		zfree(&env->hybrid_cpc_nodes[i].cpu_pmu_caps);
		zfree(&env->hybrid_cpc_nodes[i].pmu_name);
	}
	zfree(&env->hybrid_cpc_nodes);
}

void perf_env__init(struct perf_env *env)
{
#ifdef HAVE_LIBBPF_SUPPORT
	env->bpf_progs.infos = RB_ROOT;
	env->bpf_progs.btfs = RB_ROOT;
	init_rwsem(&env->bpf_progs.lock);
#endif
	env->kernel_is_64_bit = -1;
}

static void perf_env__init_kernel_mode(struct perf_env *env)
{
	const char *arch = perf_env__raw_arch(env);

	if (!strncmp(arch, "x86_64", 6) || !strncmp(arch, "aarch64", 7) ||
	    !strncmp(arch, "arm64", 5) || !strncmp(arch, "mips64", 6) ||
	    !strncmp(arch, "parisc64", 8) || !strncmp(arch, "riscv64", 7) ||
	    !strncmp(arch, "s390x", 5) || !strncmp(arch, "sparc64", 7))
		env->kernel_is_64_bit = 1;
	else
		env->kernel_is_64_bit = 0;
}

int perf_env__kernel_is_64_bit(struct perf_env *env)
{
	if (env->kernel_is_64_bit == -1)
		perf_env__init_kernel_mode(env);

	return env->kernel_is_64_bit;
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
	int cpu, nr_cpus;

	if (env->cpu != NULL)
		return 0;

	if (env->nr_cpus_avail == 0)
		env->nr_cpus_avail = cpu__max_present_cpu();

	nr_cpus = env->nr_cpus_avail;
	if (nr_cpus == -1)
		return -EINVAL;

	env->cpu = calloc(nr_cpus, sizeof(env->cpu[0]));
	if (env->cpu == NULL)
		return -ENOMEM;

	for (cpu = 0; cpu < nr_cpus; ++cpu) {
		env->cpu[cpu].core_id	= cpu_map__get_core_id(cpu);
		env->cpu[cpu].socket_id	= cpu_map__get_socket_id(cpu);
		env->cpu[cpu].die_id	= cpu_map__get_die_id(cpu);
	}

	env->nr_cpus_avail = nr_cpus;
	return 0;
}

int perf_env__read_pmu_mappings(struct perf_env *env)
{
	struct perf_pmu *pmu = NULL;
	u32 pmu_num = 0;
	struct strbuf sb;

	while ((pmu = perf_pmu__scan(pmu))) {
		if (!pmu->name)
			continue;
		pmu_num++;
	}
	if (!pmu_num) {
		pr_debug("pmu mappings not available\n");
		return -ENOENT;
	}
	env->nr_pmu_mappings = pmu_num;

	if (strbuf_init(&sb, 128 * pmu_num) < 0)
		return -ENOMEM;

	while ((pmu = perf_pmu__scan(pmu))) {
		if (!pmu->name)
			continue;
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
	int err = get_cpuid(cpuid, sizeof(cpuid));

	if (err)
		return err;

	free(env->cpuid);
	env->cpuid = strdup(cpuid);
	if (env->cpuid == NULL)
		return ENOMEM;
	return 0;
}

static int perf_env__read_arch(struct perf_env *env)
{
	struct utsname uts;

	if (env->arch)
		return 0;

	if (!uname(&uts))
		env->arch = strdup(uts.machine);

	return env->arch ? 0 : -ENOMEM;
}

static int perf_env__read_nr_cpus_avail(struct perf_env *env)
{
	if (env->nr_cpus_avail == 0)
		env->nr_cpus_avail = cpu__max_present_cpu();

	return env->nr_cpus_avail ? 0 : -ENOENT;
}

const char *perf_env__raw_arch(struct perf_env *env)
{
	return env && !perf_env__read_arch(env) ? env->arch : "unknown";
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

/*
 * Return architecture name in a normalized form.
 * The conversion logic comes from the Makefile.
 */
static const char *normalize_arch(char *arch)
{
	if (!strcmp(arch, "x86_64"))
		return "x86";
	if (arch[0] == 'i' && arch[2] == '8' && arch[3] == '6')
		return "x86";
	if (!strcmp(arch, "sun4u") || !strncmp(arch, "sparc", 5))
		return "sparc";
	if (!strncmp(arch, "aarch64", 7) || !strncmp(arch, "arm64", 5))
		return "arm64";
	if (!strncmp(arch, "arm", 3) || !strcmp(arch, "sa110"))
		return "arm";
	if (!strncmp(arch, "s390", 4))
		return "s390";
	if (!strncmp(arch, "parisc", 6))
		return "parisc";
	if (!strncmp(arch, "powerpc", 7) || !strncmp(arch, "ppc", 3))
		return "powerpc";
	if (!strncmp(arch, "mips", 4))
		return "mips";
	if (!strncmp(arch, "sh", 2) && isdigit(arch[2]))
		return "sh";

	return arch;
}

const char *perf_env__arch(struct perf_env *env)
{
	char *arch_name;

	if (!env || !env->arch) { /* Assume local operation */
		static struct utsname uts = { .machine[0] = '\0', };
		if (uts.machine[0] == '\0' && uname(&uts) < 0)
			return NULL;
		arch_name = uts.machine;
	} else
		arch_name = env->arch;

	return normalize_arch(arch_name);
}

const char *perf_env__cpuid(struct perf_env *env)
{
	int status;

	if (!env || !env->cpuid) { /* Assume local operation */
		status = perf_env__read_cpuid(env);
		if (status)
			return NULL;
	}

	return env->cpuid;
}

int perf_env__nr_pmu_mappings(struct perf_env *env)
{
	int status;

	if (!env || !env->nr_pmu_mappings) { /* Assume local operation */
		status = perf_env__read_pmu_mappings(env);
		if (status)
			return 0;
	}

	return env->nr_pmu_mappings;
}

const char *perf_env__pmu_mappings(struct perf_env *env)
{
	int status;

	if (!env || !env->pmu_mappings) { /* Assume local operation */
		status = perf_env__read_pmu_mappings(env);
		if (status)
			return NULL;
	}

	return env->pmu_mappings;
}

int perf_env__numa_node(struct perf_env *env, int cpu)
{
	if (!env->nr_numa_map) {
		struct numa_node *nn;
		int i, nr = 0;

		for (i = 0; i < env->nr_numa_nodes; i++) {
			nn = &env->numa_nodes[i];
			nr = max(nr, perf_cpu_map__max(nn->map));
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
			int tmp, j;

			nn = &env->numa_nodes[i];
			perf_cpu_map__for_each_cpu(j, tmp, nn->map)
				env->numa_map[j] = i;
		}
	}

	return cpu >= 0 && cpu < env->nr_numa_map ? env->numa_map[cpu] : -1;
}
