/* A front-end using readline to "cook" input lines for Kawa.
 *
 * Copyright (C) 1999  Per Bothner
 * 
 * This front-end program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * Some code from Johnson & Troan: "Linux Application Development"
 * (Addison-Wesley, 1998) was used directly or for inspiration.
 */

/* PROBLEMS/TODO:
 *
 * Only tested under Linux;  needs to be ported.
 *
 * When running mc -c under the Linux console, mc does not recognize
 * mouse clicks, which mc does when not running under fep.
 *
 * Pasting selected text containing tabs is like hitting the tab character,
 * which invokes readline completion.  We don't want this.  I don't know
 * if this is fixable without integrating fep into a terminal emulator.
 *
 * Echo suppression is a kludge, but can only be avoided with better kernel
 * support: We need a tty mode to disable "real" echoing, while still
 * letting the inferior think its tty driver to doing echoing.
 * Stevens's book claims SCR$ and BSD4.3+ have TIOCREMOTE.
 *
 * The latest readline may have some hooks we can use to avoid having
 * to back up the prompt.
 *
 * Desirable readline feature:  When in cooked no-echo mode (e.g. password),
 * echo characters are they are types with '*', but remove them when done.
 *
 * A synchronous output while we're editing an input line should be
 * inserted in the output view *before* the input line, so that the
 * lines being edited (with the prompt) float at the end of the input.
 *
 * A "page mode" option to emulate more/less behavior:  At each page of
 * output, pause for a user command.  This required parsing the output
 * to keep track of line lengths.  It also requires remembering the
 * output, if we want an option to scroll back, which suggests that
 * this should be integrated with a terminal emulator like xterm.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <netdb.h>
#include <stdlib.h>
#include <errno.h>
#include <grp.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <limits.h>
#include <dirent.h>

#ifdef READLINE_LIBRARY
#  include "readline.h"
#  include "history.h"
#else
#  include <readline/readline.h>
#  include <readline/history.h>
#endif

#ifndef COMMAND
#define COMMAND "/bin/sh"
#endif
#ifndef COMMAND_ARGS
#define COMMAND_ARGS COMMAND
#endif

#ifndef HAVE_MEMMOVE
#ifndef memmove
#  if __GNUC__ > 1
#    define memmove(d, s, n)	__builtin_memcpy(d, s, n)
#  else
#    define memmove(d, s, n)	memcpy(d, s, n)
#  endif
#else
#  define memmove(d, s, n)	memcpy(d, s, n)
#endif
#endif

#define APPLICATION_NAME "Rlfe"

#ifndef errno
extern int errno;
#endif

extern int optind;
extern char *optarg;

static char *progname;
static char *progversion;

static int in_from_inferior_fd;
static int out_to_inferior_fd;

/* Unfortunately, we cannot safely display echo from the inferior process.
   The reason is that the echo bit in the pty is "owned" by the inferior,
   and if we try to turn it off, we could confuse the inferior.
   Thus, when echoing, we get echo twice:  First readline echoes while
   we're actually editing. Then we send the line to the inferior, and the
   terminal driver send back an extra echo.
   The work-around is to remember the input lines, and when we see that
   line come back, we supress the output.
   A better solution (supposedly available on SVR4) would be a smarter
   terminal driver, with more flags ... */
#define ECHO_SUPPRESS_MAX 1024
char echo_suppress_buffer[ECHO_SUPPRESS_MAX];
int echo_suppress_start = 0;
int echo_suppress_limit = 0;

/* #define DEBUG */

static FILE *logfile = NULL;

#ifdef DEBUG
FILE *debugfile = NULL;
#define DPRINT0(FMT) (fprintf(debugfile, FMT), fflush(debugfile))
#define DPRINT1(FMT, V1) (fprintf(debugfile, FMT, V1), fflush(debugfile))
#define DPRINT2(FMT, V1, V2) (fprintf(debugfile, FMT, V1, V2), fflush(debugfile))
#else
#define DPRINT0(FMT) /* Do nothing */
#define DPRINT1(FMT, V1) /* Do nothing */
#define DPRINT2(FMT, V1, V2) /* Do nothing */
#endif

