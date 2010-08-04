/* Provides edac interface to mcelog events
 *
 * This file may be distributed under the terms of the
 * GNU General Public License version 2.
 *
 * Copyright (c) 2009 by:
 *	 Mauro Carvalho Chehab <mchehab@redhat.com>
 *
 * Red Hat Inc. http://www.redhat.com
 */

#if defined(CONFIG_EDAC_MCE) || \
			(defined(CONFIG_EDAC_MCE_MODULE) && defined(MODULE))

#include <asm/mce.h>
#include <linux/list.h>

struct edac_mce {
	struct list_head list;

	void *priv;
	int (*check_error)(void *priv, struct mce *mce);
};

int edac_mce_register(struct edac_mce *edac_mce);
void edac_mce_unregister(struct edac_mce *edac_mce);
int edac_mce_parse(struct mce *mce);

#else
#define edac_mce_parse(mce) (0)
#endif
