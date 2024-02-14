// SPDX-License-Identifier: GPL-2.0-only
/*
 * An implementation of the host initiated guest snapshot for Hyper-V.
 *
 * Copyright (C) 2013, Microsoft, Inc.
 * Author : K. Y. Srinivasan <kys@microsoft.com>
 */


#include <sys/types.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <fcntl.h>
#include <stdio.h>
#include <mntent.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <linux/hyperv.h>
#include <syslog.h>
#include <getopt.h>
#include <stdbool.h>
#include <dirent.h>

static bool fs_frozen;

/* Don't use syslog() in the function since that can cause write to disk */
static int vss_do_freeze(char *dir, unsigned int cmd)
{
	int ret, fd = open(dir, O_RDONLY);

	if (fd < 0)
		return 1;

	ret = ioctl(fd, cmd, 0);

	/*
	 * If a partition is mounted more than once, only the first
	 * FREEZE/THAW can succeed and the later ones will get
	 * EBUSY/EINVAL respectively: there could be 2 cases:
	 * 1) a user may mount the same partition to different directories
	 *  by mistake or on purpose;
	 * 2) The subvolume of btrfs appears to have the same partition
	 * mounted more than once.
	 */
	if (ret) {
		if ((cmd == FIFREEZE && errno == EBUSY) ||
		    (cmd == FITHAW && errno == EINVAL)) {
			close(fd);
			return 0;
		}
	}

	close(fd);
	return !!ret;
}

static bool is_dev_loop(const char *blkname)
{
	char *buffer;
	DIR *dir;
	struct dirent *entry;
	bool ret = false;

	buffer = malloc(PATH_MAX);
	if (!buffer) {
		syslog(LOG_ERR, "Can't allocate memory!");
		exit(1);
	}

	snprintf(buffer, PATH_MAX, "%s/loop", blkname);
	if (!access(buffer, R_OK | X_OK)) {
		ret = true;
		goto free_buffer;
	} else if (errno != ENOENT) {
		syslog(LOG_ERR, "Can't access: %s; error:%d %s!",
		       buffer, errno, strerror(errno));
	}

	snprintf(buffer, PATH_MAX, "%s/slaves", blkname);
	dir = opendir(buffer);
	if (!dir) {
		if (errno != ENOENT)
			syslog(LOG_ERR, "Can't opendir: %s; error:%d %s!",
			       buffer, errno, strerror(errno));
		goto free_buffer;
	}

	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 ||
		    strcmp(entry->d_name, "..") == 0)
			continue;

		snprintf(buffer, PATH_MAX, "%s/slaves/%s", blkname,
			 entry->d_name);
		if (is_dev_loop(buffer)) {
			ret = true;
			break;
		}
	}
	closedir(dir);
free_buffer:
	free(buffer);
	return ret;
}

static int vss_operate(int operation)
{
	char match[] = "/dev/";
	FILE *mounts;
	struct mntent *ent;
	struct stat sb;
	char errdir[1024] = {0};
	char blkdir[23]; /* /sys/dev/block/XXX:XXX */
	unsigned int cmd;
	int error = 0, root_seen = 0, save_errno = 0;

	switch (operation) {
	case VSS_OP_FREEZE:
		cmd = FIFREEZE;
		break;
	case VSS_OP_THAW:
		cmd = FITHAW;
		break;
	default:
		return -1;
	}

	mounts = setmntent("/proc/mounts", "r");
	if (mounts == NULL)
		return -1;

	while ((ent = getmntent(mounts))) {
		if (strncmp(ent->mnt_fsname, match, strlen(match)))
			continue;
		if (stat(ent->mnt_fsname, &sb)) {
			syslog(LOG_ERR, "Can't stat: %s; error:%d %s!",
			       ent->mnt_fsname, errno, strerror(errno));
		} else {
			sprintf(blkdir, "/sys/dev/block/%d:%d",
				major(sb.st_rdev), minor(sb.st_rdev));
			if (is_dev_loop(blkdir))
				continue;
		}
		if (hasmntopt(ent, MNTOPT_RO) != NULL)
			continue;
		if (strcmp(ent->mnt_type, "vfat") == 0)
			continue;
		if (strcmp(ent->mnt_dir, "/") == 0) {
			root_seen = 1;
			continue;
		}
		error |= vss_do_freeze(ent->mnt_dir, cmd);
		if (operation == VSS_OP_FREEZE) {
			if (error)
				goto err;
			fs_frozen = true;
		}
	}

	endmntent(mounts);

	if (root_seen) {
		error |= vss_do_freeze("/", cmd);
		if (operation == VSS_OP_FREEZE) {
			if (error)
				goto err;
			fs_frozen = true;
		}
	}

	if (operation == VSS_OP_THAW && !error)
		fs_frozen = false;

	goto out;
err:
	save_errno = errno;
	if (ent) {
		strncpy(errdir, ent->mnt_dir, sizeof(errdir)-1);
		endmntent(mounts);
	}
	vss_operate(VSS_OP_THAW);
	fs_frozen = false;
	/* Call syslog after we thaw all filesystems */
	if (ent)
		syslog(LOG_ERR, "FREEZE of %s failed; error:%d %s",
		       errdir, save_errno, strerror(save_errno));
	else
		syslog(LOG_ERR, "FREEZE of / failed; error:%d %s", save_errno,
		       strerror(save_errno));
out:
	return error;
}

