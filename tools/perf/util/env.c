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

	/*
	 * If env->cmdline_argv has already been set, do not override it.  This allows
	 * a command to set the cmdline, parse args and then call another
	 * builtin function that implements a command -- e.g, cmd_kvm calling
	 * cmd_record.
	 */
	if (env->cmdline_argv != NULL)
		return 0;

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
