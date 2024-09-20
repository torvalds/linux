// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Dell AIO Serial Backlight board emulator for testing
 * the Linux dell-uart-backlight driver.
 *
 * Copyright (C) 2024 Hans de Goede <hansg@kernel.org>
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <termios.h>
#include <unistd.h>

int serial_fd;
int brightness = 50;

static unsigned char dell_uart_checksum(unsigned char *buf, int len)
{
	unsigned char val = 0;

	while (len-- > 0)
		val += buf[len];

	return val ^ 0xff;
}

/* read() will return -1 on SIGINT / SIGTERM causing the mainloop to cleanly exit */
void signalhdlr(int signum)
{
}

int main(int argc, char *argv[])
{
	struct sigaction sigact = { .sa_handler = signalhdlr };
	unsigned char buf[4], csum, response[32];
	const char *version_str = "PHI23-V321";
	struct termios tty, saved_tty;
	int ret, idx, len = 0;

	if (argc != 2) {
		fprintf(stderr, "Invalid or missing arguments\n");
		fprintf(stderr, "Usage: %s <serial-port>\n", argv[0]);
		return 1;
	}

	serial_fd = open(argv[1], O_RDWR | O_NOCTTY);
	if (serial_fd == -1) {
		fprintf(stderr, "Error opening %s: %s\n", argv[1], strerror(errno));
		return 1;
	}

	ret = tcgetattr(serial_fd, &tty);
	if (ret == -1) {
		fprintf(stderr, "Error getting tcattr: %s\n", strerror(errno));
		goto out_close;
	}
	saved_tty = tty;

	cfsetspeed(&tty, 9600);
	cfmakeraw(&tty);
	tty.c_cflag &= ~CSTOPB;
	tty.c_cflag &= ~CRTSCTS;
	tty.c_cflag |= CLOCAL | CREAD;

	ret = tcsetattr(serial_fd, TCSANOW, &tty);
	if (ret == -1) {
		fprintf(stderr, "Error setting tcattr: %s\n", strerror(errno));
		goto out_restore;
	}

	sigaction(SIGINT, &sigact, 0);
	sigaction(SIGTERM, &sigact, 0);

	idx = 0;
	while (read(serial_fd, &buf[idx], 1) == 1) {
		if (idx == 0) {
			switch (buf[0]) {
			/* 3 MSB bits: cmd-len + 01010 SOF marker */
			case 0x6a: len = 3; break;
			case 0x8a: len = 4; break;
			default:
				fprintf(stderr, "Error unexpected first byte: 0x%02x\n", buf[0]);
				continue; /* Try to sync up with sender */
			}
		}

		/* Process msg when len bytes have been received */
		if (idx != (len - 1)) {
			idx++;
			continue;
		}

		/* Reset idx for next command */
		idx = 0;

		csum = dell_uart_checksum(buf, len - 1);
		if (buf[len - 1] != csum) {
			fprintf(stderr, "Error checksum mismatch got 0x%02x expected 0x%02x\n",
				buf[len - 1], csum);
			continue;
		}

		switch ((buf[0] << 8) | buf[1]) {
		case 0x6a06: /* cmd = 0x06, get version */
			len = strlen(version_str);
			strcpy((char *)&response[2], version_str);
			printf("Get version, reply: %s\n", version_str);
			break;
		case 0x8a0b: /* cmd = 0x0b, set brightness */
			if (buf[2] > 100) {
				fprintf(stderr, "Error invalid brightness param: %d\n", buf[2]);
				continue;
			}

			len = 0;
			brightness = buf[2];
			printf("Set brightness %d\n", brightness);
			break;
		case 0x6a0c: /* cmd = 0x0c, get brightness */
			len = 1;
			response[2] = brightness;
			printf("Get brightness, reply: %d\n", brightness);
			break;
		case 0x8a0e: /* cmd = 0x0e, set backlight power */
			if (buf[2] != 0 && buf[2] != 1) {
				fprintf(stderr, "Error invalid set power param: %d\n", buf[2]);
				continue;
			}

			len = 0;
			printf("Set power %d\n", buf[2]);
			break;
		default:
			fprintf(stderr, "Error unknown cmd 0x%04x\n",
				(buf[0] << 8) | buf[1]);
			continue;
		}

		/* Respond with <total-len> <cmd> <data...> <csum> */
		response[0] = len + 3; /* response length in bytes */
		response[1] = buf[1];  /* ack cmd */
		csum = dell_uart_checksum(response, len + 2);
		response[len + 2] = csum;
		ret = write(serial_fd, response, response[0]);
		if (ret != (response[0]))
			fprintf(stderr, "Error writing %d bytes: %d\n",
				response[0], ret);
	}

	ret = 0;
out_restore:
	tcsetattr(serial_fd, TCSANOW, &saved_tty);
out_close:
	close(serial_fd);
	return ret;
}