struct termios orig_term;

static int rlfe_directory_completion_hook __P((char **));
static int rlfe_directory_rewrite_hook __P((char **));
static char *rlfe_filename_completion_function __P((const char *, int));

/* Pid of child process. */
static pid_t child = -1;

static void
sig_child (int signo)
{
  int status;
  wait (&status);
  DPRINT0 ("(Child process died.)\n");
  tcsetattr(STDIN_FILENO, TCSANOW, &orig_term);
  exit (0);
}

volatile int propagate_sigwinch = 0;

/* sigwinch_handler
 * propagate window size changes from input file descriptor to
 * master side of pty.
 */
void sigwinch_handler(int signal) { 
   propagate_sigwinch = 1;
}

/* get_master_pty() takes a double-indirect character pointer in which
 * to put a slave name, and returns an integer file descriptor.
 * If it returns < 0, an error has occurred.
 * Otherwise, it has returned the master pty file descriptor, and fills
 * in *name with the name of the corresponding slave pty.
 * Once the slave pty has been opened, you are responsible to free *name.
 */

int get_master_pty(char **name) { 
   int i, j;
   /* default to returning error */
   int master = -1;

   /* create a dummy name to fill in */
   *name = strdup("/dev/ptyXX");

   /* search for an unused pty */
   for (i=0; i<16 && master <= 0; i++) {
      for (j=0; j<16 && master <= 0; j++) {
         (*name)[5] = 'p';
         (*name)[8] = "pqrstuvwxyzPQRST"[i];
         (*name)[9] = "0123456789abcdef"[j];
         /* open the master pty */
         if ((master = open(*name, O_RDWR)) < 0) {
            if (errno == ENOENT) {
               /* we are out of pty devices */
               free (*name);
               return (master);
            }
         }
         else {
           /* By substituting a letter, we change the master pty
            * name into the slave pty name.
            */
           (*name)[5] = 't';
           if (access(*name, R_OK|W_OK) != 0)
             {
               close(master);
               master = -1;
             }
         }
      }
   }
   if ((master < 0) && (i == 16) && (j == 16)) {
      /* must have tried every pty unsuccessfully */
      free (*name);
      return (master);
   }

   (*name)[5] = 't';

   return (master);
}

/* get_slave_pty() returns an integer file descriptor.
 * If it returns < 0, an error has occurred.
 * Otherwise, it has returned the slave file descriptor.
 */

int get_slave_pty(char *name) { 
   struct group *gptr;
   gid_t gid;
   int slave = -1;

   /* chown/chmod the corresponding pty, if possible.
    * This will only work if the process has root permissions.
    * Alternatively, write and exec a small setuid program that
    * does just this.
    */
   if ((gptr = getgrnam("tty")) != 0) {
      gid = gptr->gr_gid;
   } else {
      /* if the tty group does not exist, don't change the
       * group on the slave pty, only the owner
       */
      gid = -1;
   }

   /* Note that we do not check for errors here.  If this is code
    * where these actions are critical, check for errors!
    */
   chown(name, getuid(), gid);
   /* This code only makes the slave read/writeable for the user.
    * If this is for an interactive shell that will want to
    * receive "write" and "wall" messages, OR S_IWGRP into the
    * second argument below.
    */
   chmod(name, S_IRUSR|S_IWUSR);

   /* open the corresponding slave pty */
   slave = open(name, O_RDWR);
   return (slave);
}

/* Certain special characters, such as ctrl/C, we want to pass directly
   to the inferior, rather than letting readline handle them. */

static char special_chars[20];
static int special_chars_count;

static void
add_special_char(int ch)
{
  if (ch != 0)
    special_chars[special_chars_count++] = ch;
}

static int eof_char;

static int
is_special_char(int ch)
{
  int i;
#if 0
  if (ch == eof_char && rl_point == rl_end)
    return 1;
#endif
  for (i = special_chars_count;  --i >= 0; )
    if (special_chars[i] == ch)
      return 1;
  return 0;
}

