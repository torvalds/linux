/* { dg-do compile } */

#include <stdio.h>
#include <stdlib.h>
#include <omp.h>
omp_lock_t *
new_lock ()
{
  omp_lock_t *lock_ptr;
#pragma omp single copyprivate(lock_ptr)
  {
    lock_ptr = (omp_lock_t *) malloc (sizeof (omp_lock_t));
    omp_init_lock (lock_ptr);
  }
  return lock_ptr;
}
