#	$OpenBSD: Makefile.inc,v 1.83 2024/07/14 09:48:48 jca Exp $

# gen sources
.PATH: ${LIBCSRCDIR}/arch/${MACHINE_CPU}/gen ${LIBCSRCDIR}/gen

SRCS+=  alarm.c assert.c auth_subr.c authenticate.c \
        basename.c clock.c clock_getcpuclockid.c \
        closedir.c confstr.c ctermid.c ctype_.c \
        daemon.c devname.c dirfd.c dirname.c disklabel.c elf_aux_info.c \
        err.c errc.c errx.c errlist.c errno.c exec.c \
        fdatasync.c fnmatch.c fpclassify.c frexp.c \
        fstab.c ftok.c fts.c ftw.c getbsize.c getcap.c getcwd.c \
        getdomainname.c getgrent.c getgrouplist.c gethostname.c \
        getloadavg.c getlogin.c getmntinfo.c getnetgrent.c getpagesize.c \
        getprogname.c getpwent.c getttyent.c getusershell.c glob.c \
        initgroups.c isatty.c isctype.c isfdtype.c isfinite.c isinf.c \
        isnan.c isnormal.c signbit.c lockf.c login_cap.c nice.c \
        nlist.c nftw.c opendir.c pause.c popen.c posix_spawn.c psignal.c \
        pw_dup.c pwcache.c raise.c readdir.c readdir_r.c readpassphrase.c \
        rewinddir.c scandir.c seekdir.c setdomainname.c sethostname.c \
        setprogname.c setmode.c setproctitle.c shm_open.c \
        siginterrupt.c siglist.c signal.c signame.c \
        sigsetops.c sigwait.c sleep.c statvfs.c strtofflags.c sysconf.c \
        syslog.c syslog_r.c telldir.c time.c times.c \
        timespec_get.c tolower_.c tree.c ttyname.c \
        ttyslot.c toupper_.c ualarm.c uname.c unvis.c usleep.c \
        utime.c valloc.c vis.c wait.c wait3.c waitpid.c warn.c \
        warnc.c warnx.c vwarn.c vwarnc.c vwarnx.c verr.c verrc.c verrx.c

# machine-dependent gen sources
# m-d Makefile.inc must include sources for:
#	_setjmp() fabs() frexp() infinity ldexp() modf() __nan
#	setjmp() sigsetjmp() __tfork_thread()

.include "${LIBCSRCDIR}/arch/${MACHINE_CPU}/gen/Makefile.inc"

MAN+=	__tfork_thread.3 alarm.3 auth_subr.3 authenticate.3 basename.3 clock.3 \
	clock_getcpuclockid.3 confstr.3 \
	ctermid.3 daemon.3 devname.3 opendir.3 dirname.3 elf_aux_info.3 err.3 \
	execv.3 fabs.3 fnmatch.3 fpclassify.3 fpgetmask.3 frexp.3 ftok.3 fts_open.3 \
	ftw.3 getbsize.3 cgetent.3 getcwd.3 getdomainname.3 getdiskbyname.3 \
	getfsent.3 getgrent.3 getgrouplist.3 gethostname.3 getloadavg.3 \
	getmntinfo.3 getnetgrent.3 getpagesize.3 getpass.3 getprogname.3 \
	getpwent.3 getpwnam.3 \
	getttyent.3 getusershell.3 glob.3 initgroups.3 isalnum.3 isalpha.3 \
	isascii.3 isblank.3 iscntrl.3 isdigit.3 isfdtype.3 isgraph.3 \
	isgreater.3 islower.3 isprint.3 ispunct.3 isspace.3 isupper.3 \
	isxdigit.3 ldexp.3 lockf.3 login_cap.3 modf.3 nice.3 nlist.3 pause.3 \
	popen.3 posix_spawn.3 posix_spawn_file_actions_addopen.3 \
	posix_spawn_file_actions_init.3 posix_spawnattr_getflags.3 \
	posix_spawnattr_getpgroup.3 posix_spawnattr_init.3 \
	psignal.3 pw_dup.3 user_from_uid.3 raise.3 readpassphrase.3 \
	scandir.3 setjmp.3 setmode.3 setproctitle.3 shm_open.3 \
	siginterrupt.3 signal.3 \
	sigaddset.3 sigwait.3 sleep.3 statvfs.3 sysconf.3 strtofflags.3 \
	syslog.3 time.3 times.3 timespec_get.3 toascii.3 tolower.3 toupper.3 \
	ttyname.3 ualarm.3 uname.3 unvis.3 usleep.3 utime.3 valloc.3 vis.3
