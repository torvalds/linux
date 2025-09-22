/* Test proper lookup-uncaching of large objects */
#include "../config.h"

#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif

int main ()
{
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif
#ifdef HAVE_MMAP
  volatile unsigned char *p;
  unsigned num = getpagesize ();
  unsigned i;
  int rc;

  /* Get a bit of usable address space.  We really want an 2**N+1-sized object,
     so the low/high addresses wrap when hashed into the lookup cache.  So we
     will manually unregister the entire mmap, then re-register a slice.  */
  p = mmap (NULL, num, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, 0, 0);
  if (p == NULL)
    return 1;
  /* Now unregister it, as if munmap was called.  But don't actually munmap, so
     we can write into the memory.  */
  __mf_unregister ((void *) p, num, __MF_TYPE_HEAP_I);

  /* Now register it under a slightly inflated, 2**N+1 size.  */
  __mf_register ((void *) p, num+1, __MF_TYPE_HEAP_I, "fake mmap registration");

  /* Traverse array to ensure that entire lookup cache is made to point at it.  */
  for (i=0; i<num; i++)
    p[i] = 0;

  /* Unregister it.  This should clear the entire lookup cache, even though
     hash(low) == hash (high)  (and probably == 0) */
  __mf_unregister ((void *) p, num+1, __MF_TYPE_HEAP_I);

  /* Now touch the middle portion of the ex-array.  If the lookup cache was
     well and truly cleaned, then this access should trap.  */
  p[num/2] = 1;

  return 0;
#else
  return 1;
#endif
}
/* { dg-output "mudflap violation 1.*check/write.*" } */
/* { dg-output "Nearby object 1.*" } */
/* { dg-output "mudflap dead object.*fake mmap registration.*" } */
/* { dg-do run { xfail *-*-* } } */
