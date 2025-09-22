#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
int t;
char foo[3] = { 'b', 'c', 'd' };
int bar[3] = {1, 2, 0};
t = 1;

/* These tests check expression evaluation rules, such as
   ensuring that side-effect expression (++) get executed the
   right number of times; that array lookup checks nest correctly. */
foo[t++] = 'a';
if (foo[0] != 'b' || foo[1] != 'a' || foo[2] != 'd' || t != 2) abort ();
if (bar[0] != 1 || bar[1] != 2 || bar[2] != 0) abort();

foo[bar[t--]] = 'e';
if (foo[0] != 'e' || foo[1] != 'a' || foo[2] != 'd' || t != 1) abort ();
if (bar[0] != 1 || bar[1] != 2 || bar[2] != 0) abort();

foo[bar[++t]--] = 'g';
if (foo[0] != 'g' || foo[1] != 'a' || foo[2] != 'd' || t != 2) abort ();
if (bar[0] != 1 || bar[1] != 2 || bar[2] != -1) abort();

return 0;
}
