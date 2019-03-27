/*
 * $FreeBSD$
 *
 * portions derived from
 *      $NetBSD: config.h,v 1.11 1998/08/08 22:33:37 christos Exp $
 *
 * Additional portions derived from ports/sysutils/am-utils r416941
 * make configure config.h output.
 */

#ifndef _CONFIG_H
#define _CONFIG_H

/* We [FREEBSD-NATIVE] pick some parameters from our local config file */
#include "config_local.h"

/* Define if building universal (internal helper macro) */
/* #undef AC_APPLE_UNIVERSAL_BUILD */

/* define name of am-utils' NFS protocol header */
#define AMU_NFS_PROTOCOL_HEADER "./conf/nfs_prot/nfs_prot_freebsd3.h"

/* Type of the 5rd argument to authunix_create() */
#define AUTH_CREATE_GIDLIST_TYPE gid_t

/* Define configuration date */
/* #define CONFIG_DATE "Mon Oct  3 21:58:39 PDT 2016" */

/* Turn off general debugging by default */
/* #undef DEBUG */

/* Turn off memory debugging by default */
/* #undef DEBUG_MEM */

/* Define name of host OS's distribution name (eg. debian, redhat, suse, etc.)
   */
#define DISTRO_NAME "The FreeBSD Project"

/* Define to the type of elements in the array set by `getgroups'. Usually
   this is either `int' or `gid_t'. */
#define GETGROUPS_T gid_t

/* Define to 1 if the `getpgrp' function requires zero arguments. */
#define GETPGRP_VOID 1

/* Define if have automount filesystem */
#define HAVE_AMU_FS_AUTO 1

/* Define if have direct automount filesystem */
#define HAVE_AMU_FS_DIRECT 1

/* Define if have error filesystem */
#define HAVE_AMU_FS_ERROR 1

/* Define if have NFS host-tree filesystem */
#define HAVE_AMU_FS_HOST 1

/* Define if have symbolic-link filesystem */
#define HAVE_AMU_FS_LINK 1

/* Define if have symlink with existence check filesystem */
#define HAVE_AMU_FS_LINKX 1

/* Define if have nfsl (NFS with local link check) filesystem */
#define HAVE_AMU_FS_NFSL 1

/* Define if have multi-NFS filesystem */
#define HAVE_AMU_FS_NFSX 1

/* Define if have program filesystem */
#define HAVE_AMU_FS_PROGRAM 1

/* Define if have "top-level" filesystem */
#define HAVE_AMU_FS_TOPLVL 1

/* Define if have union filesystem */
#define HAVE_AMU_FS_UNION 1

/* Define to 1 if you have the <arpa/inet.h> header file. */
#define HAVE_ARPA_INET_H 1

/* Define to 1 if you have the <arpa/nameser.h> header file. */
#define HAVE_ARPA_NAMESER_H 1

/* Define to 1 if you have the <assert.h> header file. */
#define HAVE_ASSERT_H 1

/* Define to 1 if `addr' is member of `autofs_args_t'. */
/* #undef HAVE_AUTOFS_ARGS_T_ADDR */

/* define if have a bad version of hasmntopt() */
/* #undef HAVE_BAD_HASMNTOPT */

/* define if have a bad version of memcmp() */
/* #undef HAVE_BAD_MEMCMP */

/* define if have a bad version of yp_all() */
/* #undef HAVE_BAD_YP_ALL */

/* Define to 1 if you have the `bcmp' function. */
#define HAVE_BCMP 1

/* Define to 1 if you have the `bcopy' function. */
#define HAVE_BCOPY 1

/* Define to 1 if you have the <bsd/rpc/rpc.h> header file. */
/* #undef HAVE_BSD_RPC_RPC_H */

/* Define to 1 if you have the `bzero' function. */
#define HAVE_BZERO 1

/* System supports C99-style variable-length argument macros */
#define HAVE_C99_VARARGS_MACROS 1

/* Define to 1 if `flags' is member of `cdfs_args_t'. */
#define HAVE_CDFS_ARGS_T_FLAGS 1

/* Define to 1 if `fspec' is member of `cdfs_args_t'. */
#define HAVE_CDFS_ARGS_T_FSPEC 1

/* Define to 1 if `iso_flags' is member of `cdfs_args_t'. */
/* #undef HAVE_CDFS_ARGS_T_ISO_FLAGS */

/* Define to 1 if `iso_pgthresh' is member of `cdfs_args_t'. */
/* #undef HAVE_CDFS_ARGS_T_ISO_PGTHRESH */

/* Define to 1 if `norrip' is member of `cdfs_args_t'. */
/* #undef HAVE_CDFS_ARGS_T_NORRIP */

/* Define to 1 if `ssector' is member of `cdfs_args_t'. */
#define HAVE_CDFS_ARGS_T_SSECTOR 1

/* Define to 1 if you have the <cdfs/cdfsmount.h> header file. */
/* #undef HAVE_CDFS_CDFSMOUNT_H */

/* Define to 1 if you have the <cdfs/cdfs_mount.h> header file. */
/* #undef HAVE_CDFS_CDFS_MOUNT_H */

/* Define to 1 if you have the `clnt_create' function. */
#define HAVE_CLNT_CREATE 1

/* Define to 1 if you have the `clnt_create_vers' function. */
#define HAVE_CLNT_CREATE_VERS 1

/* Define to 1 if you have the `clnt_create_vers_timed' function. */
#define HAVE_CLNT_CREATE_VERS_TIMED 1

/* Define to 1 if you have the `clnt_spcreateerror' function. */
#define HAVE_CLNT_SPCREATEERROR 1

/* Define to 1 if you have the `clnt_sperrno' function. */
#define HAVE_CLNT_SPERRNO 1

/* Define to 1 if you have the `clock_gettime' function. */
#define HAVE_CLOCK_GETTIME 1

/* Define to 1 if you have the <cluster.h> header file. */
/* #undef HAVE_CLUSTER_H */

/* Define to 1 if you have the `cnodeid' function. */
/* #undef HAVE_CNODEID */

/* Define to 1 if you have the <ctype.h> header file. */
#define HAVE_CTYPE_H 1

/* Define to 1 if you have the <db1/ndbm.h> header file. */
/* #undef HAVE_DB1_NDBM_H */

/* Define to 1 if you have the `dbm_open' function. */
#define HAVE_DBM_OPEN 1

/* Define to 1 if you have the `dg_mount' function. */
/* #undef HAVE_DG_MOUNT */

/* Define to 1 if you have the <dirent.h> header file, and it defines `DIR'.
   */
#define HAVE_DIRENT_H 1

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Define to 1 if `flags' is member of `efs_args_t'. */
/* #undef HAVE_EFS_ARGS_T_FLAGS */

/* Define to 1 if `version' is member of `efs_args_t'. */
/* #undef HAVE_EFS_ARGS_T_VERSION */

/* Define to 1 if `fspec' is member of `efs_args_t'. */
/* #undef HAVE_EFS_ARGS_T_FSPEC */

/* Define to 1 if `version' is member of `efs_args_t'. */
/* #undef HAVE_EFS_ARGS_T_VERSION */

/* Define to 1 if you have the <errno.h> header file. */
#define HAVE_ERRNO_H 1

/* does extern definition for clnt_spcreateerror() exist? */
#define HAVE_EXTERN_CLNT_SPCREATEERROR 1

/* does extern definition for clnt_sperrno() exist? */
#define HAVE_EXTERN_CLNT_SPERRNO 1

/* does extern definition for free() exist? */
#define HAVE_EXTERN_FREE 1

/* does extern definition for getccent() (hpux) exist? */
/* #undef HAVE_EXTERN_GETCCENT */

/* does extern definition for getdomainname() exist? */
#define HAVE_EXTERN_GETDOMAINNAME 1

/* does extern definition for getdtablesize() exist? */
#define HAVE_EXTERN_GETDTABLESIZE 1

/* does extern definition for gethostname() exist? */
#define HAVE_EXTERN_GETHOSTNAME 1

/* does extern definition for getlogin() exist? */
#define HAVE_EXTERN_GETLOGIN 1

/* does extern definition for getpagesize() exist? */
#define HAVE_EXTERN_GETPAGESIZE 1

/* does extern definition for gettablesize() exist? */
/* #undef HAVE_EXTERN_GETTABLESIZE */

/* does extern definition for getwd() exist? */
#define HAVE_EXTERN_GETWD 1

/* does extern definition for get_myaddress() exist? */
#define HAVE_EXTERN_GET_MYADDRESS 1

/* does extern definition for hosts_ctl() exist? */
/* #undef HAVE_EXTERN_HOSTS_CTL */

/* does extern definition for innetgr() exist? */
#define HAVE_EXTERN_INNETGR 1

/* does extern definition for ldap_enable_cache() exist? */
/* #undef HAVE_EXTERN_LDAP_ENABLE_CACHE */

/* does extern definition for mkstemp() exist? */
#define HAVE_EXTERN_MKSTEMP 1

/* does extern definition for mntctl() exist? */
/* #undef HAVE_EXTERN_MNTCTL */

/* does extern definition for optarg exist? */
#define HAVE_EXTERN_OPTARG 1

/* does extern definition for sbrk() exist? */
#define HAVE_EXTERN_SBRK 1

/* does extern definition for seteuid() exist? */
#define HAVE_EXTERN_SETEUID 1

/* does extern definition for setitimer() exist? */
#define HAVE_EXTERN_SETITIMER 1

/* does extern definition for sleep() exist? */
#define HAVE_EXTERN_SLEEP 1

/* does extern definition for strcasecmp() exist? */
#define HAVE_EXTERN_STRCASECMP 1

/* does extern definition for strdup() exist? */
#define HAVE_EXTERN_STRDUP 1

/* does extern definition for strlcat() exist? */
#define HAVE_EXTERN_STRLCAT 1

/* does extern definition for strlcpy() exist? */
#define HAVE_EXTERN_STRLCPY 1

/* does extern definition for strstr() exist? */
#define HAVE_EXTERN_STRSTR 1

/* does extern definition for sys_errlist[] exist? */
#define HAVE_EXTERN_SYS_ERRLIST 1

/* does extern definition for ualarm() exist? */
#define HAVE_EXTERN_UALARM 1

/* does extern definition for usleep() exist? */
#define HAVE_EXTERN_USLEEP 1

/* does extern definition for vsnprintf() exist? */
#define HAVE_EXTERN_VSNPRINTF 1

/* does extern definition for wait3() exist? */
#define HAVE_EXTERN_WAIT3 1

/* does extern definition for xdr_callmsg() exist? */
#define HAVE_EXTERN_XDR_CALLMSG 1

/* does extern definition for xdr_opaque_auth() exist? */
#define HAVE_EXTERN_XDR_OPAQUE_AUTH 1

/* Define to 1 if you have the <fcntl.h> header file. */
#define HAVE_FCNTL_H 1

/* Define to 1 if `fds_bits' is member of `fd_set'. */
#define HAVE_FD_SET_FDS_BITS 1

