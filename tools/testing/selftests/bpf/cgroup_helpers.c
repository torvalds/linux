// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/sched.h>
#include <fcntl.h>
#include <unistd.h>
#include <ftw.h>

#include "cgroup_helpers.h"
#include "bpf_util.h"

/*
 * To avoid relying on the system setup, when setup_cgroup_env is called
 * we create a new mount namespace, and cgroup namespace. The cgroupv2
 * root is mounted at CGROUP_MOUNT_PATH. Unfortunately, most people don't
 * have cgroupv2 enabled at this point in time. It's easier to create our
 * own mount namespace and manage it ourselves. We assume /mnt exists.
 *
 * Related cgroupv1 helpers are named *classid*(), since we only use the
 * net_cls controller for tagging net_cls.classid. We assume the default
 * mount under /sys/fs/cgroup/net_cls, which should be the case for the
 * vast majority of users.
 */

#define WALK_FD_LIMIT			16

#define CGROUP_MOUNT_PATH		"/mnt"
#define CGROUP_MOUNT_DFLT		"/sys/fs/cgroup"
#define NETCLS_MOUNT_PATH		CGROUP_MOUNT_DFLT "/net_cls"
#define CGROUP_WORK_DIR			"/cgroup-test-work-dir"

#define format_cgroup_path_pid(buf, path, pid) \
	snprintf(buf, sizeof(buf), "%s%s%d%s", CGROUP_MOUNT_PATH, \
	CGROUP_WORK_DIR, pid, path)

#define format_cgroup_path(buf, path) \
	format_cgroup_path_pid(buf, path, getpid())

#define format_parent_cgroup_path(buf, path) \
	format_cgroup_path_pid(buf, path, getppid())

#define format_classid_path(buf)				\
	snprintf(buf, sizeof(buf), "%s%s", NETCLS_MOUNT_PATH,	\
		 CGROUP_WORK_DIR)

static __thread bool cgroup_workdir_mounted;

static void __cleanup_cgroup_environment(void);

static int __enable_controllers(const char *cgroup_path, const char *controllers)
{
	char path[PATH_MAX + 1];
	char enable[PATH_MAX + 1];
	char *c, *c2;
	int fd, cfd;
	ssize_t len;

	/* If not controllers are passed, enable all available controllers */
	if (!controllers) {
		snprintf(path, sizeof(path), "%s/cgroup.controllers",
			 cgroup_path);
		fd = open(path, O_RDONLY);
		if (fd < 0) {
			log_err("Opening cgroup.controllers: %s", path);
			return 1;
		}
		len = read(fd, enable, sizeof(enable) - 1);
		if (len < 0) {
			close(fd);
			log_err("Reading cgroup.controllers: %s", path);
			return 1;
		} else if (len == 0) { /* No controllers to enable */
			close(fd);
			return 0;
		}
		enable[len] = 0;
		close(fd);
	} else {
		bpf_strlcpy(enable, controllers, sizeof(enable));
	}

	snprintf(path, sizeof(path), "%s/cgroup.subtree_control", cgroup_path);
	cfd = open(path, O_RDWR);
	if (cfd < 0) {
		log_err("Opening cgroup.subtree_control: %s", path);
		return 1;
	}

	for (c = strtok_r(enable, " ", &c2); c; c = strtok_r(NULL, " ", &c2)) {
		if (dprintf(cfd, "+%s\n", c) <= 0) {
			log_err("Enabling controller %s: %s", c, path);
			close(cfd);
			return 1;
		}
	}
	close(cfd);
	return 0;
}

/**
 * enable_controllers() - Enable cgroup v2 controllers
 * @relative_path: The cgroup path, relative to the workdir
 * @controllers: List of controllers to enable in cgroup.controllers format
 *
 *
 * Enable given cgroup v2 controllers, if @controllers is NULL, enable all
 * available controllers.
 *
 * If successful, 0 is returned.
 */
