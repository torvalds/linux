/* signals.c -- signal handling support for readline. */

/* Copyright (C) 1987, 1989, 1992 Free Software Foundation, Inc.

   This file is part of the GNU Readline Library, a library for
   reading lines of text with interactive input and history editing.

   The GNU Readline Library is free software; you can redistribute it
   and/or modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2, or
   (at your option) any later version.

   The GNU Readline Library is distributed in the hope that it will be
   useful, but WITHOUT ANY WARRANTY; without even the implied warranty
   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   The GNU General Public License is often shipped with GNU software, and
   is generally kept in a file called COPYING or LICENSE.  If you do not
   have a copy of the license, write to the Free Software Foundation,
   59 Temple Place, Suite 330, Boston, MA 02111 USA. */
#define READLINE_LIBRARY

#if defined (HAVE_CONFIG_H)
#  include <config.h>
#endif

#include <stdio.h>		/* Just for NULL.  Yuck. */
#include <sys/types.h>
#include <signal.h>

#if defined (HAVE_UNISTD_H)
#  include <unistd.h>
#endif /* HAVE_UNISTD_H */

/* System-specific feature definitions and include files. */
#include "rldefs.h"

#if defined (GWINSZ_IN_SYS_IOCTL)
#  include <sys/ioctl.h>
#endif /* GWINSZ_IN_SYS_IOCTL */

#if defined (HANDLE_SIGNALS)
/* Some standard library routines. */
#include "readline.h"
#include "history.h"

#include "rlprivate.h"

#if !defined (RETSIGTYPE)
#  if defined (VOID_SIGHANDLER)
#    define RETSIGTYPE void
#  else
#    define RETSIGTYPE int
#  endif /* !VOID_SIGHANDLER */
#endif /* !RETSIGTYPE */

#if defined (VOID_SIGHANDLER)
#  define SIGHANDLER_RETURN return
#else
#  define SIGHANDLER_RETURN return (0)
#endif

/* This typedef is equivalent to the one for Function; it allows us
   to say SigHandler *foo = signal (SIGKILL, SIG_IGN); */
typedef RETSIGTYPE SigHandler ();

#if defined (HAVE_POSIX_SIGNALS)
typedef struct sigaction sighandler_cxt;
#  define rl_sigaction(s, nh, oh)	sigaction(s, nh, oh)
#else
typedef struct { SigHandler *sa_handler; int sa_mask, sa_flags; } sighandler_cxt;
#  define sigemptyset(m)
#endif /* !HAVE_POSIX_SIGNALS */

static SigHandler *rl_set_sighandler PARAMS((int, SigHandler *, sighandler_cxt *));
static void rl_maybe_set_sighandler PARAMS((int, SigHandler *, sighandler_cxt *));

/* Exported variables for use by applications. */

/* If non-zero, readline will install its own signal handlers for
   SIGINT, SIGTERM, SIGQUIT, SIGALRM, SIGTSTP, SIGTTIN, and SIGTTOU. */
int rl_catch_signals = 1;

/* If non-zero, readline will install a signal handler for SIGWINCH. */
#ifdef SIGWINCH
int rl_catch_sigwinch = 1;
#endif

static int signals_set_flag;
static int sigwinch_set_flag;

/* **************************************************************** */
/*								    */
/*			   Signal Handling                          */
/*								    */
/* **************************************************************** */

static sighandler_cxt old_int, old_term, old_alrm, old_quit;
#if defined (SIGTSTP)
static sighandler_cxt old_tstp, old_ttou, old_ttin;
#endif
#if defined (SIGWINCH)
static sighandler_cxt old_winch;
#endif

/* Readline signal handler functions. */

