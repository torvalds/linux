#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
struct boo { int a; };
int c;
struct boo *b = malloc (sizeof (struct boo));
__mf_set_options ("-check-initialization");
b->a = 0;
/* That __mf_set_options call could be here instead. */
c = b->a;
(void) malloc (c); /* some dummy use of c */
return 0;
}
