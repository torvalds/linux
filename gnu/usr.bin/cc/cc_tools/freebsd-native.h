/* $FreeBSD$ */

/* FREEBSD_NATIVE is defined when gcc is integrated into the FreeBSD
   source tree so it can be configured appropriately without using
   the GNU configure/build mechanism. */

#define FREEBSD_NATIVE 1

/* Fake out gcc/config/freebsd<version>.h.  */
#define	FBSD_MAJOR	13
#define	FBSD_CC_VER	1300000		/* form like __FreeBSD_version */

#undef SYSTEM_INCLUDE_DIR		/* We don't need one for now. */
#undef TOOL_INCLUDE_DIR			/* We don't need one for now. */
#undef LOCAL_INCLUDE_DIR		/* We don't wish to support one. */

/* Look for the include files in the system-defined places.  */
#define GPLUSPLUS_INCLUDE_DIR		"/usr/include/c++/"GCCVER
#define	GPLUSPLUS_BACKWARD_INCLUDE_DIR	"/usr/include/c++/"GCCVER"/backward"
#define GCC_INCLUDE_DIR			PREFIX"/include/gcc/"GCCVER
#define STANDARD_INCLUDE_DIR		"/usr/include"

/* Under FreeBSD, the normal location of the compiler back ends is the
   /usr/libexec directory.

   ``cc --print-search-dirs'' gives:
   install: STANDARD_EXEC_PREFIX/
   programs: STANDARD_EXEC_PREFIX:MD_EXEC_PREFIX
   libraries: STANDARD_STARTFILE_PREFIX
*/
#undef	STANDARD_BINDIR_PREFIX		/* We don't need one for now. */
#define	STANDARD_EXEC_PREFIX		PREFIX"/libexec/"
#define	STANDARD_LIBEXEC_PREFIX		PREFIX"/libexec/"
#define TOOLDIR_BASE_PREFIX		PREFIX
#undef	MD_EXEC_PREFIX			/* We don't want one. */
#define	FBSD_DATA_PREFIX		PREFIX"/libdata/gcc/"

/* Under FreeBSD, the normal location of the various *crt*.o files is the
   /usr/lib directory.  */

#undef  MD_STARTFILE_PREFIX		/* We don't need one for now. */
#define STANDARD_STARTFILE_PREFIX	"/usr/lib/"
#define STARTFILE_PREFIX_SPEC		"/usr/lib/"

#if 0
#define LIBGCC_SPEC		"%{shared: -lgcc_pic} \
    %{!shared: %{!pg: -lgcc} %{pg: -lgcc_p}}"
#endif
#define LIBSTDCXX_PROFILE	"-lstdc++_p"
#define MATH_LIBRARY_PROFILE	"-lm_p"
#define FORTRAN_LIBRARY_PROFILE	"-lg2c_p"

#define LIBGCC_SPEC		"-lgcc"
/* For the native system compiler, we actually build libgcc in a profiled
   version.  So we should use it with -pg.  */
#define LIBGCC_STATIC_LIB_SPEC	  "%{pg: -lgcc_p;:-lgcc}"
#define LIBGCC_EH_STATIC_LIB_SPEC "%{pg: -lgcc_eh_p;:-lgcc_eh}"

/* FreeBSD is 4.4BSD derived */
#define bsd4_4
