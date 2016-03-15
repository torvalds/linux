#include "cpumap.h"
#include "env.h"
#include "util.h"

struct perf_env perf_env;

void perf_env__exit(struct perf_env *env)
{
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
	zfree(&env->numa_nodes);
	zfree(&env->pmu_mappings);
	zfree(&env->cpu);
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
		env->nr_cpus_avail = sysconf(_SC_NPROCESSORS_CONF);

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
