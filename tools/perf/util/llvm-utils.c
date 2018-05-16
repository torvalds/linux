// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015, Wang Nan <wangnan0@huawei.com>
 * Copyright (C) 2015, Huawei Inc.
 */

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/err.h>
#include "debug.h"
#include "llvm-utils.h"
#include "config.h"
#include "util.h"
#include <sys/wait.h>
#include <subcmd/exec-cmd.h>

#define CLANG_BPF_CMD_DEFAULT_TEMPLATE				\
		"$CLANG_EXEC -D__KERNEL__ -D__NR_CPUS__=$NR_CPUS "\
		"-DLINUX_VERSION_CODE=$LINUX_VERSION_CODE "	\
		"$CLANG_OPTIONS $KERNEL_INC_OPTIONS $PERF_BPF_INC_OPTIONS " \
		"-Wno-unused-value -Wno-pointer-sign "		\
		"-working-directory $WORKING_DIR "		\
		"-c \"$CLANG_SOURCE\" -target bpf -O2 -o -"

struct llvm_param llvm_param = {
	.clang_path = "clang",
	.clang_bpf_cmd_template = CLANG_BPF_CMD_DEFAULT_TEMPLATE,
	.clang_opt = NULL,
	.kbuild_dir = NULL,
	.kbuild_opts = NULL,
	.user_set_param = false,
};

int perf_llvm_config(const char *var, const char *value)
{
	if (!strstarts(var, "llvm."))
		return 0;
	var += sizeof("llvm.") - 1;

	if (!strcmp(var, "clang-path"))
		llvm_param.clang_path = strdup(value);
	else if (!strcmp(var, "clang-bpf-cmd-template"))
		llvm_param.clang_bpf_cmd_template = strdup(value);
	else if (!strcmp(var, "clang-opt"))
		llvm_param.clang_opt = strdup(value);
	else if (!strcmp(var, "kbuild-dir"))
		llvm_param.kbuild_dir = strdup(value);
	else if (!strcmp(var, "kbuild-opts"))
		llvm_param.kbuild_opts = strdup(value);
	else if (!strcmp(var, "dump-obj"))
		llvm_param.dump_obj = !!perf_config_bool(var, value);
	else {
		pr_debug("Invalid LLVM config option: %s\n", value);
		return -1;
	}
	llvm_param.user_set_param = true;
	return 0;
}

static int
search_program(const char *def, const char *name,
	       char *output)
{
	char *env, *path, *tmp = NULL;
	char buf[PATH_MAX];
	int ret;

	output[0] = '\0';
	if (def && def[0] != '\0') {
		if (def[0] == '/') {
			if (access(def, F_OK) == 0) {
				strlcpy(output, def, PATH_MAX);
				return 0;
			}
		} else if (def[0] != '\0')
			name = def;
	}

	env = getenv("PATH");
	if (!env)
		return -1;
	env = strdup(env);
	if (!env)
		return -1;

	ret = -ENOENT;
	path = strtok_r(env, ":",  &tmp);
	while (path) {
		scnprintf(buf, sizeof(buf), "%s/%s", path, name);
		if (access(buf, F_OK) == 0) {
			strlcpy(output, buf, PATH_MAX);
			ret = 0;
			break;
		}
		path = strtok_r(NULL, ":", &tmp);
	}

	free(env);
	return ret;
}

#define READ_SIZE	4096
static int
read_from_pipe(const char *cmd, void **p_buf, size_t *p_read_sz)
{
	int err = 0;
	void *buf = NULL;
	FILE *file = NULL;
	size_t read_sz = 0, buf_sz = 0;
	char serr[STRERR_BUFSIZE];

	file = popen(cmd, "r");
	if (!file) {
		pr_err("ERROR: unable to popen cmd: %s\n",
		       str_error_r(errno, serr, sizeof(serr)));
		return -EINVAL;
	}

	while (!feof(file) && !ferror(file)) {
		/*
		 * Make buf_sz always have obe byte extra space so we
		 * can put '\0' there.
		 */
		if (buf_sz - read_sz < READ_SIZE + 1) {
			void *new_buf;

			buf_sz = read_sz + READ_SIZE + 1;
			new_buf = realloc(buf, buf_sz);

			if (!new_buf) {
				pr_err("ERROR: failed to realloc memory\n");
				err = -ENOMEM;
				goto errout;
			}

			buf = new_buf;
		}
		read_sz += fread(buf + read_sz, 1, READ_SIZE, file);
	}

	if (buf_sz - read_sz < 1) {
		pr_err("ERROR: internal error\n");
		err = -EINVAL;
		goto errout;
	}

	if (ferror(file)) {
		pr_err("ERROR: error occurred when reading from pipe: %s\n",
		       str_error_r(errno, serr, sizeof(serr)));
		err = -EIO;
		goto errout;
	}

	err = WEXITSTATUS(pclose(file));
	file = NULL;
	if (err) {
		err = -EINVAL;
		goto errout;
	}

	/*
	 * If buf is string, give it terminal '\0' to make our life
	 * easier. If buf is not string, that '\0' is out of space
	 * indicated by read_sz so caller won't even notice it.
	 */
	((char *)buf)[read_sz] = '\0';

	if (!p_buf)
		free(buf);
	else
		*p_buf = buf;

	if (p_read_sz)
		*p_read_sz = read_sz;
	return 0;

errout:
	if (file)
		pclose(file);
	free(buf);
	if (p_buf)
		*p_buf = NULL;
	if (p_read_sz)
		*p_read_sz = 0;
	return err;
}