/* Define to 1 if you have the `fgets' function. */
#define HAVE_FGETS 1

/* Define if plain fhandle type exists */
#define HAVE_FHANDLE 1

/* Define to 1 if you have the `flock' function. */
#define HAVE_FLOCK 1

/* Define to 1 if you have the `fork' function. */
#define HAVE_FORK 1

/* Define to 1 if you have the `fsmount' function. */
/* #undef HAVE_FSMOUNT */

/* Define if have AUTOFS filesystem */
/* #undef HAVE_FS_AUTOFS */

/* Define if have CACHEFS filesystem */
/* #undef HAVE_FS_CACHEFS */

/* Define if have CDFS filesystem */
#define HAVE_FS_CDFS 1

/* Define if have CFS (crypto) filesystem */
/* #undef HAVE_FS_CFS */

/* Define if have EFS filesystem (irix) */
/* #undef HAVE_FS_EFS */

/* Define to 1 if you have the <fs/efs/efs_mount.h> header file. */
/* #undef HAVE_FS_EFS_EFS_MOUNT_H */

/* Define if have EXT{2,3,4} filesystem (linux) */
/* #undef HAVE_FS_EXT */

/* Define if have FFS filesystem */
/* #undef HAVE_FS_FFS */

/* Define if have HSFS filesystem */
/* #undef HAVE_FS_HSFS */

/* Define if have LOFS filesystem */
/* #undef HAVE_FS_LOFS */

/* Define if have LUSTRE filesystem */
/* #undef HAVE_FS_LUSTRE */

/* Define if have MFS filesystem */
#define HAVE_FS_MFS 1

/* Define to 1 if you have the <fs/msdosfs/msdosfsmount.h> header file. */
/* #undef HAVE_FS_MSDOSFS_MSDOSFSMOUNT_H */

/* Define if have NFS filesystem */
#define HAVE_FS_NFS 1

/* Define if have NFS3 filesystem */
#define HAVE_FS_NFS3 1

/* Define if have NFS4 filesystem */
/* #undef HAVE_FS_NFS4 */

/* Define if have NULLFS (loopback on bsd44) filesystem */
#define HAVE_FS_NULLFS 1

/* Define if have PCFS filesystem */
#define HAVE_FS_PCFS 1

/* Define if have TFS filesystem */
/* #undef HAVE_FS_TFS */

/* Define if have TMPFS filesystem */
#define HAVE_FS_TMPFS 1

/* Define to 1 if you have the <fs/tmpfs/tmpfs_args.h> header file. */
/* #undef HAVE_FS_TMPFS_TMPFS_ARGS_H */

/* Define if have UDF filesystem */
#define HAVE_FS_UDF 1

/* Define to 1 if you have the <fs/udf/udf_mount.h> header file. */
#define HAVE_FS_UDF_UDF_MOUNT_H 1

/* Define if have UFS filesystem */
#define HAVE_FS_UFS 1

/* Define if have UMAPFS (uid/gid mapping) filesystem */
/* #undef HAVE_FS_UMAPFS */

/* Define if have UNIONFS filesystem */
#define HAVE_FS_UNIONFS 1

/* Define if have XFS filesystem (irix) */
/* #undef HAVE_FS_XFS */

/* System supports GCC-style variable-length argument macros */
/* #undef HAVE_GCC_VARARGS_MACROS */

/* Define to 1 if you have the <gdbm-ndbm.h> header file. */
/* #undef HAVE_GDBM_NDBM_H */

/* Define to 1 if you have the `getccent' function. */
/* #undef HAVE_GETCCENT */

/* Define to 1 if you have the `getcwd' function. */
#define HAVE_GETCWD 1

/* Define to 1 if you have the `getdomainname' function. */
#define HAVE_GETDOMAINNAME 1

/* Define to 1 if you have the `getdtablesize' function. */
#define HAVE_GETDTABLESIZE 1

/* Define to 1 if you have the `gethostname' function. */
#define HAVE_GETHOSTNAME 1

/* Define to 1 if you have the `getifaddrs' function. */
#define HAVE_GETIFADDRS 1

/* Define to 1 if you have the `getmntinfo' function. */
#define HAVE_GETMNTINFO 1

/* Define to 1 if you have the `getmountent' function. */
/* #undef HAVE_GETMOUNTENT */

/* Define to 1 if you have the `getpagesize' function. */
#define HAVE_GETPAGESIZE 1

/* Define to 1 if you have the `getpwnam' function. */
#define HAVE_GETPWNAM 1

/* Define to 1 if you have the `gettimeofday' function. */
#define HAVE_GETTIMEOFDAY 1

/* Define to 1 if you have the `get_myaddress' function. */
#define HAVE_GET_MYADDRESS 1

/* define if your system's getopt() is GNU getopt() (are you using glibc) */
/* #undef HAVE_GNU_GETOPT */

/* Define to 1 if you have the <grp.h> header file. */
#define HAVE_GRP_H 1

/* Define to 1 if you have the `hasmntopt' function. */
/* #undef HAVE_HASMNTOPT */

#ifdef YES_HESIOD
/* Define to 1 if you have the <hesiod.h> header file. */
#define HAVE_HESIOD_H 1

/* Define to 1 if you have the `hesiod_init' function. */
#define HAVE_HESIOD_INIT 1

/* Define to 1 if you have the `hesiod_reload' function. */
/* #undef HAVE_HESIOD_RELOAD */

/* Define to 1 if you have the `hesiod_to_bind' function. */
#define HAVE_HESIOD_TO_BIND 1

/* Define to 1 if you have the `hes_init' function. */
#define HAVE_HES_INIT 1
#else
#undef HAVE_HESIOD_H
#undef HAVE_HESIOD_INIT
#undef HAVE_HESIOD_RELOAD
#undef HAVE_HESIOD_TO_BIND
#undef HAVE_HES_INIT
#endif

/* Define to 1 if you have the <hsfs/hsfs.h> header file. */
/* #undef HAVE_HSFS_HSFS_H */

/* Define to 1 if you have the `hstrerror' function. */
#define HAVE_HSTRERROR 1

/* Define to 1 if you have the <ifaddrs.h> header file. */
#define HAVE_IFADDRS_H 1

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define to 1 if you have the <irs.h> header file. */
/* #undef HAVE_IRS_H */

/* Define to 1 if you have the <isofs/cd9660/cd9660_mount.h> header file. */
#define HAVE_ISOFS_CD9660_CD9660_MOUNT_H 1

/* Define to 1 if you have the <lber.h> header file. */
/* #undef HAVE_LBER_H */

/* Define to 1 if you have the `ldap_enable_cache' function. */
/* #undef HAVE_LDAP_ENABLE_CACHE */

/* Define to 1 if you have the <ldap.h> header file. */
/* #undef HAVE_LDAP_H */

/* Define to 1 if you have the `ldap_open' function. */
/* #undef HAVE_LDAP_OPEN */

/* Define to 1 if you have the <libgen.h> header file. */
#define HAVE_LIBGEN_H 1

/* Define to 1 if you have the `malloc' library (-lmalloc). */
/* #undef HAVE_LIBMALLOC */

/* Define to 1 if you have the `mapmalloc' library (-lmapmalloc). */
/* #undef HAVE_LIBMAPMALLOC */

/* Define to 1 if you have the `nsl' library (-lnsl). */
/* #undef HAVE_LIBNSL */

/* Define to 1 if you have the `posix4' library (-lposix4). */
/* #undef HAVE_LIBPOSIX4 */

/* Define to 1 if you have the `resolv' library (-lresolv). */
/* #undef HAVE_LIBRESOLV */

/* Define to 1 if you have the `rpc' library (-lrpc). */
/* #undef HAVE_LIBRPC */

/* Define to 1 if you have the `rpcsvc' library (-lrpcsvc). */
#define HAVE_LIBRPCSVC 1

/* Define to 1 if you have the `rt' library (-lrt). */
/* #undef HAVE_LIBRT */

/* does libwrap exist? */
/* #undef HAVE_LIBWRAP */

/* Define to 1 if you have the <limits.h> header file. */
#define HAVE_LIMITS_H 1

/* Define to 1 if you have the <linux/auto_fs4.h> header file. */
/* #undef HAVE_LINUX_AUTO_FS4_H */

/* Define to 1 if you have the <linux/auto_fs.h> header file. */
/* #undef HAVE_LINUX_AUTO_FS_H */

/* Define to 1 if you have the <linux/fs.h> header file. */
/* #undef HAVE_LINUX_FS_H */

/* Define to 1 if you have the <linux/kdev_t.h> header file. */
/* #undef HAVE_LINUX_KDEV_T_H */

/* Define to 1 if you have the <linux/list.h> header file. */
/* #undef HAVE_LINUX_LIST_H */

/* Define to 1 if you have the <linux/loop.h> header file. */
/* #undef HAVE_LINUX_LOOP_H */

/* Define to 1 if you have the <linux/nfs2.h> header file. */
/* #undef HAVE_LINUX_NFS2_H */

/* Define to 1 if you have the <linux/nfs4.h> header file. */
/* #undef HAVE_LINUX_NFS4_H */

/* Define to 1 if you have the <linux/nfs.h> header file. */
/* #undef HAVE_LINUX_NFS_H */

/* Define to 1 if you have the <linux/nfs_mount.h> header file. */
/* #undef HAVE_LINUX_NFS_MOUNT_H */

/* Define to 1 if you have the <linux/posix_types.h> header file. */
/* #undef HAVE_LINUX_POSIX_TYPES_H */

/* Define to 1 if you have the <linux/socket.h> header file. */
/* #undef HAVE_LINUX_SOCKET_H */

/* Define to 1 if you support file names longer than 14 characters. */
#define HAVE_LONG_FILE_NAMES 1

/* Define to 1 if you have the <machine/endian.h> header file. */
#define HAVE_MACHINE_ENDIAN_H 1

/* Define to 1 if you have the `madvise' function. */
#define HAVE_MADVISE 1

/* Define to 1 if you have the <malloc.h> header file. */
/* #undef HAVE_MALLOC_H */

/* Define if have DBM maps */
/* #undef HAVE_MAP_DBM */

/* Define if have executable maps */
#define HAVE_MAP_EXEC 1

/* Define if have file maps (everyone should have it!) */
#define HAVE_MAP_FILE 1

#ifdef YES_HESIOD
/* Define if have HESIOD maps */
#define HAVE_MAP_HESIOD 1
#else
#undef HAVE_MAP_HESIOD
#endif

/* Define if have LDAP maps */
/* #undef HAVE_MAP_LDAP */

/* Define if have NDBM maps */
#define HAVE_MAP_NDBM 1

/* Define if have NIS maps */
#define HAVE_MAP_NIS 1

/* Define if have NIS+ maps */
/* #undef HAVE_MAP_NISPLUS */

/* Define if have PASSWD maps */
#define HAVE_MAP_PASSWD 1

/* Define if have Sun-syntax maps */
#define HAVE_MAP_SUN 1

/* Define if have UNION maps */
#define HAVE_MAP_UNION 1

