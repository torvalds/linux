// SPDX-License-Identifier: GPL-2.0
/* net/atm/atm_misc.c - Various functions for use by ATM drivers */

/* Written 1995-2000 by Werner Almesberger, EPFL ICA */

#include <linux/module.h>
#include <linux/atm.h>
#include <linux/atmdev.h>
#include <linux/skbuff.h>
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/atomic.h>

int atm_charge(struct atm_vcc *vcc, int truesize)
{
	atm_force_charge(vcc, truesize);
	if (atomic_read(&sk_atm(vcc)->sk_rmem_alloc) <= sk_atm(vcc)->sk_rcvbuf)
		return 1;
	atm_return(vcc, truesize);
	atomic_inc(&vcc->stats->rx_drop);
	return 0;
}
EXPORT_SYMBOL(atm_charge);