static RETSIGTYPE
rl_signal_handler (sig)
     int sig;
{
#if defined (HAVE_POSIX_SIGNALS)
  sigset_t set;
#else /* !HAVE_POSIX_SIGNALS */
#  if defined (HAVE_BSD_SIGNALS)
  long omask;
#  else /* !HAVE_BSD_SIGNALS */
  sighandler_cxt dummy_cxt;	/* needed for rl_set_sighandler call */
#  endif /* !HAVE_BSD_SIGNALS */
#endif /* !HAVE_POSIX_SIGNALS */

  RL_SETSTATE(RL_STATE_SIGHANDLER);

#if !defined (HAVE_BSD_SIGNALS) && !defined (HAVE_POSIX_SIGNALS)
  /* Since the signal will not be blocked while we are in the signal
     handler, ignore it until rl_clear_signals resets the catcher. */
  if (sig == SIGINT || sig == SIGALRM)
    rl_set_sighandler (sig, SIG_IGN, &dummy_cxt);
#endif /* !HAVE_BSD_SIGNALS && !HAVE_POSIX_SIGNALS */

  switch (sig)
    {
    case SIGINT:
      rl_free_line_state ();
      /* FALLTHROUGH */

#if defined (SIGTSTP)
    case SIGTSTP:
    case SIGTTOU:
    case SIGTTIN:
#endif /* SIGTSTP */
    case SIGALRM:
    case SIGTERM:
    case SIGQUIT:
      rl_cleanup_after_signal ();

#if defined (HAVE_POSIX_SIGNALS)
      sigprocmask (SIG_BLOCK, (sigset_t *)NULL, &set);
      sigdelset (&set, sig);
#else /* !HAVE_POSIX_SIGNALS */
#  if defined (HAVE_BSD_SIGNALS)
      omask = sigblock (0);
#  endif /* HAVE_BSD_SIGNALS */
#endif /* !HAVE_POSIX_SIGNALS */

#if defined (__EMX__)
      signal (sig, SIG_ACK);
#endif

      kill (getpid (), sig);

      /* Let the signal that we just sent through.  */
#if defined (HAVE_POSIX_SIGNALS)
      sigprocmask (SIG_SETMASK, &set, (sigset_t *)NULL);
#else /* !HAVE_POSIX_SIGNALS */
#  if defined (HAVE_BSD_SIGNALS)
      sigsetmask (omask & ~(sigmask (sig)));
#  endif /* HAVE_BSD_SIGNALS */
#endif /* !HAVE_POSIX_SIGNALS */

      rl_reset_after_signal ();
    }

  RL_UNSETSTATE(RL_STATE_SIGHANDLER);
  SIGHANDLER_RETURN;
}

#if defined (SIGWINCH)
static RETSIGTYPE
rl_sigwinch_handler (sig)
     int sig;
{
  SigHandler *oh;

#if defined (MUST_REINSTALL_SIGHANDLERS)
  sighandler_cxt dummy_winch;

  /* We don't want to change old_winch -- it holds the state of SIGWINCH
     disposition set by the calling application.  We need this state
     because we call the application's SIGWINCH handler after updating
     our own idea of the screen size. */
  rl_set_sighandler (SIGWINCH, rl_sigwinch_handler, &dummy_winch);
#endif

  RL_SETSTATE(RL_STATE_SIGHANDLER);
  rl_resize_terminal ();

  /* If another sigwinch handler has been installed, call it. */
  oh = (SigHandler *)old_winch.sa_handler;
  if (oh &&  oh != (SigHandler *)SIG_IGN && oh != (SigHandler *)SIG_DFL)
    (*oh) (sig);

  RL_UNSETSTATE(RL_STATE_SIGHANDLER);
  SIGHANDLER_RETURN;
}
#endif  /* SIGWINCH */

/* Functions to manage signal handling. */

#if !defined (HAVE_POSIX_SIGNALS)
static int
rl_sigaction (sig, nh, oh)
     int sig;
     sighandler_cxt *nh, *oh;
{
  oh->sa_handler = signal (sig, nh->sa_handler);
  return 0;
}
#endif /* !HAVE_POSIX_SIGNALS */

/* Set up a readline-specific signal handler, saving the old signal
   information in OHANDLER.  Return the old signal handler, like
   signal(). */
static SigHandler *
rl_set_sighandler (sig, handler, ohandler)
     int sig;
     SigHandler *handler;
     sighandler_cxt *ohandler;
{
  sighandler_cxt old_handler;
#if defined (HAVE_POSIX_SIGNALS)
  struct sigaction act;

  act.sa_handler = handler;
  act.sa_flags = 0;	/* XXX - should we set SA_RESTART for SIGWINCH? */
  sigemptyset (&act.sa_mask);
  sigemptyset (&ohandler->sa_mask);
  sigaction (sig, &act, &old_handler);
#else
  old_handler.sa_handler = (SigHandler *)signal (sig, handler);
#endif /* !HAVE_POSIX_SIGNALS */

  /* XXX -- assume we have memcpy */
  /* If rl_set_signals is called twice in a row, don't set the old handler to
     rl_signal_handler, because that would cause infinite recursion. */
  if (handler != rl_signal_handler || old_handler.sa_handler != rl_signal_handler)
    memcpy (ohandler, &old_handler, sizeof (sighandler_cxt));

  return (ohandler->sa_handler);
}

static void
rl_maybe_set_sighandler (sig, handler, ohandler)
     int sig;
     SigHandler *handler;
     sighandler_cxt *ohandler;
{
  sighandler_cxt dummy;
  SigHandler *oh;

  sigemptyset (&dummy.sa_mask);
  oh = rl_set_sighandler (sig, handler, ohandler);
  if (oh == (SigHandler *)SIG_IGN)
    rl_sigaction (sig, ohandler, &dummy);
}

