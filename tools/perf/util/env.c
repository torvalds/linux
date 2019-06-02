// SPDX-License-Identifier: GPL-2.0
#include "cpumap.h"
#include "env.h"
#include "sane_ctype.h"
#include "util.h"
#include "bpf-event.h"
#include <errno.h>
#include <sys/utsname.h>
#include <bpf/libbpf.h>

struct perf_env perf_env;

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

void perf_env__insert_btf(struct perf_env *env, struct btf_node *btf_node)
{
	struct rb_node *parent = NULL;
	__u32 btf_id = btf_node->id;
	struct btf_node *node;
	struct rb_node **p;

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
			goto out;
		}
	}

	rb_link_node(&btf_node->rb_node, parent, p);
	rb_insert_color(&btf_node->rb_node, &env->bpf_progs.btfs);
	env->bpf_progs.btfs_cnt++;
out:
	up_write(&env->bpf_progs.lock);
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

void perf_env__exit(struct perf_env *env)
{
	int i;

	perf_env__purge_bpf(env);
	zfree(&env->hostname);
	zfree(&env->os_release);
	zfree(&env->version);
	zfree(&env->arch);
	zfree(&env->cpu_desc);
	zfree(&env->cpuid);
	zfree(&env->cmdline);
	zfree(&env->cmdline_argv);
	zfree(&env->sibling_cores);
	zfree(&env->sibling_threads);
	zfree(&env->pmu_mappings);
	zfree(&env->cpu);

	for (i = 0; i < env->nr_numa_nodes; i++)
		cpu_map__put(env->numa_nodes[i].map);
	zfree(&env->numa_nodes);

	for (i = 0; i < env->caches_cnt; i++)
		cpu_cache_level__free(&env->caches[i]);
	zfree(&env->caches);

	for (i = 0; i < env->nr_memory_nodes; i++)
		free(env->memory_nodes[i].set);
	zfree(&env->memory_nodes);
}

void perf_env__init(struct perf_env *env)
{
	env->bpf_progs.infos = RB_ROOT;
	env->bpf_progs.btfs = RB_ROOT;
	init_rwsem(&env->bpf_progs.lock);
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
	}

	env->nr_cpus_avail = nr_cpus;
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
	free(cache->type);
	free(cache->map);
	free(cache->size);
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
	if (!strcmp(arch, "aarch64") || !strcmp(arch, "arm64"))
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
	struct utsname uts;
	char *arch_name;

	if (!env || !env->arch) { /* Assume local operation */
		if (uname(&uts) < 0)
			return NULL;
		arch_name = uts.machine;
	} else
		arch_name = env->arch;

	return normalize_arch(arch_name);
}
