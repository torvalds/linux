typedef struct
{
  short f : 3;
} small;

struct
{
  int i;
  small s[4];
} x;

main ()
{
  int i;
  for (i = 0; i < 5; i++)
    x.s[i].f = 0;
  exit (0);
}
/* { dg-output "mudflap violation 1.*" } */
/* { dg-output "Nearby object.*" } */
/* { dg-output "mudflap object.* x.*" } */
/* { dg-do run { xfail *-*-* } } */