/* Define to 1 if you have the `memcmp' function. */
#define HAVE_MEMCMP 1

/* Define to 1 if you have the `memcpy' function. */
#define HAVE_MEMCPY 1

/* Define to 1 if you have the `memmove' function. */
#define HAVE_MEMMOVE 1

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the `memset' function. */
#define HAVE_MEMSET 1

/* Define to 1 if you have the `mkdir' function. */
#define HAVE_MKDIR 1

/* Define to 1 if you have the `mkstemp' function. */
#define HAVE_MKSTEMP 1

/* Define to 1 if you have the `mlockall' function. */
#define HAVE_MLOCKALL 1

/* Define to 1 if you have the `mntctl' function. */
/* #undef HAVE_MNTCTL */

/* Define to 1 if you have the <mntent.h> header file. */
/* #undef HAVE_MNTENT_H */

/* Define to 1 if `mnt_cnode' is member of `mntent_t'. */
/* #undef HAVE_MNTENT_T_MNT_CNODE */

/* Define to 1 if `mnt_ro' is member of `mntent_t'. */
/* #undef HAVE_MNTENT_T_MNT_RO */

/* Define to 1 if `mnt_time' is member of `mntent_t'. */
/* #undef HAVE_MNTENT_T_MNT_TIME */

/* does mntent_t have mnt_time field and is of type "char *" ? */
/* #undef HAVE_MNTENT_T_MNT_TIME_STRING */

/* Define to 1 if you have the <mnttab.h> header file. */
/* #undef HAVE_MNTTAB_H */

/* Define to 1 if you have the `mount' function. */
#define HAVE_MOUNT 1

/* Define to 1 if `optptr' is member of `mounta'. */
/* #undef HAVE_MOUNTA_OPTPTR */

/* Define to 1 if you have the `mountsyscall' function. */
/* #undef HAVE_MOUNTSYSCALL */

/* Define to 1 if you have the <mount.h> header file. */
/* #undef HAVE_MOUNT_H */

/* Define to 1 if you have the <msdosfs/msdosfsmount.h> header file. */
/* #undef HAVE_MSDOSFS_MSDOSFSMOUNT_H */

/* Define to 1 if you have the <fs/msdosfs/msdosfsmount.h> header file. */
#define HAVE_FS_MSDOSFS_MSDOSFSMOUNT_H 1

/* Define to 1 if you have the <ndbm.h> header file. */
#define HAVE_NDBM_H 1

/* Define to 1 if you have the <ndir.h> header file. */
/* #undef HAVE_NDIR_H */

/* Define to 1 if you have the <netconfig.h> header file. */
#define HAVE_NETCONFIG_H 1

/* Define to 1 if you have the <netdb.h> header file. */
#define HAVE_NETDB_H 1

/* Define to 1 if you have the <netdir.h> header file. */
/* #undef HAVE_NETDIR_H */

/* Define to 1 if you have the <netinet/if_ether.h> header file. */
#define HAVE_NETINET_IF_ETHER_H 1

/* Define to 1 if you have the <netinet/in.h> header file. */
#define HAVE_NETINET_IN_H 1

/* Define to 1 if you have the <net/errno.h> header file. */
/* #undef HAVE_NET_ERRNO_H */

/* Define to 1 if you have the <net/if.h> header file. */
#define HAVE_NET_IF_H 1

/* Define to 1 if you have the <net/if_var.h> header file. */
/* #undef HAVE_NET_IF_VAR_H */

/* Define to 1 if you have the <net/route.h> header file. */
#define HAVE_NET_ROUTE_H 1

/* Define to 1 if you have the <nfsclient/nfsargs.h> header file. */
#define HAVE_NFSCLIENT_NFSARGS_H 1

/* Define to 1 if `acdirmax' is member of `nfs_args_t'. */
#define HAVE_NFS_ARGS_T_ACDIRMAX 1

/* Define to 1 if `acdirmin' is member of `nfs_args_t'. */
#define HAVE_NFS_ARGS_T_ACDIRMIN 1

/* Define to 1 if `acregmax' is member of `nfs_args_t'. */
#define HAVE_NFS_ARGS_T_ACREGMAX 1

/* Define to 1 if `acregmin' is member of `nfs_args_t'. */
#define HAVE_NFS_ARGS_T_ACREGMIN 1

/* Define to 1 if `addrlen' is member of `nfs_args_t'. */
#define HAVE_NFS_ARGS_T_ADDRLEN 1

/* Define to 1 if `bsize' is member of `nfs_args_t'. */
/* #undef HAVE_NFS_ARGS_T_BSIZE */

/* Define to 1 if `context' is member of `nfs_args_t'. */
/* #undef HAVE_NFS_ARGS_T_CONTEXT */

/* Define to 1 if `fhsize' is member of `nfs_args_t'. */
#define HAVE_NFS_ARGS_T_FHSIZE 1

/* Define to 1 if `fh_len' is member of `nfs_args_t'. */
/* #undef HAVE_NFS_ARGS_T_FH_LEN */

/* Define to 1 if `gfs_flags' is member of `nfs_args_t'. */
/* #undef HAVE_NFS_ARGS_T_GFS_FLAGS */

/* Define to 1 if `namlen' is member of `nfs_args_t'. */
/* #undef HAVE_NFS_ARGS_T_NAMLEN */

/* Define to 1 if `optstr' is member of `nfs_args_t'. */
/* #undef HAVE_NFS_ARGS_T_OPTSTR */

/* Define to 1 if `pathconf' is member of `nfs_args_t'. */
/* #undef HAVE_NFS_ARGS_T_PATHCONF */

/* Define to 1 if `proto' is member of `nfs_args_t'. */
#define HAVE_NFS_ARGS_T_PROTO 1

/* Define to 1 if `pseudoflavor' is member of `nfs_args_t'. */
/* #undef HAVE_NFS_ARGS_T_PSEUDOFLAVOR */

/* Define to 1 if `sotype' is member of `nfs_args_t'. */
#define HAVE_NFS_ARGS_T_SOTYPE 1

/* Define to 1 if `version' is member of `nfs_args_t'. */
#define HAVE_NFS_ARGS_T_VERSION 1

/* Define to 1 if you have the <nfs/export.h> header file. */
/* #undef HAVE_NFS_EXPORT_H */

/* Define to 1 if you have the <nfs/mount.h> header file. */
/* #undef HAVE_NFS_MOUNT_H */

/* Define to 1 if you have the <nfs/nfsmount.h> header file. */
/* #undef HAVE_NFS_NFSMOUNT_H */

/* Define to 1 if you have the <nfs/nfsproto.h> header file. */
#define HAVE_NFS_NFSPROTO_H 1

/* Define to 1 if you have the <nfs/nfsv2.h> header file. */
/* #undef HAVE_NFS_NFSV2_H */

/* Define to 1 if you have the <nfs/nfs_clnt.h> header file. */
/* #undef HAVE_NFS_NFS_CLNT_H */

/* Define to 1 if you have the <nfs/nfs_gfs.h> header file. */
/* #undef HAVE_NFS_NFS_GFS_H */

/* Define to 1 if you have the <nfs/nfs.h> header file. */
/* #undef HAVE_NFS_NFS_H */

/* Define to 1 if you have the <nfs/nfs_mount.h> header file. */
/* #undef HAVE_NFS_NFS_MOUNT_H */

/* Define to 1 if you have the <nfs/pathconf.h> header file. */
/* #undef HAVE_NFS_PATHCONF_H */

/* define if the host has NFS protocol headers in system headers */
/* #undef HAVE_NFS_PROT_HEADERS */

/* Define to 1 if you have the <nfs/rpcv2.h> header file. */
/* #define HAVE_NFS_RPCV2_H 1 */

/* Define to 1 if you have the `nis_domain_of' function. */
/* #undef HAVE_NIS_DOMAIN_OF */

/* Define to 1 if you have the <nsswitch.h> header file. */
#define HAVE_NSSWITCH_H 1

/* Define to 1 if you have the `opendir' function. */
#define HAVE_OPENDIR 1

/* Define to 1 if `dsttime' is member of `pcfs_args_t'. */
/* #undef HAVE_PCFS_ARGS_T_DSTTIME */

/* Define to 1 if `fspec' is member of `pcfs_args_t'. */
#define HAVE_PCFS_ARGS_T_FSPEC 1

/* Define to 1 if `gid' is member of `pcfs_args_t'. */
#define HAVE_PCFS_ARGS_T_GID 1

/* Define to 1 if `mask' is member of `pcfs_args_t'. */
#define HAVE_PCFS_ARGS_T_MASK 1

/* Define to 1 if `dirmask' is member of `pcfs_args_t'. */
#define HAVE_PCFS_ARGS_T_DIRMASK 1

/* Define to 1 if `secondswest' is member of `pcfs_args_t'. */
/* #undef HAVE_PCFS_ARGS_T_SECONDSWEST */

/* Define to 1 if `uid' is member of `pcfs_args_t'. */
#define HAVE_PCFS_ARGS_T_UID 1

/* Define to 1 if you have the `plock' function. */
/* #undef HAVE_PLOCK */

/* Define to 1 if you have the <pwd.h> header file. */
#define HAVE_PWD_H 1

/* Define to 1 if you have the `regcomp' function. */
#define HAVE_REGCOMP 1

/* Define to 1 if you have the `regexec' function. */
#define HAVE_REGEXEC 1

/* Define to 1 if you have the <regex.h> header file. */
#define HAVE_REGEX_H 1

/* Define to 1 if you have the <resolv.h> header file. */
#define HAVE_RESOLV_H 1

/* Define to 1 if system calls automatically restart after interruption by a
   signal. */
#define HAVE_RESTARTABLE_SYSCALLS 1

/* Define to 1 if you have the `rmdir' function. */
#define HAVE_RMDIR 1

/* Define to 1 if you have the <rpcsvc/autofs_prot.h> header file. */
/* #undef HAVE_RPCSVC_AUTOFS_PROT_H */

/* Define to 1 if you have the <rpcsvc/mountv3.h> header file. */
/* #undef HAVE_RPCSVC_MOUNTV3_H */

/* Define to 1 if you have the <rpcsvc/mount.h> header file. */
#define HAVE_RPCSVC_MOUNT_H 1

/* Define to 1 if you have the <rpcsvc/nfs_prot.h> header file. */
#define HAVE_RPCSVC_NFS_PROT_H 1

/* Define to 1 if you have the <rpcsvc/nis.h> header file. */
#define HAVE_RPCSVC_NIS_H 1

/* Define to 1 if you have the <rpcsvc/ypclnt.h> header file. */
#define HAVE_RPCSVC_YPCLNT_H 1

/* Define to 1 if you have the <rpcsvc/yp_prot.h> header file. */
#define HAVE_RPCSVC_YP_PROT_H 1

/* Define to 1 if you have the <rpc/auth_des.h> header file. */
#define HAVE_RPC_AUTH_DES_H 1

/* Define to 1 if you have the <rpc/auth.h> header file. */
#define HAVE_RPC_AUTH_H 1

