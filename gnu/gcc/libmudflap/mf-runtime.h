/* Implementation header for mudflap runtime library.
   Mudflap: narrow-pointer bounds-checking by tree rewriting.
   Copyright (C) 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Frank Ch. Eigler <fche@redhat.com>
   and Graydon Hoare <graydon@redhat.com>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combine
executable.)

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

/* Public libmudflap declarations -*- C -*- */

#ifndef MF_RUNTIME_H
#define MF_RUNTIME_H

typedef void *__mf_ptr_t;
typedef unsigned int __mf_uintptr_t __attribute__ ((__mode__ (__pointer__)));
typedef __SIZE_TYPE__ __mf_size_t;

/* Global declarations used by instrumentation.  When _MUDFLAP is
   defined, these have been auto-declared by the compiler and we
   should not declare them again (ideally we *would* declare them
   again, to verify that the compiler's declarations match the
   library's, but the C++ front end has no mechanism for allowing
   the re-definition of a structure type).  */
#ifndef _MUDFLAP
struct __mf_cache { __mf_uintptr_t low; __mf_uintptr_t high; };
extern struct __mf_cache __mf_lookup_cache [];
extern __mf_uintptr_t __mf_lc_mask;
extern unsigned char __mf_lc_shift;
#endif

/* Multithreading support.  */
#ifdef _MUDFLAPTH
/* extern pthread_mutex_t __mf_biglock; */
#ifndef _REENTRANT
#define _REENTRANT
#endif
#ifndef _THREAD_SAFE
#define _THREAD_SAFE
#endif
#endif

/* Codes to describe the type of access to check: __mf_check arg 3 */

#define __MF_CHECK_READ 0
#define __MF_CHECK_WRITE 1


/* Codes to describe a region of memory being registered: __mf_*register arg 3 */

#define __MF_TYPE_NOACCESS 0
#define __MF_TYPE_HEAP 1
#define __MF_TYPE_HEAP_I 2
#define __MF_TYPE_STACK 3
#define __MF_TYPE_STATIC 4
#define __MF_TYPE_GUESS 5


/* The public mudflap API */

