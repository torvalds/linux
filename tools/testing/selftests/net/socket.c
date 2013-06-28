#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

struct socket_testcase {
	int	domain;
	int	type;
	int	protocol;

	/* 0    = valid file descriptor
	 * -foo = error foo
	 */
	int	expect;

	/* If non-zero, accept EAFNOSUPPORT to handle the case
	 * of the protocol not being configured into the kernel.
	 */
	int	nosupport_ok;
};

static struct socket_testcase tests[] = {
	{ AF_MAX,  0,           0,           -EAFNOSUPPORT,    0 },
	{ AF_INET, SOCK_STREAM, IPPROTO_TCP, 0,                1  },
	{ AF_INET, SOCK_DGRAM,  IPPROTO_TCP, -EPROTONOSUPPORT, 1  },
	{ AF_INET, SOCK_DGRAM,  IPPROTO_UDP, 0,                1  },
	{ AF_INET, SOCK_STREAM, IPPROTO_UDP, -EPROTONOSUPPORT, 1  },
};

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
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
			if (s->nosupport_ok &&
			    errno == EAFNOSUPPORT)
				continue;

			if (s->expect < 0 &&
			    errno == -s->expect)
				continue;

			strerror_r(-s->expect, err_string1, ERR_STRING_SZ);
			strerror_r(errno, err_string2, ERR_STRING_SZ);

			fprintf(stderr, "socket(%d, %d, %d) expected "
				"err (%s) got (%s)\n",
				s->domain, s->type, s->protocol,
				err_string1, err_string2);

			err = -1;
			break;
		} else {
			close(fd);

			if (s->expect < 0) {
				strerror_r(errno, err_string1, ERR_STRING_SZ);

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
