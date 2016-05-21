/*
 * ACPI AML interfacing userspace utility
 *
 * Copyright (C) 2015, Intel Corporation
 * Authors: Lv Zheng <lv.zheng@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <acpi/acpi.h>

/* Headers not included by include/acpi/platform/aclinux.h */
#include <stdbool.h>
#include <fcntl.h>
#include <assert.h>
#include <linux/circ_buf.h>

#define ACPI_AML_FILE		"/sys/kernel/debug/acpi/acpidbg"
#define ACPI_AML_SEC_TICK	1
#define ACPI_AML_USEC_PEEK	200
#define ACPI_AML_BUF_SIZE	4096

#define ACPI_AML_BATCH_WRITE_CMD	0x00 /* Write command to kernel */
#define ACPI_AML_BATCH_READ_LOG		0x01 /* Read log from kernel */
#define ACPI_AML_BATCH_WRITE_LOG	0x02 /* Write log to console */

#define ACPI_AML_LOG_START		0x00
#define ACPI_AML_PROMPT_START		0x01
#define ACPI_AML_PROMPT_STOP		0x02
#define ACPI_AML_LOG_STOP		0x03
#define ACPI_AML_PROMPT_ROLL		0x04

#define ACPI_AML_INTERACTIVE	0x00
#define ACPI_AML_BATCH		0x01

#define circ_count(circ) \
	(CIRC_CNT((circ)->head, (circ)->tail, ACPI_AML_BUF_SIZE))
#define circ_count_to_end(circ) \
	(CIRC_CNT_TO_END((circ)->head, (circ)->tail, ACPI_AML_BUF_SIZE))
#define circ_space(circ) \
	(CIRC_SPACE((circ)->head, (circ)->tail, ACPI_AML_BUF_SIZE))
#define circ_space_to_end(circ) \
	(CIRC_SPACE_TO_END((circ)->head, (circ)->tail, ACPI_AML_BUF_SIZE))

#define acpi_aml_cmd_count()	circ_count(&acpi_aml_cmd_crc)
#define acpi_aml_log_count()	circ_count(&acpi_aml_log_crc)
#define acpi_aml_cmd_space()	circ_space(&acpi_aml_cmd_crc)
#define acpi_aml_log_space()	circ_space(&acpi_aml_log_crc)

