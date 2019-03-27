/*-
 *  atrun.c - run jobs queued by at; run with root privileges.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 *  Copyright (C) 1993, 1994 Thomas Koenig
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author(s) may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

/* System Headers */

#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef __FreeBSD__
#include <sys/sysctl.h>
#endif
#include <sys/wait.h>
#include <sys/param.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#ifdef __FreeBSD__
#include <paths.h>
#else
#include <getopt.h>
#endif
#ifdef LOGIN_CAP
#include <login_cap.h>
#endif
#ifdef PAM
#include <security/pam_appl.h>
#include <security/openpam.h>
#endif

/* Local headers */

#include "gloadavg.h"
#define MAIN
#include "privs.h"

/* Macros */

#ifndef ATJOB_DIR
#define ATJOB_DIR "/usr/spool/atjobs/"
#endif

#ifndef ATSPOOL_DIR
#define ATSPOOL_DIR "/usr/spool/atspool/"
#endif

#ifndef LOADAVG_MX
#define LOADAVG_MX 1.5
#endif

/* File scope variables */

static const char * const atrun = "atrun"; /* service name for syslog etc. */
static int debug = 0;

void perr(const char *fmt, ...);
void perrx(const char *fmt, ...);
static void usage(void);

/* Local functions */
static int
write_string(int fd, const char* a)
{
    return write(fd, a, strlen(a));
}

#undef DEBUG_FORK
#ifdef DEBUG_FORK
static pid_t
myfork(void)
{
	pid_t res;
	res = fork();
	if (res == 0)
	    kill(getpid(),SIGSTOP);
	return res;
}

#define fork myfork
#endif

