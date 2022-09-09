// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <string.h>
#include <syscall.h>
#include <sys/capability.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdarg.h>

/*
 * NOTES about this test:
 * - requries libcap-dev to be installed on test system
 * - requires securityfs to me mounted at /sys/kernel/security, e.g.:
 * mount -n -t securityfs -o nodev,noexec,nosuid securityfs /sys/kernel/security
 * - needs CONFIG_SECURITYFS and CONFIG_SAFESETID to be enabled
 */

#ifndef CLONE_NEWUSER
# define CLONE_NEWUSER 0x10000000
#endif

#define ROOT_UGID 0
#define RESTRICTED_PARENT_UGID 1
#define ALLOWED_CHILD1_UGID 2
#define ALLOWED_CHILD2_UGID 3
#define NO_POLICY_UGID 4

#define UGID_POLICY_STRING "1:2\n1:3\n2:2\n3:3\n"

char* add_uid_whitelist_policy_file = "/sys/kernel/security/safesetid/uid_allowlist_policy";
char* add_gid_whitelist_policy_file = "/sys/kernel/security/safesetid/gid_allowlist_policy";

static void die(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

static bool vmaybe_write_file(bool enoent_ok, char *filename, char *fmt, va_list ap)
{
	char buf[4096];
	int fd;
	ssize_t written;
	int buf_len;

	buf_len = vsnprintf(buf, sizeof(buf), fmt, ap);
	if (buf_len < 0) {
		printf("vsnprintf failed: %s\n",
		    strerror(errno));
		return false;
	}
	if (buf_len >= sizeof(buf)) {
		printf("vsnprintf output truncated\n");
		return false;
	}

	fd = open(filename, O_WRONLY);
	if (fd < 0) {
		if ((errno == ENOENT) && enoent_ok)
			return true;
		return false;
	}
	written = write(fd, buf, buf_len);
	if (written != buf_len) {
		if (written >= 0) {
			printf("short write to %s\n", filename);
			return false;
		} else {
			printf("write to %s failed: %s\n",
				filename, strerror(errno));
			return false;
		}
	}
	if (close(fd) != 0) {
		printf("close of %s failed: %s\n",
			filename, strerror(errno));
		return false;
	}
	return true;
}

static bool write_file(char *filename, char *fmt, ...)
{
	va_list ap;
	bool ret;

	va_start(ap, fmt);
	ret = vmaybe_write_file(false, filename, fmt, ap);
	va_end(ap);

	return ret;
}

static void ensure_user_exists(uid_t uid)
{
	struct passwd p;

	FILE *fd;
	char name_str[10];

	if (getpwuid(uid) == NULL) {
		memset(&p,0x00,sizeof(p));
		fd=fopen("/etc/passwd","a");
		if (fd == NULL)
			die("couldn't open file\n");
		if (fseek(fd, 0, SEEK_END))
			die("couldn't fseek\n");
		snprintf(name_str, 10, "user %d", uid);
		p.pw_name=name_str;
		p.pw_uid=uid;
		p.pw_gid=uid;
		p.pw_gecos="Test account";
		p.pw_dir="/dev/null";
		p.pw_shell="/bin/false";
		int value = putpwent(&p,fd);
		if (value != 0)
			die("putpwent failed\n");
		if (fclose(fd))
			die("fclose failed\n");
	}
}

static void ensure_group_exists(gid_t gid)
{
	struct group g;

	FILE *fd;
	char name_str[10];

	if (getgrgid(gid) == NULL) {
		memset(&g,0x00,sizeof(g));
		fd=fopen("/etc/group","a");
		if (fd == NULL)
			die("couldn't open group file\n");
		if (fseek(fd, 0, SEEK_END))
			die("couldn't fseek group file\n");
		snprintf(name_str, 10, "group %d", gid);
		g.gr_name=name_str;
		g.gr_gid=gid;
		g.gr_passwd=NULL;
		g.gr_mem=NULL;
		int value = putgrent(&g,fd);
		if (value != 0)
			die("putgrent failed\n");
		if (fclose(fd))
			die("fclose failed\n");
	}
}

static void ensure_securityfs_mounted(void)
{
	int fd = open(add_uid_whitelist_policy_file, O_WRONLY);
	if (fd < 0) {
		if (errno == ENOENT) {
			// Need to mount securityfs
			if (mount("securityfs", "/sys/kernel/security",
						"securityfs", 0, NULL) < 0)
				die("mounting securityfs failed\n");
		} else {
			die("couldn't find securityfs for unknown reason\n");
		}
	} else {
		if (close(fd) != 0) {
			die("close of %s failed: %s\n",
				add_uid_whitelist_policy_file, strerror(errno));
		}
	}
}

static void write_uid_policies()
{
	static char *policy_str = UGID_POLICY_STRING;
	ssize_t written;
	int fd;

	fd = open(add_uid_whitelist_policy_file, O_WRONLY);
	if (fd < 0)
		die("can't open add_uid_whitelist_policy file\n");
	written = write(fd, policy_str, strlen(policy_str));
	if (written != strlen(policy_str)) {
		if (written >= 0) {
			die("short write to %s\n", add_uid_whitelist_policy_file);
		} else {
			die("write to %s failed: %s\n",
				add_uid_whitelist_policy_file, strerror(errno));
		}
	}
	if (close(fd) != 0) {
		die("close of %s failed: %s\n",
			add_uid_whitelist_policy_file, strerror(errno));
	}
}

static void write_gid_policies()
{
	static char *policy_str = UGID_POLICY_STRING;
	ssize_t written;
	int fd;

	fd = open(add_gid_whitelist_policy_file, O_WRONLY);
	if (fd < 0)
		die("can't open add_gid_whitelist_policy file\n");
	written = write(fd, policy_str, strlen(policy_str));
	if (written != strlen(policy_str)) {
		if (written >= 0) {
			die("short write to %s\n", add_gid_whitelist_policy_file);
		} else {
			die("write to %s failed: %s\n",
				add_gid_whitelist_policy_file, strerror(errno));
		}
	}
	if (close(fd) != 0) {
		die("close of %s failed: %s\n",
			add_gid_whitelist_policy_file, strerror(errno));
	}
}


static bool test_userns(bool expect_success)
{
	uid_t uid;
	char map_file_name[32];
	size_t sz = sizeof(map_file_name);
	pid_t cpid;
	bool success;

	uid = getuid();

	int clone_flags = CLONE_NEWUSER;
	cpid = syscall(SYS_clone, clone_flags, NULL);
	if (cpid == -1) {
	    printf("clone failed");
	    return false;
	}

	if (cpid == 0) {	/* Code executed by child */
		// Give parent 1 second to write map file
		sleep(1);
		exit(EXIT_SUCCESS);
	} else {		/* Code executed by parent */
		if(snprintf(map_file_name, sz, "/proc/%d/uid_map", cpid) < 0) {
			printf("preparing file name string failed");
			return false;
		}
		success = write_file(map_file_name, "0 %d 1", uid);
		return success == expect_success;
	}

	printf("should not reach here");
	return false;
}

static void test_setuid(uid_t child_uid, bool expect_success)
{
	pid_t cpid, w;
	int wstatus;

	cpid = fork();
	if (cpid == -1) {
		die("fork\n");
	}

	if (cpid == 0) {	    /* Code executed by child */
		if (setuid(child_uid) < 0)
			exit(EXIT_FAILURE);
		if (getuid() == child_uid)
			exit(EXIT_SUCCESS);
		else
			exit(EXIT_FAILURE);
	} else {		 /* Code executed by parent */
		do {
			w = waitpid(cpid, &wstatus, WUNTRACED | WCONTINUED);
			if (w == -1) {
				die("waitpid\n");
			}

			if (WIFEXITED(wstatus)) {
				if (WEXITSTATUS(wstatus) == EXIT_SUCCESS) {
					if (expect_success) {
						return;
					} else {
						die("unexpected success\n");
					}
				} else {
					if (expect_success) {
						die("unexpected failure\n");
					} else {
						return;
					}
				}
			} else if (WIFSIGNALED(wstatus)) {
				if (WTERMSIG(wstatus) == 9) {
					if (expect_success)
						die("killed unexpectedly\n");
					else
						return;
				} else {
					die("unexpected signal: %d\n", wstatus);
				}
			} else {
				die("unexpected status: %d\n", wstatus);
			}
		} while (!WIFEXITED(wstatus) && !WIFSIGNALED(wstatus));
	}

	die("should not reach here\n");
}

static void test_setgid(gid_t child_gid, bool expect_success)
{
	pid_t cpid, w;
	int wstatus;

	cpid = fork();
	if (cpid == -1) {
		die("fork\n");
	}

	if (cpid == 0) {	    /* Code executed by child */
		if (setgid(child_gid) < 0)
			exit(EXIT_FAILURE);
		if (getgid() == child_gid)
			exit(EXIT_SUCCESS);
		else
			exit(EXIT_FAILURE);
	} else {		 /* Code executed by parent */
		do {
			w = waitpid(cpid, &wstatus, WUNTRACED | WCONTINUED);
			if (w == -1) {
				die("waitpid\n");
			}

			if (WIFEXITED(wstatus)) {
				if (WEXITSTATUS(wstatus) == EXIT_SUCCESS) {
					if (expect_success) {
						return;
					} else {
						die("unexpected success\n");
					}
				} else {
					if (expect_success) {
						die("unexpected failure\n");
					} else {
						return;
					}
				}
			} else if (WIFSIGNALED(wstatus)) {
				if (WTERMSIG(wstatus) == 9) {
					if (expect_success)
						die("killed unexpectedly\n");
					else
						return;
				} else {
					die("unexpected signal: %d\n", wstatus);
				}
			} else {
				die("unexpected status: %d\n", wstatus);
			}
		} while (!WIFEXITED(wstatus) && !WIFSIGNALED(wstatus));
	}

	die("should not reach here\n");
}

static void test_setgroups(gid_t* child_groups, size_t len, bool expect_success)
{
	pid_t cpid, w;
	int wstatus;
	gid_t groupset[len];
	int i, j;

	cpid = fork();
	if (cpid == -1) {
		die("fork\n");
	}

	if (cpid == 0) {	    /* Code executed by child */
		if (setgroups(len, child_groups) != 0)
			exit(EXIT_FAILURE);
		if (getgroups(len, groupset) != len)
			exit(EXIT_FAILURE);
		for (i = 0; i < len; i++) {
			for (j = 0; j < len; j++) {
				if (child_groups[i] == groupset[j])
					break;
				if (j == len - 1)
					exit(EXIT_FAILURE);
			}
		}
		exit(EXIT_SUCCESS);
	} else {		 /* Code executed by parent */
		do {
			w = waitpid(cpid, &wstatus, WUNTRACED | WCONTINUED);
			if (w == -1) {
				die("waitpid\n");
			}

			if (WIFEXITED(wstatus)) {
				if (WEXITSTATUS(wstatus) == EXIT_SUCCESS) {
					if (expect_success) {
						return;
					} else {
						die("unexpected success\n");
					}
				} else {
					if (expect_success) {
						die("unexpected failure\n");
					} else {
						return;
					}
				}
			} else if (WIFSIGNALED(wstatus)) {
				if (WTERMSIG(wstatus) == 9) {
					if (expect_success)
						die("killed unexpectedly\n");
					else
						return;
				} else {
					die("unexpected signal: %d\n", wstatus);
				}
			} else {
				die("unexpected status: %d\n", wstatus);
			}
		} while (!WIFEXITED(wstatus) && !WIFSIGNALED(wstatus));
	}

	die("should not reach here\n");
}


static void ensure_users_exist(void)
{
	ensure_user_exists(ROOT_UGID);
	ensure_user_exists(RESTRICTED_PARENT_UGID);
	ensure_user_exists(ALLOWED_CHILD1_UGID);
	ensure_user_exists(ALLOWED_CHILD2_UGID);
	ensure_user_exists(NO_POLICY_UGID);
}

static void ensure_groups_exist(void)
{
	ensure_group_exists(ROOT_UGID);
	ensure_group_exists(RESTRICTED_PARENT_UGID);
	ensure_group_exists(ALLOWED_CHILD1_UGID);
	ensure_group_exists(ALLOWED_CHILD2_UGID);
	ensure_group_exists(NO_POLICY_UGID);
}

static void drop_caps(bool setid_retained)
{
	cap_value_t cap_values[] = {CAP_SETUID, CAP_SETGID};
	cap_t caps;

	caps = cap_get_proc();
	if (setid_retained)
		cap_set_flag(caps, CAP_EFFECTIVE, 2, cap_values, CAP_SET);
	else
		cap_clear(caps);
	cap_set_proc(caps);
	cap_free(caps);
}

int main(int argc, char **argv)
{
	ensure_groups_exist();
	ensure_users_exist();
	ensure_securityfs_mounted();
	write_uid_policies();
	write_gid_policies();

	if (prctl(PR_SET_KEEPCAPS, 1L))
		die("Error with set keepcaps\n");

	// First test to make sure we can write userns mappings from a non-root
	// user that doesn't have any restrictions (as long as it has
	// CAP_SETUID);
	if (setgid(NO_POLICY_UGID) < 0)
		die("Error with set gid(%d)\n", NO_POLICY_UGID);
	if (setuid(NO_POLICY_UGID) < 0)
		die("Error with set uid(%d)\n", NO_POLICY_UGID);
	// Take away all but setid caps
	drop_caps(true);
	// Need PR_SET_DUMPABLE flag set so we can write /proc/[pid]/uid_map
	// from non-root parent process.
	if (prctl(PR_SET_DUMPABLE, 1, 0, 0, 0))
		die("Error with set dumpable\n");
	if (!test_userns(true)) {
		die("test_userns failed when it should work\n");
	}

	// Now switch to a user/group with restrictions
	if (setgid(RESTRICTED_PARENT_UGID) < 0)
		die("Error with set gid(%d)\n", RESTRICTED_PARENT_UGID);
	if (setuid(RESTRICTED_PARENT_UGID) < 0)
		die("Error with set uid(%d)\n", RESTRICTED_PARENT_UGID);

	test_setuid(ROOT_UGID, false);
	test_setuid(ALLOWED_CHILD1_UGID, true);
	test_setuid(ALLOWED_CHILD2_UGID, true);
	test_setuid(NO_POLICY_UGID, false);

	test_setgid(ROOT_UGID, false);
	test_setgid(ALLOWED_CHILD1_UGID, true);
	test_setgid(ALLOWED_CHILD2_UGID, true);
	test_setgid(NO_POLICY_UGID, false);

	gid_t allowed_supp_groups[2] = {ALLOWED_CHILD1_UGID, ALLOWED_CHILD2_UGID};
	gid_t disallowed_supp_groups[2] = {ROOT_UGID, NO_POLICY_UGID};
	test_setgroups(allowed_supp_groups, 2, true);
	test_setgroups(disallowed_supp_groups, 2, false);

	if (!test_userns(false)) {
		die("test_userns worked when it should fail\n");
	}

	// Now take away all caps
	drop_caps(false);
	test_setuid(2, false);
	test_setuid(3, false);
	test_setuid(4, false);
	test_setgid(2, false);
	test_setgid(3, false);
	test_setgid(4, false);

	// NOTE: this test doesn't clean up users that were created in
	// /etc/passwd or flush policies that were added to the LSM.
	printf("test successful!\n");
	return EXIT_SUCCESS;
}
