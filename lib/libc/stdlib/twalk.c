/*	$NetBSD: twalk.c,v 1.4 2012/03/20 16:38:45 matt Exp $	*/

/*
 * Tree search generalized from Knuth (6.2.2) Algorithm T just like
 * the AT&T man page says.
 *
 * Written by reading the System V Interface Definition, not the code.
 *
 * Totally public domain.
 */

#include <sys/cdefs.h>
#if 0
#if defined(LIBC_SCCS) && !defined(lint)
__RCSID("$NetBSD: twalk.c,v 1.4 2012/03/20 16:38:45 matt Exp $");
#endif /* LIBC_SCCS and not lint */
#endif
__FBSDID("$FreeBSD$");

#define _SEARCH_PRIVATE
#include <search.h>
#include <stdlib.h>

typedef void (*cmp_fn_t)(const posix_tnode *, VISIT, int);

/* Walk the nodes of a tree */
static void
trecurse(const posix_tnode *root, cmp_fn_t action, int level)
{

	if (root->llink == NULL && root->rlink == NULL)
		(*action)(root, leaf, level);
	else {
		(*action)(root, preorder, level);
		if (root->llink != NULL)
			trecurse(root->llink, action, level + 1);
		(*action)(root, postorder, level);
		if (root->rlink != NULL)
			trecurse(root->rlink, action, level + 1);
		(*action)(root, endorder, level);
	}
}

/* Walk the nodes of a tree */
void
twalk(const posix_tnode *vroot, cmp_fn_t action)
{
	if (vroot != NULL && action != NULL)
		trecurse(vroot, action, 0);
}
