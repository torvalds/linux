#include <stddef.h>

const void *__arm_sc_memchr(const void *src, int c,
                            size_t n) __arm_streaming_compatible {
  const unsigned char *srcp = (const unsigned char *)src;
  unsigned char c8 = (unsigned char)c;
  for (size_t i = 0; i < n; ++i)
    if (srcp[i] == c8)
      return &srcp[i];

  return NULL;
}
