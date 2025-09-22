/*	$OpenBSD: unistd.h,v 1.14 2024/05/18 05:20:22 guenther Exp $	*/
/*
 * Copyright (c) 2015 Philip Guenther <guenther@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _LIBC_UNISTD_H_
#define	_LIBC_UNISTD_H_

#include_next <unistd.h>

__BEGIN_HIDDEN_DECLS
/* shared between getpagesize(), sysconf(), and _csu_finish() */
extern int	_pagesize;
__END_HIDDEN_DECLS

/* the real syscall behind getcwd(3) and getwd(3) */
int __getcwd(char *buf, size_t len);

PROTO_NORMAL(__getcwd);
PROTO_NORMAL(__tfork_thread);
PROTO_NORMAL(_exit);
PROTO_NORMAL(access);
PROTO_NORMAL(acct);
PROTO_DEPRECATED(alarm);
PROTO_DEPRECATED(brk);
PROTO_NORMAL(chdir);
PROTO_NORMAL(chown);
PROTO_NORMAL(chroot);
PROTO_CANCEL(close);
PROTO_CANCEL(closefrom);
PROTO_DEPRECATED(confstr);
PROTO_NORMAL(crypt);
PROTO_NORMAL(crypt_checkpass);
PROTO_NORMAL(crypt_newhash);
PROTO_NORMAL(dup);
PROTO_NORMAL(dup2);
PROTO_NORMAL(dup3);
PROTO_DEPRECATED(endusershell);
PROTO_NORMAL(execl);
PROTO_DEPRECATED(execle);
PROTO_DEPRECATED(execlp);
PROTO_DEPRECATED(execv);
PROTO_NORMAL(execve);
PROTO_NORMAL(execvp);
PROTO_NORMAL(execvpe);
PROTO_NORMAL(faccessat);
PROTO_NORMAL(fchdir);
PROTO_NORMAL(fchown);
PROTO_NORMAL(fchownat);
/*PROTO_CANCEL(fdatasync);*/
PROTO_DEPRECATED(fflagstostr);
PROTO_WRAP(fork);
PROTO_NORMAL(fpathconf);
PROTO_CANCEL(fsync);
PROTO_NORMAL(ftruncate);
PROTO_NORMAL(getcwd);
PROTO_NORMAL(getdomainname);
PROTO_NORMAL(getdtablecount);
PROTO_DEPRECATED(getdtablesize);
PROTO_NORMAL(getegid);
PROTO_NORMAL(getentropy);
PROTO_NORMAL(geteuid);
PROTO_NORMAL(getgid);
PROTO_NORMAL(getgrouplist);
PROTO_NORMAL(getgroups);
PROTO_DEPRECATED(gethostid);
PROTO_NORMAL(gethostname);
PROTO_DEPRECATED(getlogin);
PROTO_NORMAL(getlogin_r);
PROTO_DEPRECATED(getmode);
PROTO_DEPRECATED(getopt);
PROTO_NORMAL(getpagesize);
PROTO_DEPRECATED(getpass);
PROTO_NORMAL(getpgid);
PROTO_NORMAL(getpgrp);
PROTO_NORMAL(getpid);
PROTO_NORMAL(getppid);
PROTO_NORMAL(getresgid);
PROTO_NORMAL(getresuid);
PROTO_NORMAL(getsid);
PROTO_NORMAL(getthrid);
PROTO_NORMAL(getthrname);
PROTO_NORMAL(getuid);
PROTO_DEPRECATED(getusershell);
PROTO_DEPRECATED(getwd);
PROTO_NORMAL(initgroups);
PROTO_NORMAL(isatty);
PROTO_NORMAL(issetugid);
PROTO_NORMAL(lchown);
PROTO_NORMAL(link);
PROTO_NORMAL(linkat);
/*PROTO_CANCEL(lockf);*/
PROTO_NORMAL(lseek);
/*PROTO_DEPRECATED(mkdtemp);		use declaration from stdlib.h */
PROTO_NORMAL(mkstemp);
/*PROTO_DEPRECATED(mkstemps);		use declaration from stdlib.h */
/*PROTO_DEPRECATED(mktemp);		use declaration from stdlib.h */
PROTO_NORMAL(nfssvc);
PROTO_DEPRECATED(nice);
PROTO_NORMAL(pathconf);
PROTO_NORMAL(pathconfat);
/*PROTO_CANCEL(pause);*/
PROTO_NORMAL(pipe);
PROTO_NORMAL(pipe2);
PROTO_NORMAL(pledge);
PROTO_CANCEL(pread);
PROTO_NORMAL(profil);
PROTO_CANCEL(pwrite);
PROTO_NORMAL(quotactl);
PROTO_DEPRECATED(rcmd);
PROTO_NORMAL(rcmd_af);
PROTO_NORMAL(rcmdsh);
PROTO_CANCEL(read);
PROTO_NORMAL(readlink);
PROTO_NORMAL(readlinkat);
PROTO_NORMAL(reboot);
PROTO_NORMAL(revoke);
PROTO_NORMAL(rmdir);
PROTO_DEPRECATED(rresvport);
PROTO_NORMAL(rresvport_af);
PROTO_DEPRECATED(ruserok);
PROTO_DEPRECATED(sbrk);
PROTO_DEPRECATED(setdomainname);
PROTO_NORMAL(setegid);
PROTO_NORMAL(seteuid);
PROTO_NORMAL(setgid);
PROTO_NORMAL(setgroups);
PROTO_DEPRECATED(sethostid);
PROTO_DEPRECATED(sethostname);
PROTO_NORMAL(setlogin);
PROTO_DEPRECATED(setmode);
PROTO_NORMAL(setpgid);
PROTO_DEPRECATED(setpgrp);
PROTO_NORMAL(setregid);
PROTO_NORMAL(setresgid);
PROTO_NORMAL(setresuid);
PROTO_NORMAL(setreuid);
PROTO_NORMAL(setsid);
PROTO_NORMAL(setthrname);
PROTO_NORMAL(setuid);
PROTO_DEPRECATED(setusershell);
/*PROTO_CANCEL(sleep);*/
PROTO_DEPRECATED(strtofflags);
PROTO_DEPRECATED(swab);
PROTO_NORMAL(swapctl);
PROTO_NORMAL(symlink);
PROTO_NORMAL(symlinkat);
PROTO_NORMAL(sync);
PROTO_NORMAL(sysconf);
PROTO_DEPRECATED(tcgetpgrp);
PROTO_DEPRECATED(tcsetpgrp);
PROTO_NORMAL(truncate);
PROTO_NORMAL(ttyname);
PROTO_NORMAL(ttyname_r);
PROTO_DEPRECATED(ualarm);
PROTO_NORMAL(unlink);
PROTO_NORMAL(unlinkat);
PROTO_NORMAL(unveil);
PROTO_DEPRECATED(usleep);
PROTO_WRAP(vfork);
PROTO_CANCEL(write);

#endif /* !_LIBC_UNISTD_H_ */
