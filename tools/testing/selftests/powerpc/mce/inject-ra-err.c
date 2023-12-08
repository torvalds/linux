// SPDX-License-Identifier: GPL-2.0-or-later

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "vas-api.h"
#include "utils.h"

static bool faulted;

static void sigbus_handler(int n, siginfo_t *info, void *ctxt_v)
{
	ucontext_t *ctxt = (ucontext_t *)ctxt_v;
	struct pt_regs *regs = ctxt->uc_mcontext.regs;

	faulted = true;
	regs->nip += 4;
}

static int test_ra_error(void)
{
	struct vas_tx_win_open_attr attr;
	int fd, *paste_addr;
	char *devname = "/dev/crypto/nx-gzip";
	struct sigaction act = {
		.sa_sigaction = sigbus_handler,
		.sa_flags = SA_SIGINFO,
	};

	memset(&attr, 0, sizeof(attr));
	attr.version = 1;
	attr.vas_id = 0;

	SKIP_IF(access(devname, F_OK));

	fd = open(devname, O_RDWR);
	FAIL_IF(fd < 0);
	FAIL_IF(ioctl(fd, VAS_TX_WIN_OPEN, &attr) < 0);
	FAIL_IF(sigaction(SIGBUS, &act, NULL) != 0);

	paste_addr = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0ULL);

	/* The following assignment triggers exception */
	mb();
	*paste_addr = 1;
	mb();

	FAIL_IF(!faulted);

	return 0;
}

int main(void)
{
	return test_harness(test_ra_error, "inject-ra-err");
}