#define ACPI_AML_DO(_fd, _op, _buf, _ret)				\
	do {								\
		_ret = acpi_aml_##_op(_fd, &acpi_aml_##_buf##_crc);	\
		if (_ret == 0) {					\
			fprintf(stderr,					\
				"%s %s pipe closed.\n", #_buf, #_op);	\
			return;						\
		}							\
	} while (0)
#define ACPI_AML_BATCH_DO(_fd, _op, _buf, _ret)				\
	do {								\
		_ret = acpi_aml_##_op##_batch_##_buf(_fd,		\
			 &acpi_aml_##_buf##_crc);			\
		if (_ret == 0)						\
			return;						\
	} while (0)


static char acpi_aml_cmd_buf[ACPI_AML_BUF_SIZE];
static char acpi_aml_log_buf[ACPI_AML_BUF_SIZE];
static struct circ_buf acpi_aml_cmd_crc = {
	.buf = acpi_aml_cmd_buf,
	.head = 0,
	.tail = 0,
};
static struct circ_buf acpi_aml_log_crc = {
	.buf = acpi_aml_log_buf,
	.head = 0,
	.tail = 0,
};
static const char *acpi_aml_file_path = ACPI_AML_FILE;
static unsigned long acpi_aml_mode = ACPI_AML_INTERACTIVE;
static bool acpi_aml_exit;

static bool acpi_aml_batch_drain;
static unsigned long acpi_aml_batch_state;
static char acpi_aml_batch_prompt;
static char acpi_aml_batch_roll;
static unsigned long acpi_aml_log_state;
static char *acpi_aml_batch_cmd = NULL;
static char *acpi_aml_batch_pos = NULL;

static int acpi_aml_set_fl(int fd, int flags)
{
	int ret;

	ret = fcntl(fd, F_GETFL, 0);
	if (ret < 0) {
		perror("fcntl(F_GETFL)");
		return ret;
	}
	flags |= ret;
	ret = fcntl(fd, F_SETFL, flags);
	if (ret < 0) {
		perror("fcntl(F_SETFL)");
		return ret;
	}
	return ret;
}

static int acpi_aml_set_fd(int fd, int maxfd, fd_set *set)
{
	if (fd > maxfd)
		maxfd = fd;
	FD_SET(fd, set);
	return maxfd;
}

static int acpi_aml_read(int fd, struct circ_buf *crc)
{
	char *p;
	int len;

	p = &crc->buf[crc->head];
	len = circ_space_to_end(crc);
	len = read(fd, p, len);
	if (len < 0)
		perror("read");
	else if (len > 0)
		crc->head = (crc->head + len) & (ACPI_AML_BUF_SIZE - 1);
	return len;
}

static int acpi_aml_read_batch_cmd(int unused, struct circ_buf *crc)
{
	char *p;
	int len;
	int remained = strlen(acpi_aml_batch_pos);

	p = &crc->buf[crc->head];
	len = circ_space_to_end(crc);
	if (len > remained) {
		memcpy(p, acpi_aml_batch_pos, remained);
		acpi_aml_batch_pos += remained;
		len = remained;
	} else {
		memcpy(p, acpi_aml_batch_pos, len);
		acpi_aml_batch_pos += len;
	}
	if (len > 0)
		crc->head = (crc->head + len) & (ACPI_AML_BUF_SIZE - 1);
	return len;
}

static int acpi_aml_read_batch_log(int fd, struct circ_buf *crc)
{
	char *p;
	int len;
	int ret = 0;

	p = &crc->buf[crc->head];
	len = circ_space_to_end(crc);
	while (ret < len && acpi_aml_log_state != ACPI_AML_LOG_STOP) {
		if (acpi_aml_log_state == ACPI_AML_PROMPT_ROLL) {
			*p = acpi_aml_batch_roll;
			len = 1;
			crc->head = (crc->head + 1) & (ACPI_AML_BUF_SIZE - 1);
			ret += 1;
			acpi_aml_log_state = ACPI_AML_LOG_START;
		} else {
			len = read(fd, p, 1);
			if (len <= 0) {
				if (len < 0)
					perror("read");
				ret = len;
				break;
			}
		}
		switch (acpi_aml_log_state) {
		case ACPI_AML_LOG_START:
			if (*p == '\n')
				acpi_aml_log_state = ACPI_AML_PROMPT_START;
			crc->head = (crc->head + 1) & (ACPI_AML_BUF_SIZE - 1);
			ret += 1;
			break;
		case ACPI_AML_PROMPT_START:
			if (*p == ACPI_DEBUGGER_COMMAND_PROMPT ||
			    *p == ACPI_DEBUGGER_EXECUTE_PROMPT) {
				acpi_aml_batch_prompt = *p;
				acpi_aml_log_state = ACPI_AML_PROMPT_STOP;
			} else {
				if (*p != '\n')
					acpi_aml_log_state = ACPI_AML_LOG_START;
				crc->head = (crc->head + 1) & (ACPI_AML_BUF_SIZE - 1);
				ret += 1;
			}
			break;
		case ACPI_AML_PROMPT_STOP:
			if (*p == ' ') {
				acpi_aml_log_state = ACPI_AML_LOG_STOP;
				acpi_aml_exit = true;
			} else {
				/* Roll back */
				acpi_aml_log_state = ACPI_AML_PROMPT_ROLL;
				acpi_aml_batch_roll = *p;
				*p = acpi_aml_batch_prompt;
				crc->head = (crc->head + 1) & (ACPI_AML_BUF_SIZE - 1);
				ret += 1;
			}
			break;
		default:
			assert(0);
			break;
		}
	}
	return ret;
}

static int acpi_aml_write(int fd, struct circ_buf *crc)
{
	char *p;
	int len;

	p = &crc->buf[crc->tail];
	len = circ_count_to_end(crc);
	len = write(fd, p, len);
	if (len < 0)
		perror("write");
	else if (len > 0)
		crc->tail = (crc->tail + len) & (ACPI_AML_BUF_SIZE - 1);
	return len;
}

static int acpi_aml_write_batch_log(int fd, struct circ_buf *crc)
{
	char *p;
	int len;

	p = &crc->buf[crc->tail];
	len = circ_count_to_end(crc);
	if (!acpi_aml_batch_drain) {
		len = write(fd, p, len);
		if (len < 0)
			perror("write");
	}
	if (len > 0)
		crc->tail = (crc->tail + len) & (ACPI_AML_BUF_SIZE - 1);
	return len;
}

static int acpi_aml_write_batch_cmd(int fd, struct circ_buf *crc)
{
	int len;

	len = acpi_aml_write(fd, crc);
	if (circ_count_to_end(crc) == 0)
		acpi_aml_batch_state = ACPI_AML_BATCH_READ_LOG;
	return len;
}

static void acpi_aml_loop(int fd)
{
	fd_set rfds;
	fd_set wfds;
	struct timeval tv;
	int ret;
	int maxfd = 0;

	if (acpi_aml_mode == ACPI_AML_BATCH) {
		acpi_aml_log_state = ACPI_AML_LOG_START;
		acpi_aml_batch_pos = acpi_aml_batch_cmd;
		if (acpi_aml_batch_drain)
			acpi_aml_batch_state = ACPI_AML_BATCH_READ_LOG;
		else
			acpi_aml_batch_state = ACPI_AML_BATCH_WRITE_CMD;
	}
	acpi_aml_exit = false;
	while (!acpi_aml_exit) {
		tv.tv_sec = ACPI_AML_SEC_TICK;
		tv.tv_usec = 0;
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);

		if (acpi_aml_cmd_space()) {
			if (acpi_aml_mode == ACPI_AML_INTERACTIVE)
				maxfd = acpi_aml_set_fd(STDIN_FILENO, maxfd, &rfds);
			else if (strlen(acpi_aml_batch_pos) &&
				 acpi_aml_batch_state == ACPI_AML_BATCH_WRITE_CMD)
				ACPI_AML_BATCH_DO(STDIN_FILENO, read, cmd, ret);
		}
		if (acpi_aml_cmd_count() &&
		    (acpi_aml_mode == ACPI_AML_INTERACTIVE ||
		     acpi_aml_batch_state == ACPI_AML_BATCH_WRITE_CMD))
			maxfd = acpi_aml_set_fd(fd, maxfd, &wfds);
		if (acpi_aml_log_space() &&
		    (acpi_aml_mode == ACPI_AML_INTERACTIVE ||
		     acpi_aml_batch_state == ACPI_AML_BATCH_READ_LOG))
			maxfd = acpi_aml_set_fd(fd, maxfd, &rfds);
		if (acpi_aml_log_count())
			maxfd = acpi_aml_set_fd(STDOUT_FILENO, maxfd, &wfds);

		ret = select(maxfd+1, &rfds, &wfds, NULL, &tv);
		if (ret < 0) {
			perror("select");
			break;
		}
		if (ret > 0) {
			if (FD_ISSET(STDIN_FILENO, &rfds))
				ACPI_AML_DO(STDIN_FILENO, read, cmd, ret);
			if (FD_ISSET(fd, &wfds)) {
				if (acpi_aml_mode == ACPI_AML_BATCH)
					ACPI_AML_BATCH_DO(fd, write, cmd, ret);
				else
					ACPI_AML_DO(fd, write, cmd, ret);
			}
			if (FD_ISSET(fd, &rfds)) {
				if (acpi_aml_mode == ACPI_AML_BATCH)
					ACPI_AML_BATCH_DO(fd, read, log, ret);
				else
					ACPI_AML_DO(fd, read, log, ret);
			}
			if (FD_ISSET(STDOUT_FILENO, &wfds)) {
				if (acpi_aml_mode == ACPI_AML_BATCH)
					ACPI_AML_BATCH_DO(STDOUT_FILENO, write, log, ret);
				else
					ACPI_AML_DO(STDOUT_FILENO, write, log, ret);
			}
		}
	}
}

