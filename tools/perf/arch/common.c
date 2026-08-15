// SPDX-License-Identifier: GPL-2.0
#include "common.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/zalloc.h>
#include <unistd.h>

#include <dwarf-regs.h>

#include "../util/debug.h"
#include "../util/env.h"

static const char *const arc_triplets[] = {
	"arc-linux-",
	"arc-snps-linux-uclibc-",
	"arc-snps-linux-gnu-",
	NULL
};

static const char *const arm_triplets[] = {
	"arm-eabi-",
	"arm-linux-androideabi-",
	"arm-unknown-linux-",
	"arm-unknown-linux-gnu-",
	"arm-unknown-linux-gnueabi-",
	"arm-linux-gnu-",
	"arm-linux-gnueabihf-",
	"arm-none-eabi-",
	NULL
};

static const char *const arm64_triplets[] = {
	"aarch64-linux-android-",
	"aarch64-linux-gnu-",
	NULL
};

static const char *const powerpc_triplets[] = {
	"powerpc-unknown-linux-gnu-",
	"powerpc-linux-gnu-",
	"powerpc64-unknown-linux-gnu-",
	"powerpc64-linux-gnu-",
	"powerpc64le-linux-gnu-",
	NULL
};

static const char *const riscv32_triplets[] = {
	"riscv32-unknown-linux-gnu-",
	"riscv32-linux-android-",
	"riscv32-linux-gnu-",
	NULL
};

static const char *const riscv64_triplets[] = {
	"riscv64-unknown-linux-gnu-",
	"riscv64-linux-android-",
	"riscv64-linux-gnu-",
	NULL
};

static const char *const s390_triplets[] = {
	"s390-ibm-linux-",
	"s390x-linux-gnu-",
	NULL
};

static const char *const sh_triplets[] = {
	"sh-unknown-linux-gnu-",
	"sh-linux-gnu-",
	NULL
};

static const char *const sparc_triplets[] = {
	"sparc-unknown-linux-gnu-",
	"sparc64-unknown-linux-gnu-",
	"sparc64-linux-gnu-",
	NULL
};

static const char *const x86_triplets[] = {
	"x86_64-pc-linux-gnu-",
	"x86_64-unknown-linux-gnu-",
	"i686-pc-linux-gnu-",
	"i586-pc-linux-gnu-",
	"i486-pc-linux-gnu-",
	"i386-pc-linux-gnu-",
	"i686-linux-android-",
	"i686-android-linux-",
	"x86_64-linux-gnu-",
	"i586-linux-gnu-",
	NULL
};

static const char *const mips_triplets[] = {
	"mips-unknown-linux-gnu-",
	"mipsel-linux-android-",
	"mips-linux-gnu-",
	"mips64-linux-gnu-",
	"mips64el-linux-gnuabi64-",
	"mips64-linux-gnuabi64-",
	"mipsel-linux-gnu-",
	NULL
};

static bool lookup_path(char *name)
{
	bool found = false;
	char *path, *tmp = NULL;
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

static bool is_native_compatible(struct perf_env *env, uint16_t target, uint16_t host)
{
	if (target != host) {
		/* A 64-bit host can natively disassemble its 32-bit compat architecture */
		if (host == EM_X86_64 && target == EM_386)
			return true;
		if (host == EM_AARCH64 && target == EM_ARM)
			return true;
		if (host == EM_PPC64 && target == EM_PPC)
			return true;
		if (host == EM_SPARCV9 && target == EM_SPARC)
			return true;
		return false;
	}

	/* target == host case */
	if (target == EM_RISCV) {
		bool target_is_64 = perf_env__kernel_is_64_bit(env);
		bool host_is_64 = (sizeof(void *) == 8);

		/* 32-bit host cannot natively disassemble 64-bit target */
		if (!host_is_64 && target_is_64)
			return false;
	}

	return true;
}

static int perf_env__lookup_binutils_path(struct perf_env *env,
					  const char *name, char **path)
{
	int idx;
	uint16_t e_machine = perf_env__e_machine(env, /*e_flags=*/NULL);
	const char *cross_env;
	const char *const *path_list;
	char *buf = NULL;

	/*
	 * We don't need to try to find objdump path for native system.
	 * Just use default binutils path (e.g.: "objdump").
	 */
	if (is_native_compatible(env, e_machine, EM_HOST))
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
		zfree(&buf);
	}

	switch (e_machine) {
	case EM_ARC:
		path_list = arc_triplets;
		break;
	case EM_ARM:
		path_list = arm_triplets;
		break;
	case EM_AARCH64:
		path_list = arm64_triplets;
		break;
	case EM_PPC:
	case EM_PPC64:
		path_list = powerpc_triplets;
		break;
	case EM_RISCV:
		path_list = perf_env__kernel_is_64_bit(env) ? riscv64_triplets : riscv32_triplets;
		break;
	case EM_SH:
		path_list = sh_triplets;
		break;
	case EM_S390:
		path_list = s390_triplets;
		break;
	case EM_SPARC:
	case EM_SPARCV9:
		path_list = sparc_triplets;
		break;
	case EM_X86_64:
	case EM_386:
		path_list = x86_triplets;
		break;
	case EM_MIPS:
		path_list = mips_triplets;
		break;
	default:
		ui__error("binutils for %s not supported.\n", perf_env__arch(env));
		goto out_error;
	}

	idx = lookup_triplets(path_list, name);
	if (idx < 0) {
		ui__error("Please install %s for %s.\n"
			  "You can add it to PATH, set CROSS_COMPILE or "
			  "override the default using --%s.\n",
			  name, perf_env__arch(env), name);
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

int perf_env__lookup_objdump(struct perf_env *env, char **path)
{
	/*
	 * For live mode, env->arch will be NULL and we can use
	 * the native objdump tool.
	 */
	if (env->arch == NULL)
		return 0;

	return perf_env__lookup_binutils_path(env, "objdump", path);
}

/*
 * Some architectures have a single address space for kernel and user addresses,
 * which makes it possible to determine if an address is in kernel space or user
 * space.
 */
bool perf_env__single_address_space(struct perf_env *env)
{
	uint16_t e_machine = perf_env__e_machine(env, /*e_flags=*/NULL);

	return e_machine != EM_SPARC && e_machine != EM_SPARCV9 && e_machine != EM_S390;
}