/* Define to 1 if you have the <rpc/pmap_clnt.h> header file. */
#define HAVE_RPC_PMAP_CLNT_H 1

/* Define to 1 if you have the <rpc/pmap_prot.h> header file. */
#define HAVE_RPC_PMAP_PROT_H 1

/* Define to 1 if you have the <rpc/rpc.h> header file. */
#define HAVE_RPC_RPC_H 1

/* Define to 1 if you have the <rpc/types.h> header file. */
#define HAVE_RPC_TYPES_H 1

/* Define to 1 if you have the <rpc/xdr.h> header file. */
#define HAVE_RPC_XDR_H 1

/* Define to 1 if you have the `select' function. */
#define HAVE_SELECT 1

/* Define to 1 if you have the `seteuid' function. */
#define HAVE_SETEUID 1

/* Define to 1 if you have the `setitimer' function. */
#define HAVE_SETITIMER 1

/* Define to 1 if you have the <setjmp.h> header file. */
#define HAVE_SETJMP_H 1

/* Define to 1 if you have the `setresuid' function. */
#define HAVE_SETRESUID 1

/* Define to 1 if you have the `setsid' function. */
#define HAVE_SETSID 1

/* Define to 1 if you have the `sigaction' function. */
#define HAVE_SIGACTION 1

/* Define to 1 if you have the `signal' function. */
#define HAVE_SIGNAL 1

/* Define to 1 if you have the <signal.h> header file. */
#define HAVE_SIGNAL_H 1

/* Define to 1 if you have the `sigsuspend' function. */
#define HAVE_SIGSUSPEND 1

/* Define to 1 if you have the `socket' function. */
#define HAVE_SOCKET 1

/* Define to 1 if you have the <socketbits.h> header file. */
/* #undef HAVE_SOCKETBITS_H */

/* Define to 1 if you have the <statbuf.h> header file. */
/* #undef HAVE_STATBUF_H */

/* Define to 1 if you have the `statfs' function. */
#define HAVE_STATFS 1

/* Define to 1 if you have the `statvfs' function. */
#define HAVE_STATVFS 1

/* Define to 1 if you have the <stdarg.h> header file. */
#define HAVE_STDARG_H 1

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdio.h> header file. */
#define HAVE_STDIO_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the `strcasecmp' function. */
#define HAVE_STRCASECMP 1

/* Define to 1 if you have the `strchr' function. */
#define HAVE_STRCHR 1

/* Define to 1 if you have the `strcspn' function. */
#define HAVE_STRCSPN 1

/* Define to 1 if you have the `strdup' function. */
#define HAVE_STRDUP 1

/* Define to 1 if you have the `strerror' function. */
#define HAVE_STRERROR 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if you have the `strlcat' function. */
#define HAVE_STRLCAT 1

/* Define to 1 if you have the `strlcpy' function. */
#define HAVE_STRLCPY 1

/* Define to 1 if you have the `strspn' function. */
#define HAVE_STRSPN 1

/* Define to 1 if you have the `strstr' function. */
#define HAVE_STRSTR 1

/* Define to 1 if `fhs_fh' is member of `struct fhstatus'. */
/* #undef HAVE_STRUCT_FHSTATUS_FHS_FH */

/* Define to 1 if `ifa_next' is member of `struct ifaddrs'. */
#define HAVE_STRUCT_IFADDRS_IFA_NEXT 1

/* Define to 1 if `ifr_addr' is member of `struct ifreq'. */
#define HAVE_STRUCT_IFREQ_IFR_ADDR 1

/* Define if have struct mntent in one of the standard headers */
/* #undef HAVE_STRUCT_MNTENT */

/* Define if have struct mnttab in one of the standard headers */
/* #undef HAVE_STRUCT_MNTTAB */

/* Define if have struct nfs_args in one of the standard nfs headers */
#define HAVE_STRUCT_NFS_ARGS 1

/* Define if have struct nfs_gfs_mount in one of the standard nfs headers */
/* #undef HAVE_STRUCT_NFS_GFS_MOUNT */

/* Define to 1 if `sa_len' is member of `struct sockaddr'. */
#define HAVE_STRUCT_SOCKADDR_SA_LEN 1

/* Define to 1 if `f_fstypename' is member of `struct statfs'. */
#define HAVE_STRUCT_STATFS_F_FSTYPENAME 1

/* Define to 1 if `devid' is member of `struct umntrequest'. */
/* #undef HAVE_STRUCT_UMNTREQUEST_DEVID */

/* Define to 1 if you have the `svc_getreq' function. */
#define HAVE_SVC_GETREQ 1

/* Define to 1 if you have the `svc_getreqset' function. */
#define HAVE_SVC_GETREQSET 1

/* Define to 1 if you have the `sysfs' function. */
/* #undef HAVE_SYSFS */

/* Define to 1 if you have the `syslog' function. */
#define HAVE_SYSLOG 1

/* Define to 1 if you have the <syslog.h> header file. */
#define HAVE_SYSLOG_H 1

/* Define to 1 if you have the <sys/config.h> header file. */
/* #undef HAVE_SYS_CONFIG_H */

/* Define to 1 if you have the <sys/dg_mount.h> header file. */
/* #undef HAVE_SYS_DG_MOUNT_H */

/* Define to 1 if you have the <sys/dir.h> header file. */
#define HAVE_SYS_DIR_H 1

/* Define to 1 if you have the <sys/errno.h> header file. */
#define HAVE_SYS_ERRNO_H 1

/* Define to 1 if you have the <sys/file.h> header file. */
#define HAVE_SYS_FILE_H 1

/* Define to 1 if you have the <sys/fsid.h> header file. */
/* #undef HAVE_SYS_FSID_H */

/* Define to 1 if you have the <sys/fstyp.h> header file. */
/* #undef HAVE_SYS_FSTYP_H */

/* Define to 1 if you have the <sys/fs/autofs.h> header file. */
/* #undef HAVE_SYS_FS_AUTOFS_H */

/* Define to 1 if you have the <sys/fs/autofs_prot.h> header file. */
/* #undef HAVE_SYS_FS_AUTOFS_PROT_H */

/* Define to 1 if you have the <sys/fs/cachefs_fs.h> header file. */
/* #undef HAVE_SYS_FS_CACHEFS_FS_H */

/* Define to 1 if you have the <sys/fs/efs_clnt.h> header file. */
/* #undef HAVE_SYS_FS_EFS_CLNT_H */

/* Define to 1 if you have the <sys/fs/nfs_clnt.h> header file. */
/* #undef HAVE_SYS_FS_NFS_CLNT_H */

/* Define to 1 if you have the <sys/fs/nfs.h> header file. */
/* #undef HAVE_SYS_FS_NFS_H */

/* Define to 1 if you have the <sys/fs/nfs/mount.h> header file. */
/* #undef HAVE_SYS_FS_NFS_MOUNT_H */

/* Define to 1 if you have the <sys/fs/nfs/nfs_clnt.h> header file. */
/* #undef HAVE_SYS_FS_NFS_NFS_CLNT_H */

/* Define to 1 if you have the <sys/fs/pc_fs.h> header file. */
/* #undef HAVE_SYS_FS_PC_FS_H */

/* Define to 1 if you have the <sys/fs/tmp.h> header file. */
/* #undef HAVE_SYS_FS_TMP_H */

/* Define to 1 if you have the <sys/fs_types.h> header file. */
/* #undef HAVE_SYS_FS_TYPES_H */

/* Define to 1 if you have the <sys/fs/ufs_mount.h> header file. */
/* #undef HAVE_SYS_FS_UFS_MOUNT_H */

/* Define to 1 if you have the <sys/fs/xfs_clnt.h> header file. */
/* #undef HAVE_SYS_FS_XFS_CLNT_H */

/* Define to 1 if you have the <sys/immu.h> header file. */
/* #undef HAVE_SYS_IMMU_H */

/* Define to 1 if you have the <sys/ioctl.h> header file. */
#define HAVE_SYS_IOCTL_H 1

/* Define to 1 if you have the <sys/lock.h> header file. */
#define HAVE_SYS_LOCK_H 1

/* Define to 1 if you have the <sys/machine.h> header file. */
/* #undef HAVE_SYS_MACHINE_H */

/* Define to 1 if you have the <sys/mbuf.h> header file. */
#define HAVE_SYS_MBUF_H 1

/* Define to 1 if you have the <sys/mman.h> header file. */
#define HAVE_SYS_MMAN_H 1

/* Define to 1 if you have the <sys/mntctl.h> header file. */
/* #undef HAVE_SYS_MNTCTL_H */

/* Define to 1 if you have the <sys/mntent.h> header file. */
/* #undef HAVE_SYS_MNTENT_H */

/* Define to 1 if you have the <sys/mnttab.h> header file. */
/* #undef HAVE_SYS_MNTTAB_H */

/* Define to 1 if you have the <sys/mount.h> header file. */
#define HAVE_SYS_MOUNT_H 1

/* Define to 1 if you have the <sys/ndir.h> header file. */
/* #undef HAVE_SYS_NDIR_H */

/* Define to 1 if you have the <sys/netconfig.h> header file. */
/* #undef HAVE_SYS_NETCONFIG_H */

/* Define to 1 if you have the <sys/param.h> header file. */
#define HAVE_SYS_PARAM_H 1

/* Define to 1 if you have the <sys/pathconf.h> header file. */
/* #undef HAVE_SYS_PATHCONF_H */

/* Define to 1 if you have the <sys/proc.h> header file. */
#define HAVE_SYS_PROC_H 1

/* Define to 1 if you have the <sys/resource.h> header file. */
#define HAVE_SYS_RESOURCE_H 1

/* Define to 1 if you have the <sys/sema.h> header file. */
#define HAVE_SYS_SEMA_H 1

/* Define to 1 if you have the <sys/signal.h> header file. */
#define HAVE_SYS_SIGNAL_H 1

/* Define to 1 if you have the <sys/socket.h> header file. */
#define HAVE_SYS_SOCKET_H 1

/* Define to 1 if you have the <sys/sockio.h> header file. */
#define HAVE_SYS_SOCKIO_H 1

/* Define to 1 if you have the <sys/statfs.h> header file. */
/* #undef HAVE_SYS_STATFS_H */

/* Define to 1 if you have the <sys/statvfs.h> header file. */
#define HAVE_SYS_STATVFS_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/syscall.h> header file. */
#define HAVE_SYS_SYSCALL_H 1

/* Define to 1 if you have the <sys/syslimits.h> header file. */
#define HAVE_SYS_SYSLIMITS_H 1

/* Define to 1 if you have the <sys/syslog.h> header file. */
#define HAVE_SYS_SYSLOG_H 1

/* Define to 1 if you have the <sys/time.h> header file. */
#define HAVE_SYS_TIME_H 1

/* Define to 1 if you have the <sys/tiuser.h> header file. */
/* #undef HAVE_SYS_TIUSER_H */

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <sys/ucred.h> header file. */
#define HAVE_SYS_UCRED_H 1

