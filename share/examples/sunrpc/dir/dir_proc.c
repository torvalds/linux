/* @(#)dir_proc.c	2.1 88/08/02 4.0 RPCSRC */
/*
 * dir_proc.c: remote readdir implementation
 */
#include <rpc/rpc.h>
#include <sys/dir.h>
#include "dir.h"

extern int errno;
extern char *malloc();
extern char *strcpy();

readdir_res *
readdir_1(dirname)
	nametype *dirname;
{
	DIR *dirp;
	struct direct *d;
	namelist nl;
	namelist *nlp;
	static readdir_res res; /* must be static! */

	/*
	 * Open directory
	 */
	dirp = opendir(*dirname);
	if (dirp == NULL) {
		res.errno = errno;
		return (&res);
	}

	/*
	 * Free previous result
	 */
	xdr_free(xdr_readdir_res, &res);

	/*
	 * Collect directory entries
	 */
	nlp = &res.readdir_res_u.list;
	while (d = readdir(dirp)) {
		nl = *nlp = (namenode *) malloc(sizeof(namenode));
		nl->name = malloc(strlen(d->d_name)+1);
		strcpy(nl->name, d->d_name);
		nlp = &nl->next;
	}
	*nlp = NULL;

	/*
	 * Return the result
	 */
	res.errno = 0;
	closedir(dirp);
	return (&res);
}
