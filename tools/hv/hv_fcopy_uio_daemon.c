// SPDX-License-Identifier: GPL-2.0-only
/*
 * An implementation of host to guest copy functionality for Linux.
 *
 * Copyright (C) 2023, Microsoft, Inc.
 *
 * Author : K. Y. Srinivasan <kys@microsoft.com>
 * Author : Saurabh Sengar <ssengar@microsoft.com>
 *
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <locale.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <wchar.h>
#include <sys/stat.h>
#include <linux/hyperv.h>
#include <linux/limits.h>
#include "vmbus_bufring.h"

#define ICMSGTYPE_NEGOTIATE	0
#define ICMSGTYPE_FCOPY		7

#define WIN8_SRV_MAJOR		1
#define WIN8_SRV_MINOR		1
#define WIN8_SRV_VERSION	(WIN8_SRV_MAJOR << 16 | WIN8_SRV_MINOR)

#define FCOPY_UIO		"/sys/bus/vmbus/devices/eb765408-105f-49b6-b4aa-c123b64d17d4/uio"

#define FCOPY_VER_COUNT		1
static const int fcopy_versions[] = {
	WIN8_SRV_VERSION
};

#define FW_VER_COUNT		1
static const int fw_versions[] = {
	UTIL_FW_VERSION
};

#define HV_RING_SIZE		0x4000 /* 16KB ring buffer size */

static unsigned char desc[HV_RING_SIZE];

static int target_fd;
static char target_fname[PATH_MAX];
static unsigned long long filesize;

