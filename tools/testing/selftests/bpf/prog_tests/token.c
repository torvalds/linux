// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2023 Meta Platforms, Inc. and affiliates. */
#define _GNU_SOURCE
#include <test_progs.h>
#include <bpf/btf.h>
#include "cap_helpers.h"
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <unistd.h>
#include <linux/filter.h>
#include <linux/unistd.h>
#include <linux/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include "priv_map.skel.h"
#include "priv_prog.skel.h"
#include "dummy_st_ops_success.skel.h"
#include "token_lsm.skel.h"

static inline int sys_mount(const char *dev_name, const char *dir_name,
			    const char *type, unsigned long flags,
			    const void *data)
{
	return syscall(__NR_mount, dev_name, dir_name, type, flags, data);
}

static inline int sys_fsopen(const char *fsname, unsigned flags)
{
	return syscall(__NR_fsopen, fsname, flags);
}

static inline int sys_fspick(int dfd, const char *path, unsigned flags)
{
	return syscall(__NR_fspick, dfd, path, flags);
}

static inline int sys_fsconfig(int fs_fd, unsigned cmd, const char *key, const void *val, int aux)
{
	return syscall(__NR_fsconfig, fs_fd, cmd, key, val, aux);
}

static inline int sys_fsmount(int fs_fd, unsigned flags, unsigned ms_flags)
{
	return syscall(__NR_fsmount, fs_fd, flags, ms_flags);
}

static inline int sys_move_mount(int from_dfd, const char *from_path,
				 int to_dfd, const char *to_path,
				 unsigned flags)
{
	return syscall(__NR_move_mount, from_dfd, from_path, to_dfd, to_path, flags);
}

static int drop_priv_caps(__u64 *old_caps)
{
	return cap_disable_effective((1ULL << CAP_BPF) |
				     (1ULL << CAP_PERFMON) |
				     (1ULL << CAP_NET_ADMIN) |
				     (1ULL << CAP_SYS_ADMIN), old_caps);
}

static int restore_priv_caps(__u64 old_caps)
{
	return cap_enable_effective(old_caps, NULL);
}

static int set_delegate_mask(int fs_fd, const char *key, __u64 mask, const char *mask_str)
{
	char buf[32];
	int err;

	if (!mask_str) {
		if (mask == ~0ULL) {
			mask_str = "any";
		} else {
			snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)mask);
			mask_str = buf;
		}
	}

	err = sys_fsconfig(fs_fd, FSCONFIG_SET_STRING, key,
			   mask_str, 0);
	if (err < 0)
		err = -errno;
	return err;
}

#define zclose(fd) do { if (fd >= 0) close(fd); fd = -1; } while (0)

struct bpffs_opts {
	__u64 cmds;
	__u64 maps;
	__u64 progs;
	__u64 attachs;
	const char *cmds_str;
	const char *maps_str;
	const char *progs_str;
	const char *attachs_str;
};

static int create_bpffs_fd(void)
{
	int fs_fd;

	/* create VFS context */
	fs_fd = sys_fsopen("bpf", 0);
	ASSERT_GE(fs_fd, 0, "fs_fd");

	return fs_fd;
}

static int materialize_bpffs_fd(int fs_fd, struct bpffs_opts *opts)
{
	int mnt_fd, err;

	/* set up token delegation mount options */
	err = set_delegate_mask(fs_fd, "delegate_cmds", opts->cmds, opts->cmds_str);
	if (!ASSERT_OK(err, "fs_cfg_cmds"))
		return err;
	err = set_delegate_mask(fs_fd, "delegate_maps", opts->maps, opts->maps_str);
	if (!ASSERT_OK(err, "fs_cfg_maps"))
		return err;
	err = set_delegate_mask(fs_fd, "delegate_progs", opts->progs, opts->progs_str);
	if (!ASSERT_OK(err, "fs_cfg_progs"))
		return err;
	err = set_delegate_mask(fs_fd, "delegate_attachs", opts->attachs, opts->attachs_str);
	if (!ASSERT_OK(err, "fs_cfg_attachs"))
		return err;

	/* instantiate FS object */
	err = sys_fsconfig(fs_fd, FSCONFIG_CMD_CREATE, NULL, NULL, 0);
	if (err < 0)
		return -errno;

	/* create O_PATH fd for detached mount */
	mnt_fd = sys_fsmount(fs_fd, 0, 0);
	if (err < 0)
		return -errno;

	return mnt_fd;
}

