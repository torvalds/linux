/* { dg-do run } */

#include <assert.h>
int A[2][2] = { 1, 2, 3, 4 };
void
f (int n, int B[n][n], int C[])
{
  int D[2][2] = { 1, 2, 3, 4 };
  int E[n][n];
  assert (n >= 2);
  E[1][1] = 4;
#pragma omp parallel firstprivate(B, C, D, E)
  {
    assert (sizeof (B) == sizeof (int (*)[n]));
    assert (sizeof (C) == sizeof (int *));
    assert (sizeof (D) == 4 * sizeof (int));
    assert (sizeof (E) == n * n * sizeof (int));
    /* Private B and C have values of original B and C. */
    assert (&B[1][1] == &A[1][1]);
    assert (&C[3] == &A[1][1]);
    assert (D[1][1] == 4);
    assert (E[1][1] == 4);
  }
}
int
main ()
{
  f (2, A, A[0]);
  return 0;
}
