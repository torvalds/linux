/*
 * Initial implementation:
 * Copyright (c) 2002 Robert Drehmel
 * All rights reserved.
 *
 * As long as the above copyright statement and this notice remain
 * unchanged, you can do what ever you want with this file. 
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#define	_SEARCH_PRIVATE
#include <search.h>
#ifdef DEBUG
#include <stdio.h>
#else
#include <stdlib.h>	/* for NULL */
#endif

void
insque(void *element, void *pred)
{
	struct que_elem *prev, *next, *elem;

	elem = (struct que_elem *)element;
	prev = (struct que_elem *)pred;

	if (prev == NULL) {
		elem->prev = elem->next = NULL;
		return;
	}

	next = prev->next;
	if (next != NULL) {
#ifdef DEBUG
		if (next->prev != prev) {
			fprintf(stderr, "insque: Inconsistency detected:"
			    " next(%p)->prev(%p) != prev(%p)\n",
			    next, next->prev, prev);
		}
#endif
		next->prev = elem;
	}
	prev->next = elem;
	elem->prev = prev;
	elem->next = next;
}
