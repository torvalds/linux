const int size = 5;

#include <cstddef>
#include <cstdlib>
#include <sys/prctl.h>

void func(int *ptr) {
  int *tmp;

#if defined  __GNUC__ && !defined __INTEL_COMPILER
  __builtin___bnd_store_ptr_bounds ((void**)&ptr, ptr);
#endif
  tmp = ptr + size - 1;
#if defined  __GNUC__ && !defined __INTEL_COMPILER
  __builtin___bnd_store_ptr_bounds ((void**)&tmp, tmp);
#endif
  tmp = (int*)0x2; // Break 2.

  return; // Break 3.
}

int
main(int argc, char const *argv[])
{
  // This call returns 0 only if the CPU and the kernel support
  // Intel(R) Memory Protection Extensions (Intel(R) MPX).
  if (prctl(PR_MPX_ENABLE_MANAGEMENT, 0, 0, 0, 0) != 0)
        return -1;

  int*  a = (int *) calloc(size, sizeof(int));
#if defined  __GNUC__ && !defined __INTEL_COMPILER
  __builtin___bnd_store_ptr_bounds ((void**)&a, a);
#endif
  func(a); // Break 1.

  free(a); // Break 4.

  return 0;
}