static int hv_fcopy_create_file(char *file_name, char *path_name, __u32 flags)
{
	int error = HV_E_FAIL;
	char *q, *p;

	filesize = 0;
	p = path_name;
	snprintf(target_fname, sizeof(target_fname), "%s/%s",
		 path_name, file_name);

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
		if (access(path_name, F_OK)) {
			if (flags & CREATE_PATH) {
				if (mkdir(path_name, 0755)) {
					syslog(LOG_ERR, "Failed to create %s",
					       path_name);
					goto done;
				}
			} else {
				syslog(LOG_ERR, "Invalid path: %s", path_name);
				goto done;
			}
		}
		p = q + 1;
		*q = '/';
	}

	if (!access(target_fname, F_OK)) {
		syslog(LOG_INFO, "File: %s exists", target_fname);
		if (!(flags & OVER_WRITE)) {
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

/* copy the data into the file */
static int hv_copy_data(struct hv_do_fcopy *cpmsg)
{
	ssize_t len;
	int ret = 0;

	len = pwrite(target_fd, cpmsg->data, cpmsg->size, cpmsg->offset);

	filesize += cpmsg->size;
	if (len != cpmsg->size) {
		switch (errno) {
		case ENOSPC:
			ret = HV_ERROR_DISK_FULL;
			break;
		default:
			ret = HV_E_FAIL;
			break;
		}
		syslog(LOG_ERR, "pwrite failed to write %llu bytes: %ld (%s)",
		       filesize, (long)len, strerror(errno));
	}

	return ret;
}

static int hv_copy_finished(void)
{
	close(target_fd);
	target_fname[0] = '\0';

	return 0;
}

static void print_usage(char *argv[])
{
	fprintf(stderr, "Usage: %s [options]\n"
		"Options are:\n"
		"  -n, --no-daemon        stay in foreground, don't daemonize\n"
		"  -h, --help             print this help\n", argv[0]);
}

static bool vmbus_prep_negotiate_resp(struct icmsg_hdr *icmsghdrp, unsigned char *buf,
				      unsigned int buflen, const int *fw_version, int fw_vercnt,
				const int *srv_version, int srv_vercnt,
				int *nego_fw_version, int *nego_srv_version)
{
	int icframe_major, icframe_minor;
	int icmsg_major, icmsg_minor;
	int fw_major, fw_minor;
	int srv_major, srv_minor;
	int i, j;
	bool found_match = false;
	struct icmsg_negotiate *negop;

	/* Check that there's enough space for icframe_vercnt, icmsg_vercnt */
	if (buflen < ICMSG_HDR + offsetof(struct icmsg_negotiate, reserved)) {
		syslog(LOG_ERR, "Invalid icmsg negotiate");
		return false;
	}

	icmsghdrp->icmsgsize = 0x10;
	negop = (struct icmsg_negotiate *)&buf[ICMSG_HDR];

	icframe_major = negop->icframe_vercnt;
	icframe_minor = 0;

	icmsg_major = negop->icmsg_vercnt;
	icmsg_minor = 0;

	/* Validate negop packet */
	if (icframe_major > IC_VERSION_NEGOTIATION_MAX_VER_COUNT ||
	    icmsg_major > IC_VERSION_NEGOTIATION_MAX_VER_COUNT ||
	    ICMSG_NEGOTIATE_PKT_SIZE(icframe_major, icmsg_major) > buflen) {
		syslog(LOG_ERR, "Invalid icmsg negotiate - icframe_major: %u, icmsg_major: %u\n",
		       icframe_major, icmsg_major);
		goto fw_error;
	}

	/*
	 * Select the framework version number we will
	 * support.
	 */

	for (i = 0; i < fw_vercnt; i++) {
		fw_major = (fw_version[i] >> 16);
		fw_minor = (fw_version[i] & 0xFFFF);

		for (j = 0; j < negop->icframe_vercnt; j++) {
			if (negop->icversion_data[j].major == fw_major &&
			    negop->icversion_data[j].minor == fw_minor) {
				icframe_major = negop->icversion_data[j].major;
				icframe_minor = negop->icversion_data[j].minor;
				found_match = true;
				break;
			}
		}

		if (found_match)
			break;
	}

	if (!found_match)
		goto fw_error;

	found_match = false;

	for (i = 0; i < srv_vercnt; i++) {
		srv_major = (srv_version[i] >> 16);
		srv_minor = (srv_version[i] & 0xFFFF);

		for (j = negop->icframe_vercnt;
			(j < negop->icframe_vercnt + negop->icmsg_vercnt);
			j++) {
			if (negop->icversion_data[j].major == srv_major &&
			    negop->icversion_data[j].minor == srv_minor) {
				icmsg_major = negop->icversion_data[j].major;
				icmsg_minor = negop->icversion_data[j].minor;
				found_match = true;
				break;
			}
		}

		if (found_match)
			break;
	}

	/*
	 * Respond with the framework and service
	 * version numbers we can support.
	 */
fw_error:
	if (!found_match) {
		negop->icframe_vercnt = 0;
		negop->icmsg_vercnt = 0;
	} else {
		negop->icframe_vercnt = 1;
		negop->icmsg_vercnt = 1;
	}

	if (nego_fw_version)
		*nego_fw_version = (icframe_major << 16) | icframe_minor;

	if (nego_srv_version)
		*nego_srv_version = (icmsg_major << 16) | icmsg_minor;

	negop->icversion_data[0].major = icframe_major;
	negop->icversion_data[0].minor = icframe_minor;
	negop->icversion_data[1].major = icmsg_major;
	negop->icversion_data[1].minor = icmsg_minor;

	return found_match;
}

static void wcstoutf8(char *dest, const __u16 *src, size_t dest_size)
{
	size_t len = 0;

	while (len < dest_size) {
		if (src[len] < 0x80)
			dest[len++] = (char)(*src++);
		else
			dest[len++] = 'X';
	}

	dest[len] = '\0';
}

static int hv_fcopy_start(struct hv_start_fcopy *smsg_in)
{
	setlocale(LC_ALL, "en_US.utf8");
	size_t file_size, path_size;
	char *file_name, *path_name;
	char *in_file_name = (char *)smsg_in->file_name;
	char *in_path_name = (char *)smsg_in->path_name;

	file_size = wcstombs(NULL, (const wchar_t *restrict)in_file_name, 0) + 1;
	path_size = wcstombs(NULL, (const wchar_t *restrict)in_path_name, 0) + 1;

	file_name = (char *)malloc(file_size * sizeof(char));
	path_name = (char *)malloc(path_size * sizeof(char));

	if (!file_name || !path_name) {
		free(file_name);
		free(path_name);
		syslog(LOG_ERR, "Can't allocate memory for file name and/or path name");
		return HV_E_FAIL;
	}

	wcstoutf8(file_name, (__u16 *)in_file_name, file_size);
	wcstoutf8(path_name, (__u16 *)in_path_name, path_size);

	return hv_fcopy_create_file(file_name, path_name, smsg_in->copy_flags);
}

static int hv_fcopy_send_data(struct hv_fcopy_hdr *fcopy_msg, int recvlen)
{
	int operation = fcopy_msg->operation;

	/*
	 * The  strings sent from the host are encoded in
	 * utf16; convert it to utf8 strings.
	 * The host assures us that the utf16 strings will not exceed
	 * the max lengths specified. We will however, reserve room
	 * for the string terminating character - in the utf16s_utf8s()
	 * function we limit the size of the buffer where the converted
	 * string is placed to W_MAX_PATH -1 to guarantee
	 * that the strings can be properly terminated!
	 */

	switch (operation) {
	case START_FILE_COPY:
		return hv_fcopy_start((struct hv_start_fcopy *)fcopy_msg);
	case WRITE_TO_FILE:
		return hv_copy_data((struct hv_do_fcopy *)fcopy_msg);
	case COMPLETE_FCOPY:
		return hv_copy_finished();
	}

	return HV_E_FAIL;
}

/* process the packet recv from host */
static int fcopy_pkt_process(struct vmbus_br *txbr)
{
	int ret, offset, pktlen;
	int fcopy_srv_version;
	const struct vmbus_chanpkt_hdr *pkt;
	struct hv_fcopy_hdr *fcopy_msg;
	struct icmsg_hdr *icmsghdr;

	pkt = (const struct vmbus_chanpkt_hdr *)desc;
	offset = pkt->hlen << 3;
	pktlen = (pkt->tlen << 3) - offset;
	icmsghdr = (struct icmsg_hdr *)&desc[offset + sizeof(struct vmbuspipe_hdr)];
	icmsghdr->status = HV_E_FAIL;

	if (icmsghdr->icmsgtype == ICMSGTYPE_NEGOTIATE) {
		if (vmbus_prep_negotiate_resp(icmsghdr, desc + offset, pktlen, fw_versions,
					      FW_VER_COUNT, fcopy_versions, FCOPY_VER_COUNT,
					      NULL, &fcopy_srv_version)) {
			syslog(LOG_INFO, "FCopy IC version %d.%d",
			       fcopy_srv_version >> 16, fcopy_srv_version & 0xFFFF);
			icmsghdr->status = 0;
		}
	} else if (icmsghdr->icmsgtype == ICMSGTYPE_FCOPY) {
		/* Ensure recvlen is big enough to contain hv_fcopy_hdr */
		if (pktlen < ICMSG_HDR + sizeof(struct hv_fcopy_hdr)) {
			syslog(LOG_ERR, "Invalid Fcopy hdr. Packet length too small: %u",
			       pktlen);
			return -ENOBUFS;
		}

		fcopy_msg = (struct hv_fcopy_hdr *)&desc[offset + ICMSG_HDR];
		icmsghdr->status = hv_fcopy_send_data(fcopy_msg, pktlen);
	}

	icmsghdr->icflags = ICMSGHDRFLAG_TRANSACTION | ICMSGHDRFLAG_RESPONSE;
	ret = rte_vmbus_chan_send(txbr, 0x6, desc + offset, pktlen, 0);
	if (ret) {
		syslog(LOG_ERR, "Write to ringbuffer failed err: %d", ret);
		return ret;
	}

	return 0;
}

static void fcopy_get_first_folder(char *path, char *chan_no)
{
	DIR *dir = opendir(path);
	struct dirent *entry;

	if (!dir) {
		syslog(LOG_ERR, "Failed to open directory (errno=%s).\n", strerror(errno));
		return;
	}

	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_type == DT_DIR && strcmp(entry->d_name, ".") != 0 &&
		    strcmp(entry->d_name, "..") != 0) {
			strcpy(chan_no, entry->d_name);
			break;
		}
	}

	closedir(dir);
}

