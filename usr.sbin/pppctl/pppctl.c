/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997 Brian Somers <brian@Awfulhak.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>
#include <netdb.h>

#include <sys/time.h>
#include <err.h>
#include <errno.h>
#include <histedit.h>
#include <semaphore.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define LINELEN 2048

/* Data passed to the threads we create */
struct thread_data {
    EditLine *edit;		/* libedit stuff */
    History *hist;		/* libedit stuff */
    pthread_t trm;		/* Terminal thread (for pthread_kill()) */
    int ppp;			/* ppp descriptor */
};

/* Flags passed to Receive() */
#define REC_PASSWD  (1)		/* Handle a password request from ppp */
#define REC_SHOW    (2)		/* Show everything except prompts */
#define REC_VERBOSE (4)		/* Show everything */

static char *passwd;
static char *prompt;		/* Tell libedit what the current prompt is */
static int data = -1;		/* setjmp() has been done when data != -1 */
static jmp_buf pppdead;		/* Jump the Terminal thread out of el_gets() */
static int timetogo;		/* Tell the Monitor thread to exit */
static sem_t sem_select;	/* select() co-ordination between threads */
static int TimedOut;		/* Set if our connect() timed out */
static int want_sem_post;	/* Need to let the Monitor thread in ? */

/*
 * How to use pppctl...
 */
static int
usage()
{
    fprintf(stderr, "usage: pppctl [-v] [-t n] [-p passwd] "
            "Port|LocalSock [command[;command]...]\n");
    fprintf(stderr, "              -v tells pppctl to output all"
            " conversation\n");
    fprintf(stderr, "              -t n specifies a timeout of n"
            " seconds when connecting (default 2)\n");
    fprintf(stderr, "              -p passwd specifies your password\n");
    exit(1);
}

/*
 * Handle the SIGALRM received due to a connect() timeout.
 */
static void
Timeout(int Sig)
{
    TimedOut = 1;
}

/*
 * A callback routine for libedit to find out what the current prompt is.
 * All the work is done in Receive() below.
 */
static char *
GetPrompt(EditLine *e)
{
    if (prompt == NULL)
        prompt = "";
    return prompt;
}

/*
 * Receive data from the ppp descriptor.
 * We also handle password prompts here (if asked via the `display' arg)
 * and buffer what our prompt looks like (via the `prompt' global).
 */
static int
Receive(int fd, int display)
{
    static char Buffer[LINELEN];
    char temp[sizeof(Buffer)];
    struct timeval t;
    int Result;
    char *last;
    fd_set f;
    int len;
    int err;

    FD_ZERO(&f);
    FD_SET(fd, &f);
    t.tv_sec = 0;
    t.tv_usec = 100000;
    prompt = Buffer;
    len = 0;

    while (Result = read(fd, Buffer+len, sizeof(Buffer)-len-1), Result != -1) {
        if (Result == 0) {
            Result = -1;
            break;
        }
        len += Result;
        Buffer[len] = '\0';
        if (len > 2 && !strcmp(Buffer+len-2, "> ")) {
            prompt = strrchr(Buffer, '\n');
            if (display & (REC_SHOW|REC_VERBOSE)) {
                if (display & REC_VERBOSE)
                    last = Buffer+len-1;
                else
                    last = prompt;
                if (last) {
                    last++;
                    write(STDOUT_FILENO, Buffer, last-Buffer);
                }
            }
            prompt = prompt == NULL ? Buffer : prompt+1;
            for (last = Buffer+len-2; last > Buffer && *last != ' '; last--)
                ;
            if (last > Buffer+3 && !strncmp(last-3, " on", 3)) {
                 /* a password is required ! */
                 if (display & REC_PASSWD) {
                    /* password time */
                    if (!passwd)
                        passwd = getpass("Password: ");
                    sprintf(Buffer, "passwd %s\n", passwd);
                    memset(passwd, '\0', strlen(passwd));
                    if (display & REC_VERBOSE)
                        write(STDOUT_FILENO, Buffer, strlen(Buffer));
                    write(fd, Buffer, strlen(Buffer));
                    memset(Buffer, '\0', strlen(Buffer));
                    return Receive(fd, display & ~REC_PASSWD);
                }
                Result = 1;
            } else
                Result = 0;
            break;
        } else
            prompt = "";
        if (len == sizeof Buffer - 1) {
            int flush;
            if ((last = strrchr(Buffer, '\n')) == NULL)
                /* Yeuch - this is one mother of a line ! */
                flush = sizeof Buffer / 2;
            else
                flush = last - Buffer + 1;
            write(STDOUT_FILENO, Buffer, flush);
	    strcpy(temp, Buffer + flush);
	    strcpy(Buffer, temp);
            len -= flush;
        }
        if ((Result = select(fd + 1, &f, NULL, NULL, &t)) <= 0) {
            err = Result == -1 ? errno : 0;
            if (len)
                write(STDOUT_FILENO, Buffer, len);
            if (err == EINTR)
                continue;
            break;
        }
    }

    return Result;
}