#ifdef __cplusplus
extern "C" {
#endif

extern void __mf_check (void *ptr, __mf_size_t sz, int type, const char *location)
       __attribute((nothrow));
extern void __mf_register (void *ptr, __mf_size_t sz, int type, const char *name)
       __attribute((nothrow));
extern void __mf_unregister (void *ptr, __mf_size_t sz, int type)
       __attribute((nothrow));
extern unsigned __mf_watch (void *ptr, __mf_size_t sz);
extern unsigned __mf_unwatch (void *ptr, __mf_size_t sz);
extern void __mf_report ();
extern int __mf_set_options (const char *opts);


/* Redirect some standard library functions to libmudflap.  These are
   done by simple #define rather than linker wrapping, since only
   instrumented modules are meant to be affected.  */

#ifdef _MUDFLAP
#pragma redefine_extname memcpy __mfwrap_memcpy
#pragma redefine_extname memmove __mfwrap_memmove
#pragma redefine_extname memset __mfwrap_memset
#pragma redefine_extname memcmp __mfwrap_memcmp
#pragma redefine_extname memchr __mfwrap_memchr
#pragma redefine_extname memrchr __mfwrap_memrchr
#pragma redefine_extname strcpy __mfwrap_strcpy
#pragma redefine_extname strncpy __mfwrap_strncpy
#pragma redefine_extname strcat __mfwrap_strcat
#pragma redefine_extname strncat __mfwrap_strncat
#pragma redefine_extname strcmp __mfwrap_strcmp
#pragma redefine_extname strcasecmp __mfwrap_strcasecmp
#pragma redefine_extname strncmp __mfwrap_strncmp
#pragma redefine_extname strncasecmp __mfwrap_strncasecmp
#pragma redefine_extname strdup __mfwrap_strdup
#pragma redefine_extname strndup __mfwrap_strndup
#pragma redefine_extname strchr __mfwrap_strchr
#pragma redefine_extname strrchr __mfwrap_strrchr
#pragma redefine_extname strstr __mfwrap_strstr
#pragma redefine_extname memmem __mfwrap_memmem
#pragma redefine_extname strlen __mfwrap_strlen
#pragma redefine_extname strnlen __mfwrap_strnlen
#pragma redefine_extname bzero __mfwrap_bzero
#pragma redefine_extname bcopy __mfwrap_bcopy
#pragma redefine_extname bcmp __mfwrap_bcmp
#pragma redefine_extname index __mfwrap_index
#pragma redefine_extname rindex __mfwrap_rindex
#pragma redefine_extname asctime __mfwrap_asctime
#pragma redefine_extname ctime __mfwrap_ctime
#pragma redefine_extname gmtime __mfwrap_gmtime
#pragma redefine_extname localtime __mfwrap_localtime
#pragma redefine_extname time __mfwrap_time
#pragma redefine_extname strerror __mfwrap_strerror
#pragma redefine_extname fopen __mfwrap_fopen
#pragma redefine_extname fdopen __mfwrap_fdopen
#pragma redefine_extname freopen __mfwrap_freopen
#pragma redefine_extname fclose __mfwrap_fclose
#pragma redefine_extname fread __mfwrap_fread
#pragma redefine_extname fwrite __mfwrap_fwrite
#pragma redefine_extname fgetc __mfwrap_fgetc
#pragma redefine_extname fgets __mfwrap_fgets
#pragma redefine_extname getc __mfwrap_getc
#pragma redefine_extname gets __mfwrap_gets
#pragma redefine_extname ungetc __mfwrap_ungetc
#pragma redefine_extname fputc __mfwrap_fputc
#pragma redefine_extname fputs __mfwrap_fputs
#pragma redefine_extname putc __mfwrap_putc
#pragma redefine_extname puts __mfwrap_puts
#pragma redefine_extname clearerr __mfwrap_clearerr
#pragma redefine_extname feof __mfwrap_feof
#pragma redefine_extname ferror __mfwrap_ferror
#pragma redefine_extname fileno __mfwrap_fileno
#pragma redefine_extname printf __mfwrap_printf
#pragma redefine_extname fprintf __mfwrap_fprintf
#pragma redefine_extname sprintf __mfwrap_sprintf
#pragma redefine_extname snprintf __mfwrap_snprintf
#pragma redefine_extname vprintf __mfwrap_vprintf
#pragma redefine_extname vfprintf __mfwrap_vfprintf
#pragma redefine_extname vsprintf __mfwrap_vsprintf
#pragma redefine_extname vsnprintf __mfwrap_vsnprintf
#pragma redefine_extname access __mfwrap_access
#pragma redefine_extname remove __mfwrap_remove
#pragma redefine_extname fflush __mfwrap_fflush
#pragma redefine_extname fseek __mfwrap_fseek
#pragma redefine_extname ftell __mfwrap_ftell
#pragma redefine_extname rewind __mfwrap_rewind
#pragma redefine_extname fgetpos __mfwrap_fgetpos
#pragma redefine_extname fsetpos __mfwrap_fsetpos
#pragma redefine_extname stat __mfwrap_stat
#pragma redefine_extname fstat __mfwrap_fstat
#pragma redefine_extname lstat __mfwrap_lstat
#pragma redefine_extname mkfifo __mfwrap_mkfifo
#pragma redefine_extname setvbuf __mfwrap_setvbuf
#pragma redefine_extname setbuf __mfwrap_setbuf
#pragma redefine_extname setbuffer __mfwrap_setbuffer
#pragma redefine_extname setlinebuf __mfwrap_setlinebuf
#pragma redefine_extname opendir __mfwrap_opendir
#pragma redefine_extname closedir __mfwrap_closedir
#pragma redefine_extname readdir __mfwrap_readdir
#pragma redefine_extname recv __mfwrap_recv
#pragma redefine_extname recvfrom __mfwrap_recvfrom
#pragma redefine_extname recvmsg __mfwrap_recvmsg
#pragma redefine_extname send __mfwrap_send
#pragma redefine_extname sendto __mfwrap_sendto
#pragma redefine_extname sendmsg __mfwrap_sendmsg
#pragma redefine_extname setsockopt __mfwrap_setsockopt
#pragma redefine_extname getsockopt __mfwrap_getsockopt
#pragma redefine_extname accept __mfwrap_accept
#pragma redefine_extname bind __mfwrap_bind
#pragma redefine_extname connect __mfwrap_connect
#pragma redefine_extname gethostname __mfwrap_gethostname
#pragma redefine_extname sethostname __mfwrap_sethostname
#pragma redefine_extname gethostbyname __mfwrap_gethostbyname
#pragma redefine_extname wait __mfwrap_wait
#pragma redefine_extname waitpid __mfwrap_waitpid
#pragma redefine_extname popen __mfwrap_popen
#pragma redefine_extname pclose __mfwrap_pclose
#pragma redefine_extname execve __mfwrap_execve
#pragma redefine_extname execv __mfwrap_execv
#pragma redefine_extname execvp __mfwrap_execvp
#pragma redefine_extname system __mfwrap_system
#pragma redefine_extname dlopen __mfwrap_dlopen
#pragma redefine_extname dlerror __mfwrap_dlerror
#pragma redefine_extname dlsym __mfwrap_dlsym
#pragma redefine_extname dlclose __mfwrap_dlclose
#pragma redefine_extname fopen64 __mfwrap_fopen64
#pragma redefine_extname freopen64 __mfwrap_freopen64
#pragma redefine_extname stat64 __mfwrap_stat64
#pragma redefine_extname fseeko64 __mfwrap_fseeko64
#pragma redefine_extname ftello64 __mfwrap_ftello64
#pragma redefine_extname semop __mfwrap_semop
#pragma redefine_extname semctl __mfwrap_semctl
#pragma redefine_extname shmctl __mfwrap_shmctl
#pragma redefine_extname shmat __mfwrap_shmat
#pragma redefine_extname shmdt __mfwrap_shmdt
#pragma redefine_extname __ctype_b_loc __mfwrap___ctype_b_loc
#pragma redefine_extname __ctype_toupper_loc __mfwrap___ctype_toupper_loc
#pragma redefine_extname __ctype_tolower_loc __mfwrap___ctype_tolower_loc
#pragma redefine_extname getlogin __mfwrap_getlogin
#pragma redefine_extname cuserid __mfwrap_cuserid
#pragma redefine_extname getpwnam __mfwrap_getpwnam
#pragma redefine_extname getpwuid __mfwrap_getpwuid
#pragma redefine_extname getgrnam __mfwrap_getgrnam
#pragma redefine_extname getgrgid __mfwrap_getgrgid
#pragma redefine_extname getservent __mfwrap_getservent
#pragma redefine_extname getservbyname __mfwrap_getservbyname
#pragma redefine_extname getservbyport __mfwrap_getservbyport
#pragma redefine_extname gai_strerror __mfwrap_gai_strerror
#pragma redefine_extname getmntent __mfwrap_getmntent
#pragma redefine_extname inet_ntoa __mfwrap_inet_ntoa
#pragma redefine_extname getprotoent __mfwrap_getprotoent
#pragma redefine_extname getprotobyname __mfwrap_getprotobyname
#pragma redefine_extname getprotobynumber __mfwrap_getprotobynumber

/* Disable glibc macros.  */
#define __NO_STRING_INLINES

#endif /* _MUDFLAP */


#ifdef __cplusplus
}
#endif

#endif /* MF_RUNTIME_H */