static char buf[1024];
/* buf[0 .. buf_count-1] is the what has been emitted on the current line.
   It is used as the readline prompt. */
static int buf_count = 0;

int num_keys = 0;

static void
null_prep_terminal (int meta)
{
}

static void
null_deprep_terminal ()
{
}

char pending_special_char;

static void
line_handler (char *line)
{
  if (line == NULL)
    {
      char buf[1];
      DPRINT0("saw eof!\n");
      buf[0] = '\004'; /* ctrl/d */
      write (out_to_inferior_fd, buf, 1);
    }
  else
    {
      static char enter[] = "\r";
      /*  Send line to inferior: */
      int length = strlen (line);
      if (length > ECHO_SUPPRESS_MAX-2)
	{
	  echo_suppress_start = 0;
	  echo_suppress_limit = 0;
	}
      else
	{
	  if (echo_suppress_limit + length > ECHO_SUPPRESS_MAX - 2)
	    {
	      if (echo_suppress_limit - echo_suppress_start + length
		  <= ECHO_SUPPRESS_MAX - 2)
		{
		  memmove (echo_suppress_buffer,
			   echo_suppress_buffer + echo_suppress_start,
			   echo_suppress_limit - echo_suppress_start);
		  echo_suppress_limit -= echo_suppress_start;
		  echo_suppress_start = 0;
		}
	      else
		{
		  echo_suppress_limit = 0;
		}
	      echo_suppress_start = 0;
	    }
	  memcpy (echo_suppress_buffer + echo_suppress_limit,
		  line, length);
	  echo_suppress_limit += length;
	  echo_suppress_buffer[echo_suppress_limit++] = '\r';
	  echo_suppress_buffer[echo_suppress_limit++] = '\n';
	}
      write (out_to_inferior_fd, line, length);
      if (pending_special_char == 0)
        {
          write (out_to_inferior_fd, enter, sizeof(enter)-1);
          if (*line)
            add_history (line);
        }
      free (line);
    }
  rl_callback_handler_remove ();
  buf_count = 0;
  num_keys = 0;
  if (pending_special_char != 0)
    {
      write (out_to_inferior_fd, &pending_special_char, 1);
      pending_special_char = 0;
    }
}

/* Value of rl_getc_function.
   Use this because readline should read from stdin, not rl_instream,
   points to the pty (so readline has monitor its terminal modes). */

int
my_rl_getc (FILE *dummy)
{
  int ch = rl_getc (stdin);
  if (is_special_char (ch))
    {
      pending_special_char = ch;
      return '\r';
    }
  return ch;
}

static void
usage()
{
  fprintf (stderr, "%s: usage: %s [-l filename] [-a] [-n appname] [-hv] [command [arguments...]]\n",
		   progname, progname);
}