static inline void
force_set_env(const char *var, const char *value)
{
	if (value) {
		setenv(var, value, 1);
		pr_debug("set env: %s=%s\n", var, value);
	} else {
		unsetenv(var);
		pr_debug("unset env: %s\n", var);
	}
}

static void
version_notice(void)
{
	pr_err(
"     \tLLVM 3.7 or newer is required. Which can be found from http://llvm.org\n"
"     \tYou may want to try git trunk:\n"
"     \t\tgit clone http://llvm.org/git/llvm.git\n"
"     \t\t     and\n"
"     \t\tgit clone http://llvm.org/git/clang.git\n\n"
"     \tOr fetch the latest clang/llvm 3.7 from pre-built llvm packages for\n"
"     \tdebian/ubuntu:\n"
"     \t\thttp://llvm.org/apt\n\n"
"     \tIf you are using old version of clang, change 'clang-bpf-cmd-template'\n"
"     \toption in [llvm] section of ~/.perfconfig to:\n\n"
"     \t  \"$CLANG_EXEC $CLANG_OPTIONS $KERNEL_INC_OPTIONS $PERF_BPF_INC_OPTIONS \\\n"
"     \t     -working-directory $WORKING_DIR -c $CLANG_SOURCE \\\n"
"     \t     -emit-llvm -o - | /path/to/llc -march=bpf -filetype=obj -o -\"\n"
"     \t(Replace /path/to/llc with path to your llc)\n\n"
);
}

static int detect_kbuild_dir(char **kbuild_dir)
{
	const char *test_dir = llvm_param.kbuild_dir;
	const char *prefix_dir = "";
	const char *suffix_dir = "";

	char *autoconf_path;

	int err;

	if (!test_dir) {
		/* _UTSNAME_LENGTH is 65 */
		char release[128];

		err = fetch_kernel_version(NULL, release,
					   sizeof(release));
		if (err)
			return -EINVAL;

		test_dir = release;
		prefix_dir = "/lib/modules/";
		suffix_dir = "/build";
	}

	err = asprintf(&autoconf_path, "%s%s%s/include/generated/autoconf.h",
		       prefix_dir, test_dir, suffix_dir);
	if (err < 0)
		return -ENOMEM;

	if (access(autoconf_path, R_OK) == 0) {
		free(autoconf_path);

		err = asprintf(kbuild_dir, "%s%s%s", prefix_dir, test_dir,
			       suffix_dir);
		if (err < 0)
			return -ENOMEM;
		return 0;
	}
	free(autoconf_path);
	return -ENOENT;
}

static const char *kinc_fetch_script =
"#!/usr/bin/env sh\n"
"if ! test -d \"$KBUILD_DIR\"\n"
"then\n"
"	exit -1\n"
"fi\n"
"if ! test -f \"$KBUILD_DIR/include/generated/autoconf.h\"\n"
"then\n"
"	exit -1\n"
"fi\n"
"TMPDIR=`mktemp -d`\n"
"if test -z \"$TMPDIR\"\n"
"then\n"
"    exit -1\n"
"fi\n"
"cat << EOF > $TMPDIR/Makefile\n"
"obj-y := dummy.o\n"
"\\$(obj)/%.o: \\$(src)/%.c\n"
"\t@echo -n \"\\$(NOSTDINC_FLAGS) \\$(LINUXINCLUDE) \\$(EXTRA_CFLAGS)\"\n"
"EOF\n"
"touch $TMPDIR/dummy.c\n"
"make -s -C $KBUILD_DIR M=$TMPDIR $KBUILD_OPTS dummy.o 2>/dev/null\n"
"RET=$?\n"
"rm -rf $TMPDIR\n"
"exit $RET\n";