static void
run_file(const char *filename, uid_t uid, gid_t gid)
{
/* Run a file by spawning off a process which redirects I/O,
 * spawns a subshell, then waits for it to complete and sends
 * mail to the user.
 */
    pid_t pid;
    int fd_out, fd_in;
    int queue;
    char mailbuf[MAXLOGNAME], fmt[64];
    char *mailname = NULL;
    FILE *stream;
    int send_mail = 0;
    struct stat buf, lbuf;
    off_t size;
    struct passwd *pentry;
    int fflags;
    long nuid;
    long ngid;
#ifdef PAM
    pam_handle_t *pamh = NULL;
    int pam_err;
    struct pam_conv pamc = {
	.conv = openpam_nullconv,
	.appdata_ptr = NULL
    };
#endif

    PRIV_START

    if (chmod(filename, S_IRUSR) != 0)
    {
	perr("cannot change file permissions");
    }

    PRIV_END

    pid = fork();
    if (pid == -1)
	perr("cannot fork");
    
    else if (pid != 0)
	return;

    /* Let's see who we mail to.  Hopefully, we can read it from
     * the command file; if not, send it to the owner, or, failing that,
     * to root.
     */

    pentry = getpwuid(uid);
    if (pentry == NULL)
	perrx("Userid %lu not found - aborting job %s",
		(unsigned long) uid, filename);

#ifdef PAM
    PRIV_START

    pam_err = pam_start(atrun, pentry->pw_name, &pamc, &pamh);
    if (pam_err != PAM_SUCCESS)
	perrx("cannot start PAM: %s", pam_strerror(pamh, pam_err));

    pam_err = pam_acct_mgmt(pamh, PAM_SILENT);
    /* Expired password shouldn't prevent the job from running. */
    if (pam_err != PAM_SUCCESS && pam_err != PAM_NEW_AUTHTOK_REQD)
	perrx("Account %s (userid %lu) unavailable for job %s: %s",
	    pentry->pw_name, (unsigned long)uid,
	    filename, pam_strerror(pamh, pam_err));

    pam_end(pamh, pam_err);

    PRIV_END
#endif /* PAM */

    PRIV_START

    stream=fopen(filename, "r");

    PRIV_END

    if (stream == NULL)
	perr("cannot open input file %s", filename);

    if ((fd_in = dup(fileno(stream))) <0)
	perr("error duplicating input file descriptor");

    if (fstat(fd_in, &buf) == -1)
	perr("error in fstat of input file descriptor");

    if (lstat(filename, &lbuf) == -1)
	perr("error in fstat of input file");

    if (S_ISLNK(lbuf.st_mode))
	perrx("Symbolic link encountered in job %s - aborting", filename);
 
    if ((lbuf.st_dev != buf.st_dev) || (lbuf.st_ino != buf.st_ino) ||
        (lbuf.st_uid != buf.st_uid) || (lbuf.st_gid != buf.st_gid) ||
        (lbuf.st_size!=buf.st_size))
	perrx("Somebody changed files from under us for job %s - aborting",
		filename);
 
    if (buf.st_nlink > 1)
	perrx("Somebody is trying to run a linked script for job %s", filename);
 
    if ((fflags = fcntl(fd_in, F_GETFD)) <0)
	perr("error in fcntl");

    fcntl(fd_in, F_SETFD, fflags & ~FD_CLOEXEC);

    snprintf(fmt, sizeof(fmt),
	"#!/bin/sh\n# atrun uid=%%ld gid=%%ld\n# mail %%%ds %%d",
                          MAXLOGNAME - 1);

    if (fscanf(stream, fmt, &nuid, &ngid, mailbuf, &send_mail) != 4)
	perrx("File %s is in wrong format - aborting", filename);

    if (mailbuf[0] == '-')
	perrx("Illegal mail name %s in %s", mailbuf, filename);
 
    mailname = mailbuf;

    if (nuid != uid)
	perrx("Job %s - userid %ld does not match file uid %lu",
		filename, nuid, (unsigned long)uid);

    if (ngid != gid)
	perrx("Job %s - groupid %ld does not match file gid %lu",
		filename, ngid, (unsigned long)gid);

    fclose(stream);

    if (chdir(ATSPOOL_DIR) < 0)
	perr("cannot chdir to %s", ATSPOOL_DIR);
    
    /* Create a file to hold the output of the job we are about to run.
     * Write the mail header.
     */    
    if((fd_out=open(filename,
		O_WRONLY | O_CREAT | O_EXCL, S_IWUSR | S_IRUSR)) < 0)
	perr("cannot create output file");

    write_string(fd_out, "Subject: Output from your job ");
    write_string(fd_out, filename);
    write_string(fd_out, "\n\n");
    fstat(fd_out, &buf);
    size = buf.st_size;

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
 
    pid = fork();
    if (pid < 0)
	perr("error in fork");

    else if (pid == 0)
    {
	char *nul = NULL;
	char **nenvp = &nul;

	/* Set up things for the child; we want standard input from the input file,
	 * and standard output and error sent to our output file.
	 */

	if (lseek(fd_in, (off_t) 0, SEEK_SET) < 0)
	    perr("error in lseek");

	if (dup(fd_in) != STDIN_FILENO)
	    perr("error in I/O redirection");

	if (dup(fd_out) != STDOUT_FILENO)
	    perr("error in I/O redirection");

	if (dup(fd_out) != STDERR_FILENO)
	    perr("error in I/O redirection");

	close(fd_in);
	close(fd_out);
	if (chdir(ATJOB_DIR) < 0)
	    perr("cannot chdir to %s", ATJOB_DIR);

	queue = *filename;

	PRIV_START

        nice(tolower(queue) - 'a');
	
#ifdef LOGIN_CAP
	/*
	 * For simplicity and safety, set all aspects of the user context
	 * except for a selected subset:  Don't set priority, which was
	 * set based on the queue file name according to the tradition.
	 * Don't bother to set environment, including path vars, either
	 * because it will be discarded anyway.  Although the job file
	 * should set umask, preset it here just in case.
	 */
	if (setusercontext(NULL, pentry, uid, LOGIN_SETALL &
		~(LOGIN_SETPRIORITY | LOGIN_SETPATH | LOGIN_SETENV)) != 0)
	    exit(EXIT_FAILURE);	/* setusercontext() logged the error */
#else /* LOGIN_CAP */
	if (initgroups(pentry->pw_name,pentry->pw_gid))
	    perr("cannot init group access list");

	if (setgid(gid) < 0 || setegid(pentry->pw_gid) < 0)
	    perr("cannot change group");

	if (setlogin(pentry->pw_name))
	    perr("cannot set login name");

	if (setuid(uid) < 0 || seteuid(uid) < 0)
	    perr("cannot set user id");
#endif /* LOGIN_CAP */

	if (chdir(pentry->pw_dir))
		chdir("/");

	if(execle("/bin/sh","sh",(char *) NULL, nenvp) != 0)
	    perr("exec failed for /bin/sh");

	PRIV_END
    }
    /* We're the parent.  Let's wait.
     */
    close(fd_in);
    close(fd_out);
    waitpid(pid, (int *) NULL, 0);

    /* Send mail.  Unlink the output file first, so it is deleted after
     * the run.
     */
    stat(filename, &buf);
    if (open(filename, O_RDONLY) != STDIN_FILENO)
        perr("open of jobfile failed");

    unlink(filename);
    if ((buf.st_size != size) || send_mail)
    {    
	PRIV_START

#ifdef LOGIN_CAP
	/*
	 * This time set full context to run the mailer.
	 */
	if (setusercontext(NULL, pentry, uid, LOGIN_SETALL) != 0)
	    exit(EXIT_FAILURE);	/* setusercontext() logged the error */
#else /* LOGIN_CAP */
	if (initgroups(pentry->pw_name,pentry->pw_gid))
	    perr("cannot init group access list");

	if (setgid(gid) < 0 || setegid(pentry->pw_gid) < 0)
	    perr("cannot change group");

	if (setlogin(pentry->pw_name))
	    perr("cannot set login name");

	if (setuid(uid) < 0 || seteuid(uid) < 0)
	    perr("cannot set user id");
#endif /* LOGIN_CAP */

	if (chdir(pentry->pw_dir))
		chdir("/");

#ifdef __FreeBSD__
	execl(_PATH_SENDMAIL, "sendmail", "-F", "Atrun Service",
			"-odi", "-oem",
			mailname, (char *) NULL);
#else
        execl(MAIL_CMD, MAIL_CMD, mailname, (char *) NULL);
#endif
	    perr("exec failed for mail command");

	PRIV_END
    }
    exit(EXIT_SUCCESS);
}