int
main(int argc, char** argv)
{
  char *path;
  int i, append;
  int master;
  char *name, *logfname, *appname;
  int in_from_tty_fd;
  struct sigaction act;
  struct winsize ws;
  struct termios t;
  int maxfd;
  fd_set in_set;
  static char empty_string[1] = "";
  char *prompt = empty_string;
  int ioctl_err = 0;

  if ((progname = strrchr (argv[0], '/')) == 0)
    progname = argv[0];
  else
    progname++;
  progversion = RL_LIBRARY_VERSION;

  append = 0;
  appname = APPLICATION_NAME;
  logfname = (char *)NULL;

  while ((i = getopt (argc, argv, "ahl:n:v")) != EOF)
    {
      switch (i)
	{
	case 'l':
	  logfname = optarg;
	  break;
	case 'n':
	  appname = optarg;
	  break;
	case 'a':
	  append = 1;
	  break;
	case 'h':
	  usage ();
	  exit (0);
	case 'v':
	  fprintf (stderr, "%s version %s\n", progname, progversion);
	  exit (0);
	default:
	  usage ();
	  exit (2);
	}
    }

  argc -= optind;
  argv += optind;

  if (logfname)
    {
      logfile = fopen (logfname, append ? "a" : "w");
      if (logfile == 0)
	fprintf (stderr, "%s: warning: could not open log file %s: %s\n",
			 progname, logfname, strerror (errno));
    }
    
  rl_readline_name = appname;
  
#ifdef DEBUG
  debugfile = fopen("LOG", "w");
#endif

  if ((master = get_master_pty(&name)) < 0)
    {
      perror("ptypair: could not open master pty");
      exit(1);
    }

  DPRINT1("pty name: '%s'\n", name);

  /* set up SIGWINCH handler */
  act.sa_handler = sigwinch_handler;
  sigemptyset(&(act.sa_mask));
  act.sa_flags = 0;
  if (sigaction(SIGWINCH, &act, NULL) < 0)
    {
      perror("ptypair: could not handle SIGWINCH ");
      exit(1);
    }

  if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) < 0)
    {
      perror("ptypair: could not get window size");
      exit(1);
    }

  if ((child = fork()) < 0)
    {
      perror("cannot fork");
      exit(1);
    }

  if (child == 0)
    { 
      int slave;  /* file descriptor for slave pty */

      /* We are in the child process */
      close(master);

#ifdef TIOCSCTTY
      if ((slave = get_slave_pty(name)) < 0)
	{
	  perror("ptypair: could not open slave pty");
	  exit(1);
	}
      free(name);
#endif

      /* We need to make this process a session group leader, because
       * it is on a new PTY, and things like job control simply will
       * not work correctly unless there is a session group leader
       * and process group leader (which a session group leader
       * automatically is). This also disassociates us from our old
       * controlling tty. 
       */
      if (setsid() < 0)
	{
	  perror("could not set session leader");
	}

      /* Tie us to our new controlling tty. */
#ifdef TIOCSCTTY
      if (ioctl(slave, TIOCSCTTY, NULL))
	{
	  perror("could not set new controlling tty");
	}
#else
      if ((slave = get_slave_pty(name)) < 0)
	{
	  perror("ptypair: could not open slave pty");
	  exit(1);
	}
      free(name);
#endif

      /* make slave pty be standard in, out, and error */
      dup2(slave, STDIN_FILENO);
      dup2(slave, STDOUT_FILENO);
      dup2(slave, STDERR_FILENO);

      /* at this point the slave pty should be standard input */
      if (slave > 2)
	{
	  close(slave);
	}

      /* Try to restore window size; failure isn't critical */
      if (ioctl(STDOUT_FILENO, TIOCSWINSZ, &ws) < 0)
	{
	  perror("could not restore window size");
	}

      /* now start the shell */
      {
	static char* command_args[] = { COMMAND_ARGS, NULL };
	if (argc < 1)
	  execvp(COMMAND, command_args);
	else
	  execvp(argv[0], &argv[0]);
      }

      /* should never be reached */
      exit(1);
    }

  /* parent */
  signal (SIGCHLD, sig_child);
  free(name);

  /* Note that we only set termios settings for standard input;
   * the master side of a pty is NOT a tty.
   */
  tcgetattr(STDIN_FILENO, &orig_term);

  t = orig_term;
  eof_char = t.c_cc[VEOF];
  /*  add_special_char(t.c_cc[VEOF]);*/
  add_special_char(t.c_cc[VINTR]);
  add_special_char(t.c_cc[VQUIT]);
  add_special_char(t.c_cc[VSUSP]);
#if defined (VDISCARD)
  add_special_char(t.c_cc[VDISCARD]);
#endif

#if 0
  t.c_lflag |= (ICANON | ISIG | ECHO | ECHOCTL | ECHOE | \
		ECHOK | ECHOKE | ECHONL | ECHOPRT );
#else
  t.c_lflag &= ~(ICANON | ISIG | ECHO | ECHOCTL | ECHOE | \
		 ECHOK | ECHOKE | ECHONL | ECHOPRT );
