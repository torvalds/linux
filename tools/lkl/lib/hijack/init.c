/*
 * system calls hijack code
 * Copyright (c) 2015 Hajime Tazaki
 *
 * Author: Hajime Tazaki <tazaki@sfc.wide.ad.jp>
 *
 * Note: some of the code is picked from rumpkernel, written by Antti Kantee.
 */

#include <stdio.h>
#include <sys/socket.h>
#include <sys/select.h>

#include <lkl_host.h>

void __attribute__((constructor(102)))
hijack_init(void)
{
	int ret;

	ret = lkl_start_kernel(&lkl_host_ops, 64 * 1024 * 1024, "");
	if (ret) {
		fprintf(stderr, "can't start kernel: %s\n", lkl_strerror(ret));
		return;
	}

	/* lo iff_up */
	lkl_if_up(1);

}

void __attribute__((destructor))
hijack_fini(void)
{
	lkl_sys_halt();
}