/* send FD over Unix domain (AF_UNIX) socket */
static int sendfd(int sockfd, int fd)
{
	struct msghdr msg = {};
	struct cmsghdr *cmsg;
	int fds[1] = { fd }, err;
	char iobuf[1];
	struct iovec io = {
		.iov_base = iobuf,
		.iov_len = sizeof(iobuf),
	};
	union {
		char buf[CMSG_SPACE(sizeof(fds))];
		struct cmsghdr align;
	} u;

	msg.msg_iov = &io;
	msg.msg_iovlen = 1;
	msg.msg_control = u.buf;
	msg.msg_controllen = sizeof(u.buf);
	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(sizeof(fds));
	memcpy(CMSG_DATA(cmsg), fds, sizeof(fds));

	err = sendmsg(sockfd, &msg, 0);
	if (err < 0)
		err = -errno;
	if (!ASSERT_EQ(err, 1, "sendmsg"))
		return -EINVAL;

	return 0;
}

/* receive FD over Unix domain (AF_UNIX) socket */
static int recvfd(int sockfd, int *fd)
{
	struct msghdr msg = {};
	struct cmsghdr *cmsg;
	int fds[1], err;
	char iobuf[1];
	struct iovec io = {
		.iov_base = iobuf,
		.iov_len = sizeof(iobuf),
	};
	union {
		char buf[CMSG_SPACE(sizeof(fds))];
		struct cmsghdr align;
	} u;

	msg.msg_iov = &io;
	msg.msg_iovlen = 1;
	msg.msg_control = u.buf;
	msg.msg_controllen = sizeof(u.buf);

	err = recvmsg(sockfd, &msg, 0);
	if (err < 0)
		err = -errno;
	if (!ASSERT_EQ(err, 1, "recvmsg"))
		return -EINVAL;

	cmsg = CMSG_FIRSTHDR(&msg);
	if (!ASSERT_OK_PTR(cmsg, "cmsg_null") ||
	    !ASSERT_EQ(cmsg->cmsg_len, CMSG_LEN(sizeof(fds)), "cmsg_len") ||
	    !ASSERT_EQ(cmsg->cmsg_level, SOL_SOCKET, "cmsg_level") ||
	    !ASSERT_EQ(cmsg->cmsg_type, SCM_RIGHTS, "cmsg_type"))
		return -EINVAL;

	memcpy(fds, CMSG_DATA(cmsg), sizeof(fds));
	*fd = fds[0];

	return 0;
}

static ssize_t write_nointr(int fd, const void *buf, size_t count)
{
	ssize_t ret;

	do {
		ret = write(fd, buf, count);
	} while (ret < 0 && errno == EINTR);

	return ret;
}

static int write_file(const char *path, const void *buf, size_t count)
{
	int fd;
	ssize_t ret;

	fd = open(path, O_WRONLY | O_CLOEXEC | O_NOCTTY | O_NOFOLLOW);
	if (fd < 0)
		return -1;

	ret = write_nointr(fd, buf, count);
	close(fd);
	if (ret < 0 || (size_t)ret != count)
		return -1;

	return 0;
}

static int create_and_enter_userns(void)
{
	uid_t uid;
	gid_t gid;
	char map[100];

	uid = getuid();
	gid = getgid();

	if (unshare(CLONE_NEWUSER))
		return -1;

	if (write_file("/proc/self/setgroups", "deny", sizeof("deny") - 1) &&
	    errno != ENOENT)
		return -1;

	snprintf(map, sizeof(map), "0 %d 1", uid);
	if (write_file("/proc/self/uid_map", map, strlen(map)))
		return -1;


	snprintf(map, sizeof(map), "0 %d 1", gid);
	if (write_file("/proc/self/gid_map", map, strlen(map)))
		return -1;

	if (setgid(0))
		return -1;

	if (setuid(0))
		return -1;

	return 0;
}

typedef int (*child_callback_fn)(int bpffs_fd, struct token_lsm *lsm_skel);

