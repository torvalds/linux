/* Stub implementation of (obsolete) index(). */

/*

@deftypefn Supplemental char* index (char *@var{s}, int @var{c})

Returns a pointer to the first occurrence of the character @var{c} in
the string @var{s}, or @code{NULL} if not found.  The use of @code{index} is
deprecated in new programs in favor of @code{strchr}.

@end deftypefn

*/

extern char * strchr(const char *, int);

char *
index (const char *s, int c)
{
  return strchr (s, c);
}