#endif
  t.c_iflag |= IGNBRK;
  t.c_cc[VMIN] = 1;
  t.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &t);
  in_from_inferior_fd = master;
  out_to_inferior_fd = master;
  rl_instream = fdopen (master, "r");
  rl_getc_function = my_rl_getc;

  rl_prep_term_function = null_prep_terminal; 
  rl_deprep_term_function = null_deprep_terminal; 
  rl_callback_handler_install (prompt, line_handler);

#if 1
  rl_directory_completion_hook = rlfe_directory_completion_hook;
  rl_completion_entry_function = rlfe_filename_completion_function;
#else
  rl_directory_rewrite_hook = rlfe_directory_rewrite_hook;
#endif

  in_from_tty_fd = STDIN_FILENO;
  FD_ZERO (&in_set);
  maxfd = in_from_inferior_fd > in_from_tty_fd ? in_from_inferior_fd
    : in_from_tty_fd;
  for (;;)
    {
      int num;
      FD_SET (in_from_inferior_fd, &in_set);
      FD_SET (in_from_tty_fd, &in_set);

      num = select(maxfd+1, &in_set, NULL, NULL, NULL);

      if (propagate_sigwinch)
	{
	  struct winsize ws;
	  if (ioctl (STDIN_FILENO, TIOCGWINSZ, &ws) >= 0)
	    {
	      ioctl (master, TIOCSWINSZ, &ws);
	    }
	  propagate_sigwinch = 0;
	  continue;
	}

      if (num <= 0)
	{
	  perror ("select");
	  exit (-1);
	}
      if (FD_ISSET (in_from_tty_fd, &in_set))
	{
	  extern int readline_echoing_p;
	  struct termios term_master;
	  int do_canon = 1;
	  int ioctl_ret;

	  DPRINT1("[tty avail num_keys:%d]\n", num_keys);

	  /* If we can't get tty modes for the master side of the pty, we
	     can't handle non-canonical-mode programs.  Always assume the
	     master is in canonical echo mode if we can't tell. */
	  ioctl_ret = tcgetattr(master, &term_master);

	  if (ioctl_ret >= 0)
	    {
	      DPRINT2 ("echo:%d, canon:%d\n",
			(term_master.c_lflag & ECHO) != 0,
			(term_master.c_lflag & ICANON) != 0);
	      do_canon = (term_master.c_lflag & ICANON) != 0;
	      readline_echoing_p = (term_master.c_lflag & ECHO) != 0;
	    }
	  else
	    {
	      if (ioctl_err == 0)
		DPRINT1("tcgetattr on master fd failed: errno = %d\n", errno);
	      ioctl_err = 1;
	    }

	  if (do_canon == 0 && num_keys == 0)
	    {
	      char ch[10];
	      int count = read (STDIN_FILENO, ch, sizeof(ch));
	      write (out_to_inferior_fd, ch, count);
	    }
	  else
	    {
	      if (num_keys == 0)
		{
		  int i;
		  /* Re-install callback handler for new prompt. */
		  if (prompt != empty_string)
		    free (prompt);
		  prompt = malloc (buf_count + 1);
		  if (prompt == NULL)
		    prompt = empty_string;
		  else
		    {
		      memcpy (prompt, buf, buf_count);
		      prompt[buf_count] = '\0';
		      DPRINT1("New prompt '%s'\n", prompt);
#if 0 /* ifdef HAVE_RL_ALREADY_PROMPTED -- doesn't work */
		      rl_already_prompted = buf_count > 0;
#else
		      if (buf_count > 0)
			write (1, "\r", 1);
#endif
		    }
		  rl_callback_handler_install (prompt, line_handler);
		}
	      num_keys++;
	      rl_callback_read_char ();
	    }
	}
      else /* input from inferior. */
	{
	  int i;
	  int count;
	  int old_count;
	  if (buf_count > (sizeof(buf) >> 2))
	    buf_count = 0;
	  count = read (in_from_inferior_fd, buf+buf_count,
			sizeof(buf) - buf_count);
	  if (count <= 0)
	    {
	      DPRINT0 ("(Connection closed by foreign host.)\n");
	      tcsetattr(STDIN_FILENO, TCSANOW, &orig_term);
	      exit (0);
	    }
	  old_count = buf_count;

	  /* Do some minimal carriage return translation and backspace
	     processing before logging the input line. */
	  if (logfile)
	    {
#ifndef __GNUC__
	      char *b;
#else
	      char b[count + 1];
#endif
	      int i, j;

#ifndef __GNUC__
	      b = malloc (count + 1);
	      if (b) {
#endif
	      for (i = 0; i < count; i++)
	        b[i] = buf[buf_count + i];
	      b[i] = '\0';
	      for (i = j = 0; i <= count; i++)
		{
		  if (b[i] == '\r')
		    {
		      if (b[i+1] != '\n')
		        b[j++] = '\n';
		    }
		  else if (b[i] == '\b')
		    {
		      if (i)
			j--;
		    }
		  else
		    b[j++] = b[i];
		}
	      fprintf (logfile, "%s", b);

#ifndef __GNUC__
	      free (b);
	      }
#endif
	    }

          /* Look for any pending echo that we need to suppress. */
	  while (echo_suppress_start < echo_suppress_limit
		 && count > 0
		 && buf[buf_count] == echo_suppress_buffer[echo_suppress_start])
	    {
	      count--;
	      buf_count++;
	      echo_suppress_start++;
	    }

          /* Write to the terminal anything that was not suppressed. */
          if (count > 0)
            write (1, buf + buf_count, count);

          /* Finally, look for a prompt candidate.
           * When we get around to going input (from the keyboard),
           * we will consider the prompt to be anything since the last
           * line terminator.  So we need to save that text in the
           * initial part of buf.  However, anything before the
           * most recent end-of-line is not interesting. */
	  buf_count += count;
#if 1
	  for (i = buf_count;  --i >= old_count; )
#else
	  for (i = buf_count - 1;  i-- >= buf_count - count; )
#endif
	    {
	      if (buf[i] == '\n' || buf[i] == '\r')
		{
		  i++;
		  memmove (buf, buf+i, buf_count - i);
		  buf_count -= i;
		  break;
		}
	    }
	  DPRINT2("-> i: %d, buf_count: %d\n", i, buf_count);
	}
    }
}

