/* Definitions of target machine for GCC, for bi-arch SPARC
   running Solaris 2 using the GNU linker.  */

#undef LINK_ARCH32_SPEC
#define LINK_ARCH32_SPEC \
  LINK_ARCH32_SPEC_BASE "%{!static: -rpath-link %R/usr/lib}"

#undef LINK_ARCH64_SPEC
#define LINK_ARCH64_SPEC \
  LINK_ARCH64_SPEC_BASE "%{!static: -rpath-link %R/usr/lib/sparcv9}"

#undef LINK_ARCH_SPEC
#if DISABLE_MULTILIB
#if DEFAULT_ARCH32_P
#define LINK_ARCH_SPEC "\
%{m32:-m elf32_sparc %(link_arch32)} \
%{m64:%edoes not support multilib} \
%{!m32:%{!m64:%(link_arch_default)}} \
"
#else
#define LINK_ARCH_SPEC "\
%{m32:%edoes not support multilib} \
%{m64:-m elf64_sparc %(link_arch64)} \
%{!m32:%{!m64:%(link_arch_default)}} \
"
#endif
#else
#define LINK_ARCH_SPEC "\
%{m32:-m elf32_sparc %(link_arch32)} \
%{m64:-m elf64_sparc %(link_arch64)} \
%{!m32:%{!m64:%(link_arch_default)}} \
"
#endif