/* Define to 1 if you have the <sys/uio.h> header file. */
#define HAVE_SYS_UIO_H 1

/* Define to 1 if you have the <sys/utsname.h> header file. */
#define HAVE_SYS_UTSNAME_H 1

/* Define to 1 if you have the <sys/vfs.h> header file. */
/* #undef HAVE_SYS_VFS_H */

/* Define to 1 if you have the <sys/vmount.h> header file. */
/* #undef HAVE_SYS_VMOUNT_H */

/* Define to 1 if you have the <sys/vnode.h> header file. */
#define HAVE_SYS_VNODE_H 1

/* Define to 1 if you have <sys/wait.h> that is POSIX.1 compatible. */
#define HAVE_SYS_WAIT_H 1

/* Define to 1 if you have the <tcpd.h> header file. */
/* #undef HAVE_TCPD_H */

/* Define to 1 if you have the <time.h> header file. */
#define HAVE_TIME_H 1

/* Define to 1 if you have the <tiuser.h> header file. */
/* #undef HAVE_TIUSER_H */

/* Define to 1 if `ta_nodes_max' is member of `tmpfs_args_t'. */
/* #undef HAVE_TMPFS_ARGS_T_TA_NODES_MAX */

/* Define to 1 if `ta_root_gid' is member of `tmpfs_args_t'. */
/* #undef HAVE_TMPFS_ARGS_T_TA_ROOT_GID */

/* Define to 1 if `ta_root_mode' is member of `tmpfs_args_t'. */
/* #undef HAVE_TMPFS_ARGS_T_TA_ROOT_MODE */

/* Define to 1 if `ta_root_uid' is member of `tmpfs_args_t'. */
/* #undef HAVE_TMPFS_ARGS_T_TA_ROOT_UID */

/* Define to 1 if `ta_size_max' is member of `tmpfs_args_t'. */
/* #undef HAVE_TMPFS_ARGS_T_TA_SIZE_MAX */

/* Define to 1 if `ta_version' is member of `tmpfs_args_t'. */
/* #undef HAVE_TMPFS_ARGS_T_TA_VERSION */

/* Define to 1 if you have the <tmpfs/tmp.h> header file. */
/* #undef HAVE_TMPFS_TMP_H */

/* what type of network transport type is in use? TLI or sockets? */
/* #undef HAVE_TRANSPORT_TYPE_TLI */

/* Define to 1 if you have the `ualarm' function. */
#define HAVE_UALARM 1

/* Define to 1 if `anon_gid' is member of `udf_args_t'. */
/* #undef HAVE_UDF_ARGS_T_ANON_GID */

/* Define to 1 if `anon_uid' is member of `udf_args_t'. */
/* #undef HAVE_UDF_ARGS_T_ANON_UID */

/* Define to 1 if `fspec' is member of `udf_args_t'. */
/* #undef HAVE_UDF_ARGS_T_FSPEC */

/* Define to 1 if `gmtoff' is member of `udf_args_t'. */
/* #undef HAVE_UDF_ARGS_T_GMTOFF */

/* Define to 1 if `nobody_gid' is member of `udf_args_t'. */
/* #undef HAVE_UDF_ARGS_T_NOBODY_GID */

/* Define to 1 if `nobody_uid' is member of `udf_args_t'. */
/* #undef HAVE_UDF_ARGS_T_NOBODY_UID */

/* Define to 1 if `sector_size' is member of `udf_args_t'. */
/* #undef HAVE_UDF_ARGS_T_SECTOR_SIZE */

/* Define to 1 if `sessionnr' is member of `udf_args_t'. */
/* #undef HAVE_UDF_ARGS_T_SESSIONNR */

/* Define to 1 if `udfmflags' is member of `udf_args_t'. */
/* #undef HAVE_UDF_ARGS_T_UDFMFLAGS */

/* Define to 1 if `version' is member of `udf_args_t'. */
/* #undef HAVE_UDF_ARGS_T_VERSION */

/* Define to 1 if `flags' is member of `ufs_args_t'. */
/* #undef HAVE_UFS_ARGS_T_FLAGS */

/* Define to 1 if `fspec' is member of `ufs_args_t'. */
#define HAVE_UFS_ARGS_T_FSPEC 1

/* Define to 1 if `ufs_flags' is member of `ufs_args_t'. */
/* #undef HAVE_UFS_ARGS_T_UFS_FLAGS */

/* Define to 1 if `ufs_pgthresh' is member of `ufs_args_t'. */
/* #undef HAVE_UFS_ARGS_T_UFS_PGTHRESH */

/* Define to 1 if you have the <ufs/ufs/extattr.h> header file. */
#define HAVE_UFS_UFS_EXTATTR_H 1

/* Define to 1 if you have the <ufs/ufs_mount.h> header file. */
/* #undef HAVE_UFS_UFS_MOUNT_H */

/* Define to 1 if you have the <ufs/ufs/ufsmount.h> header file. */
#define HAVE_UFS_UFS_UFSMOUNT_H 1

/* Define to 1 if you have the `umount' function. */
/* #undef HAVE_UMOUNT */

/* Define to 1 if you have the `umount2' function. */
/* #undef HAVE_UMOUNT2 */

/* Define to 1 if you have the `uname' function. */
#define HAVE_UNAME 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to 1 if you have the `unmount' function. */
#define HAVE_UNMOUNT 1

/* Define to 1 if you have the `uvmount' function. */
/* #undef HAVE_UVMOUNT */

/* Define to 1 if you have the <varargs.h> header file. */
/* #undef HAVE_VARARGS_H */

/* Define to 1 if you have the `vfork' function. */
#define HAVE_VFORK 1

/* Define to 1 if you have the <vfork.h> header file. */
/* #undef HAVE_VFORK_H */

/* Define to 1 if you have the `vfsmount' function. */
/* #undef HAVE_VFSMOUNT */

/* Define to 1 if you have the `vmount' function. */
/* #undef HAVE_VMOUNT */

/* Define to 1 if you have the `vsnprintf' function. */
#define HAVE_VSNPRINTF 1

/* Define to 1 if you have the `wait3' function. */
#define HAVE_WAIT3 1

/* Define to 1 if you have the `waitpid' function. */
#define HAVE_WAITPID 1

/* Define to 1 if `fork' works. */
#define HAVE_WORKING_FORK 1

/* Define to 1 if `vfork' works. */
#define HAVE_WORKING_VFORK 1

/* Define to 1 if you have the `xdr_attrstat' function. */
#define HAVE_XDR_ATTRSTAT 1

/* Define to 1 if you have the `xdr_createargs' function. */
#define HAVE_XDR_CREATEARGS 1

/* Define to 1 if you have the `xdr_dirlist' function. */
#define HAVE_XDR_DIRLIST 1

/* Define to 1 if you have the `xdr_diropargs' function. */
#define HAVE_XDR_DIROPARGS 1

/* Define to 1 if you have the `xdr_diropokres' function. */
#define HAVE_XDR_DIROPOKRES 1

/* Define to 1 if you have the `xdr_diropres' function. */
#define HAVE_XDR_DIROPRES 1

/* Define to 1 if you have the `xdr_dirpath' function. */
#define HAVE_XDR_DIRPATH 1

/* Define to 1 if you have the `xdr_entry' function. */
#define HAVE_XDR_ENTRY 1

/* Define to 1 if you have the `xdr_exportnode' function. */
#define HAVE_XDR_EXPORTNODE 1

/* Define to 1 if you have the `xdr_exports' function. */
#define HAVE_XDR_EXPORTS 1

/* Define to 1 if you have the `xdr_fattr' function. */
#define HAVE_XDR_FATTR 1

/* Define to 1 if you have the `xdr_fhandle' function. */
#define HAVE_XDR_FHANDLE 1

/* Define to 1 if you have the `xdr_fhstatus' function. */
#define HAVE_XDR_FHSTATUS 1

/* Define to 1 if you have the `xdr_filename' function. */
#define HAVE_XDR_FILENAME 1

/* Define to 1 if you have the `xdr_ftype' function. */
#define HAVE_XDR_FTYPE 1

/* Define to 1 if you have the `xdr_groupnode' function. */
#define HAVE_XDR_GROUPNODE 1

/* Define to 1 if you have the `xdr_groups' function. */
#define HAVE_XDR_GROUPS 1

/* Define to 1 if you have the `xdr_linkargs' function. */
#define HAVE_XDR_LINKARGS 1

/* Define to 1 if you have the `xdr_mountbody' function. */
#define HAVE_XDR_MOUNTBODY 1

/* Define to 1 if you have the `xdr_mountlist' function. */
#define HAVE_XDR_MOUNTLIST 1

/* Define to 1 if you have the `xdr_name' function. */
#define HAVE_XDR_NAME 1

/* Define to 1 if you have the `xdr_nfscookie' function. */
#define HAVE_XDR_NFSCOOKIE 1

/* Define to 1 if you have the `xdr_nfspath' function. */
#define HAVE_XDR_NFSPATH 1

/* Define to 1 if you have the `xdr_nfsstat' function. */
#define HAVE_XDR_NFSSTAT 1

/* Define to 1 if you have the `xdr_nfstime' function. */
#define HAVE_XDR_NFSTIME 1

/* Define to 1 if you have the `xdr_nfs_fh' function. */
#define HAVE_XDR_NFS_FH 1

/* Define to 1 if you have the `xdr_pointer' function. */
#define HAVE_XDR_POINTER 1

/* Define to 1 if you have the `xdr_readargs' function. */
#define HAVE_XDR_READARGS 1

/* Define to 1 if you have the `xdr_readdirargs' function. */
#define HAVE_XDR_READDIRARGS 1

/* Define to 1 if you have the `xdr_readdirres' function. */
#define HAVE_XDR_READDIRRES 1

/* Define to 1 if you have the `xdr_readlinkres' function. */
#define HAVE_XDR_READLINKRES 1

/* Define to 1 if you have the `xdr_readokres' function. */
#define HAVE_XDR_READOKRES 1

/* Define to 1 if you have the `xdr_readres' function. */
#define HAVE_XDR_READRES 1

/* Define to 1 if you have the `xdr_renameargs' function. */
#define HAVE_XDR_RENAMEARGS 1

/* Define to 1 if you have the `xdr_sattr' function. */
#define HAVE_XDR_SATTR 1

/* Define to 1 if you have the `xdr_sattrargs' function. */
#define HAVE_XDR_SATTRARGS 1

/* Define to 1 if you have the `xdr_statfsokres' function. */
#define HAVE_XDR_STATFSOKRES 1

/* Define to 1 if you have the `xdr_statfsres' function. */
#define HAVE_XDR_STATFSRES 1

/* Define to 1 if you have the `xdr_symlinkargs' function. */
#define HAVE_XDR_SYMLINKARGS 1

/* Define to 1 if you have the `xdr_u_int64_t' function. */
#define HAVE_XDR_U_INT64_T 1

/* Define to 1 if you have the `xdr_writeargs' function. */
#define HAVE_XDR_WRITEARGS 1