void print_usage(char *argv[])
{
	fprintf(stderr, "Usage: %s [options]\n"
		"Options are:\n"
		"  -n, --no-daemon        stay in foreground, don't daemonize\n"
		"  -h, --help             print this help\n", argv[0]);
}

int main(int argc, char *argv[])
{
	int vss_fd = -1, len;
	int error;
	struct pollfd pfd;
	int	op;
	struct hv_vss_msg vss_msg[1];
	int daemonize = 1, long_index = 0, opt;
	int in_handshake;
	__u32 kernel_modver;

	static struct option long_options[] = {
		{"help",	no_argument,	   0,  'h' },
		{"no-daemon",	no_argument,	   0,  'n' },
		{0,		0,		   0,  0   }
	};

	while ((opt = getopt_long(argc, argv, "hn", long_options,
				  &long_index)) != -1) {
		switch (opt) {
		case 'n':
			daemonize = 0;
			break;
		case 'h':
			print_usage(argv);
			exit(0);
		default:
			print_usage(argv);
			exit(EXIT_FAILURE);
		}
	}

	if (daemonize && daemon(1, 0))
		return 1;

	openlog("Hyper-V VSS", 0, LOG_USER);
	syslog(LOG_INFO, "VSS starting; pid is:%d", getpid());

reopen_vss_fd:
	if (vss_fd != -1)
		close(vss_fd);
	if (fs_frozen) {
		if (vss_operate(VSS_OP_THAW) || fs_frozen) {
			syslog(LOG_ERR, "failed to thaw file system: err=%d",
			       errno);
			exit(EXIT_FAILURE);
		}
	}

	in_handshake = 1;
	vss_fd = open("/dev/vmbus/hv_vss", O_RDWR);
	if (vss_fd < 0) {
		syslog(LOG_ERR, "open /dev/vmbus/hv_vss failed; error: %d %s",
		       errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
	/*
	 * Register ourselves with the kernel.
	 */
	vss_msg->vss_hdr.operation = VSS_OP_REGISTER1;

	len = write(vss_fd, vss_msg, sizeof(struct hv_vss_msg));
	if (len < 0) {
		syslog(LOG_ERR, "registration to kernel failed; error: %d %s",
		       errno, strerror(errno));
		close(vss_fd);
		exit(EXIT_FAILURE);
	}

	pfd.fd = vss_fd;

	while (1) {
		pfd.events = POLLIN;
		pfd.revents = 0;

		if (poll(&pfd, 1, -1) < 0) {
			syslog(LOG_ERR, "poll failed; error:%d %s", errno, strerror(errno));
			if (errno == EINVAL) {
				close(vss_fd);
				exit(EXIT_FAILURE);
			}
			else
				continue;
		}

		len = read(vss_fd, vss_msg, sizeof(struct hv_vss_msg));

		if (in_handshake) {
			if (len != sizeof(kernel_modver)) {
				syslog(LOG_ERR, "invalid version negotiation");
				exit(EXIT_FAILURE);
			}
			kernel_modver = *(__u32 *)vss_msg;
			in_handshake = 0;
			syslog(LOG_INFO, "VSS: kernel module version: %d",
			       kernel_modver);
			continue;
		}

		if (len != sizeof(struct hv_vss_msg)) {
			syslog(LOG_ERR, "read failed; error:%d %s",
			       errno, strerror(errno));
			goto reopen_vss_fd;
		}

		op = vss_msg->vss_hdr.operation;
		error =  HV_S_OK;

		switch (op) {
		case VSS_OP_FREEZE:
		case VSS_OP_THAW:
			error = vss_operate(op);
			syslog(LOG_INFO, "VSS: op=%s: %s\n",
				op == VSS_OP_FREEZE ? "FREEZE" : "THAW",
				error ? "failed" : "succeeded");

			if (error) {
				error = HV_E_FAIL;
				syslog(LOG_ERR, "op=%d failed!", op);
				syslog(LOG_ERR, "report it with these files:");
				syslog(LOG_ERR, "/etc/fstab and /proc/mounts");
			}
			break;
		case VSS_OP_HOT_BACKUP:
			syslog(LOG_INFO, "VSS: op=CHECK HOT BACKUP\n");
			break;
		default:
			syslog(LOG_ERR, "Illegal op:%d\n", op);
		}

		/*
		 * The write() may return an error due to the faked VSS_OP_THAW
		 * message upon hibernation. Ignore the error by resetting the
		 * dev file, i.e. closing and re-opening it.
		 */
		vss_msg->error = error;
		len = write(vss_fd, vss_msg, sizeof(struct hv_vss_msg));
		if (len != sizeof(struct hv_vss_msg)) {
			syslog(LOG_ERR, "write failed; error: %d %s", errno,
			       strerror(errno));
			goto reopen_vss_fd;
		}
	}

	close(vss_fd);
	exit(0);
}