/* Global functions */

/* Needed in gloadavg.c */
void
perr(const char *fmt, ...)
{
    const char * const fmtadd = ": %m";
    char nfmt[strlen(fmt) + strlen(fmtadd) + 1];
    va_list ap;

    va_start(ap, fmt);
    if (debug)
    {
	vwarn(fmt, ap);
    }
    else
    {
	snprintf(nfmt, sizeof(nfmt), "%s%s", fmt, fmtadd);
	vsyslog(LOG_ERR, nfmt, ap);
    }
    va_end(ap);

    exit(EXIT_FAILURE);
}

void
perrx(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    if (debug)
	vwarnx(fmt, ap);
    else
	vsyslog(LOG_ERR, fmt, ap);
    va_end(ap);

    exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
/* Browse through  ATJOB_DIR, checking all the jobfiles wether they should
 * be executed and or deleted. The queue is coded into the first byte of
 * the job filename, the date (in minutes since Eon) as a hex number in the
 * following eight bytes, followed by a dot and a serial number.  A file
 * which has not been executed yet is denoted by its execute - bit set.
 * For those files which are to be executed, run_file() is called, which forks
 * off a child which takes care of I/O redirection, forks off another child
 * for execution and yet another one, optionally, for sending mail.
 * Files which already have run are removed during the next invocation.
 */
    DIR *spool;
    struct dirent *dirent;
    struct stat buf;
    unsigned long ctm;
    unsigned long jobno;
    char queue;
    time_t now, run_time;
    char batch_name[] = "Z2345678901234";
    uid_t batch_uid;
    gid_t batch_gid;
    int c;
    int run_batch;
#ifdef __FreeBSD__
    size_t ncpusz;
    double load_avg = -1;
    int ncpu;
#else
    double load_avg = LOADAVG_MX;
#endif

/* We don't need root privileges all the time; running under uid and gid daemon
 * is fine.
 */

    RELINQUISH_PRIVS_ROOT(DAEMON_UID, DAEMON_GID)

    openlog(atrun, LOG_PID, LOG_CRON);

    opterr = 0;
    while((c=getopt(argc, argv, "dl:"))!= -1)
    {
	switch (c)
	{
	case 'l': 
	    if (sscanf(optarg, "%lf", &load_avg) != 1)
		perr("garbled option -l");
#ifndef __FreeBSD__
	    if (load_avg <= 0.)
		load_avg = LOADAVG_MX;
#endif
	    break;

	case 'd':
	    debug ++;
	    break;

	case '?':
	default:
	    usage();
	}
    }

    if (chdir(ATJOB_DIR) != 0)
	perr("cannot change to %s", ATJOB_DIR);

#ifdef __FreeBSD__
    if (load_avg <= 0.) {
	ncpusz = sizeof(size_t);
	if (sysctlbyname("hw.ncpu", &ncpu, &ncpusz, NULL, 0) < 0)
		ncpu = 1;
	load_avg = LOADAVG_MX * ncpu;
    }
#endif

    /* Main loop. Open spool directory for reading and look over all the
     * files in there. If the filename indicates that the job should be run
     * and the x bit is set, fork off a child which sets its user and group
     * id to that of the files and exec a /bin/sh which executes the shell
     * script. Unlink older files if they should no longer be run.  For
     * deletion, their r bit has to be turned on.
     *
     * Also, pick the oldest batch job to run, at most one per invocation of
     * atrun.
     */
    if ((spool = opendir(".")) == NULL)
	perr("cannot read %s", ATJOB_DIR);

    if (flock(dirfd(spool), LOCK_EX) == -1)
	perr("cannot lock %s", ATJOB_DIR);

    now = time(NULL);
    run_batch = 0;
    batch_uid = (uid_t) -1;
    batch_gid = (gid_t) -1;

    while ((dirent = readdir(spool)) != NULL) {
	if (stat(dirent->d_name,&buf) != 0)
	    perr("cannot stat in %s", ATJOB_DIR);

	/* We don't want directories
	 */
	if (!S_ISREG(buf.st_mode)) 
	    continue;

	if (sscanf(dirent->d_name,"%c%5lx%8lx",&queue,&jobno,&ctm) != 3)
	    continue;

	run_time = (time_t) ctm*60;

	if ((S_IXUSR & buf.st_mode) && (run_time <=now)) {
	    if (isupper(queue) && (strcmp(batch_name,dirent->d_name) > 0)) {
		run_batch = 1;
		strlcpy(batch_name, dirent->d_name, sizeof(batch_name));
		batch_uid = buf.st_uid;
		batch_gid = buf.st_gid;
	    }
	
	/* The file is executable and old enough
	 */
	    if (islower(queue))
		run_file(dirent->d_name, buf.st_uid, buf.st_gid);
	}
	/*  Delete older files
	 */
	if ((run_time < now) && !(S_IXUSR & buf.st_mode) && (S_IRUSR & buf.st_mode))
	    unlink(dirent->d_name);
    }
    /* run the single batch file, if any
    */
    if (run_batch && (gloadavg() < load_avg))
	run_file(batch_name, batch_uid, batch_gid);

    if (flock(dirfd(spool), LOCK_UN) == -1)
	perr("cannot unlock %s", ATJOB_DIR);

    if (closedir(spool) == -1)
	perr("cannot closedir %s", ATJOB_DIR);

    closelog();
    exit(EXIT_SUCCESS);
}

static void
usage(void)
{
    if (debug)
	fprintf(stderr, "usage: atrun [-l load_avg] [-d]\n");
    else
	syslog(LOG_ERR, "usage: atrun [-l load_avg] [-d]"); 

    exit(EXIT_FAILURE);
}
