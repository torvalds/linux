/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2007 Tim Kientzle
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <osreldate.h>

/* FreeBSD 5.0 and later has ACL and extattr support. */
#if __FreeBSD__ > 4
#define ARCHIVE_ACL_FREEBSD 1
#define ARCHIVE_XATTR_FREEBSD 1
#define HAVE_ACL_GET_PERM_NP 1
#define HAVE_ARC4RANDOM_BUF 1
#define HAVE_STRUCT_XVFSCONF 1
#define HAVE_SYS_ACL_H 1
#define HAVE_SYS_EXTATTR_H 1
#if __FreeBSD__ > 7
/* FreeBSD 8.0 and later has NFSv4 ACL support */
#define ARCHIVE_ACL_FREEBSD_NFS4 1
#define HAVE_ACL_GET_LINK_NP 1
#define HAVE_ACL_IS_TRIVIAL_NP 1
#define HAVE_ACL_SET_LINK_NP 1
#endif /* __FreeBSD__ > 7 */
#endif /* __FreeBSD__ > 4 */

#ifdef WITH_OPENSSL
#define HAVE_LIBCRYPTO 1
#define HAVE_OPENSSL_EVP_H 1
#define HAVE_OPENSSL_MD5_H 1
#define HAVE_OPENSSL_RIPEMD_H 1
#define HAVE_OPENSSL_SHA_H 1
#define HAVE_OPENSSL_SHA256_INIT 1
#define HAVE_OPENSSL_SHA384_INIT 1
#define HAVE_OPENSSL_SHA512_INIT 1
#define HAVE_PKCS5_PBKDF2_HMAC_SHA1 1
#define HAVE_SHA256 1
#define HAVE_SHA384 1
#define HAVE_SHA512 1
#else
#define HAVE_LIBMD 1
#define HAVE_MD5_H 1
#define HAVE_MD5INIT 1
#define HAVE_RIPEMD_H 1
#define HAVE_SHA_H 1
#define HAVE_SHA1 1
#define HAVE_SHA1_INIT 1
#define HAVE_SHA256 1
#define HAVE_SHA256_H 1
#define HAVE_SHA256_INIT 1
#define HAVE_SHA512 1
#define HAVE_SHA512_H 1
#define HAVE_SHA512_INIT 1
#endif