static bool acpi_aml_readable(int fd)
{
	fd_set rfds;
	struct timeval tv;
	int ret;
	int maxfd = 0;

	tv.tv_sec = 0;
	tv.tv_usec = ACPI_AML_USEC_PEEK;
	FD_ZERO(&rfds);
	maxfd = acpi_aml_set_fd(fd, maxfd, &rfds);
	ret = select(maxfd+1, &rfds, NULL, NULL, &tv);
	if (ret < 0)
		perror("select");
	if (ret > 0 && FD_ISSET(fd, &rfds))
		return true;
	return false;
}

/*
 * This is a userspace IO flush implementation, replying on the prompt
 * characters and can be turned into a flush() call after kernel implements
 * .flush() filesystem operation.
 */
static void acpi_aml_flush(int fd)
{
	while (acpi_aml_readable(fd)) {
		acpi_aml_batch_drain = true;
		acpi_aml_loop(fd);
		acpi_aml_batch_drain = false;
	}
}

void usage(FILE *file, char *progname)
{
	fprintf(file, "usage: %s [-b cmd] [-f file] [-h]\n", progname);
	fprintf(file, "\nOptions:\n");
	fprintf(file, "  -b     Specify command to be executed in batch mode\n");
	fprintf(file, "  -f     Specify interface file other than");
	fprintf(file, "         /sys/kernel/debug/acpi/acpidbg\n");
	fprintf(file, "  -h     Print this help message\n");
}

