/*-
 * Copyright (c) 2005 Andrey Simonenko
 * All rights reserved.
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

#include <sys/types.h>
#include <sys/limits.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <err.h>
#include <inttypes.h>
#include <paths.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "uc_common.h"
#include "t_cmsgcred.h"
#include "t_bintime.h"
#include "t_generic.h"
#include "t_peercred.h"
#include "t_timeval.h"
#include "t_sockcred.h"
#include "t_cmsgcred_sockcred.h"
#include "t_cmsg_len.h"
#include "t_timespec_real.h"
#include "t_timespec_mono.h"

/*
 * There are tables with tests descriptions and pointers to test
 * functions.  Each t_*() function returns 0 if its test passed,
 * -1 if its test failed, -2 if some system error occurred.
 * If a test function returns -2, then a program exits.
 *
 * If a test function forks a client process, then it waits for its
 * termination.  If a return code of a client process is not equal
 * to zero, or if a client process was terminated by a signal, then
 * a test function returns -1 or -2 depending on exit status of
 * a client process.
 *
 * Each function which can block, is run under TIMEOUT.  If timeout
 * occurs, then a test function returns -2 or a client process exits
 * with a non-zero return code.
 */

struct test_func {
	int		(*func)(void);
	const char	*desc;
};

static const struct test_func test_stream_tbl[] = {
	{
	  .func = NULL,
	  .desc = "All tests"
	},
	{
	  .func = t_cmsgcred,
	  .desc = "Sending, receiving cmsgcred"
	},
	{
	  .func = t_sockcred_1,
	  .desc = "Receiving sockcred (listening socket)"
	},
	{
	  .func = t_sockcred_2,
	  .desc = "Receiving sockcred (accepted socket)"
	},
	{
	  .func = t_cmsgcred_sockcred,
	  .desc = "Sending cmsgcred, receiving sockcred"
	},
	{
	  .func = t_timeval,
	  .desc = "Sending, receiving timeval"
	},
	{
	  .func = t_bintime,
	  .desc = "Sending, receiving bintime"
	},
/*
 * The testcase fails on 64-bit architectures (amd64), but passes on 32-bit
 * architectures (i386); see bug 206543
 */
#ifndef __LP64__
	{
	  .func = t_cmsg_len,
	  .desc = "Check cmsghdr.cmsg_len"
	},
#endif
	{
	  .func = t_peercred,
	  .desc = "Check LOCAL_PEERCRED socket option"
	},
#if defined(SCM_REALTIME)
	{
	  .func = t_timespec_real,
	  .desc = "Sending, receiving realtime"
	},
#endif
#if defined(SCM_MONOTONIC)
	{
	  .func = t_timespec_mono,
	  .desc = "Sending, receiving monotonic time (uptime)"
	}
#endif
};

#define TEST_STREAM_TBL_SIZE \
	(sizeof(test_stream_tbl) / sizeof(test_stream_tbl[0]))

static const struct test_func test_dgram_tbl[] = {
	{
	  .func = NULL,
	  .desc = "All tests"
	},
	{
	  .func = t_cmsgcred,
	  .desc = "Sending, receiving cmsgcred"
	},
	{
	  .func = t_sockcred_2,
	  .desc = "Receiving sockcred"
	},
	{
	  .func = t_cmsgcred_sockcred,
	  .desc = "Sending cmsgcred, receiving sockcred"
	},
	{
	  .func = t_timeval,
	  .desc = "Sending, receiving timeval"
	},
	{
	  .func = t_bintime,
	  .desc = "Sending, receiving bintime"
	},
#ifndef __LP64__
	{
	  .func = t_cmsg_len,
	  .desc = "Check cmsghdr.cmsg_len"
	},
#endif
#if defined(SCM_REALTIME)
	{
	  .func = t_timespec_real,
	  .desc = "Sending, receiving realtime"
	},
#endif
#if defined(SCM_MONOTONIC)
	{
	  .func = t_timespec_mono,
	  .desc = "Sending, receiving monotonic time (uptime)"
	}
#endif
};

#define TEST_DGRAM_TBL_SIZE \
	(sizeof(test_dgram_tbl) / sizeof(test_dgram_tbl[0]))

static bool	failed_flag = false;

struct uc_cfg uc_cfg;

static char	work_dir[] = _PATH_TMP "unix_cmsg.XXXXXXX";

#define IPC_MSG_NUM_DEF		5
#define IPC_MSG_NUM_MAX		10
#define IPC_MSG_SIZE_DEF	7
#define IPC_MSG_SIZE_MAX	128

