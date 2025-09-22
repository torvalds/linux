/*

@deftypefn Supplemental char* strdup (const char *@var{s})

Returns a pointer to a copy of @var{s} in memory obtained from
@code{malloc}, or @code{NULL} if insufficient memory was available.

@end deftypefn

*/

#include <ansidecl.h>
#include <stddef.h>

extern size_t	strlen (const char*);
extern PTR	malloc (size_t);
extern PTR	memcpy (PTR, const PTR, size_t);

char *
strdup(const char *s)
{
  size_t len = strlen (s) + 1;
  char *result = (char*) malloc (len);
  if (result == (char*) 0)
    return (char*) 0;
  return (char*) memcpy (result, s, len);
}
