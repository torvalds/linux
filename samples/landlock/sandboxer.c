// SPDX-License-Identifier: BSD-3-Clause
/*
 * Simple Landlock sandbox manager able to execute a process restricted by
 * user-defined file system and network access control policies.
 *
 * Copyright © 2017-2020 Mickaël Salaün <mic@digikod.net>
 * Copyright © 2020 ANSSI
 */

#define _GNU_SOURCE
#define __SANE_USERSPACE_TYPES__
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/landlock.h>
#include <linux/prctl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef landlock_create_ruleset
static inline int
landlock_create_ruleset(const struct landlock_ruleset_attr *const attr,
			const size_t size, const __u32 flags)
{
	return syscall(__NR_landlock_create_ruleset, attr, size, flags);
}
#endif

#ifndef landlock_add_rule
static inline int landlock_add_rule(const int ruleset_fd,
				    const enum landlock_rule_type rule_type,
				    const void *const rule_attr,
				    const __u32 flags)
{
	return syscall(__NR_landlock_add_rule, ruleset_fd, rule_type, rule_attr,
		       flags);
}
#endif

#ifndef landlock_restrict_self
static inline int landlock_restrict_self(const int ruleset_fd,
					 const __u32 flags)
{
	return syscall(__NR_landlock_restrict_self, ruleset_fd, flags);
}
#endif

#define ENV_FS_RO_NAME "LL_FS_RO"
#define ENV_FS_RW_NAME "LL_FS_RW"
#define ENV_TCP_BIND_NAME "LL_TCP_BIND"
#define ENV_TCP_CONNECT_NAME "LL_TCP_CONNECT"
#define ENV_DELIMITER ":"

static int parse_path(char *env_path, const char ***const path_list)
{
	int i, num_paths = 0;

	if (env_path) {
		num_paths++;
		for (i = 0; env_path[i]; i++) {
			if (env_path[i] == ENV_DELIMITER[0])
				num_paths++;
		}
	}
	*path_list = malloc(num_paths * sizeof(**path_list));
	for (i = 0; i < num_paths; i++)
		(*path_list)[i] = strsep(&env_path, ENV_DELIMITER);

	return num_paths;
}

/* clang-format off */

#define ACCESS_FILE ( \
	LANDLOCK_ACCESS_FS_EXECUTE | \
	LANDLOCK_ACCESS_FS_WRITE_FILE | \
	LANDLOCK_ACCESS_FS_READ_FILE | \
	LANDLOCK_ACCESS_FS_TRUNCATE)

/* clang-format on */

static int populate_ruleset_fs(const char *const env_var, const int ruleset_fd,
			       const __u64 allowed_access)
{
	int num_paths, i, ret = 1;
	char *env_path_name;
	const char **path_list = NULL;
	struct landlock_path_beneath_attr path_beneath = {
		.parent_fd = -1,
	};

	env_path_name = getenv(env_var);
	if (!env_path_name) {
		/* Prevents users to forget a setting. */
		fprintf(stderr, "Missing environment variable %s\n", env_var);
		return 1;
	}
	env_path_name = strdup(env_path_name);
	unsetenv(env_var);
	num_paths = parse_path(env_path_name, &path_list);
	if (num_paths == 1 && path_list[0][0] == '\0') {
		/*
		 * Allows to not use all possible restrictions (e.g. use
		 * LL_FS_RO without LL_FS_RW).
		 */
		ret = 0;
		goto out_free_name;
	}

	for (i = 0; i < num_paths; i++) {
		struct stat statbuf;

		path_beneath.parent_fd = open(path_list[i], O_PATH | O_CLOEXEC);
		if (path_beneath.parent_fd < 0) {
			fprintf(stderr, "Failed to open \"%s\": %s\n",
				path_list[i], strerror(errno));
			continue;
		}
		if (fstat(path_beneath.parent_fd, &statbuf)) {
			fprintf(stderr, "Failed to stat \"%s\": %s\n",
				path_list[i], strerror(errno));
			close(path_beneath.parent_fd);
			goto out_free_name;
		}
		path_beneath.allowed_access = allowed_access;
		if (!S_ISDIR(statbuf.st_mode))
			path_beneath.allowed_access &= ACCESS_FILE;
		if (landlock_add_rule(ruleset_fd, LANDLOCK_RULE_PATH_BENEATH,
				      &path_beneath, 0)) {
			fprintf(stderr,
				"Failed to update the ruleset with \"%s\": %s\n",
				path_list[i], strerror(errno));
			close(path_beneath.parent_fd);
			goto out_free_name;
		}
		close(path_beneath.parent_fd);
	}
	ret = 0;

out_free_name:
	free(path_list);
	free(env_path_name);
	return ret;
}