int main(int argc, char *argv[])
{
	int fcopy_fd = -1, tmp = 1;
	int daemonize = 1, long_index = 0, opt, ret = -EINVAL;
	struct vmbus_br txbr, rxbr;
	void *ring;
	uint32_t len = HV_RING_SIZE;
	char uio_name[NAME_MAX] = {0};
	char uio_dev_path[PATH_MAX] = {0};

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
			goto exit;
		}
	}

	if (daemonize && daemon(1, 0)) {
		syslog(LOG_ERR, "daemon() failed; error: %s", strerror(errno));
		goto exit;
	}

	openlog("HV_UIO_FCOPY", 0, LOG_USER);
	syslog(LOG_INFO, "starting; pid is:%d", getpid());

	fcopy_get_first_folder(FCOPY_UIO, uio_name);
	snprintf(uio_dev_path, sizeof(uio_dev_path), "/dev/%s", uio_name);
	fcopy_fd = open(uio_dev_path, O_RDWR);

	if (fcopy_fd < 0) {
		syslog(LOG_ERR, "open %s failed; error: %d %s",
		       uio_dev_path, errno, strerror(errno));
		ret = fcopy_fd;
		goto exit;
	}

	ring = vmbus_uio_map(&fcopy_fd, HV_RING_SIZE);
	if (!ring) {
		ret = errno;
		syslog(LOG_ERR, "mmap ringbuffer failed; error: %d %s", ret, strerror(ret));
		goto close;
	}
	vmbus_br_setup(&txbr, ring, HV_RING_SIZE);
	vmbus_br_setup(&rxbr, (char *)ring + HV_RING_SIZE, HV_RING_SIZE);

	rxbr.vbr->imask = 0;

	while (1) {
		/*
		 * In this loop we process fcopy messages after the
		 * handshake is complete.
		 */
		ret = pread(fcopy_fd, &tmp, sizeof(int), 0);
		if (ret < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			syslog(LOG_ERR, "pread failed: %s", strerror(errno));
			goto close;
		}

		len = HV_RING_SIZE;
		ret = rte_vmbus_chan_recv_raw(&rxbr, desc, &len);
		if (unlikely(ret <= 0)) {
			/* This indicates a failure to communicate (or worse) */
			syslog(LOG_ERR, "VMBus channel recv error: %d", ret);
		} else {
			ret = fcopy_pkt_process(&txbr);
			if (ret < 0)
				goto close;

			/* Signal host */
			if ((write(fcopy_fd, &tmp, sizeof(int))) != sizeof(int)) {
				ret = errno;
				syslog(LOG_ERR, "Signal to host failed: %s\n", strerror(ret));
				goto close;
			}
		}
	}
close:
	close(fcopy_fd);
exit:
	return ret;
}