static void child(int sock_fd, struct bpffs_opts *opts, child_callback_fn callback)
{
	int mnt_fd = -1, fs_fd = -1, err = 0, bpffs_fd = -1, token_fd = -1;
	struct token_lsm *lsm_skel = NULL;

	/* load and attach LSM "policy" before we go into unpriv userns */
	lsm_skel = token_lsm__open_and_load();
	if (!ASSERT_OK_PTR(lsm_skel, "lsm_skel_load")) {
		err = -EINVAL;
		goto cleanup;
	}
	lsm_skel->bss->my_pid = getpid();
	err = token_lsm__attach(lsm_skel);
	if (!ASSERT_OK(err, "lsm_skel_attach"))
		goto cleanup;

	/* setup userns with root mappings */
	err = create_and_enter_userns();
	if (!ASSERT_OK(err, "create_and_enter_userns"))
		goto cleanup;

	/* setup mountns to allow creating BPF FS (fsopen("bpf")) from unpriv process */
	err = unshare(CLONE_NEWNS);
	if (!ASSERT_OK(err, "create_mountns"))
		goto cleanup;

	err = sys_mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, 0);
	if (!ASSERT_OK(err, "remount_root"))
		goto cleanup;

	fs_fd = create_bpffs_fd();
	if (!ASSERT_GE(fs_fd, 0, "create_bpffs_fd")) {
		err = -EINVAL;
		goto cleanup;
	}

	/* ensure unprivileged child cannot set delegation options */
	err = set_delegate_mask(fs_fd, "delegate_cmds", 0x1, NULL);
	ASSERT_EQ(err, -EPERM, "delegate_cmd_eperm");
	err = set_delegate_mask(fs_fd, "delegate_maps", 0x1, NULL);
	ASSERT_EQ(err, -EPERM, "delegate_maps_eperm");
	err = set_delegate_mask(fs_fd, "delegate_progs", 0x1, NULL);
	ASSERT_EQ(err, -EPERM, "delegate_progs_eperm");
	err = set_delegate_mask(fs_fd, "delegate_attachs", 0x1, NULL);
	ASSERT_EQ(err, -EPERM, "delegate_attachs_eperm");

	/* pass BPF FS context object to parent */
	err = sendfd(sock_fd, fs_fd);
	if (!ASSERT_OK(err, "send_fs_fd"))
		goto cleanup;
	zclose(fs_fd);

	/* avoid mucking around with mount namespaces and mounting at
	 * well-known path, just get detach-mounted BPF FS fd back from parent
	 */
	err = recvfd(sock_fd, &mnt_fd);
	if (!ASSERT_OK(err, "recv_mnt_fd"))
		goto cleanup;

	/* try to fspick() BPF FS and try to add some delegation options */
	fs_fd = sys_fspick(mnt_fd, "", FSPICK_EMPTY_PATH);
	if (!ASSERT_GE(fs_fd, 0, "bpffs_fspick")) {
		err = -EINVAL;
		goto cleanup;
	}

	/* ensure unprivileged child cannot reconfigure to set delegation options */
	err = set_delegate_mask(fs_fd, "delegate_cmds", 0, "any");
	if (!ASSERT_EQ(err, -EPERM, "delegate_cmd_eperm_reconfig")) {
		err = -EINVAL;
		goto cleanup;
	}
	err = set_delegate_mask(fs_fd, "delegate_maps", 0, "any");
	if (!ASSERT_EQ(err, -EPERM, "delegate_maps_eperm_reconfig")) {
		err = -EINVAL;
		goto cleanup;
	}
	err = set_delegate_mask(fs_fd, "delegate_progs", 0, "any");
	if (!ASSERT_EQ(err, -EPERM, "delegate_progs_eperm_reconfig")) {
		err = -EINVAL;
		goto cleanup;
	}
	err = set_delegate_mask(fs_fd, "delegate_attachs", 0, "any");
	if (!ASSERT_EQ(err, -EPERM, "delegate_attachs_eperm_reconfig")) {
		err = -EINVAL;
		goto cleanup;
	}
	zclose(fs_fd);

	bpffs_fd = openat(mnt_fd, ".", 0, O_RDWR);
	if (!ASSERT_GE(bpffs_fd, 0, "bpffs_open")) {
		err = -EINVAL;
		goto cleanup;
	}

	/* create BPF token FD and pass it to parent for some extra checks */
	token_fd = bpf_token_create(bpffs_fd, NULL);
	if (!ASSERT_GT(token_fd, 0, "child_token_create")) {
		err = -EINVAL;
		goto cleanup;
	}
	err = sendfd(sock_fd, token_fd);
	if (!ASSERT_OK(err, "send_token_fd"))
		goto cleanup;
	zclose(token_fd);

	/* do custom test logic with customly set up BPF FS instance */
	err = callback(bpffs_fd, lsm_skel);
	if (!ASSERT_OK(err, "test_callback"))
		goto cleanup;

	err = 0;
cleanup:
	zclose(sock_fd);
	zclose(mnt_fd);
	zclose(fs_fd);
	zclose(bpffs_fd);
	zclose(token_fd);

	lsm_skel->bss->my_pid = 0;
	token_lsm__destroy(lsm_skel);

	exit(-err);
}

static int wait_for_pid(pid_t pid)
{
	int status, ret;

again:
	ret = waitpid(pid, &status, 0);
	if (ret == -1) {
		if (errno == EINTR)
			goto again;

		return -1;
	}

	if (!WIFEXITED(status))
		return -1;

	return WEXITSTATUS(status);
}

static void parent(int child_pid, struct bpffs_opts *bpffs_opts, int sock_fd)
{
	int fs_fd = -1, mnt_fd = -1, token_fd = -1, err;

	err = recvfd(sock_fd, &fs_fd);
	if (!ASSERT_OK(err, "recv_bpffs_fd"))
		goto cleanup;

	mnt_fd = materialize_bpffs_fd(fs_fd, bpffs_opts);
	if (!ASSERT_GE(mnt_fd, 0, "materialize_bpffs_fd")) {
		err = -EINVAL;
		goto cleanup;
	}
	zclose(fs_fd);

	/* pass BPF FS context object to parent */
	err = sendfd(sock_fd, mnt_fd);
	if (!ASSERT_OK(err, "send_mnt_fd"))
		goto cleanup;
	zclose(mnt_fd);

	/* receive BPF token FD back from child for some extra tests */
	err = recvfd(sock_fd, &token_fd);
	if (!ASSERT_OK(err, "recv_token_fd"))
		goto cleanup;

	err = wait_for_pid(child_pid);
	ASSERT_OK(err, "waitpid_child");

cleanup:
	zclose(sock_fd);
	zclose(fs_fd);
	zclose(mnt_fd);
	zclose(token_fd);

	if (child_pid > 0)
		(void)kill(child_pid, SIGKILL);
}