static void
usage(bool verbose)
{
	u_int i;

	printf("usage: %s [-dh] [-n num] [-s size] [-t type] "
	    "[-z value] [testno]\n", getprogname());
	if (!verbose)
		return;
	printf("\n Options are:\n\
  -d            Output debugging information\n\
  -h            Output the help message and exit\n\
  -n num        Number of messages to send\n\
  -s size       Specify size of data for IPC\n\
  -t type       Specify socket type (stream, dgram) for tests\n\
  -z value      Do not send data in a message (bit 0x1), do not send\n\
                data array associated with a cmsghdr structure (bit 0x2)\n\
  testno        Run one test by its number (require the -t option)\n\n");
	printf(" Available tests for stream sockets:\n");
	for (i = 0; i < TEST_STREAM_TBL_SIZE; ++i)
		printf("   %u: %s\n", i, test_stream_tbl[i].desc);
	printf("\n Available tests for datagram sockets:\n");
	for (i = 0; i < TEST_DGRAM_TBL_SIZE; ++i)
		printf("   %u: %s\n", i, test_dgram_tbl[i].desc);
}

static int
run_tests(int type, u_int testno1)
{
	const struct test_func *tf;
	u_int i, testno2, failed_num;

	uc_cfg.sock_type = type;
	if (type == SOCK_STREAM) {
		uc_cfg.sock_type_str = "SOCK_STREAM";
		tf = test_stream_tbl;
		i = TEST_STREAM_TBL_SIZE - 1;
	} else {
		uc_cfg.sock_type_str = "SOCK_DGRAM";
		tf = test_dgram_tbl;
		i = TEST_DGRAM_TBL_SIZE - 1;
	}
	if (testno1 == 0) {
		testno1 = 1;
		testno2 = i;
	} else
		testno2 = testno1;

	uc_output("Running tests for %s sockets:\n", uc_cfg.sock_type_str);
	failed_num = 0;
	for (i = testno1, tf += testno1; i <= testno2; ++tf, ++i) {
		uc_output("  %u: %s\n", i, tf->desc);
		switch (tf->func()) {
		case -1:
			++failed_num;
			break;
		case -2:
			uc_logmsgx("some system error or timeout occurred");
			return (-1);
		}
	}

	if (failed_num != 0)
		failed_flag = true;

	if (testno1 != testno2) {
		if (failed_num == 0)
			uc_output("-- all tests passed!\n");
		else
			uc_output("-- %u test%s failed!\n",
			    failed_num, failed_num == 1 ? "" : "s");
	} else {
		if (failed_num == 0)
			uc_output("-- test passed!\n");
		else
			uc_output("-- test failed!\n");
	}

	return (0);
}

static int
init(void)
{
	struct sigaction sigact;
	size_t idx;
	int rv;

	uc_cfg.proc_name = "SERVER";

	sigact.sa_handler = SIG_IGN;
	sigact.sa_flags = 0;
	sigemptyset(&sigact.sa_mask);
	if (sigaction(SIGPIPE, &sigact, (struct sigaction *)NULL) < 0) {
		uc_logmsg("init: sigaction");
		return (-1);
	}

	if (uc_cfg.ipc_msg.buf_size == 0)
		uc_cfg.ipc_msg.buf_send = uc_cfg.ipc_msg.buf_recv = NULL;
	else {
		uc_cfg.ipc_msg.buf_send = malloc(uc_cfg.ipc_msg.buf_size);
		uc_cfg.ipc_msg.buf_recv = malloc(uc_cfg.ipc_msg.buf_size);
		if (uc_cfg.ipc_msg.buf_send == NULL || uc_cfg.ipc_msg.buf_recv == NULL) {
			uc_logmsg("init: malloc");
			return (-1);
		}
		for (idx = 0; idx < uc_cfg.ipc_msg.buf_size; ++idx)
			uc_cfg.ipc_msg.buf_send[idx] = (char)idx;
	}

	uc_cfg.proc_cred.uid = getuid();
	uc_cfg.proc_cred.euid = geteuid();
	uc_cfg.proc_cred.gid = getgid();
	uc_cfg.proc_cred.egid = getegid();
	uc_cfg.proc_cred.gid_num = getgroups(0, (gid_t *)NULL);
	if (uc_cfg.proc_cred.gid_num < 0) {
		uc_logmsg("init: getgroups");
		return (-1);
	}
	uc_cfg.proc_cred.gid_arr = malloc(uc_cfg.proc_cred.gid_num *
	    sizeof(*uc_cfg.proc_cred.gid_arr));
	if (uc_cfg.proc_cred.gid_arr == NULL) {
		uc_logmsg("init: malloc");
		return (-1);
	}
	if (getgroups(uc_cfg.proc_cred.gid_num, uc_cfg.proc_cred.gid_arr) < 0) {
		uc_logmsg("init: getgroups");
		return (-1);
	}

	memset(&uc_cfg.serv_addr_sun, 0, sizeof(uc_cfg.serv_addr_sun));
	rv = snprintf(uc_cfg.serv_addr_sun.sun_path, sizeof(uc_cfg.serv_addr_sun.sun_path),
	    "%s/%s", work_dir, uc_cfg.proc_name);
	if (rv < 0) {
		uc_logmsg("init: snprintf");
		return (-1);
	}
	if ((size_t)rv >= sizeof(uc_cfg.serv_addr_sun.sun_path)) {
		uc_logmsgx("init: not enough space for socket pathname");
		return (-1);
	}
	uc_cfg.serv_addr_sun.sun_family = PF_LOCAL;
	uc_cfg.serv_addr_sun.sun_len = SUN_LEN(&uc_cfg.serv_addr_sun);

	return (0);
}