int main(int argc, char **argv)
{
	int fd = -1;
	int ch;
	int len;
	int ret = EXIT_SUCCESS;

	while ((ch = getopt(argc, argv, "b:f:h")) != -1) {
		switch (ch) {
		case 'b':
			if (acpi_aml_batch_cmd) {
				fprintf(stderr, "Already specify %s\n",
					acpi_aml_batch_cmd);
				ret = EXIT_FAILURE;
				goto exit;
			}
			len = strlen(optarg);
			acpi_aml_batch_cmd = calloc(len + 2, 1);
			if (!acpi_aml_batch_cmd) {
				perror("calloc");
				ret = EXIT_FAILURE;
				goto exit;
			}
			memcpy(acpi_aml_batch_cmd, optarg, len);
			acpi_aml_batch_cmd[len] = '\n';
			acpi_aml_mode = ACPI_AML_BATCH;
			break;
		case 'f':
			acpi_aml_file_path = optarg;
			break;
		case 'h':
			usage(stdout, argv[0]);
			goto exit;
			break;
		case '?':
		default:
			usage(stderr, argv[0]);
			ret = EXIT_FAILURE;
			goto exit;
			break;
		}
	}

	fd = open(acpi_aml_file_path, O_RDWR | O_NONBLOCK);
	if (fd < 0) {
		perror("open");
		ret = EXIT_FAILURE;
		goto exit;
	}
	acpi_aml_set_fl(STDIN_FILENO, O_NONBLOCK);
	acpi_aml_set_fl(STDOUT_FILENO, O_NONBLOCK);

	if (acpi_aml_mode == ACPI_AML_BATCH)
		acpi_aml_flush(fd);
	acpi_aml_loop(fd);

exit:
	if (fd >= 0)
		close(fd);
	if (acpi_aml_batch_cmd)
		free(acpi_aml_batch_cmd);
	return ret;
}
