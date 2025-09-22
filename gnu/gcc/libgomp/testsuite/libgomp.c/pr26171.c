/* PR c/26171 */
/* { dg-do run } */
/* { dg-options "-fopenmp" } */
/* { dg-require-effective-target tls_runtime } */

int thrv = 0;
#pragma omp threadprivate (thrv)

int
main ()
{
  thrv = 1;
  return 0;
}