static int populate_ruleset_net(const char *const env_var, const int ruleset_fd,
				const __u64 allowed_access)
{
	int ret = 1;
	char *env_port_name, *env_port_name_next, *strport;
	struct landlock_net_port_attr net_port = {
		.allowed_access = allowed_access,
		.port = 0,
	};

	env_port_name = getenv(env_var);
	if (!env_port_name)
		return 0;
	env_port_name = strdup(env_port_name);
	unsetenv(env_var);

	env_port_name_next = env_port_name;
	while ((strport = strsep(&env_port_name_next, ENV_DELIMITER))) {
		net_port.port = atoi(strport);
		if (landlock_add_rule(ruleset_fd, LANDLOCK_RULE_NET_PORT,
				      &net_port, 0)) {
			fprintf(stderr,
				"Failed to update the ruleset with port \"%llu\": %s\n",
				net_port.port, strerror(errno));
			goto out_free_name;
		}
	}
	ret = 0;

out_free_name:
	free(env_port_name);
	return ret;
}

/* clang-format off */

#define ACCESS_FS_ROUGHLY_READ ( \
	LANDLOCK_ACCESS_FS_EXECUTE | \
	LANDLOCK_ACCESS_FS_READ_FILE | \
	LANDLOCK_ACCESS_FS_READ_DIR)

#define ACCESS_FS_ROUGHLY_WRITE ( \
	LANDLOCK_ACCESS_FS_WRITE_FILE | \
	LANDLOCK_ACCESS_FS_REMOVE_DIR | \
	LANDLOCK_ACCESS_FS_REMOVE_FILE | \
	LANDLOCK_ACCESS_FS_MAKE_CHAR | \
	LANDLOCK_ACCESS_FS_MAKE_DIR | \
	LANDLOCK_ACCESS_FS_MAKE_REG | \
	LANDLOCK_ACCESS_FS_MAKE_SOCK | \
	LANDLOCK_ACCESS_FS_MAKE_FIFO | \
	LANDLOCK_ACCESS_FS_MAKE_BLOCK | \
	LANDLOCK_ACCESS_FS_MAKE_SYM | \
	LANDLOCK_ACCESS_FS_REFER | \
	LANDLOCK_ACCESS_FS_TRUNCATE)

/* clang-format on */

#define LANDLOCK_ABI_LAST 4

