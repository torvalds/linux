/*-
 * Copyright (c) 1998 Andrzej Bialecki
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * A primitive version of init(8) with simplistic user interface
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <ctype.h>
#include <err.h>

#ifdef USE_HISTORY
#error "Not yet. But it's quite simple to add - patches are welcome!"
#endif

#include <errno.h>
#include <fcntl.h>
#include <libutil.h>
#include <paths.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <varargs.h>

#define BUFSIZE 1024
#define MAX_CONS 12

#define	NONE	0
#define	SINGLE	1
#define	MULTI	2
#define	DEATH	3

#define	FALSE	0
#define TRUE	1

char cwd[BUFSIZE];
char vty[]="0123456789abcdef";
char *progname;
char *motd=NULL;
int ncons=MAX_CONS;
int Reboot=FALSE;
int transition=MULTI;
int prevtrans=SINGLE;
jmp_buf machine;

char *trans[]={ "NONE", "SINGLE", "MULTI", "DEATH" };

extern char **environ;

/* Struct for holding session state */
struct sess {
	char tty[16];	/* vty device path */
	pid_t pid;	/* pid of process running on it */
	int (*func)(int argc, char **argv);
			/* internal function to run on it (after forking) */
} ttys[MAX_CONS];

/* Struct for built-in command */
struct command {
	char *cmd;		/* command name */
	char *descr;		/* command description */
	char *usage;		/* usage */
	char *example;		/* example of usage */
	int (*func)(char *);	/* callback function */
};

/* Prototypes */
int cd(char *);
int pwd(char *);
int echo(char *);
int xit(char *);
int set(char *);
int unset(char *);
int env(char *);
int help(char *);
int sourcer(char *);
void do_command(int shell, char *cmdline);
void transition_handler(int);

/* Table of built-in functions */
struct command bltins[]={
	{"cd","Change working directory","cd [dir]","cd /etc",cd},
	{"pwd","Print current directory","pwd","pwd",pwd},
	{"exit","Exit from shell()","exit","exit",xit},
	{"set","Set environment variable","set [VAR=value]","set TERM=xterm",set},
	{"unset","Unset environment variable","unset VAR","unset EDITOR",unset},
	{"echo","Echo arguments on stdout","echo arg1 arg2 ...","echo Hello World!",echo},
	{"env","Print all environment variables","env","env",env},
	{".","Source-in a file with commands",". filename",". /etc/rc",sourcer},
	{"?","Print this help :-)","? [command]","? set",help},
	{NULL,NULL,NULL,NULL,NULL}
};

/*
 * Built-in 'cd <path>' handler
 */
int
cd(char *path)
{
	if(chdir(path)) return(-1);
	getcwd(cwd,BUFSIZE);
	return(0);
}

/*
 * Built-in 'pwd' handler
 */
int
pwd(char *dummy)
{

	if(getcwd(cwd,BUFSIZE)==NULL) return(-1);
	printf("%s\n",cwd);
	return(0);
}

/*
 * Built-in 'exit' handler
 */
int
xit(char *dummy)
{
	_exit(0);
}

/*
 * Built-in 'echo' handler
 */
int
echo(char *args)
{
	int i=0,j;
	int len;
	char c;
	int s_quote=0,d_quote=0;
	int sep=0,no_lf=0;

	if(args==NULL) {
		printf("\n");
		return;
	}
	len=strlen(args);
	if(len>=2) {
		if(args[0]=='-' && args[1]=='n') {
			no_lf++;
			i=2;
			while(i<len && (args[i]==' ' || args[i]=='\t')) i++;
		}
	}
	while(i<len) {
		c=args[i];
		switch(c) {
		case ' ':
		case '\t':
			if(s_quote||d_quote) {
				putchar(c);
			} else if(!sep) {
				putchar(' ');
				sep=1;
			}
			break;
		case '\\':
			i++;
			c=args[i];
			switch(c) {
			case 'n':
				putchar('\n');
				break;
			case 'b':
				putchar('\b');
				break;
			case 't':
				putchar('\t');
				break;
			case 'r':
				putchar('\r');
				break;
			default:
				putchar(c);
				break;
			}
			break;
		case '"':
			if(!d_quote) {
				d_quote=1;
				for(j=i+1;j<len;j++) {
					if(args[j]=='\\') {
						j++;
						continue;
					}
					if(args[j]=='"') {
						d_quote=2;
						break;
					}
				}
				if(d_quote!=2) {
					printf("\necho(): unmatched \"\n");
					return;
				}
			} else d_quote=0;
			break;
		case '\'':
			if(!s_quote) {
				s_quote=1;
				for(j=i+1;j<len;j++) {
					if(args[j]=='\\') {
						j++;
						continue;
					}
					if(args[j]=='\'') {
						s_quote=2;
						break;
					}
				}
				if(s_quote!=2) {
					printf("\necho(): unmatched '\n");
					return;
				}
			} else s_quote=0;
			break;
		case '`':
			printf("echo(): backquote not implemented yet!\n");
			break;
		default:
			sep=0;
			putchar(c);
			break;
		}
		i++;
	}
	if(!no_lf) putchar('\n');
	fflush(stdout);
}