static void subtest_userns(struct bpffs_opts *bpffs_opts,
			   child_callback_fn child_cb)
{
	int sock_fds[2] = { -1, -1 };
	int child_pid = 0, err;

	err = socketpair(AF_UNIX, SOCK_STREAM, 0, sock_fds);
	if (!ASSERT_OK(err, "socketpair"))
		goto cleanup;

	child_pid = fork();
	if (!ASSERT_GE(child_pid, 0, "fork"))
		goto cleanup;

	if (child_pid == 0) {
		zclose(sock_fds[0]);
		return child(sock_fds[1], bpffs_opts, child_cb);

	} else {
		zclose(sock_fds[1]);
		return parent(child_pid, bpffs_opts, sock_fds[0]);
	}

cleanup:
	zclose(sock_fds[0]);
	zclose(sock_fds[1]);
	if (child_pid > 0)
		(void)kill(child_pid, SIGKILL);
}

static int userns_map_create(int mnt_fd, struct token_lsm *lsm_skel)
{
	LIBBPF_OPTS(bpf_map_create_opts, map_opts);
	int err, token_fd = -1, map_fd = -1;
	__u64 old_caps = 0;

	/* create BPF token from BPF FS mount */
	token_fd = bpf_token_create(mnt_fd, NULL);
	if (!ASSERT_GT(token_fd, 0, "token_create")) {
		err = -EINVAL;
		goto cleanup;
	}

	/* while inside non-init userns, we need both a BPF token *and*
	 * CAP_BPF inside current userns to create privileged map; let's test
	 * that neither BPF token alone nor namespaced CAP_BPF is sufficient
	 */
	err = drop_priv_caps(&old_caps);
	if (!ASSERT_OK(err, "drop_caps"))
		goto cleanup;

	/* no token, no CAP_BPF -> fail */
	map_opts.map_flags = 0;
	map_opts.token_fd = 0;
	map_fd = bpf_map_create(BPF_MAP_TYPE_STACK, "wo_token_wo_bpf", 0, 8, 1, &map_opts);
	if (!ASSERT_LT(map_fd, 0, "stack_map_wo_token_wo_cap_bpf_should_fail")) {
		err = -EINVAL;
		goto cleanup;
	}

	/* token without CAP_BPF -> fail */
	map_opts.map_flags = BPF_F_TOKEN_FD;
	map_opts.token_fd = token_fd;
	map_fd = bpf_map_create(BPF_MAP_TYPE_STACK, "w_token_wo_bpf", 0, 8, 1, &map_opts);
	if (!ASSERT_LT(map_fd, 0, "stack_map_w_token_wo_cap_bpf_should_fail")) {
		err = -EINVAL;
		goto cleanup;
	}

	/* get back effective local CAP_BPF (and CAP_SYS_ADMIN) */
	err = restore_priv_caps(old_caps);
	if (!ASSERT_OK(err, "restore_caps"))
		goto cleanup;

	/* CAP_BPF without token -> fail */
	map_opts.map_flags = 0;
	map_opts.token_fd = 0;
	map_fd = bpf_map_create(BPF_MAP_TYPE_STACK, "wo_token_w_bpf", 0, 8, 1, &map_opts);
	if (!ASSERT_LT(map_fd, 0, "stack_map_wo_token_w_cap_bpf_should_fail")) {
		err = -EINVAL;
		goto cleanup;
	}

	/* finally, namespaced CAP_BPF + token -> success */
	map_opts.map_flags = BPF_F_TOKEN_FD;
	map_opts.token_fd = token_fd;
	map_fd = bpf_map_create(BPF_MAP_TYPE_STACK, "w_token_w_bpf", 0, 8, 1, &map_opts);
	if (!ASSERT_GT(map_fd, 0, "stack_map_w_token_w_cap_bpf")) {
		err = -EINVAL;
		goto cleanup;
	}

cleanup:
	zclose(token_fd);
	zclose(map_fd);
	return err;
}

