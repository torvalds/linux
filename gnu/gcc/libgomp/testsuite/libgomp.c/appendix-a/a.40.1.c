/* { dg-do compile } */

#include <omp.h>
typedef struct
{
  int a, b;
  omp_nest_lock_t lck;
} pair;
int work1 ();
int work2 ();
int work3 ();
void
incr_a (pair * p, int a)
{
  /* Called only from incr_pair, no need to lock. */
  p->a += a;
}

void
incr_b (pair * p, int b)
{
  /* Called both from incr_pair and elsewhere, */
  /* so need a nestable lock. */
  omp_set_nest_lock (&p->lck);
  p->b += b;
  omp_unset_nest_lock (&p->lck);
}

void
incr_pair (pair * p, int a, int b)
{
  omp_set_nest_lock (&p->lck);
  incr_a (p, a);
  incr_b (p, b);
  omp_unset_nest_lock (&p->lck);
}

void
a40 (pair * p)
{
#pragma omp parallel sections
  {
#pragma omp section
    incr_pair (p, work1 (), work2 ());
#pragma omp section
    incr_b (p, work3 ());
  }
}
