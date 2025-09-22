/* Test object-spanning accesses.  This is most conveniently done with
   mmap, thus the config.h specificity here.  */
#include "../config.h"

#include <unistd.h>
#include <string.h>
#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif

int main ()
{
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif
#ifdef HAVE_MMAP
  void *p;
  unsigned pg = getpagesize ();
  int rc;

  p = mmap (NULL, 4 * pg, PROT_READ|PROT_WRITE, 
            MAP_PRIVATE|MAP_ANONYMOUS, 0, 0);
  if (p == NULL)
    return 1;

  memset (p, 0, 4*pg);
  rc = munmap (p, pg);
  if (rc < 0) return 1;
  memset (p+pg, 0, 3*pg);
  rc = munmap (p+pg, pg);
  if (rc < 0) return 1;
  memset (p+2*pg, 0, 2*pg);
  rc = munmap (p+2*pg, pg);
  if (rc < 0) return 1;
  memset (p+3*pg, 0, pg);
  rc = munmap (p+3*pg, pg);
  if (rc < 0) return 1;
#endif

  return 0;
}
