/* posixstat.h -- Posix stat(2) definitions for systems that
   don't have them. */

/* Copyright (C) 1987,1991 Free Software Foundation, Inc.

   This file is part of GNU Bash, the Bourne Again SHell.

   Bash is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   Bash is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with Bash; see the file COPYING.  If not, write to the Free
   Software Foundation, 59 Temple Place, Suite 330, Boston, MA 02111 USA. */

/* This file should be included instead of <sys/stat.h>.
   It relies on the local sys/stat.h to work though. */
#if !defined (_POSIXSTAT_H_)
#define _POSIXSTAT_H_

#include <sys/stat.h>

#if defined (STAT_MACROS_BROKEN)
#  undef S_ISBLK
#  undef S_ISCHR
#  undef S_ISDIR
#  undef S_ISFIFO
#  undef S_ISREG
#  undef S_ISLNK
#endif /* STAT_MACROS_BROKEN */

/* These are guaranteed to work only on isc386 */
#if !defined (S_IFDIR) && !defined (S_ISDIR)
#  define S_IFDIR 0040000
#endif /* !S_IFDIR && !S_ISDIR */
#if !defined (S_IFMT)
#  define S_IFMT  0170000
#endif /* !S_IFMT */

/* Posix 1003.1 5.6.1.1 <sys/stat.h> file types */

/* Some Posix-wannabe systems define _S_IF* macros instead of S_IF*, but
   do not provide the S_IS* macros that Posix requires. */

#if defined (_S_IFMT) && !defined (S_IFMT)
#define S_IFMT _S_IFMT
#endif
#if defined (_S_IFIFO) && !defined (S_IFIFO)
#define S_IFIFO _S_IFIFO
#endif
#if defined (_S_IFCHR) && !defined (S_IFCHR)
#define S_IFCHR _S_IFCHR
#endif
#if defined (_S_IFDIR) && !defined (S_IFDIR)
#define S_IFDIR _S_IFDIR
#endif
#if defined (_S_IFBLK) && !defined (S_IFBLK)
#define S_IFBLK _S_IFBLK
#endif
#if defined (_S_IFREG) && !defined (S_IFREG)
#define S_IFREG _S_IFREG
#endif
#if defined (_S_IFLNK) && !defined (S_IFLNK)
#define S_IFLNK _S_IFLNK
#endif
#if defined (_S_IFSOCK) && !defined (S_IFSOCK)
#define S_IFSOCK _S_IFSOCK
#endif

/* Test for each symbol individually and define the ones necessary (some
   systems claiming Posix compatibility define some but not all). */

#if defined (S_IFBLK) && !defined (S_ISBLK)
#define	S_ISBLK(m)	(((m)&S_IFMT) == S_IFBLK)	/* block device */
#endif

#if defined (S_IFCHR) && !defined (S_ISCHR)
#define	S_ISCHR(m)	(((m)&S_IFMT) == S_IFCHR)	/* character device */
#endif

#if defined (S_IFDIR) && !defined (S_ISDIR)
#define	S_ISDIR(m)	(((m)&S_IFMT) == S_IFDIR)	/* directory */
#endif

#if defined (S_IFREG) && !defined (S_ISREG)
#define	S_ISREG(m)	(((m)&S_IFMT) == S_IFREG)	/* file */
#endif

#if defined (S_IFIFO) && !defined (S_ISFIFO)
#define	S_ISFIFO(m)	(((m)&S_IFMT) == S_IFIFO)	/* fifo - named pipe */
#endif

#if defined (S_IFLNK) && !defined (S_ISLNK)
#define	S_ISLNK(m)	(((m)&S_IFMT) == S_IFLNK)	/* symbolic link */
#endif

#if defined (S_IFSOCK) && !defined (S_ISSOCK)
#define	S_ISSOCK(m)	(((m)&S_IFMT) == S_IFSOCK)	/* socket */
#endif

/*
 * POSIX 1003.1 5.6.1.2 <sys/stat.h> File Modes
 */

#if !defined (S_IRWXU)
#  if !defined (S_IREAD)
#    define S_IREAD	00400
#    define S_IWRITE	00200
#    define S_IEXEC	00100
#  endif /* S_IREAD */

#  if !defined (S_IRUSR)
#    define S_IRUSR	S_IREAD			/* read, owner */
#    define S_IWUSR	S_IWRITE		/* write, owner */
#    define S_IXUSR	S_IEXEC			/* execute, owner */

#    define S_IRGRP	(S_IREAD  >> 3)		/* read, group */
#    define S_IWGRP	(S_IWRITE >> 3)		/* write, group */
#    define S_IXGRP	(S_IEXEC  >> 3)		/* execute, group */

#    define S_IROTH	(S_IREAD  >> 6)		/* read, other */
#    define S_IWOTH	(S_IWRITE >> 6)		/* write, other */
#    define S_IXOTH	(S_IEXEC  >> 6)		/* execute, other */
#  endif /* !S_IRUSR */

#  define S_IRWXU	(S_IRUSR | S_IWUSR | S_IXUSR)
#  define S_IRWXG	(S_IRGRP | S_IWGRP | S_IXGRP)
#  define S_IRWXO	(S_IROTH | S_IWOTH | S_IXOTH)
#endif /* !S_IRWXU */

/* These are non-standard, but are used in builtins.c$symbolic_umask() */
#define S_IRUGO		(S_IRUSR | S_IRGRP | S_IROTH)
#define S_IWUGO		(S_IWUSR | S_IWGRP | S_IWOTH)
#define S_IXUGO		(S_IXUSR | S_IXGRP | S_IXOTH)

#endif /* _POSIXSTAT_H_ */
