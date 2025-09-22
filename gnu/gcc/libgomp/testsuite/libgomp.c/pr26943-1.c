/* PR c++/26943 */
/* { dg-do run } */

extern void abort (void);
extern void omp_set_dynamic (int);
int n = 6;

int
main (void)
{
  int i, x = 0;
  omp_set_dynamic (0);
#pragma omp parallel for num_threads (16) firstprivate (n) lastprivate (n) \
			 schedule (static, 1) reduction (+: x)
  for (i = 0; i < 16; i++)
    {
      if (n != 6)
	++x;
      n = i;
    }
  if (x || n != 15)
    abort ();
  return 0;
}
