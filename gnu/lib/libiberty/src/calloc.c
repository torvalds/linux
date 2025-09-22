/* calloc -- allocate memory which has been initialized to zero.
   This function is in the public domain. */

/*

@deftypefn Supplemental void* calloc (size_t @var{nelem}, size_t @var{elsize})

Uses @code{malloc} to allocate storage for @var{nelem} objects of
@var{elsize} bytes each, then zeros the memory.

@end deftypefn

*/
 
#include "ansidecl.h"
#include <stddef.h>

/* For systems with larger pointers than ints, this must be declared.  */
PTR malloc (size_t);
void bzero (PTR, size_t);

PTR
calloc (size_t nelem, size_t elsize)
{
  register PTR ptr;  

  if (nelem == 0 || elsize == 0)
    nelem = elsize = 1;
  
  ptr = malloc (nelem * elsize);
  if (ptr) bzero (ptr, nelem * elsize);
  
  return ptr;
}