int enable_controllers(const char *relative_path, const char *controllers)
{
	char cgroup_path[PATH_MAX + 1];

	format_cgroup_path(cgroup_path, relative_path);
	return __enable_controllers(cgroup_path, controllers);
}

static int __write_cgroup_file(const char *cgroup_path, const char *file,
			       const char *buf)
{
	char file_path[PATH_MAX + 1];
	int fd;

	snprintf(file_path, sizeof(file_path), "%s/%s", cgroup_path, file);
	fd = open(file_path, O_RDWR);
	if (fd < 0) {
		log_err("Opening %s", file_path);
		return 1;
	}

	if (dprintf(fd, "%s", buf) <= 0) {
		log_err("Writing to %s", file_path);
		close(fd);
		return 1;
	}
	close(fd);
	return 0;
}

/**
 * write_cgroup_file() - Write to a cgroup file
 * @relative_path: The cgroup path, relative to the workdir
 * @file: The name of the file in cgroupfs to write to
 * @buf: Buffer to write to the file
 *
 * Write to a file in the given cgroup's directory.
 *
 * If successful, 0 is returned.
 */
int write_cgroup_file(const char *relative_path, const char *file,
		      const char *buf)
{
	char cgroup_path[PATH_MAX - 24];

	format_cgroup_path(cgroup_path, relative_path);
	return __write_cgroup_file(cgroup_path, file, buf);
}

/**
 * write_cgroup_file_parent() - Write to a cgroup file in the parent process
 *                              workdir
 * @relative_path: The cgroup path, relative to the parent process workdir
 * @file: The name of the file in cgroupfs to write to
 * @buf: Buffer to write to the file
 *
 * Write to a file in the given cgroup's directory under the parent process
 * workdir.
 *
 * If successful, 0 is returned.
 */
int write_cgroup_file_parent(const char *relative_path, const char *file,
			     const char *buf)
{
	char cgroup_path[PATH_MAX - 24];

	format_parent_cgroup_path(cgroup_path, relative_path);
	return __write_cgroup_file(cgroup_path, file, buf);
}

/**
 * setup_cgroup_environment() - Setup the cgroup environment
 *
 * After calling this function, cleanup_cgroup_environment should be called
 * once testing is complete.
 *
 * This function will print an error to stderr and return 1 if it is unable
 * to setup the cgroup environment. If setup is successful, 0 is returned.
 */
int setup_cgroup_environment(void)
{
	char cgroup_workdir[PATH_MAX - 24];

	format_cgroup_path(cgroup_workdir, "");

	if (mkdir(CGROUP_MOUNT_PATH, 0777) && errno != EEXIST) {
		log_err("mkdir mount");
		return 1;
	}

	if (unshare(CLONE_NEWNS)) {
		log_err("unshare");
		return 1;
	}

	if (mount("none", "/", NULL, MS_REC | MS_PRIVATE, NULL)) {
		log_err("mount fakeroot");
		return 1;
	}

	if (mount("none", CGROUP_MOUNT_PATH, "cgroup2", 0, NULL) && errno != EBUSY) {
		log_err("mount cgroup2");
		return 1;
	}
	cgroup_workdir_mounted = true;

	/* Cleanup existing failed runs, now that the environment is setup */
	__cleanup_cgroup_environment();

	if (mkdir(cgroup_workdir, 0777) && errno != EEXIST) {
		log_err("mkdir cgroup work dir");
		return 1;
	}

	/* Enable all available controllers to increase test coverage */
	if (__enable_controllers(CGROUP_MOUNT_PATH, NULL) ||
	    __enable_controllers(cgroup_workdir, NULL))
		return 1;

	return 0;
}

static int nftwfunc(const char *filename, const struct stat *statptr,
		    int fileflags, struct FTW *pfwt)
{
	if ((fileflags & FTW_D) && rmdir(filename))
		log_err("Removing cgroup: %s", filename);
	return 0;
}

