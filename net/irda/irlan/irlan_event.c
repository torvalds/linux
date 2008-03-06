/*********************************************************************
 *
 * Filename:      irlan_event.c
 * Version:
 * Description:
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Tue Oct 20 09:10:16 1998
 * Modified at:   Sat Oct 30 12:59:01 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 *
 *     Copyright (c) 1998-1999 Dag Brattli, All Rights Reserved.
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     Neither Dag Brattli nor University of Tromsø admit liability nor
 *     provide warranty for any of this software. This material is
 *     provided "AS-IS" and at no charge.
 *
 ********************************************************************/

#include <net/irda/irlan_event.h>

char *irlan_state[] = {
	"IRLAN_IDLE",
	"IRLAN_QUERY",
	"IRLAN_CONN",
	"IRLAN_INFO",
	"IRLAN_MEDIA",
	"IRLAN_OPEN",
	"IRLAN_WAIT",
	"IRLAN_ARB",
	"IRLAN_DATA",
	"IRLAN_CLOSE",
	"IRLAN_SYNC",
};

void irlan_next_client_state(struct irlan_cb *self, IRLAN_STATE state)
{
	IRDA_DEBUG(2, "%s(), %s\n", __func__ , irlan_state[state]);

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return;);

	self->client.state = state;
}

void irlan_next_provider_state(struct irlan_cb *self, IRLAN_STATE state)
{
	IRDA_DEBUG(2, "%s(), %s\n", __func__ , irlan_state[state]);

	IRDA_ASSERT(self != NULL, return;);
	IRDA_ASSERT(self->magic == IRLAN_MAGIC, return;);

	self->provider.state = state;
}

