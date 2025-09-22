/* Stub implementation of (obsolete) rindex(). */

/*

@deftypefn Supplemental char* rindex (const char *@var{s}, int @var{c})

Returns a pointer to the last occurrence of the character @var{c} in
the string @var{s}, or @code{NULL} if not found.  The use of @code{rindex} is
deprecated in new programs in favor of @code{strrchr}.

@end deftypefn

*/

extern char *strrchr (const char *, int);

char *
rindex (const char *s, int c)
{
  return strrchr (s, c);
}
