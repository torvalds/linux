#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stddef.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/user.h>
#include <sys/ioctl.h>
#include <sys/ptrace.h>
#include <sys/mount.h>
#include <linux/limits.h>
#include <linux/filter.h>
#include <linux/seccomp.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*(x)))

static int seccomp(unsigned int op, unsigned int flags, void *args)
{
	errno = 0;
	return syscall(__NR_seccomp, op, flags, args);
}

static int send_fd(int sock, int fd)
{
	struct msghdr msg = {};
	struct cmsghdr *cmsg;
	int *fd_ptr;
	char buf[CMSG_SPACE(sizeof(int))] = {0}, c = 'c';
	struct iovec io = {
		.iov_base = &c,
		.iov_len = 1,
	};

	msg.msg_iov = &io;
	msg.msg_iovlen = 1;
	msg.msg_control = buf;
	msg.msg_controllen = sizeof(buf);
	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));
	fd_ptr = (int *)CMSG_DATA(cmsg);
	*fd_ptr = fd;
	msg.msg_controllen = cmsg->cmsg_len;

	if (sendmsg(sock, &msg, 0) < 0) {
		perror("sendmsg");
		return -1;
	}

	return 0;
}

static int recv_fd(int sock)
{
	struct msghdr msg = {};
	struct cmsghdr *cmsg;
	int *fd_ptr;
	char buf[CMSG_SPACE(sizeof(int))] = {0}, c = 'c';
	struct iovec io = {
		.iov_base = &c,
		.iov_len = 1,
	};

	msg.msg_iov = &io;
	msg.msg_iovlen = 1;
	msg.msg_control = buf;
	msg.msg_controllen = sizeof(buf);

	if (recvmsg(sock, &msg, 0) < 0) {
		perror("recvmsg");
		return -1;
	}

	cmsg = CMSG_FIRSTHDR(&msg);
	fd_ptr = (int *)CMSG_DATA(cmsg);

	return *fd_ptr;
}

static int user_trap_syscall(int nr, unsigned int flags)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_LD+BPF_W+BPF_ABS,
			offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, nr, 0, 1),
		BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_USER_NOTIF),
		BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),
	};

	struct sock_fprog prog = {
		.len = (unsigned short)ARRAY_SIZE(filter),
		.filter = filter,
	};

	return seccomp(SECCOMP_SET_MODE_FILTER, flags, &prog);
}

static int handle_req(struct seccomp_notif *req,
		      struct seccomp_notif_resp *resp, int listener)
{
	char path[PATH_MAX], source[PATH_MAX], target[PATH_MAX];
	int ret = -1, mem;

	resp->id = req->id;
	resp->error = -EPERM;
	resp->val = 0;

	if (req->data.nr != __NR_mount) {
		fprintf(stderr, "huh? trapped something besides mount? %d\n", req->data.nr);
		return -1;
	}

	/* Only allow bind mounts. */
	if (!(req->data.args[3] & MS_BIND))
		return 0;

	/*
	 * Ok, let's read the task's memory to see where they wanted their
	 * mount to go.
	 */
	snprintf(path, sizeof(path), "/proc/%d/mem", req->pid);
	mem = open(path, O_RDONLY);
	if (mem < 0) {
		perror("open mem");
		return -1;
	}

	/*
	 * Now we avoid a TOCTOU: we referred to a pid by its pid, but since
	 * the pid that made the syscall may have died, we need to confirm that
	 * the pid is still valid after we open its /proc/pid/mem file. We can
	 * ask the listener fd this as follows.
	 *
	 * Note that this check should occur *after* any task-specific
	 * resources are opened, to make sure that the task has not died and
	 * we're not wrongly reading someone else's state in order to make
	 * decisions.
	 */
	if (ioctl(listener, SECCOMP_IOCTL_NOTIF_ID_VALID, &req->id) < 0) {
		fprintf(stderr, "task died before we could map its memory\n");
		goto out;
	}

	/*
	 * Phew, we've got the right /proc/pid/mem. Now we can read it. Note
	 * that to avoid another TOCTOU, we should read all of the pointer args
	 * before we decide to allow the syscall.
	 */
	if (lseek(mem, req->data.args[0], SEEK_SET) < 0) {
		perror("seek");
		goto out;
	}

	ret = read(mem, source, sizeof(source));
	if (ret < 0) {
		perror("read");
		goto out;
	}

	if (lseek(mem, req->data.args[1], SEEK_SET) < 0) {
		perror("seek");
		goto out;
	}

	ret = read(mem, target, sizeof(target));
	if (ret < 0) {
		perror("read");
		goto out;
	}

	/*
	 * Our policy is to only allow bind mounts inside /tmp. This isn't very
	 * interesting, because we could do unprivlieged bind mounts with user
	 * namespaces already, but you get the idea.
	 */
	if (!strncmp(source, "/tmp/", 5) && !strncmp(target, "/tmp/", 5)) {
		if (mount(source, target, NULL, req->data.args[3], NULL) < 0) {
			ret = -1;
			perror("actual mount");
			goto out;
		}
		resp->error = 0;
	}