/* Define to 1 if `flags' is member of `xfs_args_t'. */
/* #undef HAVE_XFS_ARGS_T_FLAGS */

/* Define to 1 if `fspec' is member of `xfs_args_t'. */
/* #undef HAVE_XFS_ARGS_T_FSPEC */

/* Define to 1 if you have the `yp_all' function. */
/* #undef HAVE_YP_ALL */

/* Define to 1 if you have the `yp_get_default_domain' function. */
#define HAVE_YP_GET_DEFAULT_DOMAIN 1

/* Define to 1 if you have the `_seterr_reply' function. */
#define HAVE__SETERR_REPLY 1

/* Define to 1 if you have the `__rpc_get_local_uid' function. */
#define HAVE___RPC_GET_LOCAL_UID 1

/* Define to 1 if you have the `__seterr_reply' function. */
/* #undef HAVE___SETERR_REPLY */

/* Name of mount type to hide amd mount from df(1) */
#define HIDE_MOUNT_TYPE "nfs"

/* Define name of host machine's architecture (eg. sun4) */
/* #define HOST_ARCH "i386" */

/* Define name of host machine's cpu (eg. sparc) */
/* #define HOST_CPU "i386" */

/* Define the header version of (linux) hosts (eg. 2.2.10) */
// #undef define HOST_HEADER_VERSION */

/* Define name of host */
/* #define HOST_NAME "trang.nuxi.org" */

/* Define name and version of host machine (eg. solaris2.5.1) */
/* #define HOST_OS "freebsd12.0" */

/* Define only name of host machine OS (eg. solaris2) */
/* #define HOST_OS_NAME "freebsd12" */

/* Define only version of host machine (eg. 2.5.1) */
/* #define HOST_OS_VERSION "12.0" */

/* Define name of host machine's vendor (eg. sun) */
#define HOST_VENDOR "undermydesk"

/* Define to the sub-directory in which libtool stores uninstalled libraries.
   */
#define LT_OBJDIR ".libs/"

/* Ignore permission bits */
/* #undef MNT2_CDFS_OPT_DEFPERM */

/* Enable external attributes */
#define MNT2_CDFS_OPT_EXTATT 0x4

/* Show file generations */
#define MNT2_CDFS_OPT_GENS 0x2

/* Disable filename case translation */
/* #undef MNT2_CDFS_OPT_NOCASETRANS */

/* Use on-disk permission bits */
/* #undef MNT2_CDFS_OPT_NODEFPERM */

/* Disable Joliet extensions */
#define MNT2_CDFS_OPT_NOJOLIET 0x8

/* Disable Rock Ridge Interchange Protocol (RRIP) extensions */
#define MNT2_CDFS_OPT_NORRIP 0x1

/* Strip off extension from version string */
/* #undef MNT2_CDFS_OPT_NOVERSION */

/* Enable Rock Ridge Interchange Protocol (RRIP) case insensitive filename
   extensions */
/* #undef MNT2_CDFS_OPT_RRCASEINS */

/* Use Rock Ridge Interchange Protocol (RRIP) extensions */
/* #undef MNT2_CDFS_OPT_RRIP */

/* asynchronous filesystem access */
#define MNT2_GEN_OPT_ASYNC 0x40

/* automounter filesystem (ignore) flag, used in bsdi-4.1 */
/* #undef MNT2_GEN_OPT_AUTOMNTFS */

/* automounter filesystem flag, used in Mac OS X / Darwin */
/* #undef MNT2_GEN_OPT_AUTOMOUNTED */

/* directory hardlink */
/* #undef MNT2_GEN_OPT_BIND */

/* cache (what?) */
/* #undef MNT2_GEN_OPT_CACHE */

/* 6-argument mount */
/* #undef MNT2_GEN_OPT_DATA */

/* Use a lazy unmount (detach) */
/* #undef MNT2_GEN_OPT_DETACH */

/* Use a forced unmount */
#define MNT2_GEN_OPT_FORCE 0x80000

/* old (4-argument) mount (compatibility) */
/* #undef MNT2_GEN_OPT_FSS */

/* old BSD group-id on create */
/* #undef MNT2_GEN_OPT_GRPID */

/* ignore mount entry in df output */
#define MNT2_GEN_OPT_IGNORE 0x800000

/* journaling filesystem (AIX's UFS/FFS) */
/* #undef MNT2_GEN_OPT_JFS */

/* honor mandatory locking requests */
/* #undef MNT2_GEN_OPT_MANDLOCK */

/* do multi-component lookup on files */
/* #undef MNT2_GEN_OPT_MULTI */

/* use type string instead of int */
/* #undef MNT2_GEN_OPT_NEWTYPE */

/* NFS mount */
/* #undef MNT2_GEN_OPT_NFS */

/* don't update access times */
#define MNT2_GEN_OPT_NOATIME 0x10000000

/* nocache (what?) */
/* #undef MNT2_GEN_OPT_NOCACHE */

/* do not interpret special device files */
#define MNT2_GEN_OPT_NODEV 0x0

/* don't update directory access times */
/* #undef MNT2_GEN_OPT_NODIRATIME */

/* no exec calls allowed */
#define MNT2_GEN_OPT_NOEXEC 0x4

/* do not interpret special device files */
/* #undef MNT2_GEN_OPT_NONDEV */

/* Disallow mounts beneath this mount */
/* #undef MNT2_GEN_OPT_NOSUB */

/* Setuid programs disallowed */
#define MNT2_GEN_OPT_NOSUID 0x8

/* Return ENAMETOOLONG for long filenames */
/* #undef MNT2_GEN_OPT_NOTRUNC */

/* Pass mount option string to kernel */
/* #undef MNT2_GEN_OPT_OPTIONSTR */

/* allow overlay mounts */
/* #undef MNT2_GEN_OPT_OVERLAY */

/* check quotas */
#define MNT2_GEN_OPT_QUOTA 0x2000

/* Read-only */
#define MNT2_GEN_OPT_RDONLY 0x1

/* change options on an existing mount */
/* #undef MNT2_GEN_OPT_REMOUNT */

/* read only */
/* #undef MNT2_GEN_OPT_RONLY */

/* synchronize data immediately to filesystem */
/* #undef MNT2_GEN_OPT_SYNC */

/* synchronous filesystem access (same as SYNC) */
#define MNT2_GEN_OPT_SYNCHRONOUS 0x2

/* Mount with Sys 5-specific semantics */
/* #undef MNT2_GEN_OPT_SYS5 */

/* Union mount */
#define MNT2_GEN_OPT_UNION 0x20

/* set max secs for dir attr cache */
#define MNT2_NFS_OPT_ACDIRMAX 0x200000

/* set min secs for dir attr cache */
#define MNT2_NFS_OPT_ACDIRMIN 0x100000

/* set max secs for file attr cache */
#define MNT2_NFS_OPT_ACREGMAX 0x80000

/* set min secs for file attr cache */
#define MNT2_NFS_OPT_ACREGMIN 0x40000

/* Authentication error */
/* #undef MNT2_NFS_OPT_AUTHERR */

/* hide mount type from df(1) */
/* #undef MNT2_NFS_OPT_AUTO */

/* Linux broken setuid */
/* #undef MNT2_NFS_OPT_BROKEN_SUID */

/* set dead server retry thresh */
#define MNT2_NFS_OPT_DEADTHRESH 0x4000

/* Dismount in progress */
/* #undef MNT2_NFS_OPT_DISMINPROG */

/* Dismounted */
/* #undef MNT2_NFS_OPT_DISMNT */

/* Don't estimate rtt dynamically */
#define MNT2_NFS_OPT_DUMBTIMR 0x800

/* provide name of server's fs to system */
/* #undef MNT2_NFS_OPT_FSNAME */

/* System V-style gid inheritance */
/* #undef MNT2_NFS_OPT_GRPID */

/* Has authenticator */
/* #undef MNT2_NFS_OPT_HASAUTH */

/* set hostname for error printf */
/* #undef MNT2_NFS_OPT_HOSTNAME */

/* ignore mount point */
/* #undef MNT2_NFS_OPT_IGNORE */

/* allow interrupts on hard mount */
#define MNT2_NFS_OPT_INT 0x40

/* Bits set internally */
/* #undef MNT2_NFS_OPT_INTERNAL */

/* allow interrupts on hard mount */
/* #undef MNT2_NFS_OPT_INTR */

/* Use Kerberos authentication */
/* #undef MNT2_NFS_OPT_KERB */

/* use kerberos credentials */
/* #undef MNT2_NFS_OPT_KERBEROS */

/* transport's knetconfig structure */
/* #undef MNT2_NFS_OPT_KNCONF */

/* set lease term (nqnfs) */
/* #undef MNT2_NFS_OPT_LEASETERM */

/* Local locking (no lock manager) */
/* #undef MNT2_NFS_OPT_LLOCK */

/* set maximum grouplist size */
#define MNT2_NFS_OPT_MAXGRPS 0x20

/* Mnt server for mnt point */
/* #undef MNT2_NFS_OPT_MNTD */

/* Assume writes were mine */
/* #undef MNT2_NFS_OPT_MYWRITE */

/* mount NFS Version 3 */
#define MNT2_NFS_OPT_NFSV3 0x200

/* don't cache attributes */
/* #undef MNT2_NFS_OPT_NOAC */

/* does not support Access Control Lists */
/* #undef MNT2_NFS_OPT_NOACL */

/* Don't Connect the socket */
#define MNT2_NFS_OPT_NOCONN 0x80

/* no close-to-open consistency */
#define MNT2_NFS_OPT_NOCTO 0x20000000

/* disallow interrupts on hard mounts */
/* #undef MNT2_NFS_OPT_NOINT */

/* Don't use locking */
/* #undef MNT2_NFS_OPT_NONLM */

/* does not support readdir+ */
/* #undef MNT2_NFS_OPT_NORDIRPLUS */

/* Get lease for lookup */
/* #undef MNT2_NFS_OPT_NQLOOKLEASE */

/* Use Nqnfs protocol */
/* #undef MNT2_NFS_OPT_NQNFS */

/* paging threshold */
/* #undef MNT2_NFS_OPT_PGTHRESH */

/* static pathconf kludge info */
/* #undef MNT2_NFS_OPT_POSIX */

/* Use local locking */
/* #undef MNT2_NFS_OPT_PRIVATE */

/* allow property list operations (ACLs over NFS) */
/* #undef MNT2_NFS_OPT_PROPLIST */

/* Rcv socket lock */
/* #undef MNT2_NFS_OPT_RCVLOCK */

/* Do lookup with readdir (nqnfs) */
/* #undef MNT2_NFS_OPT_RDIRALOOK */

/* Use Readdirplus for NFSv3 */
#define MNT2_NFS_OPT_RDIRPLUS 0x10000

/* set read ahead */
#define MNT2_NFS_OPT_READAHEAD 0x2000

/* Set readdir size */
#define MNT2_NFS_OPT_READDIRSIZE 0x20000

/* Allocate a reserved port */
#define MNT2_NFS_OPT_RESVPORT 0x8000

