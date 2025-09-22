/* Prints the current time_t to stdout.  Equivalent to the
 * nonstandard %s format option to GNU date(1).
*/

#include <sys/types.h>
#include <stdio.h>
#include <time.h>

int main ()
{
    printf ("%lu\n", time(NULL));
    exit(0);
}