/*
 * Handle being told by the Monitor thread that there's data to be read
 * on the ppp descriptor.
 *
 * Note, this is a signal handler - be careful of what we do !
 */
static void
InputHandler(int sig)
{
    static char buf[LINELEN];
    struct timeval t;
    int len;
    fd_set f;

    if (data != -1) {
        FD_ZERO(&f);
        FD_SET(data, &f);
        t.tv_sec = t.tv_usec = 0;

        if (select(data + 1, &f, NULL, NULL, &t) > 0) {
            len = read(data, buf, sizeof buf);

            if (len > 0)
                write(STDOUT_FILENO, buf, len);
            else if (data != -1)
                longjmp(pppdead, -1);
        }

        sem_post(&sem_select);
    } else
        /* Don't let the Monitor thread in 'till we've set ``data'' up again */
        want_sem_post = 1;
}

/*
 * This is a simple wrapper for el_gets(), allowing our SIGUSR1 signal
 * handler (above) to take effect only after we've done a setjmp().
 *
 * We don't want it to do anything outside of here as we're going to
 * service the ppp descriptor anyway.
 */
static const char *
SmartGets(EditLine *e, int *count, int fd)
{
    const char *result;

    if (setjmp(pppdead))
        result = NULL;
    else {
        data = fd;
        if (want_sem_post)
            /* Let the Monitor thread in again */
            sem_post(&sem_select);
        result = el_gets(e, count);
    }

    data = -1;

    return result;
}

/*
 * The Terminal thread entry point.
 *
 * The bulk of the interactive work is done here.  We read the terminal,
 * write the results to our ppp descriptor and read the results back.
 *
 * While reading the terminal (using el_gets()), it's possible to take
 * a SIGUSR1 from the Monitor thread, telling us that the ppp descriptor
 * has some data.  The data is read and displayed by the signal handler
 * itself.
 */
static void *
Terminal(void *v)
{
    struct sigaction act, oact;
    struct thread_data *td;
    const char *l;
    int len;
#ifndef __OpenBSD__
    HistEvent hev = { 0, "" };
#endif

    act.sa_handler = InputHandler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_RESTART;
    sigaction(SIGUSR1, &act, &oact);

    td = (struct thread_data *)v;
    want_sem_post = 1;

    while ((l = SmartGets(td->edit, &len, td->ppp))) {
        if (len > 1)
#ifdef __OpenBSD__
            history(td->hist, H_ENTER, l);
#else
            history(td->hist, &hev, H_ENTER, l);
#endif
        write(td->ppp, l, len);
        if (Receive(td->ppp, REC_SHOW) != 0)
            break;
    }

    return NULL;
}

/*
 * The Monitor thread entry point.
 *
 * This thread simply monitors our ppp descriptor.  When there's something
 * to read, a SIGUSR1 is sent to the Terminal thread.
 *
 * sem_select() is used by the Terminal thread to keep us from sending
 * flurries of SIGUSR1s, and is used from the main thread to wake us up
 * when it's time to exit.
 */
static void *
Monitor(void *v)
{
    struct thread_data *td;
    fd_set f;
    int ret;

    td = (struct thread_data *)v;
    FD_ZERO(&f);
    FD_SET(td->ppp, &f);

    sem_wait(&sem_select);
    while (!timetogo)
        if ((ret = select(td->ppp + 1, &f, NULL, NULL, NULL)) > 0) {
            pthread_kill(td->trm, SIGUSR1);
            sem_wait(&sem_select);
        }

    return NULL;
}

static const char *
sockaddr_ntop(const struct sockaddr *sa)
{
    const void *addr;
    static char addrbuf[INET6_ADDRSTRLEN];
 
    switch (sa->sa_family) {
    case AF_INET:
	addr = &((const struct sockaddr_in *)sa)->sin_addr;
	break;
    case AF_UNIX:
	addr = &((const struct sockaddr_un *)sa)->sun_path;                
	break;
    case AF_INET6:
	addr = &((const struct sockaddr_in6 *)sa)->sin6_addr;
	break;
    default:
	return NULL;
    }
    inet_ntop(sa->sa_family, addr, addrbuf, sizeof(addrbuf));                
    return addrbuf;
}

/*
 * Connect to ppp using either a local domain socket or a tcp socket.
 *
 * If we're given arguments, process them and quit, otherwise create two
 * threads to handle interactive mode.
 */
