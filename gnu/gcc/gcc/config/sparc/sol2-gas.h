/* Definitions of target machine for GCC, for SPARC running Solaris 2
   using the GNU assembler.  */

/* Undefine this so that BNSYM/ENSYM pairs are emitted by STABS+.  */
#undef NO_DBX_BNSYM_ENSYM

/* Use GNU extensions to TLS support.  */
#ifdef HAVE_AS_TLS
#undef TARGET_SUN_TLS
#undef TARGET_GNU_TLS
#define TARGET_SUN_TLS 0
#define TARGET_GNU_TLS 1
#endif