/*
 * Built-in 'set VAR=value' handler
 */
int
set(char *var)
{
	int res;

	if(var==NULL) return(env(NULL));
	res=putenv(var);
	if(res) printf("set: %s\n",strerror(errno));
	return(res);
}

/*
 * Built-in 'env' handler
 */
int
env(char *dummy)
{
	char **e;

	e=environ;
	while(*e!=NULL) {
		printf("%s\n",*e++);
	}
	return(0);
}

/*
 * Built-in 'unset VAR' handler
 */
int
unset(char *var)
{
	if(var==NULL) {
		printf("%s: parameter required.\n",progname);
		return(-1);
	}
	return(unsetenv(var));
}

/*
 * Built-in '?' handler
 */
int
help(char *cmd)
{
	struct command *x;
	int found=0;

	if(cmd==NULL) {
		printf("\nBuilt-in commands:\n");
		printf("-------------------\n");
		x=bltins;
		while(x->cmd!=NULL) {
			printf("%s\t%s\n",x->cmd,x->descr);
			x++;
		}
		printf("\nEnter '? <cmd>' for details.\n\n");
		return(0);
	} else {
		x=bltins;
		while(x->cmd!=NULL) {
			if(strcmp(x->cmd,cmd)==0) {
				found++;
				break;
			}
			x++;
		}
		if(found) {
			printf("\n%s\t%s:\n",x->cmd,x->descr);
			printf("\tUsage:\n\t\t%s\n",x->usage);
			printf("\te.g:\n\t\t%s\n\n",x->example);
			return(0);
		} else {
			printf("\n%s: no such command.\n\n",cmd);
			return(-1);
		}
	}
}

/*
 * Signal handler for shell()
 */
void
shell_sig(int sig)
{
	switch(sig) {
	case SIGINT:
	case SIGQUIT:
	case SIGTERM:
		/* ignore ? */
		break;
	default:
		break;
	}
}

/*
 * Built-in '.' handler (read-in and execute commands from file)
 */
int
sourcer(char *fname)
{
	FILE *fd;
	char buf[512],*tok,*arg,**av;
	int ac,len,f,res,i;
	pid_t pid;
	char *sep=" \t";

	fd=fopen(fname,"r");
	if(fd==NULL) {
		printf("Couldn't open file '%s'\n",fname);
		return(-1);
	}
	while(!feof(fd)) {
		memset(buf,0,512);
		if(fgets(buf,512,fd)==NULL) continue;
		if((*buf=='#') || (*buf=='\n')) continue;
		len=strlen(buf);
		buf[len-1]='\0';
		if(strncmp(buf,"ncons",5)==0) {
			tok=strtok(buf,sep);
			tok=strtok(NULL,sep);
			ncons=atoi(tok);
			if((ncons<1)||(ncons>MAX_CONS)) {
				syslog(LOG_EMERG,"%s: bad ncons value; defaulting to %d\n",fname,MAX_CONS);
				ncons=MAX_CONS;
			}
			continue;
		} else if(strncmp(buf,"motd",4)==0) {
			tok=strtok(buf,sep);
			motd=strdup(strtok(NULL,sep));
			continue;
		} else {
			do_command(0,buf);
		}
		/* Next command, please. */
	}
	fclose(fd);
	syslog(LOG_EMERG,"Done with %s",fname);
}