#define HAVE_BSDXML_H 1
#define HAVE_BZLIB_H 1
#define HAVE_CHFLAGS 1
#define HAVE_CHOWN 1
#define HAVE_CHROOT 1
#define HAVE_CTIME_R 1
#define HAVE_CTYPE_H 1
#define HAVE_DECL_EXTATTR_NAMESPACE_USER 1
#define HAVE_DECL_INT32_MAX 1
#define HAVE_DECL_INT32_MIN 1
#define HAVE_DECL_INT64_MAX 1
#define HAVE_DECL_INT64_MIN 1
#define HAVE_DECL_INTMAX_MAX 1
#define HAVE_DECL_INTMAX_MIN 1
#define HAVE_DECL_SIZE_MAX 1
#define HAVE_DECL_SSIZE_MAX 1
#define HAVE_DECL_STRERROR_R 1
#define HAVE_DECL_UINT32_MAX 1
#define HAVE_DECL_UINT64_MAX 1
#define HAVE_DECL_UINTMAX_MAX 1
#define HAVE_DIRENT_H 1
#define HAVE_DLFCN_H 1
#define HAVE_D_MD_ORDER 1
#define HAVE_EFTYPE 1
#define HAVE_EILSEQ 1
#define HAVE_ERRNO_H 1
#define HAVE_FCHDIR 1
#define HAVE_FCHFLAGS 1
#define HAVE_FCHMOD 1
#define HAVE_FCHOWN 1
#define HAVE_FCNTL 1
#define HAVE_FCNTL_H 1
#define HAVE_FDOPENDIR 1
#define HAVE_FORK 1
#define HAVE_FSEEKO 1
#define HAVE_FSTAT 1
#define HAVE_FSTATAT 1
#define HAVE_FSTATFS 1
#define HAVE_FSTATVFS 1
#define HAVE_FTRUNCATE 1
#define HAVE_FUTIMES 1
#define HAVE_FUTIMESAT 1
#define HAVE_GETEUID 1
#define HAVE_GETGRGID_R 1
#define HAVE_GETGRNAM_R 1
#define HAVE_GETPID 1
#define HAVE_GETPWNAM_R 1
#define HAVE_GETPWUID_R 1
#define HAVE_GETVFSBYNAME 1
#define HAVE_GMTIME_R 1
#define HAVE_GRP_H 1
#define HAVE_INTMAX_T 1
#define HAVE_INTTYPES_H 1
#define HAVE_LANGINFO_H 1
#define HAVE_LCHFLAGS 1
#define HAVE_LCHMOD 1
#define HAVE_LCHOWN 1
#define HAVE_LIBZ 1
#define HAVE_LIMITS_H 1
#define HAVE_LINK 1
#define HAVE_LOCALE_H 1
#define HAVE_LOCALTIME_R 1
#define HAVE_LONG_LONG_INT 1
#define HAVE_LSTAT 1
#define HAVE_LUTIMES 1
#define HAVE_MBRTOWC 1
#define HAVE_MEMMOVE 1
#define HAVE_MEMORY_H 1
#define HAVE_MEMSET 1
#define HAVE_MKDIR 1
#define HAVE_MKFIFO 1
#define HAVE_MKNOD 1
#define HAVE_MKSTEMP 1
#define HAVE_NL_LANGINFO 1
#define HAVE_OPENAT 1
#define HAVE_PATHS_H 1
#define HAVE_PIPE 1
#define HAVE_POLL 1
#define HAVE_POLL_H 1
#define HAVE_POSIX_SPAWNP 1
#define HAVE_PTHREAD_H 1
#define HAVE_PWD_H 1
#define HAVE_READDIR_R 1
#define HAVE_READLINK 1
#define HAVE_READLINKAT 1
#define HAVE_READPASSPHRASE 1
#define HAVE_READPASSPHRASE_H 1
#define HAVE_REGEX_H 1
#define HAVE_SELECT 1
#define HAVE_SETENV 1
#define HAVE_SETLOCALE 1
#define HAVE_SIGACTION 1
#define HAVE_SIGNAL_H 1
#define HAVE_SPAWN_H 1
#define HAVE_STATFS 1
#define HAVE_STATVFS 1
#define HAVE_STDARG_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRCHR 1
#define HAVE_STRDUP 1
#define HAVE_STRERROR 1
#define HAVE_STRERROR_R 1
#define HAVE_STRFTIME 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_STRRCHR 1
#define HAVE_STRUCT_STATFS_F_NAMEMAX 1
#define HAVE_STRUCT_STAT_ST_BIRTHTIME 1
#define HAVE_STRUCT_STAT_ST_BIRTHTIMESPEC_TV_NSEC 1
#define HAVE_STRUCT_STAT_ST_BLKSIZE 1
#define HAVE_STRUCT_STAT_ST_FLAGS 1
#define HAVE_STRUCT_STAT_ST_MTIMESPEC_TV_NSEC 1
#define HAVE_STRUCT_STAT_ST_MTIM_TV_NSEC 1
#define HAVE_STRUCT_TM_TM_GMTOFF 1
#define HAVE_SYMLINK 1
#define HAVE_SYS_CDEFS_H 1
#define HAVE_SYS_IOCTL_H 1
#define HAVE_SYS_MOUNT_H 1
#define HAVE_SYS_PARAM_H 1
#define HAVE_SYS_POLL_H 1
#define HAVE_SYS_SELECT_H 1
#define HAVE_SYS_STATVFS_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_UTSNAME_H 1
#define HAVE_SYS_WAIT_H 1
#define HAVE_TIMEGM 1
#define HAVE_TIME_H 1
#define HAVE_TZSET 1
#define HAVE_UINTMAX_T 1
#define HAVE_UNISTD_H 1
#define HAVE_UNSETENV 1
#define HAVE_UNSIGNED_LONG_LONG 1
#define HAVE_UNSIGNED_LONG_LONG_INT 1
#define HAVE_UTIME 1
#define HAVE_UTIMES 1
#define HAVE_UTIME_H 1
#define HAVE_VFORK 1
#define HAVE_VPRINTF 1
#define HAVE_WCHAR_H 1
#define HAVE_WCHAR_T 1
#define HAVE_WCRTOMB 1
#define HAVE_WCSCMP 1
#define HAVE_WCSCPY 1
#define HAVE_WCSLEN 1
#define HAVE_WCTOMB 1
#define HAVE_WCTYPE_H 1
#define HAVE_WMEMCMP 1
#define HAVE_WMEMCPY 1
#define HAVE_WMEMMOVE 1
#define HAVE_ZLIB_H 1
#define TIME_WITH_SYS_TIME 1

#if __FreeBSD_version >= 1100056
#define HAVE_FUTIMENS 1
#define HAVE_UTIMENSAT 1
#endif

/* FreeBSD 4 and earlier lack intmax_t/uintmax_t */
#if __FreeBSD__ < 5
#define intmax_t int64_t
#define uintmax_t uint64_t
#endif

/* FreeBSD defines for archive_hash.h */
#ifdef WITH_OPENSSL
#define ARCHIVE_CRYPTO_MD5_OPENSSL 1
#define ARCHIVE_CRYPTO_RMD160_OPENSSL 1
#define ARCHIVE_CRYPTO_SHA1_OPENSSL
#define ARCHIVE_CRYPTO_SHA256_OPENSSL 1
#define ARCHIVE_CRYPTO_SHA384_OPENSSL 1
#define ARCHIVE_CRYPTO_SHA512_OPENSSL 1
#else
#define ARCHIVE_CRYPTO_MD5_LIBMD 1
#define ARCHIVE_CRYPTO_SHA1_LIBMD 1
#define ARCHIVE_CRYPTO_SHA256_LIBMD 1
#define ARCHIVE_CRYPTO_SHA512_LIBMD 1
#endif