int
rl_set_signals ()
{
  sighandler_cxt dummy;
  SigHandler *oh;

  if (rl_catch_signals && signals_set_flag == 0)
    {
      rl_maybe_set_sighandler (SIGINT, rl_signal_handler, &old_int);
      rl_maybe_set_sighandler (SIGTERM, rl_signal_handler, &old_term);
      rl_maybe_set_sighandler (SIGQUIT, rl_signal_handler, &old_quit);

      oh = rl_set_sighandler (SIGALRM, rl_signal_handler, &old_alrm);
      if (oh == (SigHandler *)SIG_IGN)
	rl_sigaction (SIGALRM, &old_alrm, &dummy);
#if defined (HAVE_POSIX_SIGNALS) && defined (SA_RESTART)
      /* If the application using readline has already installed a signal
	 handler with SA_RESTART, SIGALRM will cause reads to be restarted
	 automatically, so readline should just get out of the way.  Since
	 we tested for SIG_IGN above, we can just test for SIG_DFL here. */
      if (oh != (SigHandler *)SIG_DFL && (old_alrm.sa_flags & SA_RESTART))
	rl_sigaction (SIGALRM, &old_alrm, &dummy);
#endif /* HAVE_POSIX_SIGNALS */

#if defined (SIGTSTP)
      rl_maybe_set_sighandler (SIGTSTP, rl_signal_handler, &old_tstp);
#endif /* SIGTSTP */

#if defined (SIGTTOU)
      rl_maybe_set_sighandler (SIGTTOU, rl_signal_handler, &old_ttou);
#endif /* SIGTTOU */

#if defined (SIGTTIN)
      rl_maybe_set_sighandler (SIGTTIN, rl_signal_handler, &old_ttin);
#endif /* SIGTTIN */

      signals_set_flag = 1;
    }

#if defined (SIGWINCH)
  if (rl_catch_sigwinch && sigwinch_set_flag == 0)
    {
      rl_maybe_set_sighandler (SIGWINCH, rl_sigwinch_handler, &old_winch);
      sigwinch_set_flag = 1;
    }
#endif /* SIGWINCH */

  return 0;
}

int
rl_clear_signals ()
{
  sighandler_cxt dummy;

  if (rl_catch_signals && signals_set_flag == 1)
    {
      sigemptyset (&dummy.sa_mask);

      rl_sigaction (SIGINT, &old_int, &dummy);
      rl_sigaction (SIGTERM, &old_term, &dummy);
      rl_sigaction (SIGQUIT, &old_quit, &dummy);
      rl_sigaction (SIGALRM, &old_alrm, &dummy);

#if defined (SIGTSTP)
      rl_sigaction (SIGTSTP, &old_tstp, &dummy);
#endif /* SIGTSTP */

#if defined (SIGTTOU)
      rl_sigaction (SIGTTOU, &old_ttou, &dummy);
#endif /* SIGTTOU */

#if defined (SIGTTIN)
      rl_sigaction (SIGTTIN, &old_ttin, &dummy);
#endif /* SIGTTIN */

      signals_set_flag = 0;
    }

#if defined (SIGWINCH)
  if (rl_catch_sigwinch && sigwinch_set_flag == 1)
    {
      sigemptyset (&dummy.sa_mask);
      rl_sigaction (SIGWINCH, &old_winch, &dummy);
      sigwinch_set_flag = 0;
    }
#endif

  return 0;
}

/* Clean up the terminal and readline state after catching a signal, before
   resending it to the calling application. */
void
rl_cleanup_after_signal ()
{
  _rl_clean_up_for_exit ();
  (*rl_deprep_term_function) ();
  rl_clear_signals ();
  rl_clear_pending_input ();
}

/* Reset the terminal and readline state after a signal handler returns. */
void
rl_reset_after_signal ()
{
  (*rl_prep_term_function) (_rl_meta_flag);
  rl_set_signals ();
}

/* Free up the readline variable line state for the current line (undo list,
   any partial history entry, any keyboard macros in progress, and any
   numeric arguments in process) after catching a signal, before calling
   rl_cleanup_after_signal(). */
void
rl_free_line_state ()
{
  register HIST_ENTRY *entry;

  rl_free_undo_list ();

  entry = current_history ();
  if (entry)
    entry->data = (char *)NULL;

  _rl_kill_kbd_macro ();
  rl_clear_message ();
  _rl_init_argument ();
}

#endif  /* HANDLE_SIGNALS */