void llvm__get_kbuild_opts(char **kbuild_dir, char **kbuild_include_opts)
{
	static char *saved_kbuild_dir;
	static char *saved_kbuild_include_opts;
	int err;

	if (!kbuild_dir || !kbuild_include_opts)
		return;

	*kbuild_dir = NULL;
	*kbuild_include_opts = NULL;

	if (saved_kbuild_dir && saved_kbuild_include_opts &&
	    !IS_ERR(saved_kbuild_dir) && !IS_ERR(saved_kbuild_include_opts)) {
		*kbuild_dir = strdup(saved_kbuild_dir);
		*kbuild_include_opts = strdup(saved_kbuild_include_opts);

		if (*kbuild_dir && *kbuild_include_opts)
			return;

		zfree(kbuild_dir);
		zfree(kbuild_include_opts);
		/*
		 * Don't fall through: it may breaks saved_kbuild_dir and
		 * saved_kbuild_include_opts if detect them again when
		 * memory is low.
		 */
		return;
	}

	if (llvm_param.kbuild_dir && !llvm_param.kbuild_dir[0]) {
		pr_debug("[llvm.kbuild-dir] is set to \"\" deliberately.\n");
		pr_debug("Skip kbuild options detection.\n");
		goto errout;
	}

	err = detect_kbuild_dir(kbuild_dir);
	if (err) {
		pr_warning(
"WARNING:\tunable to get correct kernel building directory.\n"
"Hint:\tSet correct kbuild directory using 'kbuild-dir' option in [llvm]\n"
"     \tsection of ~/.perfconfig or set it to \"\" to suppress kbuild\n"
"     \tdetection.\n\n");
		goto errout;
	}

	pr_debug("Kernel build dir is set to %s\n", *kbuild_dir);
	force_set_env("KBUILD_DIR", *kbuild_dir);
	force_set_env("KBUILD_OPTS", llvm_param.kbuild_opts);
	err = read_from_pipe(kinc_fetch_script,
			     (void **)kbuild_include_opts,
			     NULL);
	if (err) {
		pr_warning(
"WARNING:\tunable to get kernel include directories from '%s'\n"
"Hint:\tTry set clang include options using 'clang-bpf-cmd-template'\n"
"     \toption in [llvm] section of ~/.perfconfig and set 'kbuild-dir'\n"
"     \toption in [llvm] to \"\" to suppress this detection.\n\n",
			*kbuild_dir);

		free(*kbuild_dir);
		*kbuild_dir = NULL;
		goto errout;
	}

	pr_debug("include option is set to %s\n", *kbuild_include_opts);

	saved_kbuild_dir = strdup(*kbuild_dir);
	saved_kbuild_include_opts = strdup(*kbuild_include_opts);

	if (!saved_kbuild_dir || !saved_kbuild_include_opts) {
		zfree(&saved_kbuild_dir);
		zfree(&saved_kbuild_include_opts);
	}
	return;
errout:
	saved_kbuild_dir = ERR_PTR(-EINVAL);
	saved_kbuild_include_opts = ERR_PTR(-EINVAL);
}

int llvm__get_nr_cpus(void)
{
	static int nr_cpus_avail = 0;
	char serr[STRERR_BUFSIZE];

	if (nr_cpus_avail > 0)
		return nr_cpus_avail;

	nr_cpus_avail = sysconf(_SC_NPROCESSORS_CONF);
	if (nr_cpus_avail <= 0) {
		pr_err(
"WARNING:\tunable to get available CPUs in this system: %s\n"
"        \tUse 128 instead.\n", str_error_r(errno, serr, sizeof(serr)));
		nr_cpus_avail = 128;
	}
	return nr_cpus_avail;
}

void llvm__dump_obj(const char *path, void *obj_buf, size_t size)
{
	char *obj_path = strdup(path);
	FILE *fp;
	char *p;

	if (!obj_path) {
		pr_warning("WARNING: Not enough memory, skip object dumping\n");
		return;
	}

	p = strrchr(obj_path, '.');
	if (!p || (strcmp(p, ".c") != 0)) {
		pr_warning("WARNING: invalid llvm source path: '%s', skip object dumping\n",
			   obj_path);
		goto out;
	}

	p[1] = 'o';
	fp = fopen(obj_path, "wb");
	if (!fp) {
		pr_warning("WARNING: failed to open '%s': %s, skip object dumping\n",
			   obj_path, strerror(errno));
		goto out;
	}

	pr_info("LLVM: dumping %s\n", obj_path);
	if (fwrite(obj_buf, size, 1, fp) != 1)
		pr_warning("WARNING: failed to write to file '%s': %s, skip object dumping\n",
			   obj_path, strerror(errno));
	fclose(fp);
out:
	free(obj_path);
}