int main(const int argc, char *const argv[], char *const *const envp)
{
	const char *cmd_path;
	char *const *cmd_argv;
	int ruleset_fd, abi;
	char *env_port_name;
	__u64 access_fs_ro = ACCESS_FS_ROUGHLY_READ,
	      access_fs_rw = ACCESS_FS_ROUGHLY_READ | ACCESS_FS_ROUGHLY_WRITE;

	struct landlock_ruleset_attr ruleset_attr = {
		.handled_access_fs = access_fs_rw,
		.handled_access_net = LANDLOCK_ACCESS_NET_BIND_TCP |
				      LANDLOCK_ACCESS_NET_CONNECT_TCP,
	};

	if (argc < 2) {
		fprintf(stderr,
			"usage: %s=\"...\" %s=\"...\" %s=\"...\" %s=\"...\"%s "
			"<cmd> [args]...\n\n",
			ENV_FS_RO_NAME, ENV_FS_RW_NAME, ENV_TCP_BIND_NAME,
			ENV_TCP_CONNECT_NAME, argv[0]);
		fprintf(stderr,
			"Execute a command in a restricted environment.\n\n");
		fprintf(stderr,
			"Environment variables containing paths and ports "
			"each separated by a colon:\n");
		fprintf(stderr,
			"* %s: list of paths allowed to be used in a read-only way.\n",
			ENV_FS_RO_NAME);
		fprintf(stderr,
			"* %s: list of paths allowed to be used in a read-write way.\n\n",
			ENV_FS_RW_NAME);
		fprintf(stderr,
			"Environment variables containing ports are optional "
			"and could be skipped.\n");
		fprintf(stderr,
			"* %s: list of ports allowed to bind (server).\n",
			ENV_TCP_BIND_NAME);
		fprintf(stderr,
			"* %s: list of ports allowed to connect (client).\n",
			ENV_TCP_CONNECT_NAME);
		fprintf(stderr,
			"\nexample:\n"
			"%s=\"${PATH}:/lib:/usr:/proc:/etc:/dev/urandom\" "
			"%s=\"/dev/null:/dev/full:/dev/zero:/dev/pts:/tmp\" "
			"%s=\"9418\" "
			"%s=\"80:443\" "
			"%s bash -i\n\n",
			ENV_FS_RO_NAME, ENV_FS_RW_NAME, ENV_TCP_BIND_NAME,
			ENV_TCP_CONNECT_NAME, argv[0]);
		fprintf(stderr,
			"This sandboxer can use Landlock features "
			"up to ABI version %d.\n",
			LANDLOCK_ABI_LAST);
		return 1;
	}

	abi = landlock_create_ruleset(NULL, 0, LANDLOCK_CREATE_RULESET_VERSION);
	if (abi < 0) {
		const int err = errno;

		perror("Failed to check Landlock compatibility");
		switch (err) {
		case ENOSYS:
			fprintf(stderr,
				"Hint: Landlock is not supported by the current kernel. "
				"To support it, build the kernel with "
				"CONFIG_SECURITY_LANDLOCK=y and prepend "
				"\"landlock,\" to the content of CONFIG_LSM.\n");
			break;
		case EOPNOTSUPP:
			fprintf(stderr,
				"Hint: Landlock is currently disabled. "
				"It can be enabled in the kernel configuration by "
				"prepending \"landlock,\" to the content of CONFIG_LSM, "
				"or at boot time by setting the same content to the "
				"\"lsm\" kernel parameter.\n");
			break;
		}
		return 1;
	}

	/* Best-effort security. */
	switch (abi) {
	case 1:
		/*
		 * Removes LANDLOCK_ACCESS_FS_REFER for ABI < 2
		 *
		 * Note: The "refer" operations (file renaming and linking
		 * across different directories) are always forbidden when using
		 * Landlock with ABI 1.
		 *
		 * If only ABI 1 is available, this sandboxer knowingly forbids
		 * refer operations.
		 *
		 * If a program *needs* to do refer operations after enabling
		 * Landlock, it can not use Landlock at ABI level 1.  To be
		 * compatible with different kernel versions, such programs
		 * should then fall back to not restrict themselves at all if
		 * the running kernel only supports ABI 1.
		 */
		ruleset_attr.handled_access_fs &= ~LANDLOCK_ACCESS_FS_REFER;
		__attribute__((fallthrough));
	case 2:
		/* Removes LANDLOCK_ACCESS_FS_TRUNCATE for ABI < 3 */
		ruleset_attr.handled_access_fs &= ~LANDLOCK_ACCESS_FS_TRUNCATE;
		__attribute__((fallthrough));
	case 3:
		/* Removes network support for ABI < 4 */
		ruleset_attr.handled_access_net &=
			~(LANDLOCK_ACCESS_NET_BIND_TCP |
			  LANDLOCK_ACCESS_NET_CONNECT_TCP);
		fprintf(stderr,
			"Hint: You should update the running kernel "
			"to leverage Landlock features "
			"provided by ABI version %d (instead of %d).\n",
			LANDLOCK_ABI_LAST, abi);
		__attribute__((fallthrough));
	case LANDLOCK_ABI_LAST:
		break;
	default:
		fprintf(stderr,
			"Hint: You should update this sandboxer "
			"to leverage Landlock features "
			"provided by ABI version %d (instead of %d).\n",
			abi, LANDLOCK_ABI_LAST);
	}
	access_fs_ro &= ruleset_attr.handled_access_fs;
	access_fs_rw &= ruleset_attr.handled_access_fs;

	/* Removes bind access attribute if not supported by a user. */
	env_port_name = getenv(ENV_TCP_BIND_NAME);
	if (!env_port_name) {
		ruleset_attr.handled_access_net &=
			~LANDLOCK_ACCESS_NET_BIND_TCP;
	}
	/* Removes connect access attribute if not supported by a user. */
	env_port_name = getenv(ENV_TCP_CONNECT_NAME);
	if (!env_port_name) {
		ruleset_attr.handled_access_net &=
			~LANDLOCK_ACCESS_NET_CONNECT_TCP;
	}

	ruleset_fd =
		landlock_create_ruleset(&ruleset_attr, sizeof(ruleset_attr), 0);
	if (ruleset_fd < 0) {
		perror("Failed to create a ruleset");
		return 1;
	}

	if (populate_ruleset_fs(ENV_FS_RO_NAME, ruleset_fd, access_fs_ro)) {
		goto err_close_ruleset;
	}
	if (populate_ruleset_fs(ENV_FS_RW_NAME, ruleset_fd, access_fs_rw)) {
		goto err_close_ruleset;
	}

	if (populate_ruleset_net(ENV_TCP_BIND_NAME, ruleset_fd,
				 LANDLOCK_ACCESS_NET_BIND_TCP)) {
		goto err_close_ruleset;
	}
	if (populate_ruleset_net(ENV_TCP_CONNECT_NAME, ruleset_fd,
				 LANDLOCK_ACCESS_NET_CONNECT_TCP)) {
		goto err_close_ruleset;
	}

	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) {
		perror("Failed to restrict privileges");
		goto err_close_ruleset;
	}
	if (landlock_restrict_self(ruleset_fd, 0)) {
		perror("Failed to enforce ruleset");
		goto err_close_ruleset;
	}
	close(ruleset_fd);

	cmd_path = argv[1];
	cmd_argv = argv + 1;
	fprintf(stderr, "Executing the sandboxed command...\n");
	execvpe(cmd_path, cmd_argv, envp);
	fprintf(stderr, "Failed to execute \"%s\": %s\n", cmd_path,
		strerror(errno));
	fprintf(stderr, "Hint: access to the binary, the interpreter or "
			"shared libraries may be denied.\n");
	return 1;

err_close_ruleset:
	close(ruleset_fd);
	return 1;
}
