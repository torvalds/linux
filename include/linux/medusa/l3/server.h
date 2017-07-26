#ifndef _MEDUSA_SERVER_H
#define _MEDUSA_SERVER_H

/*
 * Used by l3, l4
 *
 * This file defines the authorization server structure
 * and constants, and API for the auth. server.
 */

#include <linux/medusa/l3/constants.h>
#include <linux/medusa/l3/kobject.h>

#pragma GCC optimize ("Og")

struct medusa_authserver_s {
	char name[MEDUSA_SERVERNAME_MAX];
	int use_count; /* don't modify this directly from L2/L4 code */

	/*
	 * callbacks of authorization server.
	 *   any callback, except from decide(),
	 *   can be NULL.
	 */

	void (*close)(void);		/* this gets called after the use-count
					 * has dropped to zero, and the
					 * server can safely exit.
					 */

	/*
	 * the following callbacks must return MED_YES
	 * for the forward compatibility. del_* are unconditional;
	 * after return, no use of kclasses or evtypes is permitted.
	 *
	 * they're guaranteed to be run one at time by l3; cannot sleep.
	 */

	int (*add_kclass)(struct medusa_kclass_s * cl);
			/* called when new kclass arrives */
	void (*del_kclass)(struct medusa_kclass_s * cl);
			/* and this when it dies */
	int (*add_evtype)(struct medusa_evtype_s * at);
			/* called when new event type arrives */
	void (*del_evtype)(struct medusa_evtype_s * at);
			/* and this when it dies */

	/*
	 * this is the main callback routine of any auth server.
	 * - and the only which must NOT be NULL.
	 * May sleep. I hope.
	 */

	medusa_answer_t (*decide)(struct medusa_event_s * req,
		struct medusa_kobject_s * o1,
		struct medusa_kobject_s * o2);
};

#endif
