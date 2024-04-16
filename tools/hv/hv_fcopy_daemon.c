// SPDX-License-Identifier: GPL-2.0-only
/*
 * An implementation of host to guest copy functionality for Linux.
 *
 * Copyright (C) 2014, Microsoft, Inc.
 *
 * Author : K. Y. Srinivasan <kys@microsoft.com>
 */


#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <linux/hyperv.h>
#include <linux/limits.h>
#include <syslog.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>

static int target_fd;
static char target_fname[PATH_MAX];
static unsigned long long filesize;

static int hv_start_fcopy(struct hv_start_fcopy *smsg)
{
	int error = HV_E_FAIL;
	char *q, *p;

	filesize = 0;
	p = (char *)smsg->path_name;
	snprintf(target_fname, sizeof(target_fname), "%s/%s",
		 (char *)smsg->path_name, (char *)smsg->file_name);

	syslog(LOG_INFO, "Target file name: %s", target_fname);
	/*
	 * Check to see if the path is already in place; if not,
	 * create if required.
	 */
	while ((q = strchr(p, '/')) != NULL) {
		if (q == p) {
			p++;
			continue;
		}
		*q = '\0';
		if (access((char *)smsg->path_name, F_OK)) {
			if (smsg->copy_flags & CREATE_PATH) {
				if (mkdir((char *)smsg->path_name, 0755)) {
					syslog(LOG_ERR, "Failed to create %s",
						(char *)smsg->path_name);
					goto done;
				}
			} else {
				syslog(LOG_ERR, "Invalid path: %s",
					(char *)smsg->path_name);
				goto done;
			}
		}
		p = q + 1;
		*q = '/';
	}

	if (!access(target_fname, F_OK)) {
		syslog(LOG_INFO, "File: %s exists", target_fname);
		if (!(smsg->copy_flags & OVER_WRITE)) {
			error = HV_ERROR_ALREADY_EXISTS;
			goto done;
		}
	}

	target_fd = open(target_fname,
			 O_RDWR | O_CREAT | O_TRUNC | O_CLOEXEC, 0744);
	if (target_fd == -1) {
		syslog(LOG_INFO, "Open Failed: %s", strerror(errno));
		goto done;
	}

	error = 0;
done:
	if (error)
		target_fname[0] = '\0';
	return error;
}

static int hv_copy_data(struct hv_do_fcopy *cpmsg)
{
	ssize_t bytes_written;
	int ret = 0;

	bytes_written = pwrite(target_fd, cpmsg->data, cpmsg->size,
				cpmsg->offset);

	filesize += cpmsg->size;
	if (bytes_written != cpmsg->size) {
		switch (errno) {
		case ENOSPC:
			ret = HV_ERROR_DISK_FULL;
			break;
		default:
			ret = HV_E_FAIL;
			break;
		}
		syslog(LOG_ERR, "pwrite failed to write %llu bytes: %ld (%s)",
		       filesize, (long)bytes_written, strerror(errno));
	}

	return ret;
}

/*
 * Reset target_fname to "" in the two below functions for hibernation: if
 * the fcopy operation is aborted by hibernation, the daemon should remove the
 * partially-copied file; to achieve this, the hv_utils driver always fakes a
 * CANCEL_FCOPY message upon suspend, and later when the VM resumes back,
 * the daemon calls hv_copy_cancel() to remove the file; if a file is copied
 * successfully before suspend, hv_copy_finished() must reset target_fname to
 * avoid that the file can be incorrectly removed upon resume, since the faked
 * CANCEL_FCOPY message is spurious in this case.
 */
static int hv_copy_finished(void)
{
	close(target_fd);
	target_fname[0] = '\0';
	return 0;
}
static int hv_copy_cancel(void)
{
	close(target_fd);
	if (strlen(target_fname) > 0) {
		unlink(target_fname);
		target_fname[0] = '\0';
	}
	return 0;

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
	int fcopy_fd = -1;
	int error;
	int daemonize = 1, long_index = 0, opt;
	int version = FCOPY_CURRENT_VERSION;
	union {
		struct hv_fcopy_hdr hdr;
		struct hv_start_fcopy start;
		struct hv_do_fcopy copy;
		__u32 kernel_modver;
	} buffer = { };
	int in_handshake;

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
		default:
			print_usage(argv);
			exit(EXIT_FAILURE);
		}
	}

	if (daemonize && daemon(1, 0)) {
		syslog(LOG_ERR, "daemon() failed; error: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	openlog("HV_FCOPY", 0, LOG_USER);
	syslog(LOG_INFO, "starting; pid is:%d", getpid());

reopen_fcopy_fd:
	if (fcopy_fd != -1)
		close(fcopy_fd);
	/* Remove any possible partially-copied file on error */
	hv_copy_cancel();
	in_handshake = 1;
	fcopy_fd = open("/dev/vmbus/hv_fcopy", O_RDWR);

	if (fcopy_fd < 0) {
		syslog(LOG_ERR, "open /dev/vmbus/hv_fcopy failed; error: %d %s",
			errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	/*
	 * Register with the kernel.
	 */
	if ((write(fcopy_fd, &version, sizeof(int))) != sizeof(int)) {
		syslog(LOG_ERR, "Registration failed: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	while (1) {
		/*
		 * In this loop we process fcopy messages after the
		 * handshake is complete.
		 */
		ssize_t len;

		len = pread(fcopy_fd, &buffer, sizeof(buffer), 0);
		if (len < 0) {
			syslog(LOG_ERR, "pread failed: %s", strerror(errno));
			goto reopen_fcopy_fd;
		}

		if (in_handshake) {
			if (len != sizeof(buffer.kernel_modver)) {
				syslog(LOG_ERR, "invalid version negotiation");
				exit(EXIT_FAILURE);
			}
			in_handshake = 0;
			syslog(LOG_INFO, "kernel module version: %u",
			       buffer.kernel_modver);
			continue;
		}

		switch (buffer.hdr.operation) {
		case START_FILE_COPY:
			error = hv_start_fcopy(&buffer.start);
			break;
		case WRITE_TO_FILE:
			error = hv_copy_data(&buffer.copy);
			break;
		case COMPLETE_FCOPY:
			error = hv_copy_finished();
			break;
		case CANCEL_FCOPY:
			error = hv_copy_cancel();
			break;

		default:
			error = HV_E_FAIL;
			syslog(LOG_ERR, "Unknown operation: %d",
				buffer.hdr.operation);

		}

		/*
		 * pwrite() may return an error due to the faked CANCEL_FCOPY
		 * message upon hibernation. Ignore the error by resetting the
		 * dev file, i.e. closing and re-opening it.
		 */
		if (pwrite(fcopy_fd, &error, sizeof(int), 0) != sizeof(int)) {
			syslog(LOG_ERR, "pwrite failed: %s", strerror(errno));
			goto reopen_fcopy_fd;
		}
	}
}
