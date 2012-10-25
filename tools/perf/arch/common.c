#include <stdio.h>
#include <sys/utsname.h>
#include "common.h"
#include "../util/debug.h"

const char *const arm_triplets[] = {
	"arm-eabi-",
	"arm-linux-androideabi-",
	"arm-unknown-linux-",
	"arm-unknown-linux-gnu-",
	"arm-unknown-linux-gnueabi-",
	NULL
};

const char *const powerpc_triplets[] = {
	"powerpc-unknown-linux-gnu-",
	"powerpc64-unknown-linux-gnu-",
	NULL
};

const char *const s390_triplets[] = {
	"s390-ibm-linux-",
	NULL
};

const char *const sh_triplets[] = {
	"sh-unknown-linux-gnu-",
	"sh64-unknown-linux-gnu-",
	NULL
};

const char *const sparc_triplets[] = {
	"sparc-unknown-linux-gnu-",
	"sparc64-unknown-linux-gnu-",
	NULL
};

const char *const x86_triplets[] = {
	"x86_64-pc-linux-gnu-",
	"x86_64-unknown-linux-gnu-",
	"i686-pc-linux-gnu-",
	"i586-pc-linux-gnu-",
	"i486-pc-linux-gnu-",
	"i386-pc-linux-gnu-",
	"i686-linux-android-",
	"i686-android-linux-",
	NULL
};

const char *const mips_triplets[] = {
	"mips-unknown-linux-gnu-",
	"mipsel-linux-android-",
	NULL
};

static bool lookup_path(char *name)
{
	bool found = false;
	char *path, *tmp;
	char buf[PATH_MAX];
	char *env = getenv("PATH");

	if (!env)
		return false;

	env = strdup(env);
	if (!env)
		return false;

	path = strtok_r(env, ":", &tmp);
	while (path) {
		scnprintf(buf, sizeof(buf), "%s/%s", path, name);
		if (access(buf, F_OK) == 0) {
			found = true;
			break;
		}
		path = strtok_r(NULL, ":", &tmp);
	}
	free(env);
	return found;
}

static int lookup_triplets(const char *const *triplets, const char *name)
{
	int i;
	char buf[PATH_MAX];

	for (i = 0; triplets[i] != NULL; i++) {
		scnprintf(buf, sizeof(buf), "%s%s", triplets[i], name);
		if (lookup_path(buf))
			return i;
	}
	return -1;
}

static int perf_session_env__lookup_binutils_path(struct perf_session_env *env,
						  const char *name,
						  const char **path)
{
	int idx;
	char *arch, *cross_env;
	struct utsname uts;
	const char *const *path_list;
	char *buf = NULL;

	if (uname(&uts) < 0)
		goto out;

	/*
	 * We don't need to try to find objdump path for native system.
	 * Just use default binutils path (e.g.: "objdump").
	 */
	if (!strcmp(uts.machine, env->arch))
		goto out;

	cross_env = getenv("CROSS_COMPILE");
	if (cross_env) {
		if (asprintf(&buf, "%s%s", cross_env, name) < 0)
			goto out_error;
		if (buf[0] == '/') {
			if (access(buf, F_OK) == 0)
				goto out;
			goto out_error;
		}
		if (lookup_path(buf))
			goto out;
		free(buf);
	}

	arch = env->arch;

	if (!strcmp(arch, "arm"))
		path_list = arm_triplets;
	else if (!strcmp(arch, "powerpc"))
		path_list = powerpc_triplets;
	else if (!strcmp(arch, "sh"))
		path_list = sh_triplets;
	else if (!strcmp(arch, "s390"))
		path_list = s390_triplets;
	else if (!strcmp(arch, "sparc"))
		path_list = sparc_triplets;
	else if (!strcmp(arch, "x86") || !strcmp(arch, "i386") ||
		 !strcmp(arch, "i486") || !strcmp(arch, "i586") ||
		 !strcmp(arch, "i686"))
		path_list = x86_triplets;
	else if (!strcmp(arch, "mips"))
		path_list = mips_triplets;
	else {
		ui__error("binutils for %s not supported.\n", arch);
		goto out_error;
	}

	idx = lookup_triplets(path_list, name);
	if (idx < 0) {
		ui__error("Please install %s for %s.\n"
			  "You can add it to PATH, set CROSS_COMPILE or "
			  "override the default using --%s.\n",
			  name, arch, name);
		goto out_error;
	}

	if (asprintf(&buf, "%s%s", path_list[idx], name) < 0)
		goto out_error;

out:
	*path = buf;
	return 0;
out_error:
	free(buf);
	*path = NULL;
	return -1;
}

int perf_session_env__lookup_objdump(struct perf_session_env *env)
{
	return perf_session_env__lookup_binutils_path(env, "objdump",
						      &objdump_path);
}
