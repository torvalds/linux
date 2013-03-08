/*
 * Copyright (c) 2010, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Author: John Fastabend <john.r.fastabend@intel.com>
 */

#include <linux/rtnetlink.h>
#include <linux/notifier.h>
#include <linux/export.h>
#include <net/dcbevent.h>

static ATOMIC_NOTIFIER_HEAD(dcbevent_notif_chain);

int register_dcbevent_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&dcbevent_notif_chain, nb);
}
EXPORT_SYMBOL(register_dcbevent_notifier);

int unregister_dcbevent_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&dcbevent_notif_chain, nb);
}
EXPORT_SYMBOL(unregister_dcbevent_notifier);

int call_dcbevent_notifiers(unsigned long val, void *v)
{
	return atomic_notifier_call_chain(&dcbevent_notif_chain, val, v);
}
