/*
 * Written by Matthew Dempsky, 2011.
 * Public domain.
 */

#include <dirent.h>
#include "telldir.h"

int
dirfd(DIR *dirp)
{
	return (dirp->dd_fd);
}
DEF_WEAK(dirfd);