int
main(int argc, char **argv)
{
    struct sockaddr_un ifsun;
    int n, arg, fd, len, verbose, save_errno, hide1, hide1off, hide2;
    unsigned TimeoutVal;
    char *DoneWord = "x", *next, *start;
    struct sigaction act, oact;
    void *thread_ret;
    pthread_t mon;
    char Command[LINELEN];
    char Buffer[LINELEN];

    verbose = 0;
    TimeoutVal = 2;
    hide1 = hide1off = hide2 = 0;

    for (arg = 1; arg < argc; arg++)
        if (*argv[arg] == '-') {
            for (start = argv[arg] + 1; *start; start++)
                switch (*start) {
                    case 't':
                        TimeoutVal = (unsigned)atoi
                            (start[1] ? start + 1 : argv[++arg]);
                        start = DoneWord;
                        break;
    
                    case 'v':
                        verbose = REC_VERBOSE;
                        break;

                    case 'p':
                        if (start[1]) {
                          hide1 = arg;
                          hide1off = start - argv[arg];
                          passwd = start + 1;
                        } else {
                          hide1 = arg;
                          hide1off = start - argv[arg];
                          passwd = argv[++arg];
                          hide2 = arg;
                        }
                        start = DoneWord;
                        break;
    
                    default:
                        usage();
                }
        }
        else
            break;


    if (argc < arg + 1)
        usage();

    if (hide1) {
      char title[1024];
      int pos, harg;

      for (harg = pos = 0; harg < argc; harg++)
        if (harg == 0 || harg != hide2) {
          if (harg == 0 || harg != hide1)
            n = snprintf(title + pos, sizeof title - pos, "%s%s",
                            harg ? " " : "", argv[harg]);
          else if (hide1off > 1)
            n = snprintf(title + pos, sizeof title - pos, " %.*s",
                            hide1off, argv[harg]);
          else
            n = 0;
          if (n < 0 || n >= sizeof title - pos)
            break;
          pos += n;
        }
#ifdef __FreeBSD__
      setproctitle("-%s", title);
#else
      setproctitle("%s", title);
#endif
    }

    if (*argv[arg] == '/') {
        memset(&ifsun, '\0', sizeof ifsun);
        ifsun.sun_len = strlen(argv[arg]);
        if (ifsun.sun_len > sizeof ifsun.sun_path - 1) {
            warnx("%s: path too long", argv[arg]);
            return 1;
        }
        ifsun.sun_family = AF_LOCAL;
        strcpy(ifsun.sun_path, argv[arg]);

        if (fd = socket(AF_LOCAL, SOCK_STREAM, 0), fd < 0) {
            warnx("cannot create local domain socket");
            return 2;
        }
	if (connect(fd, (struct sockaddr *)&ifsun, sizeof(ifsun)) < 0) {
	    if (errno)
		warn("cannot connect to socket %s", argv[arg]);
	    else
		warnx("cannot connect to socket %s", argv[arg]);
	    close(fd);
	    return 3;
	}
    } else {
        char *addr, *p, *port;
	const char *caddr;
	struct addrinfo hints, *res, *pai;
        int gai;
	char local[] = "localhost";

	addr = argv[arg];
	if (addr[strspn(addr, "0123456789")] == '\0') {
	    /* port on local machine */
	    port = addr;
	    addr = local;
	} else if (*addr == '[') {
	    /* [addr]:port */
	    if ((p = strchr(addr, ']')) == NULL) {
		warnx("%s: mismatched '['", addr);
		return 1;
	    }
	    addr++;
	    *p++ = '\0';
	    if (*p != ':') {
		warnx("%s: missing port", addr);
		return 1;
	    }
	    port = ++p;
	} else {
	    /* addr:port */
	    p = addr + strcspn(addr, ":");
	    if (*p != ':') {
		warnx("%s: missing port", addr);
		return 1;
	    }
	    *p++ = '\0';
	    port = p;
	}
	memset(&hints, 0, sizeof(hints));
	hints.ai_socktype = SOCK_STREAM;
	gai = getaddrinfo(addr, port, &hints, &res);
	if (gai != 0) {
	    warnx("%s: %s", addr, gai_strerror(gai));
	    return 1;
	}
	for (pai = res; pai != NULL; pai = pai->ai_next) {
	    if (fd = socket(pai->ai_family, pai->ai_socktype,
		pai->ai_protocol), fd < 0) {
		warnx("cannot create socket");
		continue;
	    }
	    TimedOut = 0;
	    if (TimeoutVal) {
		act.sa_handler = Timeout;
		sigemptyset(&act.sa_mask);
		act.sa_flags = 0;
		sigaction(SIGALRM, &act, &oact);
		alarm(TimeoutVal);
	    }
	    if (connect(fd, pai->ai_addr, pai->ai_addrlen) == 0)
		break;
	    if (TimeoutVal) {
		save_errno = errno;
		alarm(0);
		sigaction(SIGALRM, &oact, 0);
		errno = save_errno;
	    }
	    caddr = sockaddr_ntop(pai->ai_addr);
	    if (caddr == NULL)
		caddr = argv[arg];
	    if (TimedOut)
		warnx("timeout: cannot connect to %s", caddr);
	    else {
		if (errno)
		    warn("cannot connect to %s", caddr);
		else
		    warnx("cannot connect to %s", caddr);
	    }
	    close(fd);
	}
	freeaddrinfo(res);
	if (pai == NULL)
	    return 1;
	if (TimeoutVal) {
	    alarm(0);
	    sigaction(SIGALRM, &oact, 0);
	}
    }

    len = 0;
    Command[sizeof(Command)-1] = '\0';
    for (arg++; arg < argc; arg++) {
        if (len && len < sizeof(Command)-1)
            strcpy(Command+len++, " ");
        strncpy(Command+len, argv[arg], sizeof(Command)-len-1);
        len += strlen(Command+len);
    }

    switch (Receive(fd, verbose | REC_PASSWD)) {
        case 1:
            fprintf(stderr, "Password incorrect\n");
            break;

        case 0:
            passwd = NULL;
            if (len == 0) {
                struct thread_data td;
                const char *env;
                int size;
#ifndef __OpenBSD__
                HistEvent hev = { 0, "" };
#endif

                td.hist = history_init();
                if ((env = getenv("EL_SIZE"))) {
                    size = atoi(env);
                    if (size < 0)
                      size = 20;
                } else
                    size = 20;
#ifdef __OpenBSD__
                history(td.hist, H_EVENT, size);
                td.edit = el_init("pppctl", stdin, stdout);
#else
                history(td.hist, &hev, H_SETSIZE, size);
                td.edit = el_init("pppctl", stdin, stdout, stderr);
#endif
                el_source(td.edit, NULL);
                el_set(td.edit, EL_PROMPT, GetPrompt);
                if ((env = getenv("EL_EDITOR"))) {
                    if (!strcmp(env, "vi"))
                        el_set(td.edit, EL_EDITOR, "vi");
                    else if (!strcmp(env, "emacs"))
                        el_set(td.edit, EL_EDITOR, "emacs");
                }
                el_set(td.edit, EL_SIGNAL, 1);
                el_set(td.edit, EL_HIST, history, (const char *)td.hist);

                td.ppp = fd;
                td.trm = NULL;

                /*
                 * We create two threads.  The Terminal thread does all the
                 * work while the Monitor thread simply tells the Terminal
                 * thread when ``fd'' becomes readable.  The telling is done
                 * by sending a SIGUSR1 to the Terminal thread.  The
                 * sem_select semaphore is used to prevent the monitor
                 * thread from firing excessive signals at the Terminal
                 * thread (it's abused for exit handling too - see below).
                 *
                 * The Terminal thread never uses td.trm !
                 */
                sem_init(&sem_select, 0, 0);

                pthread_create(&td.trm, NULL, Terminal, &td);
                pthread_create(&mon, NULL, Monitor, &td);

                /* Wait for the terminal thread to finish */
                pthread_join(td.trm, &thread_ret);
                fprintf(stderr, "Connection closed\n");

                /* Get rid of the monitor thread by abusing sem_select */
                timetogo = 1;
                close(fd);
                fd = -1;
                sem_post(&sem_select);
                pthread_join(mon, &thread_ret);

                /* Restore our terminal and release resources */
                el_end(td.edit);
                history_end(td.hist);
                sem_destroy(&sem_select);
            } else {
                start = Command;
                do {
                    next = strchr(start, ';');
                    while (*start == ' ' || *start == '\t')
                        start++;
                    if (next)
                        *next = '\0';
                    strcpy(Buffer, start);
                    Buffer[sizeof(Buffer)-2] = '\0';
                    strcat(Buffer, "\n");
                    if (verbose)
                        write(STDOUT_FILENO, Buffer, strlen(Buffer));
                    write(fd, Buffer, strlen(Buffer));
                    if (Receive(fd, verbose | REC_SHOW) != 0) {
                        fprintf(stderr, "Connection closed\n");
                        break;
                    }
                    if (next)
                        start = ++next;
                } while (next && *next);
                if (verbose)
                    write(STDOUT_FILENO, "quit\n", 5);
                write(fd, "quit\n", 5);
                while (Receive(fd, verbose | REC_SHOW) == 0)
                    ;
                if (verbose)
                    puts("");
            }
            break;

        default:
            warnx("ppp is not responding");
            break;
    }

    if (fd != -1)
        close(fd);
    
    return 0;
}