static int userns_btf_load(int mnt_fd, struct token_lsm *lsm_skel)
{
	LIBBPF_OPTS(bpf_btf_load_opts, btf_opts);
	int err, token_fd = -1, btf_fd = -1;
	const void *raw_btf_data;
	struct btf *btf = NULL;
	__u32 raw_btf_size;
	__u64 old_caps = 0;

	/* create BPF token from BPF FS mount */
	token_fd = bpf_token_create(mnt_fd, NULL);
	if (!ASSERT_GT(token_fd, 0, "token_create")) {
		err = -EINVAL;
		goto cleanup;
	}

	/* while inside non-init userns, we need both a BPF token *and*
	 * CAP_BPF inside current userns to create privileged map; let's test
	 * that neither BPF token alone nor namespaced CAP_BPF is sufficient
	 */
	err = drop_priv_caps(&old_caps);
	if (!ASSERT_OK(err, "drop_caps"))
		goto cleanup;

	/* setup a trivial BTF data to load to the kernel */
	btf = btf__new_empty();
	if (!ASSERT_OK_PTR(btf, "empty_btf"))
		goto cleanup;

	ASSERT_GT(btf__add_int(btf, "int", 4, 0), 0, "int_type");

	raw_btf_data = btf__raw_data(btf, &raw_btf_size);
	if (!ASSERT_OK_PTR(raw_btf_data, "raw_btf_data"))
		goto cleanup;

	/* no token + no CAP_BPF -> failure */
	btf_opts.btf_flags = 0;
	btf_opts.token_fd = 0;
	btf_fd = bpf_btf_load(raw_btf_data, raw_btf_size, &btf_opts);
	if (!ASSERT_LT(btf_fd, 0, "no_token_no_cap_should_fail"))
		goto cleanup;

	/* token + no CAP_BPF -> failure */
	btf_opts.btf_flags = BPF_F_TOKEN_FD;
	btf_opts.token_fd = token_fd;
	btf_fd = bpf_btf_load(raw_btf_data, raw_btf_size, &btf_opts);
	if (!ASSERT_LT(btf_fd, 0, "token_no_cap_should_fail"))
		goto cleanup;

	/* get back effective local CAP_BPF (and CAP_SYS_ADMIN) */
	err = restore_priv_caps(old_caps);
	if (!ASSERT_OK(err, "restore_caps"))
		goto cleanup;

	/* token + CAP_BPF -> success */
	btf_opts.btf_flags = BPF_F_TOKEN_FD;
	btf_opts.token_fd = token_fd;
	btf_fd = bpf_btf_load(raw_btf_data, raw_btf_size, &btf_opts);
	if (!ASSERT_GT(btf_fd, 0, "token_and_cap_success"))
		goto cleanup;

	err = 0;
cleanup:
	btf__free(btf);
	zclose(btf_fd);
	zclose(token_fd);
	return err;
}

static int userns_prog_load(int mnt_fd, struct token_lsm *lsm_skel)
{
	LIBBPF_OPTS(bpf_prog_load_opts, prog_opts);
	int err, token_fd = -1, prog_fd = -1;
	struct bpf_insn insns[] = {
		/* bpf_jiffies64() requires CAP_BPF */
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_jiffies64),
		/* bpf_get_current_task() requires CAP_PERFMON */
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_get_current_task),
		/* r0 = 0; exit; */
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	size_t insn_cnt = ARRAY_SIZE(insns);
	__u64 old_caps = 0;

	/* create BPF token from BPF FS mount */
	token_fd = bpf_token_create(mnt_fd, NULL);
	if (!ASSERT_GT(token_fd, 0, "token_create")) {
		err = -EINVAL;
		goto cleanup;
	}

	/* validate we can successfully load BPF program with token; this
	 * being XDP program (CAP_NET_ADMIN) using bpf_jiffies64() (CAP_BPF)
	 * and bpf_get_current_task() (CAP_PERFMON) helpers validates we have
	 * BPF token wired properly in a bunch of places in the kernel
	 */
	prog_opts.prog_flags = BPF_F_TOKEN_FD;
	prog_opts.token_fd = token_fd;
	prog_opts.expected_attach_type = BPF_XDP;
	prog_fd = bpf_prog_load(BPF_PROG_TYPE_XDP, "token_prog", "GPL",
				insns, insn_cnt, &prog_opts);
	if (!ASSERT_GT(prog_fd, 0, "prog_fd")) {
		err = -EPERM;
		goto cleanup;
	}

	/* no token + caps -> failure */
	prog_opts.prog_flags = 0;
	prog_opts.token_fd = 0;
	prog_fd = bpf_prog_load(BPF_PROG_TYPE_XDP, "token_prog", "GPL",
				insns, insn_cnt, &prog_opts);
	if (!ASSERT_EQ(prog_fd, -EPERM, "prog_fd_eperm")) {
		err = -EPERM;
		goto cleanup;
	}

	err = drop_priv_caps(&old_caps);
	if (!ASSERT_OK(err, "drop_caps"))
		goto cleanup;

	/* no caps + token -> failure */
	prog_opts.prog_flags = BPF_F_TOKEN_FD;
	prog_opts.token_fd = token_fd;
	prog_fd = bpf_prog_load(BPF_PROG_TYPE_XDP, "token_prog", "GPL",
				insns, insn_cnt, &prog_opts);
	if (!ASSERT_EQ(prog_fd, -EPERM, "prog_fd_eperm")) {
		err = -EPERM;
		goto cleanup;
	}

	/* no caps + no token -> definitely a failure */
	prog_opts.prog_flags = 0;
	prog_opts.token_fd = 0;
	prog_fd = bpf_prog_load(BPF_PROG_TYPE_XDP, "token_prog", "GPL",
				insns, insn_cnt, &prog_opts);
	if (!ASSERT_EQ(prog_fd, -EPERM, "prog_fd_eperm")) {
		err = -EPERM;
		goto cleanup;
	}

	err = 0;