/*
 *
 * FILENAME COMPLETION FOR RLFE
 *
 */

#ifndef PATH_MAX
#  define PATH_MAX 1024
#endif

#define DIRSEP		'/'
#define ISDIRSEP(x)	((x) == '/')
#define PATHSEP(x)	(ISDIRSEP(x) || (x) == 0)

#define DOT_OR_DOTDOT(x) \
	((x)[0] == '.' && (PATHSEP((x)[1]) || \
			  ((x)[1] == '.' && PATHSEP((x)[2]))))

#define FREE(x)		if (x) free(x)

#define STRDUP(s, x)	do { \
			  s = strdup (x);\
			  if (s == 0) \
			    return ((char *)NULL); \
			} while (0)

static int
get_inferior_cwd (path, psize)
     char *path;
     size_t psize;
{
  int n;
  static char procfsbuf[PATH_MAX] = { '\0' };

  if (procfsbuf[0] == '\0')
    sprintf (procfsbuf, "/proc/%d/cwd", (int)child);
  n = readlink (procfsbuf, path, psize);
  if (n < 0)
    return n;
  if (n > psize)
    return -1;
  path[n] = '\0';
  return n;
}

static int
rlfe_directory_rewrite_hook (dirnamep)
     char **dirnamep;
{
  char *ldirname, cwd[PATH_MAX], *retdir, *ld;
  int n, ldlen;

  ldirname = *dirnamep;

  if (*ldirname == '/')
    return 0;

  n = get_inferior_cwd (cwd, sizeof(cwd) - 1);
  if (n < 0)
    return 0;
  if (n == 0)	/* current directory */
    {
      cwd[0] = '.';
      cwd[1] = '\0';
      n = 1;
    }

  /* Minimally canonicalize ldirname by removing leading `./' */
  for (ld = ldirname; *ld; )
    {
      if (ISDIRSEP (ld[0]))
	ld++;
      else if (ld[0] == '.' && PATHSEP(ld[1]))
	ld++;
      else
	break;
    }
  ldlen = (ld && *ld) ? strlen (ld) : 0;

  retdir = (char *)malloc (n + ldlen + 3);
  if (retdir == 0)
    return 0;
  if (ldlen)
    sprintf (retdir, "%s/%s", cwd, ld);
  else
    strcpy (retdir, cwd);
  free (ldirname);

  *dirnamep = retdir;

  DPRINT1("rl_directory_rewrite_hook returns %s\n", retdir);
  return 1;
}

