/*
 * resizewin
 *
 * Query terminal for size and inform the kernel
 *
 * Copyright 2015 EMC / Isilon Storage Division
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/ioctl.h>
#include <sys/time.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <termios.h>
#include <unistd.h>

/* screen doesn't support ESC[18t (return terminal size) so do it the hard way */
static const char query[] =
    "\0337"		/* Save cursor position */
    "\033[r"		/* Scroll whole screen */
    "\033[999;999H"	/* Move cursor */
    "\033[6n"		/* Get cursor position */
    "\0338";		/* Restore cursor position */

static void
usage(void)
{

	fprintf(stderr, "usage: resizewin [-z]\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	struct termios old, new;
	struct winsize w;
	struct timeval then, now;
	char data[20];
	int ch, cnt, error, fd, ret, zflag;

	error = 0;
	zflag = 0;
	while ((ch = getopt(argc, argv, "z")) != -1) {
		switch (ch) {
		case 'z':
			zflag = 1;
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	if (argc != 0)
		usage();

	if ((fd = open("/dev/tty", O_RDWR | O_NONBLOCK)) == -1)
		exit(1);

	if (zflag) {
		if (ioctl(fd, TIOCGWINSZ, &w) == -1)
			exit(1);
		if (w.ws_row != 0 && w.ws_col != 0)
			exit(0);
	}

	/* Disable echo, flush the input, and drain the output */
	if (tcgetattr(fd, &old) == -1)
		exit(1);

	new = old;
	new.c_cflag |= (CLOCAL | CREAD);
	new.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
	if (tcsetattr(fd, TCSAFLUSH, &new) == -1)
		exit(1);

	if (write(fd, query, sizeof(query)) != sizeof(query)) {
		error = 1;
		goto out;
	}

	/* Read the response */
	bzero(data, sizeof(data));
	gettimeofday(&then, NULL);
	cnt = 0;
	while (1) {
		ret = read(fd, data + cnt, 1);

		if (ret == -1) {
			if (errno == EAGAIN) {
				gettimeofday(&now, NULL);
				timersub(&now, &then, &now);
				if (now.tv_sec >= 2) {
					warnx("timeout reading from terminal");
					error = 1;
					goto out;
				}

				usleep(20000);
				continue;
			}
			error = 1;
			goto out;
		}
		if (data[cnt] == 'R')
			break;

		cnt++;
		if (cnt == sizeof(data) - 2) {
			warnx("response too long");
			error = 1;
			goto out;
		}
	}

	/* Parse */
	if (sscanf(data, "\033[%hu;%huR", &w.ws_row, &w.ws_col) != 2) {
		error = 1;
		warnx("unable to parse response");
		goto out;
	}

	/* Finally, what we want */
	if (ioctl(fd, TIOCSWINSZ, &w) == -1)
		error = 1;
 out:
	/* Restore echo */
	tcsetattr(fd, TCSANOW, &old);

	close(fd);
	exit(error);
}
