/* PR c++/26943 */
/* { dg-do run } */

extern int omp_set_dynamic (int);
extern int omp_get_thread_num (void);
extern void abort (void);

int a = 8, b = 12, c = 16, d = 20, j = 0, l = 0;
char e[10] = "a", f[10] = "b", g[10] = "c", h[10] = "d";
volatile int k;

int
main (void)
{
  int i;
  omp_set_dynamic (0);
  omp_set_nested (1);
#pragma omp parallel num_threads (2) reduction (+:l)
  if (k == omp_get_thread_num ())
    {
#pragma omp parallel for shared (a, e) firstprivate (b, f) \
			 lastprivate (c, g) private (d, h) \
			 schedule (static, 1) num_threads (4) \
			 reduction (+:j)
      for (i = 0; i < 4; i++)
	{
	  if (a != 8 || b != 12 || e[0] != 'a' || f[0] != 'b')
	    j++;
#pragma omp barrier
#pragma omp atomic
	  a += i;
	  b += i;
	  c = i;
	  d = i;
#pragma omp atomic
	  e[0] += i;
	  f[0] += i;
	  g[0] = 'g' + i;
	  h[0] = 'h' + i;
#pragma omp barrier
	  if (a != 8 + 6 || b != 12 + i || c != i || d != i)
	    j += 8;
	  if (e[0] != 'a' + 6 || f[0] != 'b' + i || g[0] != 'g' + i)
	    j += 64;
	  if (h[0] != 'h' + i)
	    j += 512;
	}
      if (j || a != 8 + 6 || b != 12 || c != 3 || d != 20)
	++l;
      if (e[0] != 'a' + 6 || f[0] != 'b' || g[0] != 'g' + 3 || h[0] != 'd')
	l += 8;
    }
  if (l)
    abort ();
  return 0;
}
