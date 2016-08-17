/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/zfs_context.h>
#include <sys/crypto/common.h>
#include <sys/crypto/api.h>
#include <sys/crypto/impl.h>
#include <sys/crypto/sched_impl.h>

/*
 * All event subscribers are put on a list. kcf_notify_list_lock
 * protects changes to this list.
 *
 * The following locking order is maintained in the code - The
 * global kcf_notify_list_lock followed by the individual lock
 * in a kcf_ntfy_elem structure (kn_lock).
 */
kmutex_t		ntfy_list_lock;
kcondvar_t		ntfy_list_cv;   /* cv the service thread waits on */
static kcf_ntfy_elem_t *ntfy_list_head;

/*
 * crypto_mech2id()
 *
 * Arguments:
 *	. mechname: A null-terminated string identifying the mechanism name.
 *
 * Description:
 *	Walks the mechanisms tables, looking for an entry that matches the
 *	mechname. Once it find it, it builds the 64-bit mech_type and returns
 *	it.  If there are no hardware or software providers for the mechanism,
 *	but there is an unloaded software provider, this routine will attempt
 *	to load it.
 *
 * Context:
 *	Process and interruption.
 *
 * Returns:
 *	The unique mechanism identified by 'mechname', if found.
 *	CRYPTO_MECH_INVALID otherwise.
 */
crypto_mech_type_t
crypto_mech2id(char *mechname)
{
	return (crypto_mech2id_common(mechname, B_TRUE));
}

/*
 * We walk the notification list and do the callbacks.
 */
void
kcf_walk_ntfylist(uint32_t event, void *event_arg)
{
	kcf_ntfy_elem_t *nep;
	int nelem = 0;

	mutex_enter(&ntfy_list_lock);

	/*
	 * Count how many clients are on the notification list. We need
	 * this count to ensure that clients which joined the list after we
	 * have started this walk, are not wrongly notified.
	 */
	for (nep = ntfy_list_head; nep != NULL; nep = nep->kn_next)
		nelem++;

	for (nep = ntfy_list_head; (nep != NULL && nelem); nep = nep->kn_next) {
		nelem--;

		/*
		 * Check if this client is interested in the
		 * event.
		 */
		if (!(nep->kn_event_mask & event))
			continue;

		mutex_enter(&nep->kn_lock);
		nep->kn_state = NTFY_RUNNING;
		mutex_exit(&nep->kn_lock);
		mutex_exit(&ntfy_list_lock);

		/*
		 * We invoke the callback routine with no locks held. Another
		 * client could have joined the list meanwhile. This is fine
		 * as we maintain nelem as stated above. The NULL check in the
		 * for loop guards against shrinkage. Also, any callers of
		 * crypto_unnotify_events() at this point cv_wait till kn_state
		 * changes to NTFY_WAITING. Hence, nep is assured to be valid.
		 */
		(*nep->kn_func)(event, event_arg);

		mutex_enter(&nep->kn_lock);
		nep->kn_state = NTFY_WAITING;
		cv_broadcast(&nep->kn_cv);
		mutex_exit(&nep->kn_lock);

		mutex_enter(&ntfy_list_lock);
	}

	mutex_exit(&ntfy_list_lock);
}

#if defined(_KERNEL) && defined(HAVE_SPL)
EXPORT_SYMBOL(crypto_mech2id);
#endif
