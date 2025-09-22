/* { dg-do run } */

#include <stdio.h>
#include <omp.h>
void
skip (int i)
{
}

void
work (int i)
{
}
int
main ()
{
  omp_lock_t lck;
  int id;
  omp_init_lock (&lck);
#pragma omp parallel shared(lck) private(id)
  {
    id = omp_get_thread_num ();
    omp_set_lock (&lck);
    /* only one thread at a time can execute this printf */
    printf ("My thread id is %d.\n", id);
    omp_unset_lock (&lck);
    while (!omp_test_lock (&lck))
      {
	skip (id);		/* we do not yet have the lock,
				   so we must do something else */
      }
    work (id);			/* we now have the lock
				   and can do the work */
    omp_unset_lock (&lck);
  }
  omp_destroy_lock (&lck);
  return 0;
}