	/* Even if we didn't allow it because of policy, generating the
	 * response was be a success, because we want to tell the worker EPERM.
	 */
	ret = 0;

out:
	close(mem);
	return ret;
}

int main(void)
{
	int sk_pair[2], ret = 1, status, listener;
	pid_t worker = 0 , tracer = 0;

	if (socketpair(PF_LOCAL, SOCK_SEQPACKET, 0, sk_pair) < 0) {
		perror("socketpair");
		return 1;
	}

	worker = fork();
	if (worker < 0) {
		perror("fork");
		goto close_pair;
	}

	if (worker == 0) {
		listener = user_trap_syscall(__NR_mount,
					     SECCOMP_FILTER_FLAG_NEW_LISTENER);
		if (listener < 0) {
			perror("seccomp");
			exit(1);
		}

		/*
		 * Drop privileges. We definitely can't mount as uid 1000.
		 */
		if (setuid(1000) < 0) {
			perror("setuid");
			exit(1);
		}

		/*
		 * Send the listener to the parent; also serves as
		 * synchronization.
		 */
		if (send_fd(sk_pair[1], listener) < 0)
			exit(1);
		close(listener);

		if (mkdir("/tmp/foo", 0755) < 0) {
			perror("mkdir");
			exit(1);
		}

		/*
		 * Try a bad mount just for grins.
		 */
		if (mount("/dev/sda", "/tmp/foo", NULL, 0, NULL) != -1) {
			fprintf(stderr, "huh? mounted /dev/sda?\n");
			exit(1);
		}

		if (errno != EPERM) {
			perror("bad error from mount");
			exit(1);
		}

		/*
		 * Ok, we expect this one to succeed.
		 */
		if (mount("/tmp/foo", "/tmp/foo", NULL, MS_BIND, NULL) < 0) {
			perror("mount");
			exit(1);
		}

		exit(0);
	}

	/*
	 * Get the listener from the child.
	 */
	listener = recv_fd(sk_pair[0]);
	if (listener < 0)
		goto out_kill;

	/*
	 * Fork a task to handle the requests. This isn't strictly necessary,
	 * but it makes the particular writing of this sample easier, since we
	 * can just wait ofr the tracee to exit and kill the tracer.
	 */
	tracer = fork();
	if (tracer < 0) {
		perror("fork");
		goto out_kill;
	}

	if (tracer == 0) {
		struct seccomp_notif *req;
		struct seccomp_notif_resp *resp;
		struct seccomp_notif_sizes sizes;

		if (seccomp(SECCOMP_GET_NOTIF_SIZES, 0, &sizes) < 0) {
			perror("seccomp(GET_NOTIF_SIZES)");
			goto out_close;
		}

		req = malloc(sizes.seccomp_notif);
		if (!req)
			goto out_close;

		resp = malloc(sizes.seccomp_notif_resp);
		if (!resp)
			goto out_req;
		memset(resp, 0, sizes.seccomp_notif_resp);

		while (1) {
			memset(req, 0, sizes.seccomp_notif);
			if (ioctl(listener, SECCOMP_IOCTL_NOTIF_RECV, req)) {
				perror("ioctl recv");
				goto out_resp;
			}

			if (handle_req(req, resp, listener) < 0)
				goto out_resp;

			/*
			 * ENOENT here means that the task may have gotten a
			 * signal and restarted the syscall. It's up to the
			 * handler to decide what to do in this case, but for
			 * the sample code, we just ignore it. Probably
			 * something better should happen, like undoing the
			 * mount, or keeping track of the args to make sure we
			 * don't do it again.
			 */
			if (ioctl(listener, SECCOMP_IOCTL_NOTIF_SEND, resp) < 0 &&
			    errno != ENOENT) {
				perror("ioctl send");
				goto out_resp;
			}
		}
out_resp:
		free(resp);
out_req:
		free(req);
out_close:
		close(listener);
		exit(1);
	}

	close(listener);

	if (waitpid(worker, &status, 0) != worker) {
		perror("waitpid");
		goto out_kill;
	}

	if (umount2("/tmp/foo", MNT_DETACH) < 0 && errno != EINVAL) {
		perror("umount2");
		goto out_kill;
	}

	if (remove("/tmp/foo") < 0 && errno != ENOENT) {
		perror("remove");
		exit(1);
	}

	if (!WIFEXITED(status) || WEXITSTATUS(status)) {
		fprintf(stderr, "worker exited nonzero\n");
		goto out_kill;
	}

	ret = 0;

out_kill:
	if (tracer > 0)
		kill(tracer, SIGKILL);
	if (worker > 0)
		kill(worker, SIGKILL);

close_pair:
	close(sk_pair[0]);
	close(sk_pair[1]);
	return ret;
}