/* Translate *DIRNAMEP to be relative to the inferior's CWD.  Leave a trailing
   slash on the result. */
static int
rlfe_directory_completion_hook (dirnamep)
     char **dirnamep;
{
  char *ldirname, *retdir;
  int n, ldlen;

  ldirname = *dirnamep;

  if (*ldirname == '/')
    return 0;

  n = rlfe_directory_rewrite_hook (dirnamep);
  if (n == 0)
    return 0;

  ldirname = *dirnamep;
  ldlen = (ldirname && *ldirname) ? strlen (ldirname) : 0;

  if (ldlen == 0 || ldirname[ldlen - 1] != '/')
    {
      retdir = (char *)malloc (ldlen + 3);
      if (retdir == 0)
	return 0;
      if (ldlen)
	strcpy (retdir, ldirname);
      else
	retdir[ldlen++] = '.';
      retdir[ldlen] = '/';
      retdir[ldlen+1] = '\0';
      free (ldirname);

      *dirnamep = retdir;
    }

  DPRINT1("rl_directory_completion_hook returns %s\n", retdir);
  return 1;
}

static char *
rlfe_filename_completion_function (text, state)
     const char *text;
     int state;
{
  static DIR *directory;
  static char *filename = (char *)NULL;
  static char *dirname = (char *)NULL, *ud = (char *)NULL;
  static int flen, udlen;
  char *temp;
  struct dirent *dentry;

  if (state == 0)
    {
      if (directory)
	{
	  closedir (directory);
	  directory = 0;
	}
      FREE (dirname);
      FREE (filename);
      FREE (ud);

      if (text && *text)
        STRDUP (filename, text);
      else
	{
	  filename = malloc(1); 
	  if (filename == 0)
	    return ((char *)NULL);
	  filename[0] = '\0';
	}
      dirname = (text && *text) ? strdup (text) : strdup (".");
      if (dirname == 0)
        return ((char *)NULL);

      temp = strrchr (dirname, '/');
      if (temp)
	{
	  strcpy (filename, ++temp);
	  *temp = '\0';
	}
      else
	{
	  dirname[0] = '.';
	  dirname[1] = '\0';
	}

      STRDUP (ud, dirname);
      udlen = strlen (ud);

      rlfe_directory_completion_hook (&dirname);

      directory = opendir (dirname);
      flen = strlen (filename);

      rl_filename_completion_desired = 1;
    }

  dentry = 0;
  while (directory && (dentry = readdir (directory)))
    {
      if (flen == 0)
	{
          if (DOT_OR_DOTDOT(dentry->d_name) == 0)
            break;
	}
      else
	{
	  if ((dentry->d_name[0] == filename[0]) &&
	      (strlen (dentry->d_name) >= flen) &&
	      (strncmp (filename, dentry->d_name, flen) == 0))
	    break;
	}
    }

  if (dentry == 0)
    {
      if (directory)
	{
	  closedir (directory);
	  directory = 0;
	}
      FREE (dirname);
      FREE (filename);
      FREE (ud);
      dirname = filename = ud = 0;
      return ((char *)NULL);
    }

  if (ud == 0 || (ud[0] == '.' && ud[1] == '\0'))
    temp = strdup (dentry->d_name);
  else
    {
      temp = malloc (1 + udlen + strlen (dentry->d_name));
      strcpy (temp, ud);
      strcpy (temp + udlen, dentry->d_name);
    }
  return (temp);
}
