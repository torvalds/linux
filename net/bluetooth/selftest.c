/*
   BlueZ - Bluetooth protocol stack for Linux

   Copyright (C) 2014 Intel Corporation

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS,
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS
   SOFTWARE IS DISCLAIMED.
*/

#include <net/bluetooth/bluetooth.h>

#include "selftest.h"

static int __init run_selftest(void)
{
	BT_INFO("Starting self testing");

	BT_INFO("Finished self testing");

	return 0;
}

#if IS_MODULE(CONFIG_BT)

/* This is run when CONFIG_BT_SELFTEST=y and CONFIG_BT=m and is just a
 * wrapper to allow running this at module init.
 *
 * If CONFIG_BT_SELFTEST=n, then this code is not compiled at all.
 */
int __init bt_selftest(void)
{
	return run_selftest();
}

#else

/* This is run when CONFIG_BT_SELFTEST=y and CONFIG_BT=y and is run
 * via late_initcall() as last item in the initialization sequence.
 *
 * If CONFIG_BT_SELFTEST=n, then this code is not compiled at all.
 */
static int __init bt_selftest_init(void)
{
	return run_selftest();
}
late_initcall(bt_selftest_init);

#endif