int
main(int argc, char *argv[])
{
	const char *errstr;
	u_int testno, zvalue;
	int opt, rv;
	bool dgram_flag, stream_flag;

	memset(&uc_cfg, '\0', sizeof(uc_cfg));
	uc_cfg.debug = false;
	uc_cfg.server_flag = true;
	uc_cfg.send_data_flag = true;
	uc_cfg.send_array_flag = true;
	uc_cfg.ipc_msg.buf_size = IPC_MSG_SIZE_DEF;
	uc_cfg.ipc_msg.msg_num = IPC_MSG_NUM_DEF;
	dgram_flag = stream_flag = false;
	while ((opt = getopt(argc, argv, "dhn:s:t:z:")) != -1)
		switch (opt) {
		case 'd':
			uc_cfg.debug = true;
			break;
		case 'h':
			usage(true);
			return (EXIT_SUCCESS);
		case 'n':
			uc_cfg.ipc_msg.msg_num = strtonum(optarg, 1,
			    IPC_MSG_NUM_MAX, &errstr);
			if (errstr != NULL)
				errx(EXIT_FAILURE, "option -n: number is %s",
				    errstr);
			break;
		case 's':
			uc_cfg.ipc_msg.buf_size = strtonum(optarg, 0,
			    IPC_MSG_SIZE_MAX, &errstr);
			if (errstr != NULL)
				errx(EXIT_FAILURE, "option -s: number is %s",
				    errstr);
			break;
		case 't':
			if (strcmp(optarg, "stream") == 0)
				stream_flag = true;
			else if (strcmp(optarg, "dgram") == 0)
				dgram_flag = true;
			else
				errx(EXIT_FAILURE, "option -t: "
				    "wrong socket type");
			break;
		case 'z':
			zvalue = strtonum(optarg, 0, 3, &errstr);
			if (errstr != NULL)
				errx(EXIT_FAILURE, "option -z: number is %s",
				    errstr);
			if (zvalue & 0x1)
				uc_cfg.send_data_flag = false;
			if (zvalue & 0x2)
				uc_cfg.send_array_flag = false;
			break;
		default:
			usage(false);
			return (EXIT_FAILURE);
		}

	if (optind < argc) {
		if (optind + 1 != argc)
			errx(EXIT_FAILURE, "too many arguments");
		testno = strtonum(argv[optind], 0, UINT_MAX, &errstr);
		if (errstr != NULL)
			errx(EXIT_FAILURE, "test number is %s", errstr);
		if (stream_flag && testno >= TEST_STREAM_TBL_SIZE)
			errx(EXIT_FAILURE, "given test %u for stream "
			    "sockets does not exist", testno);
		if (dgram_flag && testno >= TEST_DGRAM_TBL_SIZE)
			errx(EXIT_FAILURE, "given test %u for datagram "
			    "sockets does not exist", testno);
	} else
		testno = 0;

	if (!dgram_flag && !stream_flag) {
		if (testno != 0)
			errx(EXIT_FAILURE, "particular test number "
			    "can be used with the -t option only");
		dgram_flag = stream_flag = true;
	}

	if (mkdtemp(work_dir) == NULL)
		err(EXIT_FAILURE, "mkdtemp(%s)", work_dir);

	rv = EXIT_FAILURE;
	if (init() < 0)
		goto done;

	if (stream_flag)
		if (run_tests(SOCK_STREAM, testno) < 0)
			goto done;
	if (dgram_flag)
		if (run_tests(SOCK_DGRAM, testno) < 0)
			goto done;

	rv = EXIT_SUCCESS;
done:
	if (rmdir(work_dir) < 0) {
		uc_logmsg("rmdir(%s)", work_dir);
		rv = EXIT_FAILURE;
	}
	return (failed_flag ? EXIT_FAILURE : rv);
}
