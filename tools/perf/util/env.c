#include "env.h"
#include "util.h"

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