void
do_command(int shell, char *cmdline)
{
	char *tok,*c,*sep=" \t";
	char **av;
	struct command *x;
	int found,len;
	int ac,i,f,res;
	int bg=0;
	pid_t pid;

	len=strlen(cmdline);
	if(cmdline[len-1]=='&') {
		bg++;
		cmdline[len-1]='\0';
		len--;
	} else bg=0;
	tok=strtok(cmdline,sep);
	x=bltins;
	found=0;
	while(x->cmd!=NULL) {
		if(strcmp(x->cmd,tok)==0) {
			found++;
			break;
		}
		x++;
	}
	if(found) {
		tok=cmdline+strlen(x->cmd)+1;
		while(*tok && isblank(*tok) && (tok<(cmdline+len))) tok++;
		if(*tok==NULL) tok=NULL;
		x->func(tok);
		return;
	}
	ac=0;
	av=(char **)calloc(((len+1)/2+1),sizeof(char *));
	av[ac++]=tok;
	while((av[ac++]=strtok(NULL,sep))!=NULL)
		continue;
	switch((pid=fork())) {
	case 0:
		if(shell) {
			signal(SIGINT,SIG_DFL);
			signal(SIGQUIT,SIG_DFL);
			signal(SIGTERM,SIG_DFL);
			signal(SIGHUP,SIG_DFL);
		} else {
			close(0);
			close(1);
			close(2);
			f=open(_PATH_CONSOLE,O_RDWR);
			dup2(f,0);
			dup2(f,1);
			dup2(f,2);
			if(f>2) close(f);
		}
		if(bg) {
			if(daemon(0,0)) {
				printf("do_command(%s): failed to run bg: %s\n",
				av[0],strerror(errno));
				_exit(100);
			}
		}
		execvp(av[0],av);
		/* Something went wrong... */
		printf("do_command(%s): %s\n",av[0],strerror(errno));
		_exit(100);
		break;
	case -1:
		printf("do_command(): %s\n",strerror(errno));
		break;
	default:
		while(waitpid(pid,&res,0)!=pid) continue;
		if(WEXITSTATUS(res)) {
			printf("do_command(%s): exit code=%d\n",
				av[0],WEXITSTATUS(res));
		}
		break;
	}
	free(av);
	return;
}

/*
 * This is the user interface. This routine gets executed on each
 * virtual console serviced by init.
 *
 * It works as normal shell does - for each external command it forks
 * and execs, for each internal command just executes a function.
 */

int
shell(int argc, char **argv)
{
	char buf[BUFSIZE];
	char *prompt=" # ";
	int fd;
	int res;
	pid_t mypid;

	if(motd!=NULL) {
		if((fd=open(motd,O_RDONLY))!=-1) {
			do {
				res=read(fd,buf,BUFSIZE);
				res=write(1,buf,res);
			} while(res>0);
			close(fd);
		}
	}

	printf("\n\n+=========================================================+\n");
	printf("| Built-in shell() (enter '?' for short help on commands) |\n");
	printf("+=========================================================+\n\n");
	getcwd(cwd,BUFSIZE);
	mypid=getpid();
	signal(SIGINT,shell_sig);
	signal(SIGQUIT,shell_sig);
	signal(SIGTERM,shell_sig);
	while(!feof(stdin)) {
		memset(buf,0,BUFSIZE);
		printf("(%d)%s%s",mypid,cwd,prompt);
		fflush(stdout);
		if(fgets(buf,BUFSIZE-1,stdin)==NULL) continue;
		buf[strlen(buf)-1]='\0';
		if(strlen(buf)==0) continue;
		do_command(1,buf);
	}
	return(0);
}

/*
 * Stub for executing some external program on a console. This is called
 * from previously forked copy of our process, so that exec is ok.
 */
int
external_cmd(int argc, char **argv)
{
	execvp(argv[0],argv);
}

/*
 * Acquire vty and properly attach ourselves to it.
 * Also, build basic environment for running user interface.
 */

int
start_session(int vty, int argc, char **argv)
{
	int fd;
	char *t;

	close(0);
	close(1);
	close(2);
	revoke(ttys[vty].tty);
	fd=open(ttys[vty].tty,O_RDWR);
	dup2(fd,0);
	dup2(fd,1);
	dup2(fd,2);
	if(fd>2) close(fd);
	login_tty(fd);
	setpgid(0,getpid());
	putenv("TERM=xterm");
	putenv("HOME=/");
	putenv("PATH=/stand:/bin:/usr/bin:/sbin:.");
	signal(SIGHUP,SIG_DFL);
	signal(SIGINT,SIG_DFL);
	signal(SIGQUIT,SIG_DFL);
	signal(SIGTERM,SIG_DFL);
	chdir("/");
	t=(char *)(rindex(ttys[vty].tty,'/')+1);
	printf("\n\n\nStarting session on %s.\n",t);
	ttys[vty].func(argc,argv);
	_exit(0);
}

/*
 * Execute system startup script /etc/rc
 *
 * (Of course if you don't like it - I don't - you can run anything you
 * want here. Perhaps it would be useful just to read some config DB and
 * do these things ourselves, avoiding forking lots of shells and scripts.)
 */