static int join_cgroup_from_top(const char *cgroup_path)
{
	char cgroup_procs_path[PATH_MAX + 1];
	pid_t pid = getpid();
	int fd, rc = 0;

	snprintf(cgroup_procs_path, sizeof(cgroup_procs_path),
		 "%s/cgroup.procs", cgroup_path);

	fd = open(cgroup_procs_path, O_WRONLY);
	if (fd < 0) {
		log_err("Opening Cgroup Procs: %s", cgroup_procs_path);
		return 1;
	}

	if (dprintf(fd, "%d\n", pid) < 0) {
		log_err("Joining Cgroup");
		rc = 1;
	}

	close(fd);
	return rc;
}

/**
 * join_cgroup() - Join a cgroup
 * @relative_path: The cgroup path, relative to the workdir, to join
 *
 * This function expects a cgroup to already be created, relative to the cgroup
 * work dir, and it joins it. For example, passing "/my-cgroup" as the path
 * would actually put the calling process into the cgroup
 * "/cgroup-test-work-dir/my-cgroup"
 *
 * On success, it returns 0, otherwise on failure it returns 1.
 */
int join_cgroup(const char *relative_path)
{
	char cgroup_path[PATH_MAX + 1];

	format_cgroup_path(cgroup_path, relative_path);
	return join_cgroup_from_top(cgroup_path);
}

/**
 * join_root_cgroup() - Join the root cgroup
 *
 * This function joins the root cgroup.
 *
 * On success, it returns 0, otherwise on failure it returns 1.
 */
int join_root_cgroup(void)
{
	return join_cgroup_from_top(CGROUP_MOUNT_PATH);
}

/**
 * join_parent_cgroup() - Join a cgroup in the parent process workdir
 * @relative_path: The cgroup path, relative to parent process workdir, to join
 *
 * See join_cgroup().
 *
 * On success, it returns 0, otherwise on failure it returns 1.
 */
int join_parent_cgroup(const char *relative_path)
{
	char cgroup_path[PATH_MAX + 1];

	format_parent_cgroup_path(cgroup_path, relative_path);
	return join_cgroup_from_top(cgroup_path);
}

/**
 * __cleanup_cgroup_environment() - Delete temporary cgroups
 *
 * This is a helper for cleanup_cgroup_environment() that is responsible for
 * deletion of all temporary cgroups that have been created during the test.
 */
static void __cleanup_cgroup_environment(void)
{
	char cgroup_workdir[PATH_MAX + 1];

	format_cgroup_path(cgroup_workdir, "");
	join_cgroup_from_top(CGROUP_MOUNT_PATH);
	nftw(cgroup_workdir, nftwfunc, WALK_FD_LIMIT, FTW_DEPTH | FTW_MOUNT);
}

/**
 * cleanup_cgroup_environment() - Cleanup Cgroup Testing Environment
 *
 * This is an idempotent function to delete all temporary cgroups that
 * have been created during the test and unmount the cgroup testing work
 * directory.
 *
 * At call time, it moves the calling process to the root cgroup, and then
 * runs the deletion process. It is idempotent, and should not fail, unless
 * a process is lingering.
 *
 * On failure, it will print an error to stderr, and try to continue.
 */
void cleanup_cgroup_environment(void)
{
	__cleanup_cgroup_environment();
	if (cgroup_workdir_mounted && umount(CGROUP_MOUNT_PATH))
		log_err("umount cgroup2");
	cgroup_workdir_mounted = false;
}

/**
 * get_root_cgroup() - Get the FD of the root cgroup
 *
 * On success, it returns the file descriptor. On failure, it returns -1.
 * If there is a failure, it prints the error to stderr.
 */
int get_root_cgroup(void)
{
	int fd;

	fd = open(CGROUP_MOUNT_PATH, O_RDONLY);
	if (fd < 0) {
		log_err("Opening root cgroup");
		return -1;
	}
	return fd;
}