/* set number of request retries */
#define MNT2_NFS_OPT_RETRANS 0x10

/* read only */
/* #undef MNT2_NFS_OPT_RONLY */

/* use RPC to do secure NFS time sync */
/* #undef MNT2_NFS_OPT_RPCTIMESYNC */

/* set read size */
#define MNT2_NFS_OPT_RSIZE 0x4

/* secure mount */
/* #undef MNT2_NFS_OPT_SECURE */

/* Send socket lock */
/* #undef MNT2_NFS_OPT_SNDLOCK */

/* soft mount (hard is default) */
#define MNT2_NFS_OPT_SOFT 0x1

/* spongy mount */
/* #undef MNT2_NFS_OPT_SPONGY */

/* Reserved for nfsv4 */
/* #undef MNT2_NFS_OPT_STRICTLOCK */

/* set symlink cache time-to-live */
/* #undef MNT2_NFS_OPT_SYMTTL */

/* use TCP for mounts */
/* #undef MNT2_NFS_OPT_TCP */

/* set initial timeout */
#define MNT2_NFS_OPT_TIMEO 0x8

/* do not use shared cache for all mountpoints */
/* #undef MNT2_NFS_OPT_UNSHARED */

/* linux NFSv3 */
/* #undef MNT2_NFS_OPT_VER3 */

/* Wait for authentication */
/* #undef MNT2_NFS_OPT_WAITAUTH */

/* Wants an authenticator */
/* #undef MNT2_NFS_OPT_WANTAUTH */

/* Want receive socket lock */
/* #undef MNT2_NFS_OPT_WANTRCV */

/* Want send socket lock */
/* #undef MNT2_NFS_OPT_WANTSND */

/* set write size */
#define MNT2_NFS_OPT_WSIZE 0x2

/* 32<->64 dir cookie translation */
/* #undef MNT2_NFS_OPT_XLATECOOKIE */

/* Force Win95 long names */
#define MNT2_PCFS_OPT_LONGNAME 0x2

/* Completely ignore Win95 entries */
#define MNT2_PCFS_OPT_NOWIN95 0x4

/* Force old DOS short names only */
#define MNT2_PCFS_OPT_SHORTNAME 0x1

/* Name of mount table file name */
/* #undef MNTTAB_FILE_NAME */

/* Mount Table option string: Max attr cache timeout (dirs) */
/* #undef MNTTAB_OPT_ACDIRMAX */

/* Mount Table option string: Min attr cache timeout (dirs) */
/* #undef MNTTAB_OPT_ACDIRMIN */

/* Mount Table option string: Max attr cache timeout (files) */
/* #undef MNTTAB_OPT_ACREGMAX */

/* Mount Table option string: Min attr cache timeout (files) */
/* #undef MNTTAB_OPT_ACREGMIN */

/* Mount Table option string: Attr cache timeout (sec) */
/* #undef MNTTAB_OPT_ACTIMEO */

/* Mount Table option string: Do mount retries in background */
/* #undef MNTTAB_OPT_BG */

/* Mount Table option string: compress */
/* #undef MNTTAB_OPT_COMPRESS */

/* Mount Table option string: Device id of mounted fs */
/* #undef MNTTAB_OPT_DEV */

/* Mount Table option string: Automount direct map mount */
/* #undef MNTTAB_OPT_DIRECT */

/* Mount Table option string: Do mount retries in foreground */
/* #undef MNTTAB_OPT_FG */

/* Mount Table option string: Filesystem id of mounted fs */
/* #undef MNTTAB_OPT_FSID */

/* Mount Table option string: SysV-compatible gid on create */
/* #undef MNTTAB_OPT_GRPID */

/* Mount Table option string: Hard mount */
/* #undef MNTTAB_OPT_HARD */

/* Mount Table option string: Ignore this entry */
/* #undef MNTTAB_OPT_IGNORE */

/* Mount Table option string: Automount indirect map mount */
/* #undef MNTTAB_OPT_INDIRECT */

/* Mount Table option string: Allow NFS ops to be interrupted */
/* #undef MNTTAB_OPT_INTR */

/* Mount Table option string: Secure (AUTH_Kerb) mounting */
/* #undef MNTTAB_OPT_KERB */

/* Mount Table option string: Local locking (no lock manager) */
/* #undef MNTTAB_OPT_LLOCK */

/* Force Win95 long names */
/* #undef MNTTAB_OPT_LONGNAME */

/* Mount Table option string: Automount map */
/* #undef MNTTAB_OPT_MAP */

/* Mount Table option string: max groups */
/* #undef MNTTAB_OPT_MAXGROUPS */

/* Mount Table option string: Do multi-component lookup */
/* #undef MNTTAB_OPT_MULTI */

/* Mount Table option string: Don't cache attributes at all */
/* #undef MNTTAB_OPT_NOAC */

/* Access Control Lists are not supported */
/* #undef MNTTAB_OPT_NOACL */

/* Mount Table option string: No auto (what?) */
/* #undef MNTTAB_OPT_NOAUTO */

/* Mount Table option string: No connection */
/* #undef MNTTAB_OPT_NOCONN */

/* Mount Table option string: No close-to-open consistency */
/* #undef MNTTAB_OPT_NOCTO */

/* Mount Table option string: Don't allow interrupted ops */
/* #undef MNTTAB_OPT_NOINTR */

/* Mount Table option string: Don't check quotas */
/* #undef MNTTAB_OPT_NOQUOTA */

/* Mount Table option string: Do no allow setting sec attrs */
/* #undef MNTTAB_OPT_NOSETSEC */

/* Mount Table option string: Disallow mounts on subdirs */
/* #undef MNTTAB_OPT_NOSUB */

/* Mount Table option string: Set uid not allowed */
/* #undef MNTTAB_OPT_NOSUID */

/* Completely ignore Win95 entries */
/* #undef MNTTAB_OPT_NOWIN95 */

/* Mount Table option string: action to taken on error */
/* #undef MNTTAB_OPT_ONERROR */

/* Mount Table option string: paging threshold */
/* #undef MNTTAB_OPT_PGTHRESH */

/* Mount Table option string: NFS server IP port number */
/* #undef MNTTAB_OPT_PORT */

/* Mount Table option string: Get static pathconf for mount */
/* #undef MNTTAB_OPT_POSIX */

/* Mount Table option string: Use local locking */
/* #undef MNTTAB_OPT_PRIVATE */

/* Mount Table option string: support property lists (ACLs) */
/* #undef MNTTAB_OPT_PROPLIST */

/* Mount Table option string: protocol network_id indicator */
/* #undef MNTTAB_OPT_PROTO */

/* Mount Table option string: Check quotas */
/* #undef MNTTAB_OPT_QUOTA */

/* Mount Table option string: Change mount options */
/* #undef MNTTAB_OPT_REMOUNT */

/* Mount Table option string: Max retransmissions (soft mnts) */
/* #undef MNTTAB_OPT_RETRANS */

/* Mount Table option string: Number of mount retries */
/* #undef MNTTAB_OPT_RETRY */

/* Mount Table option string: Read only */
/* #undef MNTTAB_OPT_RO */

/* Mount Table option string: Read/write with quotas */
/* #undef MNTTAB_OPT_RQ */

/* Mount Table option string: Max NFS read size (bytes) */
/* #undef MNTTAB_OPT_RSIZE */

/* Mount Table option string: Read/write */
/* #undef MNTTAB_OPT_RW */

/* Mount Table option string: Secure (AUTH_DES) mounting */
/* #undef MNTTAB_OPT_SECURE */

/* Force old DOS short names only */
/* #undef MNTTAB_OPT_SHORTNAME */

/* Mount Table option string: Soft mount */
/* #undef MNTTAB_OPT_SOFT */

/* Mount Table option string: spongy mount */
/* #undef MNTTAB_OPT_SPONGY */

/* Mount Table option string: Set uid allowed */
/* #undef MNTTAB_OPT_SUID */

/* Mount Table option string: set symlink cache time-to-live */
/* #undef MNTTAB_OPT_SYMTTL */

/* Mount Table option string: Synchronous local directory ops */
/* #undef MNTTAB_OPT_SYNCDIR */

/* Mount Table option string: NFS timeout (1/10 sec) */
/* #undef MNTTAB_OPT_TIMEO */

/* Mount Table option string: min. time between inconsistencies */
/* #undef MNTTAB_OPT_TOOSOON */

/* Mount Table option string: protocol version number indicator */
/* #undef MNTTAB_OPT_VERS */

/* Mount Table option string: Max NFS write size (bytes) */
/* #undef MNTTAB_OPT_WSIZE */

/* Mount-table entry name for AUTOFS filesystem */
/* #undef MNTTAB_TYPE_AUTOFS */

/* Mount-table entry name for CACHEFS filesystem */
/* #undef MNTTAB_TYPE_CACHEFS */

/* Mount-table entry name for CDFS filesystem */
#define MNTTAB_TYPE_CDFS "cd9660"

/* Mount-table entry name for CFS (crypto) filesystem */
/* #undef MNTTAB_TYPE_CFS */

/* Mount-table entry name for EFS filesystem (irix) */
/* #undef MNTTAB_TYPE_EFS */

/* Mount-table entry name for EXT2 filesystem (linux) */
/* #undef MNTTAB_TYPE_EXT2 */

/* Mount-table entry name for EXT3 filesystem (linux) */
/* #undef MNTTAB_TYPE_EXT3 */

/* Mount-table entry name for EXT4 filesystem (linux) */
/* #undef MNTTAB_TYPE_EXT4 */

/* Mount-table entry name for FFS filesystem */
/* #undef MNTTAB_TYPE_FFS */

/* Mount-table entry name for LOFS filesystem */
/* #undef MNTTAB_TYPE_LOFS */

/* Mount-table entry name for LUSTRE filesystem */
/* #undef MNTTAB_TYPE_LUSTRE */

/* Mount-table entry name for MFS filesystem */
#define MNTTAB_TYPE_MFS "mfs"

/* Mount-table entry name for NFS filesystem */
#define MNTTAB_TYPE_NFS "nfs"

/* Mount-table entry name for NFS3 filesystem */
#define MNTTAB_TYPE_NFS3 "nfs"

/* Mount-table entry name for NFS4 filesystem */
#define MNTTAB_TYPE_NFS4 "nfs"

/* Mount-table entry name for NULLFS (loopback on bsd44) filesystem */
#define MNTTAB_TYPE_NULLFS "nullfs"

/* Mount-table entry name for PCFS filesystem */
#define MNTTAB_TYPE_PCFS "msdosfs"

/* Mount-table entry name for TFS filesystem */
/* #undef MNTTAB_TYPE_TFS */

/* Mount(2) type/name for TMPFS filesystem */
#define MNTTAB_TYPE_TMPFS "tmpfs"

/* Mount(2) type/name for UDF filesystem */
#define MNTTAB_TYPE_UDF "udf"

/* Mount-table entry name for UFS filesystem */
#define MNTTAB_TYPE_UFS "ufs"

