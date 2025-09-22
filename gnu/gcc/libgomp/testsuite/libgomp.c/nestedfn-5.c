/* { dg-do run } */
/* { dg-options "-O2" } */

extern void abort (void);

void
foo (int *j)
{
  int i = 5;
  int bar (void) { return i + 1; }
#pragma omp sections
  {
    #pragma omp section
      {
	if (bar () != 6)
	#pragma omp atomic
	  ++*j;
      }
    #pragma omp section
      {
	if (bar () != 6)
	#pragma omp atomic
	  ++*j;
      }
  }
}

int
main (void)
{
  int j = 0;
#pragma omp parallel num_threads (2)
  foo (&j);
  if (j)
    abort ();
  return 0;
}

