/*

@deftypefn Supplemental int vprintf (const char *@var{format}, va_list @var{ap})
@deftypefnx Supplemental int vfprintf (FILE *@var{stream}, const char *@var{format}, va_list @var{ap})
@deftypefnx Supplemental int vsprintf (char *@var{str}, const char *@var{format}, va_list @var{ap})

These functions are the same as @code{printf}, @code{fprintf}, and
@code{sprintf}, respectively, except that they are called with a
@code{va_list} instead of a variable number of arguments.  Note that
they do not call @code{va_end}; this is the application's
responsibility.  In @libib{} they are implemented in terms of the
nonstandard but common function @code{_doprnt}.

@end deftypefn

*/

#include <ansidecl.h>
#include <stdarg.h>
#include <stdio.h>
#undef vprintf
int
vprintf (const char *format, va_list ap)
{
  return vfprintf (stdout, format, ap);
}