cleanup:
	zclose(prog_fd);
	zclose(token_fd);
	return err;
}

static int userns_obj_priv_map(int mnt_fd, struct token_lsm *lsm_skel)
{
	LIBBPF_OPTS(bpf_object_open_opts, opts);
	char buf[256];
	struct priv_map *skel;
	int err;

	skel = priv_map__open_and_load();
	if (!ASSERT_ERR_PTR(skel, "obj_tokenless_load")) {
		priv_map__destroy(skel);
		return -EINVAL;
	}

	/* use bpf_token_path to provide BPF FS path */
	snprintf(buf, sizeof(buf), "/proc/self/fd/%d", mnt_fd);
	opts.bpf_token_path = buf;
	skel = priv_map__open_opts(&opts);
	if (!ASSERT_OK_PTR(skel, "obj_token_path_open"))
		return -EINVAL;

	err = priv_map__load(skel);
	priv_map__destroy(skel);
	if (!ASSERT_OK(err, "obj_token_path_load"))
		return -EINVAL;

	return 0;
}

static int userns_obj_priv_prog(int mnt_fd, struct token_lsm *lsm_skel)
{
	LIBBPF_OPTS(bpf_object_open_opts, opts);
	char buf[256];
	struct priv_prog *skel;
	int err;

	skel = priv_prog__open_and_load();
	if (!ASSERT_ERR_PTR(skel, "obj_tokenless_load")) {
		priv_prog__destroy(skel);
		return -EINVAL;
	}

	/* use bpf_token_path to provide BPF FS path */
	snprintf(buf, sizeof(buf), "/proc/self/fd/%d", mnt_fd);
	opts.bpf_token_path = buf;
	skel = priv_prog__open_opts(&opts);
	if (!ASSERT_OK_PTR(skel, "obj_token_path_open"))
		return -EINVAL;
	err = priv_prog__load(skel);
	priv_prog__destroy(skel);
	if (!ASSERT_OK(err, "obj_token_path_load"))
		return -EINVAL;

	/* provide BPF token, but reject bpf_token_capable() with LSM */
	lsm_skel->bss->reject_capable = true;
	lsm_skel->bss->reject_cmd = false;
	skel = priv_prog__open_opts(&opts);
	if (!ASSERT_OK_PTR(skel, "obj_token_lsm_reject_cap_open"))
		return -EINVAL;
	err = priv_prog__load(skel);
	priv_prog__destroy(skel);
	if (!ASSERT_ERR(err, "obj_token_lsm_reject_cap_load"))
		return -EINVAL;

	/* provide BPF token, but reject bpf_token_cmd() with LSM */
	lsm_skel->bss->reject_capable = false;
	lsm_skel->bss->reject_cmd = true;
	skel = priv_prog__open_opts(&opts);
	if (!ASSERT_OK_PTR(skel, "obj_token_lsm_reject_cmd_open"))
		return -EINVAL;
	err = priv_prog__load(skel);
	priv_prog__destroy(skel);
	if (!ASSERT_ERR(err, "obj_token_lsm_reject_cmd_load"))
		return -EINVAL;

	return 0;
}

/* this test is called with BPF FS that doesn't delegate BPF_BTF_LOAD command,
 * which should cause struct_ops application to fail, as BTF won't be uploaded
 * into the kernel, even if STRUCT_OPS programs themselves are allowed
 */
static int validate_struct_ops_load(int mnt_fd, bool expect_success)
{
	LIBBPF_OPTS(bpf_object_open_opts, opts);
	char buf[256];
	struct dummy_st_ops_success *skel;
	int err;

	snprintf(buf, sizeof(buf), "/proc/self/fd/%d", mnt_fd);
	opts.bpf_token_path = buf;
	skel = dummy_st_ops_success__open_opts(&opts);
	if (!ASSERT_OK_PTR(skel, "obj_token_path_open"))
		return -EINVAL;

	err = dummy_st_ops_success__load(skel);
	dummy_st_ops_success__destroy(skel);
	if (expect_success) {
		if (!ASSERT_OK(err, "obj_token_path_load"))
			return -EINVAL;
	} else /* expect failure */ {
		if (!ASSERT_ERR(err, "obj_token_path_load"))
			return -EINVAL;
	}

	return 0;
}