/* Mount-table entry name for UMAPFS (uid/gid mapping) filesystem */
/* #undef MNTTAB_TYPE_UMAPFS */

/* Mount-table entry name for UNIONFS filesystem */
#define MNTTAB_TYPE_UNIONFS "unionfs"

/* Mount-table entry name for XFS filesystem (irix) */
/* #undef MNTTAB_TYPE_XFS */

/* Define if mount table is on file, undefine if in kernel */
/* #undef MOUNT_TABLE_ON_FILE */

/* Mount(2) type/name for AUTOFS filesystem */
/* #undef MOUNT_TYPE_AUTOFS */

/* Mount(2) type/name for CACHEFS filesystem */
/* #undef MOUNT_TYPE_CACHEFS */

/* Mount(2) type/name for CDFS filesystem */
#define MOUNT_TYPE_CDFS "cd9660"

/* Mount(2) type/name for CFS (crypto) filesystem */
/* #undef MOUNT_TYPE_CFS */

/* Mount(2) type/name for EFS filesystem (irix) */
/* #undef MOUNT_TYPE_EFS */

/* Mount(2) type/name for EXT2 filesystem (linux) */
/* #undef MOUNT_TYPE_EXT2 */

/* Mount(2) type/name for EXT3 filesystem (linux) */
/* #undef MOUNT_TYPE_EXT3 */

/* Mount(2) type/name for EXT4 filesystem (linux) */
/* #undef MOUNT_TYPE_EXT4 */

/* Mount(2) type/name for FFS filesystem */
/* #undef MOUNT_TYPE_FFS */

/* Mount(2) type/name for IGNORE filesystem (not real just ignore for df) */
#define MOUNT_TYPE_IGNORE MNT_IGNORE

/* Mount(2) type/name for LOFS filesystem */
/* #undef MOUNT_TYPE_LOFS */

/* Mount(2) type/name for MFS filesystem */
#define MOUNT_TYPE_MFS "mfs"

/* Mount(2) type/name for NFS filesystem */
#define MOUNT_TYPE_NFS "nfs"

/* Mount(2) type/name for NFS3 filesystem */
#define MOUNT_TYPE_NFS3 MOUNT_NFS3

/* Mount(2) type/name for NFS4 filesystem */
/* #undef MOUNT_TYPE_NFS4 */

/* Mount(2) type/name for NULLFS (loopback on bsd44) filesystem */
#define MOUNT_TYPE_NULLFS "nullfs"

/* Mount(2) type/name for PCFS filesystem. XXX: conf/trap/trap_hpux.h may
   override this definition for HPUX 9.0 */
#define MOUNT_TYPE_PCFS "msdosfs"

/* Mount(2) type/name for TFS filesystem */
#define MOUNT_TYPE_TMPFS "tmpfs"

/* Mount(2) type/name for UDF filesystem */
#define MOUNT_TYPE_UDF "udf"

/* Mount(2) type/name for TMPFS filesystem */
/* #undef MOUNT_TYPE_TMPFS */

/* Mount(2) type/name for UFS filesystem */
#define MOUNT_TYPE_UFS "ufs"

/* Mount(2) type/name for UMAPFS (uid/gid mapping) filesystem */
/* #undef MOUNT_TYPE_UMAPFS */

/* Mount(2) type/name for UNIONFS filesystem */
#define MOUNT_TYPE_UNIONFS MNT_UNION

/* Mount(2) type/name for XFS filesystem (irix) */
/* #undef MOUNT_TYPE_XFS */

/* The string used in printf to print the mount-type field of mount(2) */
#define MTYPE_PRINTF_TYPE "%s"

/* Type of the mount-type field in the mount() system call */
#define MTYPE_TYPE char *

/* does libwrap expect caller to define the variables allow_severity and
   deny_severity */
/* #undef NEED_LIBWRAP_SEVERITY_VARIABLES */

/* Defined to the header file containing ndbm-compatible definitions */
#define NEW_DBM_H <ndbm.h>

/* Define the field name for the filehandle within nfs_args_t */
#define NFS_FH_FIELD fh

/* Define to 1 if your C compiler doesn't accept -c and -o together. */
/* #undef NO_MINUS_C_MINUS_O */

/* Name of package */
#define PACKAGE "am-utils"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "https://bugzilla.am-utils.org/ or am-utils@am-utils.org"

/* Define to the full name of this package. */
#define PACKAGE_NAME "am-utils"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "am-utils 6.2"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "am-utils"

/* Define to the home page for this package. */
#define PACKAGE_URL ""

/* Define to the version of this package. */
#define PACKAGE_VERSION "6.2"

/* Type of the 6th argument to recvfrom() */
#define RECVFROM_FROMLEN_TYPE socklen_t

/* should signal handlers be reinstalled? */
/* #undef REINSTALL_SIGNAL_HANDLER */

/* Define as the return type of signal handlers (`int' or `void'). */
#define RETSIGTYPE void

/* Define to 1 if the `setpgrp' function takes no argument. */
/* #undef SETPGRP_VOID */

/* Define to 1 if the `S_IS*' macros in <sys/stat.h> do not work properly. */
/* #undef STAT_MACROS_BROKEN */

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Define the type of the 3rd argument ('in') to svc_getargs() */
#define SVC_IN_ARG_TYPE caddr_t

/* Define to 1 if you can safely include both <sys/time.h> and <time.h>. */
#define TIME_WITH_SYS_TIME 1

/* Define to 1 if your <sys/time.h> declares `struct tm'. */
/* #undef TM_IN_SYS_TIME */

/* Define user name */
/* #define USER_NAME "cy" */

/* define if must NOT use NFS "noconn" option */
#define USE_CONNECTED_NFS_SOCKETS 1

/* Enable extensions on AIX 3, Interix.  */
#ifndef _ALL_SOURCE
# define _ALL_SOURCE 1
#endif
/* Enable GNU extensions on systems that have them.  */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif
/* Enable threading extensions on Solaris.  */
#ifndef _POSIX_PTHREAD_SEMANTICS
# define _POSIX_PTHREAD_SEMANTICS 1
#endif
/* Enable extensions on HP NonStop.  */
#ifndef _TANDEM_SOURCE
# define _TANDEM_SOURCE 1
#endif
/* Enable general extensions on Solaris.  */
#ifndef __EXTENSIONS__
# define __EXTENSIONS__ 1
#endif

/* define if must use NFS "noconn" option */
/* #undef USE_UNCONNECTED_NFS_SOCKETS */

/* Version number of package */
#define VERSION "6.2"

/* Define WORDS_BIGENDIAN to 1 if your processor stores words with the most
   significant byte first (like Motorola and SPARC, unlike Intel). */
#if defined AC_APPLE_UNIVERSAL_BUILD
# if defined __BIG_ENDIAN__
#  define WORDS_BIGENDIAN 1
# endif
#else
# ifndef WORDS_BIGENDIAN
/* #  undef WORDS_BIGENDIAN */
# endif
#endif

/* Define to the type of xdr procedure type */
#define XDRPROC_T_TYPE xdrproc_t

/* Type of the 3rd argument to yp_order() */
#define YP_ORDER_OUTORDER_TYPE int

/* Define to 1 if `lex' declares `yytext' as a `char *' by default, not a
   `char[]'. */
#define YYTEXT_POINTER 1

/* Enable large inode numbers on Mac OS X 10.5.  */
#ifndef _DARWIN_USE_64_BIT_INODE
# define _DARWIN_USE_64_BIT_INODE 1
#endif

/* Number of bits in a file offset, on hosts where this is settable. */
/* #undef _FILE_OFFSET_BITS */

/* Define for large files, on AIX-style hosts. */
/* #undef _LARGE_FILES */

/* Define to 1 if on MINIX. */
/* #undef _MINIX */

/* Define to 2 if the system does not provide POSIX.1 features except with
   this defined. */
/* #undef _POSIX_1_SOURCE */

/* Define to 1 if you need to in order for `stat' and other things to work. */
/* #undef _POSIX_SOURCE */

/* Define a type/structure for an NFS V2 filehandle */
#define am_nfs_fh nfs_fh

/* Define a type/structure for an NFS V3 filehandle */
#define am_nfs_fh3 nfs_fh3_freebsd3

/* Define a type for the autofs_args structure */
/* #undef autofs_args_t */

/* Define a type for the cachefs_args structure */
/* #undef cachefs_args_t */

/* Define a type for the cdfs_args structure */
#define cdfs_args_t struct iso_args

/* Define to empty if `const' does not conform to ANSI C. */
/* #undef const */

/* Define a type for the efs_args structure */
/* #undef efs_args_t */

/* Define to `int' if <sys/types.h> doesn't define. */
/* #undef gid_t */

/* Define to `__inline__' or `__inline' if that's what the C compiler
   calls it, or to nothing if 'inline' is not supported under any name.  */
#ifndef __cplusplus
/* #undef inline */
#endif

/* Define a type for the lofs_args structure */
/* #undef lofs_args_t */

/* Define a type for the mfs_args structure */
/* #undef mfs_args_t */

/* Define to `int' if <sys/types.h> does not define. */
/* #undef mode_t */

/* Define a type for the nfs_args structure */
#define nfs_args_t struct nfs_args

/* Define a type for the pcfs_args structure */
#define pcfs_args_t struct msdosfs_args

/* Define to `int' if <sys/types.h> does not define. */
/* #undef pid_t */

/* Check if pte_t is defined in <sys/immu.h> */
/* #undef pte_t */

/* Define a type for the rfs_args structure */
/* #undef rfs_args_t */

/* Check if rpcvers_t is defined in <rpc/types.h> */
/* #undef rpcvers_t */

/* Define to `unsigned int' if <sys/types.h> does not define. */
/* #undef size_t */

/* Define to `long' if <sys/types.h> does not define. */
/* #undef time_t */

/* Define a type for the tmpfs_args structure */
/* #undef tmpfs_args_t */

/* Define a type for the udf_args structure */
/* #undef udf_args_t */

/* Define a type for the ufs_args structure */
#define ufs_args_t struct ufs_args

/* Define to `int' if <sys/types.h> doesn't define. */
/* #undef uid_t */

/* Define as `fork' if `vfork' does not work. */
/* #undef vfork */

/* Define to "void *" if compiler can handle, otherwise "char *" */
#define voidp void *

/* Define to empty if the keyword `volatile' does not work. Warning: valid
   code using `volatile' can become incorrect without. Disable with care. */
/* #undef volatile */

/* Define a type for the xfs_args structure */
/* #undef xfs_args_t */


/****************************************************************************/
/*** INCLUDE localconfig.h if it exists, to allow users to make some      ***/
/*** compile time configuration changes.                                  ***/
/****************************************************************************/
/* does a local configuration file exist? */
/* #undef HAVE_LOCALCONFIG_H */
#ifdef HAVE_LOCALCONFIG_H
# include <localconfig.h>
#endif /* HAVE_LOCALCONFIG_H */

#endif /* not _CONFIG_H */

/*
 * Local Variables:
 * mode: c
 * End:
 */

/* End of am-utils-6.x config.h file */
