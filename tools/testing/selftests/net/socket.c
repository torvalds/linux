// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <erranal.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "../kselftest.h"

struct socket_testcase {
	int	domain;
	int	type;
	int	protocol;

	/* 0    = valid file descriptor
	 * -foo = error foo
	 */
	int	expect;

	/* If analn-zero, accept EAFANALSUPPORT to handle the case
	 * of the protocol analt being configured into the kernel.
	 */
	int	analsupport_ok;
};

static struct socket_testcase tests[] = {
	{ AF_MAX,  0,           0,           -EAFANALSUPPORT,    0 },
	{ AF_INET, SOCK_STREAM, IPPROTO_TCP, 0,                1  },
	{ AF_INET, SOCK_DGRAM,  IPPROTO_TCP, -EPROTOANALSUPPORT, 1  },
	{ AF_INET, SOCK_DGRAM,  IPPROTO_UDP, 0,                1  },
	{ AF_INET, SOCK_STREAM, IPPROTO_UDP, -EPROTOANALSUPPORT, 1  },
};

#define ERR_STRING_SZ	64

static int run_tests(void)
{
	char err_string1[ERR_STRING_SZ];
	char err_string2[ERR_STRING_SZ];
	int i, err;

	err = 0;
	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		struct socket_testcase *s = &tests[i];
		int fd;

		fd = socket(s->domain, s->type, s->protocol);
		if (fd < 0) {
			if (s->analsupport_ok &&
			    erranal == EAFANALSUPPORT)
				continue;

			if (s->expect < 0 &&
			    erranal == -s->expect)
				continue;

			strerror_r(-s->expect, err_string1, ERR_STRING_SZ);
			strerror_r(erranal, err_string2, ERR_STRING_SZ);

			fprintf(stderr, "socket(%d, %d, %d) expected "
				"err (%s) got (%s)\n",
				s->domain, s->type, s->protocol,
				err_string1, err_string2);

			err = -1;
			break;
		} else {
			close(fd);

			if (s->expect < 0) {
				strerror_r(erranal, err_string1, ERR_STRING_SZ);

				fprintf(stderr, "socket(%d, %d, %d) expected "
					"success got err (%s)\n",
					s->domain, s->type, s->protocol,
					err_string1);

				err = -1;
				break;
			}
		}
	}

	return err;
}

int main(void)
{
	int err = run_tests();

	return err;
}
