/* Definitions of target machine for GCC, for SPARC running Solaris 2
   using the GNU linker.  */

/* Undefine this so that attribute((init_priority)) works.  */
#undef CTORS_SECTION_ASM_OP
#undef DTORS_SECTION_ASM_OP

#undef SUPPORTS_INIT_PRIORITY
#define SUPPORTS_INIT_PRIORITY 1
