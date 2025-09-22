/* xmemdup.c -- Duplicate a memory buffer, using xcalloc.
   This trivial function is in the public domain.
   Jeff Garzik, September 1999.  */

/*

@deftypefn Replacement void* xmemdup (void *@var{input}, size_t @var{copy_size}, size_t @var{alloc_size})

Duplicates a region of memory without fail.  First, @var{alloc_size} bytes
are allocated, then @var{copy_size} bytes from @var{input} are copied into
it, and the new memory is returned.  If fewer bytes are copied than were
allocated, the remaining memory is zeroed.

@end deftypefn

*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "ansidecl.h"
#include "libiberty.h"

#include <sys/types.h> /* For size_t. */
#ifdef HAVE_STRING_H
#include <string.h>
#else
# ifdef HAVE_STRINGS_H
#  include <strings.h>
# endif
#endif

PTR
xmemdup (const PTR input, size_t copy_size, size_t alloc_size)
{
  PTR output = xcalloc (1, alloc_size);
  return (PTR) memcpy (output, input, copy_size);
}