/* If OINIT_RC is defined, oinit will use it's own configuration file, 
 * /etc/oinit.rc. It's format is described below. Otherwise, it will use
 * normal /etc/rc interpreted by Bourne shell.
 */
#ifndef OINIT_RC
#ifndef SH_NAME
#define SH_NAME	"-sh"
#endif
#ifndef SH_PATH
#define SH_PATH	_PATH_BSHELL
#endif
#ifndef SH_ARG
#define SH_ARG	"/etc/rc"
#endif
void
runcom()
{
	char *argv[3];
	pid_t pid;
	int st;
	int fd;

	if((pid=fork())==0) {
		/* child */
		close(0);
		close(1);
		close(2);
		fd=open(_PATH_CONSOLE,O_RDWR);
		dup2(fd,0);
		dup2(fd,1);
		dup2(fd,2);
		if(fd>2) close(fd);
		argv[0]=SH_NAME;
		argv[1]=SH_ARG;
		argv[2]=0;
		execvp(SH_PATH,argv);
		printf("runcom(): %s\n",strerror(errno));
		_exit(1);
	}
	/* Wait for child to exit */
	while(pid!=waitpid(pid,(int *)0,0)) continue;
	return;
}
#else
/* Alternative /etc/rc - default is /etc/oinit.rc. Its format is as follows:
 * - each empty line or line beginning with a '#' is discarded
 * - any other line must contain a keyword, or a (nonblocking) command to run.
 *
 * Thus far, the following keywords are defined:
 * ncons <number>	number of virtual consoles to open
 * motd <pathname>	full path to motd file
 *
 * Examples of commands to run:
 *
 * ifconfig lo0 inet 127.0.0.1 netmask 255.0.0.0
 * ifconfig ed0 inet 148.81.168.10 netmask 255.255.255.0
 * kbdcontrol -l /usr/share/syscons/my_map.kbd
 */
void
runcom(char *fname)
{
	int fd;

	close(0);
	close(1);
	close(2);
	fd=open(_PATH_CONSOLE,O_RDWR);
	dup2(fd,0);
	dup2(fd,1);
	dup2(fd,2);
	if(fd>2) close(fd);
	sourcer(fname);
}
#endif

int
run_multi()
{
	int i,j;
	pid_t pid;
	int found;

	/* Run /etc/rc if not in single user */
#ifndef OINIT_RC
	if(prevtrans==SINGLE) runcom();
#else
	if(prevtrans==SINGLE) runcom(OINIT_RC);
#endif
	if(transition!=MULTI) return(-1);

	syslog(LOG_EMERG,"*** Starting multi-user mode ***");

	/* Fork shell interface for each console */
	for(i=0;i<ncons;i++) {
		if(ttys[i].pid==0) {
			switch(pid=fork()) {
			case 0:
				start_session(i,0,NULL);
				break;
			case -1:
				printf("%s: %s\n",progname,strerror(errno));
				break;
			default:
				ttys[i].pid=pid;
				break;
			}
		}
	}
	/* Initialize any other services we'll use - most probably this will
	 * be a 'telnet' service (some day...).
	 */
	/* */

	/* Emulate multi-user */
	while(transition==MULTI) {
		/* XXX Modify this to allow for checking for the input on
		 * XXX listening sockets, and forking a 'telnet' service.
		 */
		/* */

		/* Wait for any process to exit */
		pid=waitpid(-1,(int *)0,0);
		if(pid==-1) continue;
		found=0;
		j=-1;
		/* search if it's one of our sessions */
		for(i=0;i<ncons;i++) {
			if(ttys[i].pid==pid) {
				found++;
				j=i;
				ttys[j].pid=0;
				break;
			}
		}
		if(!found) {
			/* Just collect the process's status */
			continue;
		} else {
			/* restart shell() on a console, if it died */
			if(transition!=MULTI) return(0);
			switch(pid=fork()) {
			case 0:
				sleep(1);
				start_session(j,0,NULL);
				break;
			case -1:
				printf("%s: %s\n",progname,strerror(errno));
				break;
			default:
				ttys[j].pid=pid;
				break;
			}
		}
	}
}

int clang;

void
kill_timer(int sig)
{
	clang=1;
}

kill_ttys()
{
}

/*
 * Start a shell on ttyv0 (i.e. the console).
 */

