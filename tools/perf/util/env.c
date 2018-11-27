// SPDX-License-Identifier: GPL-2.0
#include "cpumap.h"
#include "env.h"
#include "sane_ctype.h"
#include "util.h"
#include <errno.h>
#include <sys/utsname.h>

struct perf_env perf_env;

void perf_env__exit(struct perf_env *env)
{
	int i;

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