static int userns_obj_priv_btf_fail(int mnt_fd, struct token_lsm *lsm_skel)
{
	return validate_struct_ops_load(mnt_fd, false /* should fail */);
}

static int userns_obj_priv_btf_success(int mnt_fd, struct token_lsm *lsm_skel)
{
	return validate_struct_ops_load(mnt_fd, true /* should succeed */);
}

static const char *token_bpffs_custom_dir()
{
	return getenv("BPF_SELFTESTS_BPF_TOKEN_DIR") ?: "/tmp/bpf-token-fs";
}

#define TOKEN_ENVVAR "LIBBPF_BPF_TOKEN_PATH"

static int userns_obj_priv_implicit_token(int mnt_fd, struct token_lsm *lsm_skel)
{
	LIBBPF_OPTS(bpf_object_open_opts, opts);
	struct dummy_st_ops_success *skel;
	int err;

	/* before we mount BPF FS with token delegation, struct_ops skeleton
	 * should fail to load
	 */
	skel = dummy_st_ops_success__open_and_load();
	if (!ASSERT_ERR_PTR(skel, "obj_tokenless_load")) {
		dummy_st_ops_success__destroy(skel);
		return -EINVAL;
	}

	/* mount custom BPF FS over /sys/fs/bpf so that libbpf can create BPF
	 * token automatically and implicitly
	 */
	err = sys_move_mount(mnt_fd, "", AT_FDCWD, "/sys/fs/bpf", MOVE_MOUNT_F_EMPTY_PATH);
	if (!ASSERT_OK(err, "move_mount_bpffs"))
		return -EINVAL;

	/* disable implicit BPF token creation by setting
	 * LIBBPF_BPF_TOKEN_PATH envvar to empty value, load should fail
	 */
	err = setenv(TOKEN_ENVVAR, "", 1 /*overwrite*/);
	if (!ASSERT_OK(err, "setenv_token_path"))
		return -EINVAL;
	skel = dummy_st_ops_success__open_and_load();
	if (!ASSERT_ERR_PTR(skel, "obj_token_envvar_disabled_load")) {
		unsetenv(TOKEN_ENVVAR);
		dummy_st_ops_success__destroy(skel);
		return -EINVAL;
	}
	unsetenv(TOKEN_ENVVAR);

	/* now the same struct_ops skeleton should succeed thanks to libbpf
	 * creating BPF token from /sys/fs/bpf mount point
	 */
	skel = dummy_st_ops_success__open_and_load();
	if (!ASSERT_OK_PTR(skel, "obj_implicit_token_load"))
		return -EINVAL;

	dummy_st_ops_success__destroy(skel);

	/* now disable implicit token through empty bpf_token_path, should fail */
	opts.bpf_token_path = "";
	skel = dummy_st_ops_success__open_opts(&opts);
	if (!ASSERT_OK_PTR(skel, "obj_empty_token_path_open"))
		return -EINVAL;

	err = dummy_st_ops_success__load(skel);
	dummy_st_ops_success__destroy(skel);
	if (!ASSERT_ERR(err, "obj_empty_token_path_load"))
		return -EINVAL;

	return 0;
}

static int userns_obj_priv_implicit_token_envvar(int mnt_fd, struct token_lsm *lsm_skel)
{
	const char *custom_dir = token_bpffs_custom_dir();
	LIBBPF_OPTS(bpf_object_open_opts, opts);
	struct dummy_st_ops_success *skel;
	int err;

	/* before we mount BPF FS with token delegation, struct_ops skeleton
	 * should fail to load
	 */
	skel = dummy_st_ops_success__open_and_load();
	if (!ASSERT_ERR_PTR(skel, "obj_tokenless_load")) {
		dummy_st_ops_success__destroy(skel);
		return -EINVAL;
	}

	/* mount custom BPF FS over custom location, so libbpf can't create
	 * BPF token implicitly, unless pointed to it through
	 * LIBBPF_BPF_TOKEN_PATH envvar
	 */
	rmdir(custom_dir);
	if (!ASSERT_OK(mkdir(custom_dir, 0777), "mkdir_bpffs_custom"))
		goto err_out;
	err = sys_move_mount(mnt_fd, "", AT_FDCWD, custom_dir, MOVE_MOUNT_F_EMPTY_PATH);
	if (!ASSERT_OK(err, "move_mount_bpffs"))
		goto err_out;

	/* even though we have BPF FS with delegation, it's not at default
	 * /sys/fs/bpf location, so we still fail to load until envvar is set up
	 */
	skel = dummy_st_ops_success__open_and_load();
	if (!ASSERT_ERR_PTR(skel, "obj_tokenless_load2")) {
		dummy_st_ops_success__destroy(skel);
		goto err_out;
	}

	err = setenv(TOKEN_ENVVAR, custom_dir, 1 /*overwrite*/);
	if (!ASSERT_OK(err, "setenv_token_path"))
		goto err_out;

	/* now the same struct_ops skeleton should succeed thanks to libbpf
	 * creating BPF token from custom mount point
	 */
	skel = dummy_st_ops_success__open_and_load();
	if (!ASSERT_OK_PTR(skel, "obj_implicit_token_load"))
		goto err_out;

	dummy_st_ops_success__destroy(skel);

	/* now disable implicit token through empty bpf_token_path, envvar
	 * will be ignored, should fail
	 */
	opts.bpf_token_path = "";
	skel = dummy_st_ops_success__open_opts(&opts);
	if (!ASSERT_OK_PTR(skel, "obj_empty_token_path_open"))
		goto err_out;

	err = dummy_st_ops_success__load(skel);
	dummy_st_ops_success__destroy(skel);
	if (!ASSERT_ERR(err, "obj_empty_token_path_load"))
		goto err_out;

	rmdir(custom_dir);
	unsetenv(TOKEN_ENVVAR);
	return 0;
err_out:
	rmdir(custom_dir);
	unsetenv(TOKEN_ENVVAR);
	return -EINVAL;
}