int
run_single()
{
	int i;
	pid_t pid,wpid;
	static int sigs[2]={SIGTERM,SIGKILL};

	syslog(LOG_EMERG,"*** Starting single-user mode ***");
	/* Kill all existing sessions */
	syslog(LOG_EMERG,"Killing all existing sessions...");
	for(i=0;i<MAX_CONS;i++) {
		kill(ttys[i].pid,SIGHUP);
		ttys[i].pid=0;
	}
	for(i=0;i<2;i++) {
		if(kill(-1,sigs[i])==-1 && errno==ESRCH) break;
		clang=0;
		alarm(10);
		do {
			pid=waitpid(-1,(int *)0,WUNTRACED);
			if(errno==EINTR) continue;
			else break;
		} while (clang==0);
	}
	if(errno!=ECHILD) {
		syslog(LOG_EMERG,"Some processes would not die; ps -axl advised");
	}
	/* Single-user */
	switch(pid=fork()) {
	case 0:
		start_session(0,0,NULL);
		break;
	case -1:
		printf("%s: %s\n",progname,strerror(errno));
		printf("The system is seriously hosed. I'm dying...\n");
		transition=DEATH;
		return(-1);
		break;
	default:
		do {
			wpid=waitpid(pid,(int *)0,WUNTRACED);
		} while(wpid!=pid && transition==SINGLE);
		if(transition!=DEATH) {
			prevtrans=transition;
			transition=MULTI;
		}
		break;
	}
	return(0);
}

/*
 * Transition handler - installed as signal handler.
 */

void
transition_handler(int sig)
{

	switch(sig) {
	case SIGHUP:
	case SIGTERM:
		prevtrans=transition;
		transition=SINGLE;
		syslog(LOG_EMERG,"*** Going from %s -> %s\n",trans[prevtrans],trans[transition]);
		if(prevtrans!=transition) longjmp(machine,sig);
		break;
	case SIGINT:
	case SIGQUIT:
		prevtrans=transition;
		transition=DEATH;
		syslog(LOG_EMERG,"*** Going from %s -> %s\n",trans[prevtrans],trans[transition]);
		if(prevtrans!=transition) longjmp(machine,sig);
		break;
	default:
		syslog(LOG_EMERG,"pid=%d sig=%s (ignored)\n",getpid(),sys_siglist[sig]);
		break;
	}
}

/*
 * Change system state appropriately to the signals
 */

int
transition_machine()
{
	int i;

	while(transition!=DEATH) {
		switch(transition) {
		case MULTI:
			run_multi();
			break;
		case SINGLE:
			run_single();
			break;
		}
	}
	syslog(LOG_EMERG,"Killing all existing sessions...");
	/* Kill all sessions */
	kill(-1,SIGKILL);
	/* Be nice and wait for them */
	while(waitpid(-1,(int *)0,WNOHANG|WUNTRACED)>0) continue;
	unmount("/",0);
	reboot(RB_AUTOBOOT);
	/* NOTREACHED */
}

int
main(int argc, char **argv)
{
	int devfs=0,c,i;

	/* These are copied from real init(8) */
	if(getuid()!=0)
		errx(1,"%s",strerror(EPERM));
	openlog("init",LOG_CONS|LOG_ODELAY,LOG_AUTH);
	if(setsid()<0)
		warn("initial setsid() failed");
	if(setlogin("root")<0)
		warn("setlogin() failed");

	close(0);
	close(1);
	close(2);
	chdir("/");

	progname=rindex(argv[0],'/');
	if(progname==NULL) {
		progname=argv[0];
	} else progname++;

	transition=MULTI;

	/* We must recognize the same options as real init does */
	while((c=getopt(argc,argv,"dsf"))!=-1) {
		switch(c) {
		case 'd':
			devfs=1;
			break;
		case 's':
			transition=SINGLE;
			break;
		case 'f':
			break;
		default:
			printf("%s: unrecognized flag '-%c'\n",progname,c);
			break;
		}
	}
	if(devfs)
		mount("devfs",_PATH_DEV,MNT_NOEXEC|MNT_RDONLY,0);

	/* Fill in the sess structures. */
	/* XXX Really, should be filled based upon config file. */
	for(i=0;i<MAX_CONS;i++) {
		if(i==0) {
			sprintf(ttys[i].tty,_PATH_CONSOLE);
		} else {
			sprintf(ttys[i].tty,"%sv%c",_PATH_TTY,vty[i]);
		}
		ttys[i].pid=0;
		ttys[i].func=shell;
	}

	getcwd(cwd,BUFSIZE);

	signal(SIGINT,transition_handler);
	signal(SIGQUIT,transition_handler);
	signal(SIGTERM,transition_handler);
	signal(SIGHUP,transition_handler);
	signal(SIGALRM,kill_timer);

	setjmp(machine);
	transition_machine(transition);
	/* NOTREACHED */
	exit(100);
}
