/*	$OpenBSD: search.h,v 1.10 2014/07/18 04:16:09 matthew Exp $	*/
/*	$NetBSD: search.h,v 1.9 1995/08/08 21:14:45 jtc Exp $	*/

/*
 * Written by J.T. Conklin <jtc@netbsd.org>
 * Public domain.
 */

#ifndef _SEARCH_H_
#define _SEARCH_H_

#include <sys/cdefs.h>
#include <machine/_types.h>

#ifndef	_SIZE_T_DEFINED_
#define	_SIZE_T_DEFINED_
typedef	__size_t	size_t;
#endif

typedef struct entry {
	char *key;
	void *data;
} ENTRY;

typedef enum {
	FIND, ENTER
} ACTION;

typedef enum {
	preorder,
	postorder,
	endorder,
	leaf
} VISIT;

__BEGIN_DECLS
int	 hcreate(size_t);
void	 hdestroy(void);
ENTRY	*hsearch(ENTRY, ACTION);

void	*lfind(const void *, const void *, size_t *, size_t,
	    int (*)(const void *, const void *));
void	*lsearch(const void *, void *, size_t *, size_t,
	    int (*)(const void *, const void *));
void	 insque(void *, void *);
void	 remque(void *);

void	*tdelete(const void * __restrict, void ** __restrict,
	    int (*)(const void *, const void *));
void	*tfind(const void *, void * const *,
	    int (*)(const void *, const void *));
void	*tsearch(const void *, void **, 
	    int (*)(const void *, const void *));
void      twalk(const void *, void (*)(const void *, VISIT, int));
__END_DECLS

#endif /* !_SEARCH_H_ */
