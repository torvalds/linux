/*	$OpenBSD: tfind.c,v 1.7 2015/09/26 16:03:48 guenther Exp $	*/

/*
 * Tree search generalized from Knuth (6.2.2) Algorithm T just like
 * the AT&T man page says.
 *
 * The node_t structure is for internal use only
 *
 * Written by reading the System V Interface Definition, not the code.
 *
 * Totally public domain.
 */
#include <search.h>

typedef struct node_t
{
    char	  *key;
    struct node_t *llink, *rlink;
} node;

/* find a node, or return 0 */
void *
tfind(const void *vkey, void * const *vrootp,
    int (*compar)(const void *, const void *))
{
    char *key = (char *)vkey;
    node **rootp = (node **)vrootp;

    if (rootp == (struct node_t **)0)
	return ((struct node_t *)0);
    while (*rootp != (struct node_t *)0) {	/* T1: */
	int r;
	if ((r = (*compar)(key, (*rootp)->key)) == 0)	/* T2: */
	    return (*rootp);		/* key found */
	rootp = (r < 0) ?
	    &(*rootp)->llink :		/* T3: follow left branch */
	    &(*rootp)->rlink;		/* T4: follow right branch */
    }
    return (node *)0;
}