int llvm__compile_bpf(const char *path, void **p_obj_buf,
		      size_t *p_obj_buf_sz)
{
	size_t obj_buf_sz;
	void *obj_buf = NULL;
	int err, nr_cpus_avail;
	unsigned int kernel_version;
	char linux_version_code_str[64];
	const char *clang_opt = llvm_param.clang_opt;
	char clang_path[PATH_MAX], abspath[PATH_MAX], nr_cpus_avail_str[64];
	char serr[STRERR_BUFSIZE];
	char *kbuild_dir = NULL, *kbuild_include_opts = NULL,
	     *perf_bpf_include_opts = NULL;
	const char *template = llvm_param.clang_bpf_cmd_template;
	char *command_echo = NULL, *command_out;
	char *perf_include_dir = system_path(PERF_INCLUDE_DIR);

	if (path[0] != '-' && realpath(path, abspath) == NULL) {
		err = errno;
		pr_err("ERROR: problems with path %s: %s\n",
		       path, str_error_r(err, serr, sizeof(serr)));
		return -err;
	}

	if (!template)
		template = CLANG_BPF_CMD_DEFAULT_TEMPLATE;

	err = search_program(llvm_param.clang_path,
			     "clang", clang_path);
	if (err) {
		pr_err(
"ERROR:\tunable to find clang.\n"
"Hint:\tTry to install latest clang/llvm to support BPF. Check your $PATH\n"
"     \tand 'clang-path' option in [llvm] section of ~/.perfconfig.\n");
		version_notice();
		return -ENOENT;
	}

	/*
	 * This is an optional work. Even it fail we can continue our
	 * work. Needn't to check error return.
	 */
	llvm__get_kbuild_opts(&kbuild_dir, &kbuild_include_opts);

	nr_cpus_avail = llvm__get_nr_cpus();
	snprintf(nr_cpus_avail_str, sizeof(nr_cpus_avail_str), "%d",
		 nr_cpus_avail);

	if (fetch_kernel_version(&kernel_version, NULL, 0))
		kernel_version = 0;

	snprintf(linux_version_code_str, sizeof(linux_version_code_str),
		 "0x%x", kernel_version);
	if (asprintf(&perf_bpf_include_opts, "-I%s/bpf", perf_include_dir) < 0)
		goto errout;
	force_set_env("NR_CPUS", nr_cpus_avail_str);
	force_set_env("LINUX_VERSION_CODE", linux_version_code_str);
	force_set_env("CLANG_EXEC", clang_path);
	force_set_env("CLANG_OPTIONS", clang_opt);
	force_set_env("KERNEL_INC_OPTIONS", kbuild_include_opts);
	force_set_env("PERF_BPF_INC_OPTIONS", perf_bpf_include_opts);
	force_set_env("WORKING_DIR", kbuild_dir ? : ".");

	/*
	 * Since we may reset clang's working dir, path of source file
	 * should be transferred into absolute path, except we want
	 * stdin to be source file (testing).
	 */
	force_set_env("CLANG_SOURCE",
		      (path[0] == '-') ? path : abspath);

	pr_debug("llvm compiling command template: %s\n", template);

	if (asprintf(&command_echo, "echo -n \"%s\"", template) < 0)
		goto errout;

	err = read_from_pipe(command_echo, (void **) &command_out, NULL);
	if (err)
		goto errout;

	pr_debug("llvm compiling command : %s\n", command_out);

	err = read_from_pipe(template, &obj_buf, &obj_buf_sz);
	if (err) {
		pr_err("ERROR:\tunable to compile %s\n", path);
		pr_err("Hint:\tCheck error message shown above.\n");
		pr_err("Hint:\tYou can also pre-compile it into .o using:\n");
		pr_err("     \t\tclang -target bpf -O2 -c %s\n", path);
		pr_err("     \twith proper -I and -D options.\n");
		goto errout;
	}

	free(command_echo);
	free(command_out);
	free(kbuild_dir);
	free(kbuild_include_opts);
	free(perf_bpf_include_opts);
	free(perf_include_dir);

	if (!p_obj_buf)
		free(obj_buf);
	else
		*p_obj_buf = obj_buf;

	if (p_obj_buf_sz)
		*p_obj_buf_sz = obj_buf_sz;
	return 0;
errout:
	free(command_echo);
	free(kbuild_dir);
	free(kbuild_include_opts);
	free(obj_buf);
	free(perf_bpf_include_opts);
	free(perf_include_dir);
	if (p_obj_buf)
		*p_obj_buf = NULL;
	if (p_obj_buf_sz)
		*p_obj_buf_sz = 0;
	return err;
}

int llvm__search_clang(void)
{
	char clang_path[PATH_MAX];

	return search_program(llvm_param.clang_path, "clang", clang_path);
}