/*
 * remove_cgroup() - Remove a cgroup
 * @relative_path: The cgroup path, relative to the workdir, to remove
 *
 * This function expects a cgroup to already be created, relative to the cgroup
 * work dir. It also expects the cgroup doesn't have any children or live
 * processes and it removes the cgroup.
 *
 * On failure, it will print an error to stderr.
 */
void remove_cgroup(const char *relative_path)
{
	char cgroup_path[PATH_MAX + 1];

	format_cgroup_path(cgroup_path, relative_path);
	if (rmdir(cgroup_path))
		log_err("rmdiring cgroup %s .. %s", relative_path, cgroup_path);
}

/**
 * create_and_get_cgroup() - Create a cgroup, relative to workdir, and get the FD
 * @relative_path: The cgroup path, relative to the workdir, to join
 *
 * This function creates a cgroup under the top level workdir and returns the
 * file descriptor. It is idempotent.
 *
 * On success, it returns the file descriptor. On failure it returns -1.
 * If there is a failure, it prints the error to stderr.
 */
int create_and_get_cgroup(const char *relative_path)
{
	char cgroup_path[PATH_MAX + 1];
	int fd;

	format_cgroup_path(cgroup_path, relative_path);
	if (mkdir(cgroup_path, 0777) && errno != EEXIST) {
		log_err("mkdiring cgroup %s .. %s", relative_path, cgroup_path);
		return -1;
	}

	fd = open(cgroup_path, O_RDONLY);
	if (fd < 0) {
		log_err("Opening Cgroup");
		return -1;
	}

	return fd;
}

/**
 * get_cgroup_id() - Get cgroup id for a particular cgroup path
 * @relative_path: The cgroup path, relative to the workdir, to join
 *
 * On success, it returns the cgroup id. On failure it returns 0,
 * which is an invalid cgroup id.
 * If there is a failure, it prints the error to stderr.
 */
unsigned long long get_cgroup_id(const char *relative_path)
{
	int dirfd, err, flags, mount_id, fhsize;
	union {
		unsigned long long cgid;
		unsigned char raw_bytes[8];
	} id;
	char cgroup_workdir[PATH_MAX + 1];
	struct file_handle *fhp, *fhp2;
	unsigned long long ret = 0;

	format_cgroup_path(cgroup_workdir, relative_path);

	dirfd = AT_FDCWD;
	flags = 0;
	fhsize = sizeof(*fhp);
	fhp = calloc(1, fhsize);
	if (!fhp) {
		log_err("calloc");
		return 0;
	}
	err = name_to_handle_at(dirfd, cgroup_workdir, fhp, &mount_id, flags);
	if (err >= 0 || fhp->handle_bytes != 8) {
		log_err("name_to_handle_at");
		goto free_mem;
	}

	fhsize = sizeof(struct file_handle) + fhp->handle_bytes;
	fhp2 = realloc(fhp, fhsize);
	if (!fhp2) {
		log_err("realloc");
		goto free_mem;
	}
	err = name_to_handle_at(dirfd, cgroup_workdir, fhp2, &mount_id, flags);
	fhp = fhp2;
	if (err < 0) {
		log_err("name_to_handle_at");
		goto free_mem;
	}

	memcpy(id.raw_bytes, fhp->f_handle, 8);
	ret = id.cgid;

free_mem:
	free(fhp);
	return ret;
}

int cgroup_setup_and_join(const char *path) {
	int cg_fd;

	if (setup_cgroup_environment()) {
		fprintf(stderr, "Failed to setup cgroup environment\n");
		return -EINVAL;
	}

	cg_fd = create_and_get_cgroup(path);
	if (cg_fd < 0) {
		fprintf(stderr, "Failed to create test cgroup\n");
		cleanup_cgroup_environment();
		return cg_fd;
	}

	if (join_cgroup(path)) {
		fprintf(stderr, "Failed to join cgroup\n");
		cleanup_cgroup_environment();
		return -EINVAL;
	}
	return cg_fd;
}

