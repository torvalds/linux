/* The linker can generate branch islands.  */
#define DARWIN_LINKER_GENERATES_ISLANDS 1

#undef  ASM_OUTPUT_ALIGNED_COMMON
#define ASM_OUTPUT_ALIGNED_COMMON(FILE, NAME, SIZE, ALIGN)		\
  do {									\
    unsigned HOST_WIDE_INT _new_size = (SIZE);				\
    fprintf ((FILE), ".comm ");						\
    assemble_name ((FILE), (NAME));					\
    if (_new_size == 0) _new_size = 1;					\
    fprintf ((FILE), ","HOST_WIDE_INT_PRINT_UNSIGNED",%u\n",		\
	     _new_size, floor_log2 ((ALIGN) / BITS_PER_UNIT));		\
  } while (0)
