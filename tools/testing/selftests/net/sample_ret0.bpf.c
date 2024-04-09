/* SPDX-License-Identifier: (GPL-2.0 OR BSD-2-Clause) */

#define SEC(name) __attribute__((section(name), used))

/* Sample program which should always load for testing control paths. */
SEC("xdp")
int func()
{
	return 0;
}