/**
 * setup_classid_environment() - Setup the cgroupv1 net_cls environment
 *
 * After calling this function, cleanup_classid_environment should be called
 * once testing is complete.
 *
 * This function will print an error to stderr and return 1 if it is unable
 * to setup the cgroup environment. If setup is successful, 0 is returned.
 */
int setup_classid_environment(void)
{
	char cgroup_workdir[PATH_MAX + 1];

	format_classid_path(cgroup_workdir);

	if (mount("tmpfs", CGROUP_MOUNT_DFLT, "tmpfs", 0, NULL) &&
	    errno != EBUSY) {
		log_err("mount cgroup base");
		return 1;
	}

	if (mkdir(NETCLS_MOUNT_PATH, 0777) && errno != EEXIST) {
		log_err("mkdir cgroup net_cls");
		return 1;
	}

	if (mount("net_cls", NETCLS_MOUNT_PATH, "cgroup", 0, "net_cls")) {
		if (errno != EBUSY) {
			log_err("mount cgroup net_cls");
			return 1;
		}

		if (rmdir(NETCLS_MOUNT_PATH)) {
			log_err("rmdir cgroup net_cls");
			return 1;
		}
		if (umount(CGROUP_MOUNT_DFLT)) {
			log_err("umount cgroup base");
			return 1;
		}
	}

	cleanup_classid_environment();

	if (mkdir(cgroup_workdir, 0777) && errno != EEXIST) {
		log_err("mkdir cgroup work dir");
		return 1;
	}

	return 0;
}

/**
 * set_classid() - Set a cgroupv1 net_cls classid
 * @id: the numeric classid
 *
 * Writes the passed classid into the cgroup work dir's net_cls.classid
 * file in order to later on trigger socket tagging.
 *
 * On success, it returns 0, otherwise on failure it returns 1. If there
 * is a failure, it prints the error to stderr.
 */
int set_classid(unsigned int id)
{
	char cgroup_workdir[PATH_MAX - 42];
	char cgroup_classid_path[PATH_MAX + 1];
	int fd, rc = 0;

	format_classid_path(cgroup_workdir);
	snprintf(cgroup_classid_path, sizeof(cgroup_classid_path),
		 "%s/net_cls.classid", cgroup_workdir);

	fd = open(cgroup_classid_path, O_WRONLY);
	if (fd < 0) {
		log_err("Opening cgroup classid: %s", cgroup_classid_path);
		return 1;
	}

	if (dprintf(fd, "%u\n", id) < 0) {
		log_err("Setting cgroup classid");
		rc = 1;
	}

	close(fd);
	return rc;
}

/**
 * join_classid() - Join a cgroupv1 net_cls classid
 *
 * This function expects the cgroup work dir to be already created, as we
 * join it here. This causes the process sockets to be tagged with the given
 * net_cls classid.
 *
 * On success, it returns 0, otherwise on failure it returns 1.
 */
int join_classid(void)
{
	char cgroup_workdir[PATH_MAX + 1];

	format_classid_path(cgroup_workdir);
	return join_cgroup_from_top(cgroup_workdir);
}

/**
 * cleanup_classid_environment() - Cleanup the cgroupv1 net_cls environment
 *
 * At call time, it moves the calling process to the root cgroup, and then
 * runs the deletion process.
 *
 * On failure, it will print an error to stderr, and try to continue.
 */
void cleanup_classid_environment(void)
{
	char cgroup_workdir[PATH_MAX + 1];

	format_classid_path(cgroup_workdir);
	join_cgroup_from_top(NETCLS_MOUNT_PATH);
	nftw(cgroup_workdir, nftwfunc, WALK_FD_LIMIT, FTW_DEPTH | FTW_MOUNT);
}