#define bit(n) (1ULL << (n))

void test_token(void)
{
	if (test__start_subtest("map_token")) {
		struct bpffs_opts opts = {
			.cmds_str = "map_create",
			.maps_str = "stack",
		};

		subtest_userns(&opts, userns_map_create);
	}
	if (test__start_subtest("btf_token")) {
		struct bpffs_opts opts = {
			.cmds = 1ULL << BPF_BTF_LOAD,
		};

		subtest_userns(&opts, userns_btf_load);
	}
	if (test__start_subtest("prog_token")) {
		struct bpffs_opts opts = {
			.cmds_str = "PROG_LOAD",
			.progs_str = "XDP",
			.attachs_str = "xdp",
		};

		subtest_userns(&opts, userns_prog_load);
	}
	if (test__start_subtest("obj_priv_map")) {
		struct bpffs_opts opts = {
			.cmds = bit(BPF_MAP_CREATE),
			.maps = bit(BPF_MAP_TYPE_QUEUE),
		};

		subtest_userns(&opts, userns_obj_priv_map);
	}
	if (test__start_subtest("obj_priv_prog")) {
		struct bpffs_opts opts = {
			.cmds = bit(BPF_PROG_LOAD),
			.progs = bit(BPF_PROG_TYPE_KPROBE),
			.attachs = ~0ULL,
		};

		subtest_userns(&opts, userns_obj_priv_prog);
	}
	if (test__start_subtest("obj_priv_btf_fail")) {
		struct bpffs_opts opts = {
			/* disallow BTF loading */
			.cmds = bit(BPF_MAP_CREATE) | bit(BPF_PROG_LOAD),
			.maps = bit(BPF_MAP_TYPE_STRUCT_OPS),
			.progs = bit(BPF_PROG_TYPE_STRUCT_OPS),
			.attachs = ~0ULL,
		};

		subtest_userns(&opts, userns_obj_priv_btf_fail);
	}
	if (test__start_subtest("obj_priv_btf_success")) {
		struct bpffs_opts opts = {
			/* allow BTF loading */
			.cmds = bit(BPF_BTF_LOAD) | bit(BPF_MAP_CREATE) | bit(BPF_PROG_LOAD),
			.maps = bit(BPF_MAP_TYPE_STRUCT_OPS),
			.progs = bit(BPF_PROG_TYPE_STRUCT_OPS),
			.attachs = ~0ULL,
		};

		subtest_userns(&opts, userns_obj_priv_btf_success);
	}
	if (test__start_subtest("obj_priv_implicit_token")) {
		struct bpffs_opts opts = {
			/* allow BTF loading */
			.cmds = bit(BPF_BTF_LOAD) | bit(BPF_MAP_CREATE) | bit(BPF_PROG_LOAD),
			.maps = bit(BPF_MAP_TYPE_STRUCT_OPS),
			.progs = bit(BPF_PROG_TYPE_STRUCT_OPS),
			.attachs = ~0ULL,
		};

		subtest_userns(&opts, userns_obj_priv_implicit_token);
	}
	if (test__start_subtest("obj_priv_implicit_token_envvar")) {
		struct bpffs_opts opts = {
			/* allow BTF loading */
			.cmds = bit(BPF_BTF_LOAD) | bit(BPF_MAP_CREATE) | bit(BPF_PROG_LOAD),
			.maps = bit(BPF_MAP_TYPE_STRUCT_OPS),
			.progs = bit(BPF_PROG_TYPE_STRUCT_OPS),
			.attachs = ~0ULL,
		};

		subtest_userns(&opts, userns_obj_priv_implicit_token_envvar);
	}
}
