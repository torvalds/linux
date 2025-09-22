/* display.c -- readline redisplay facility. */

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

#include <sys/types.h>

#if defined (HAVE_UNISTD_H)
#  include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include "posixstat.h"

#if defined (HAVE_STDLIB_H)
#  include <stdlib.h>
#else
#  include "ansi_stdlib.h"
#endif /* HAVE_STDLIB_H */

#include <stdio.h>

/* System-specific feature definitions and include files. */
#include "rldefs.h"
#include "rlmbutil.h"

/* Termcap library stuff. */
#include "tcap.h"

/* Some standard library routines. */
#include "readline.h"
#include "history.h"

#include "rlprivate.h"
#include "xmalloc.h"

#if !defined (strchr) && !defined (__STDC__)
extern char *strchr (), *strrchr ();
#endif /* !strchr && !__STDC__ */

#if defined (HACK_TERMCAP_MOTION)
extern char *_rl_term_forward_char;
#endif

static void update_line PARAMS((char *, char *, int, int, int, int));
static void space_to_eol PARAMS((int));
static void delete_chars PARAMS((int));
static void insert_some_chars PARAMS((char *, int, int));
static void cr PARAMS((void));

#if defined (HANDLE_MULTIBYTE)
static int _rl_col_width PARAMS((const char *, int, int));
static int *_rl_wrapped_line;
#else
#  define _rl_col_width(l, s, e)	(((e) <= (s)) ? 0 : (e) - (s))
#endif

static int *inv_lbreaks, *vis_lbreaks;
static int inv_lbsize, vis_lbsize;

/* Heuristic used to decide whether it is faster to move from CUR to NEW
   by backing up or outputting a carriage return and moving forward. */
#define CR_FASTER(new, cur) (((new) + 1) < ((cur) - (new)))

/* **************************************************************** */
/*								    */
/*			Display stuff				    */
/*								    */
/* **************************************************************** */

/* This is the stuff that is hard for me.  I never seem to write good
   display routines in C.  Let's see how I do this time. */

/* (PWP) Well... Good for a simple line updater, but totally ignores
   the problems of input lines longer than the screen width.

   update_line and the code that calls it makes a multiple line,
   automatically wrapping line update.  Careful attention needs
   to be paid to the vertical position variables. */

/* Keep two buffers; one which reflects the current contents of the
   screen, and the other to draw what we think the new contents should
   be.  Then compare the buffers, and make whatever changes to the
   screen itself that we should.  Finally, make the buffer that we
   just drew into be the one which reflects the current contents of the
   screen, and place the cursor where it belongs.

   Commands that want to can fix the display themselves, and then let
   this function know that the display has been fixed by setting the
   RL_DISPLAY_FIXED variable.  This is good for efficiency. */

/* Application-specific redisplay function. */
rl_voidfunc_t *rl_redisplay_function = rl_redisplay;

/* Global variables declared here. */
/* What YOU turn on when you have handled all redisplay yourself. */
int rl_display_fixed = 0;

int _rl_suppress_redisplay = 0;

/* The stuff that gets printed out before the actual text of the line.
   This is usually pointing to rl_prompt. */
char *rl_display_prompt = (char *)NULL;

/* Pseudo-global variables declared here. */
/* The visible cursor position.  If you print some text, adjust this. */
int _rl_last_c_pos = 0;
int _rl_last_v_pos = 0;

/* Number of lines currently on screen minus 1. */
int _rl_vis_botlin = 0;

/* Variables used only in this file. */
/* The last left edge of text that was displayed.  This is used when
   doing horizontal scrolling.  It shifts in thirds of a screenwidth. */
static int last_lmargin;

/* The line display buffers.  One is the line currently displayed on
   the screen.  The other is the line about to be displayed. */
static char *visible_line = (char *)NULL;
static char *invisible_line = (char *)NULL;

/* A buffer for `modeline' messages. */
static char msg_buf[128];

/* Non-zero forces the redisplay even if we thought it was unnecessary. */
static int forced_display;

/* Default and initial buffer size.  Can grow. */
static int line_size = 1024;

/* Variables to keep track of the expanded prompt string, which may
   include invisible characters. */

static char *local_prompt, *local_prompt_prefix;
static int prompt_visible_length, prompt_prefix_length;

/* The number of invisible characters in the line currently being
   displayed on the screen. */
static int visible_wrap_offset;

/* The number of invisible characters in the prompt string.  Static so it
   can be shared between rl_redisplay and update_line */
static int wrap_offset;

/* The index of the last invisible character in the prompt string. */
static int prompt_last_invisible;

/* The length (buffer offset) of the first line of the last (possibly
   multi-line) buffer displayed on the screen. */
static int visible_first_line_len;

/* Number of invisible characters on the first physical line of the prompt.
   Only valid when the number of physical characters in the prompt exceeds
   (or is equal to) _rl_screenwidth. */
static int prompt_invis_chars_first_line;

static int prompt_last_screen_line;

/* Expand the prompt string S and return the number of visible
   characters in *LP, if LP is not null.  This is currently more-or-less
   a placeholder for expansion.  LIP, if non-null is a place to store the
   index of the last invisible character in the returned string. NIFLP,
   if non-zero, is a place to store the number of invisible characters in
   the first prompt line. */

/* Current implementation:
	\001 (^A) start non-visible characters
	\002 (^B) end non-visible characters
   all characters except \001 and \002 (following a \001) are copied to
   the returned string; all characters except those between \001 and
   \002 are assumed to be `visible'. */

static char *
expand_prompt (pmt, lp, lip, niflp)
     char *pmt;
     int *lp, *lip, *niflp;
{
  char *r, *ret, *p;
  int l, rl, last, ignoring, ninvis, invfl;

  /* Short-circuit if we can. */
  if (strchr (pmt, RL_PROMPT_START_IGNORE) == 0)
    {
      r = savestring (pmt);
      if (lp)
	*lp = strlen (r);
      return r;
    }

  l = strlen (pmt);
  r = ret = (char *)xmalloc (l + 1);

  invfl = 0;	/* invisible chars in first line of prompt */

  for (rl = ignoring = last = ninvis = 0, p = pmt; p && *p; p++)
    {
      /* This code strips the invisible character string markers
	 RL_PROMPT_START_IGNORE and RL_PROMPT_END_IGNORE */
      if (*p == RL_PROMPT_START_IGNORE)
	{
	  ignoring++;
	  continue;
	}
      else if (ignoring && *p == RL_PROMPT_END_IGNORE)
	{
	  ignoring = 0;
	  last = r - ret - 1;
	  continue;
	}
      else
	{
	  *r++ = *p;
	  if (!ignoring)
	    rl++;
	  else
	    ninvis++;
	  if (rl == _rl_screenwidth)
	    invfl = ninvis;
	}
    }

  if (rl < _rl_screenwidth)
    invfl = ninvis;

  *r = '\0';
  if (lp)
    *lp = rl;
  if (lip)
    *lip = last;
  if (niflp)
    *niflp = invfl;
  return ret;
}

/* Just strip out RL_PROMPT_START_IGNORE and RL_PROMPT_END_IGNORE from
   PMT and return the rest of PMT. */
char *
_rl_strip_prompt (pmt)
     char *pmt;
{
  char *ret;

  ret = expand_prompt (pmt, (int *)NULL, (int *)NULL, (int *)NULL);
  return ret;
}

/*
 * Expand the prompt string into the various display components, if
 * necessary.
 *
 * local_prompt = expanded last line of string in rl_display_prompt
 *		  (portion after the final newline)
 * local_prompt_prefix = portion before last newline of rl_display_prompt,
 *			 expanded via expand_prompt
 * prompt_visible_length = number of visible characters in local_prompt
 * prompt_prefix_length = number of visible characters in local_prompt_prefix
 *
 * This function is called once per call to readline().  It may also be
 * called arbitrarily to expand the primary prompt.
 *
 * The return value is the number of visible characters on the last line
 * of the (possibly multi-line) prompt.
 */
int
rl_expand_prompt (prompt)
     char *prompt;
{
  char *p, *t;
  int c;

  /* Clear out any saved values. */
  FREE (local_prompt);
  FREE (local_prompt_prefix);

  local_prompt = local_prompt_prefix = (char *)0;
  prompt_last_invisible = prompt_visible_length = 0;

  if (prompt == 0 || *prompt == 0)
    return (0);

  p = strrchr (prompt, '\n');
  if (!p)
    {
      /* The prompt is only one logical line, though it might wrap. */
      local_prompt = expand_prompt (prompt, &prompt_visible_length,
					    &prompt_last_invisible,
					    &prompt_invis_chars_first_line);
      local_prompt_prefix = (char *)0;
      return (prompt_visible_length);
    }
  else
    {
      /* The prompt spans multiple lines. */
      t = ++p;
      local_prompt = expand_prompt (p, &prompt_visible_length,
				       &prompt_last_invisible,
				       &prompt_invis_chars_first_line);
      c = *t; *t = '\0';
      /* The portion of the prompt string up to and including the
	 final newline is now null-terminated. */
      local_prompt_prefix = expand_prompt (prompt, &prompt_prefix_length,
						   (int *)NULL,
						   &prompt_invis_chars_first_line);
      *t = c;
      return (prompt_prefix_length);
    }
}

/* Initialize the VISIBLE_LINE and INVISIBLE_LINE arrays, and their associated
   arrays of line break markers.  MINSIZE is the minimum size of VISIBLE_LINE
   and INVISIBLE_LINE; if it is greater than LINE_SIZE, LINE_SIZE is
   increased.  If the lines have already been allocated, this ensures that
   they can hold at least MINSIZE characters. */
static void
init_line_structures (minsize)
      int minsize;
{
  register int n;

  if (invisible_line == 0)	/* initialize it */
    {
      if (line_size < minsize)
	line_size = minsize;
      visible_line = (char *)xmalloc (line_size);
      invisible_line = (char *)xmalloc (line_size);
    }
  else if (line_size < minsize)	/* ensure it can hold MINSIZE chars */
    {
      line_size *= 2;
      if (line_size < minsize)
	line_size = minsize;
      visible_line = (char *)xrealloc (visible_line, line_size);
      invisible_line = (char *)xrealloc (invisible_line, line_size);
    }

  for (n = minsize; n < line_size; n++)
    {
      visible_line[n] = 0;
      invisible_line[n] = 1;
    }

  if (vis_lbreaks == 0)
    {
      /* should be enough. */
      inv_lbsize = vis_lbsize = 256;
      inv_lbreaks = (int *)xmalloc (inv_lbsize * sizeof (int));
      vis_lbreaks = (int *)xmalloc (vis_lbsize * sizeof (int));
#if defined (HANDLE_MULTIBYTE)
      _rl_wrapped_line = (int *)xmalloc (vis_lbsize * sizeof (int));
#endif
      inv_lbreaks[0] = vis_lbreaks[0] = 0;
    }
}

/* Basic redisplay algorithm. */
void
rl_redisplay ()
{
  register int in, out, c, linenum, cursor_linenum;
  register char *line;
  int c_pos, inv_botlin, lb_botlin, lb_linenum;
  int newlines, lpos, temp;
  char *prompt_this_line;
#if defined (HANDLE_MULTIBYTE)
  wchar_t wc;
  size_t wc_bytes;
  int wc_width;
  mbstate_t ps;
  int _rl_wrapped_multicolumn = 0;
#endif

  if (!readline_echoing_p)
    return;

  if (!rl_display_prompt)
    rl_display_prompt = "";

  if (invisible_line == 0)
    {
      init_line_structures (0);
      rl_on_new_line ();
    }

  /* Draw the line into the buffer. */
  c_pos = -1;

  line = invisible_line;
  out = inv_botlin = 0;

  /* Mark the line as modified or not.  We only do this for history
     lines. */
  if (_rl_mark_modified_lines && current_history () && rl_undo_list)
    {
      line[out++] = '*';
      line[out] = '\0';
    }

  /* If someone thought that the redisplay was handled, but the currently
     visible line has a different modification state than the one about
     to become visible, then correct the caller's misconception. */
  if (visible_line[0] != invisible_line[0])
    rl_display_fixed = 0;

  /* If the prompt to be displayed is the `primary' readline prompt (the
     one passed to readline()), use the values we have already expanded.
     If not, use what's already in rl_display_prompt.  WRAP_OFFSET is the
     number of non-visible characters in the prompt string. */
  if (rl_display_prompt == rl_prompt || local_prompt)
    {
      int local_len = local_prompt ? strlen (local_prompt) : 0;
      if (local_prompt_prefix && forced_display)
	_rl_output_some_chars (local_prompt_prefix, strlen (local_prompt_prefix));

      if (local_len > 0)
	{
	  temp = local_len + out + 2;
	  if (temp >= line_size)
	    {
	      line_size = (temp + 1024) - (temp % 1024);
	      visible_line = (char *)xrealloc (visible_line, line_size);
	      line = invisible_line = (char *)xrealloc (invisible_line, line_size);
	    }
	  strncpy (line + out, local_prompt, local_len);
	  out += local_len;
	}
      line[out] = '\0';
      wrap_offset = local_len - prompt_visible_length;
    }
  else
    {
      int pmtlen;
      prompt_this_line = strrchr (rl_display_prompt, '\n');
      if (!prompt_this_line)
	prompt_this_line = rl_display_prompt;
      else
	{
	  prompt_this_line++;
	  pmtlen = prompt_this_line - rl_display_prompt;	/* temp var */
	  if (forced_display)
	    {
	      _rl_output_some_chars (rl_display_prompt, pmtlen);
	      /* Make sure we are at column zero even after a newline,
		 regardless of the state of terminal output processing. */
	      if (pmtlen < 2 || prompt_this_line[-2] != '\r')
		cr ();
	    }
	}

      pmtlen = strlen (prompt_this_line);
      temp = pmtlen + out + 2;
      if (temp >= line_size)
	{
	  line_size = (temp + 1024) - (temp % 1024);
	  visible_line = (char *)xrealloc (visible_line, line_size);
	  line = invisible_line = (char *)xrealloc (invisible_line, line_size);
	}
      strncpy (line + out,  prompt_this_line, pmtlen);
      out += pmtlen;
      line[out] = '\0';
      wrap_offset = prompt_invis_chars_first_line = 0;
    }

#define CHECK_INV_LBREAKS() \
      do { \
	if (newlines >= (inv_lbsize - 2)) \
	  { \
	    inv_lbsize *= 2; \
	    inv_lbreaks = (int *)xrealloc (inv_lbreaks, inv_lbsize * sizeof (int)); \
	  } \
      } while (0)

#if defined (HANDLE_MULTIBYTE)
#define CHECK_LPOS() \
      do { \
	lpos++; \
	if (lpos >= _rl_screenwidth) \
	  { \
	    if (newlines >= (inv_lbsize - 2)) \
	      { \
		inv_lbsize *= 2; \
		inv_lbreaks = (int *)xrealloc (inv_lbreaks, inv_lbsize * sizeof (int)); \
		_rl_wrapped_line = (int *)xrealloc (_rl_wrapped_line, inv_lbsize * sizeof (int)); \
	      } \
	    inv_lbreaks[++newlines] = out; \
	    _rl_wrapped_line[newlines] = _rl_wrapped_multicolumn; \
	    lpos = 0; \
	  } \
      } while (0)
#else
#define CHECK_LPOS() \
      do { \
	lpos++; \
	if (lpos >= _rl_screenwidth) \
	  { \
	    if (newlines >= (inv_lbsize - 2)) \
	      { \
		inv_lbsize *= 2; \
		inv_lbreaks = (int *)xrealloc (inv_lbreaks, inv_lbsize * sizeof (int)); \
	      } \
	    inv_lbreaks[++newlines] = out; \
	    lpos = 0; \
	  } \
      } while (0)
#endif

  /* inv_lbreaks[i] is where line i starts in the buffer. */
  inv_lbreaks[newlines = 0] = 0;
  lpos = out - wrap_offset;
#if defined (HANDLE_MULTIBYTE)
  memset (_rl_wrapped_line, 0, vis_lbsize);
#endif

  /* prompt_invis_chars_first_line is the number of invisible characters in
     the first physical line of the prompt.
     wrap_offset - prompt_invis_chars_first_line is the number of invis
     chars on the second line. */

  /* what if lpos is already >= _rl_screenwidth before we start drawing the
     contents of the command line? */
  while (lpos >= _rl_screenwidth)
    {
      /* fix from Darin Johnson <darin@acuson.com> for prompt string with
         invisible characters that is longer than the screen width.  The
         prompt_invis_chars_first_line variable could be made into an array
         saying how many invisible characters there are per line, but that's
         probably too much work for the benefit gained.  How many people have
         prompts that exceed two physical lines? */
      temp = ((newlines + 1) * _rl_screenwidth) +
#if 0
             ((newlines == 0) ? prompt_invis_chars_first_line : 0) +
#else
             ((newlines == 0 && local_prompt_prefix == 0) ? prompt_invis_chars_first_line : 0) +
#endif
             ((newlines == 1) ? wrap_offset : 0);

      inv_lbreaks[++newlines] = temp;
      lpos -= _rl_screenwidth;
    }

  prompt_last_screen_line = newlines;

  /* Draw the rest of the line (after the prompt) into invisible_line, keeping
     track of where the cursor is (c_pos), the number of the line containing
     the cursor (lb_linenum), the last line number (lb_botlin and inv_botlin).
     It maintains an array of line breaks for display (inv_lbreaks).
     This handles expanding tabs for display and displaying meta characters. */
  lb_linenum = 0;
#if defined (HANDLE_MULTIBYTE)
  in = 0;
  if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
    {
      memset (&ps, 0, sizeof (mbstate_t));
      wc_bytes = mbrtowc (&wc, rl_line_buffer, rl_end, &ps);
    }
  else
    wc_bytes = 1;
  while (in < rl_end)
#else
  for (in = 0; in < rl_end; in++)
#endif
    {
      c = (unsigned char)rl_line_buffer[in];

#if defined (HANDLE_MULTIBYTE)
      if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
	{
	  if (wc_bytes == (size_t)-1 || wc_bytes == (size_t)-2)
	    {
	      /* Byte sequence is invalid or shortened.  Assume that the
	         first byte represents a character. */
	      wc_bytes = 1;
	      /* Assume that a character occupies a single column. */
	      wc_width = 1;
	      memset (&ps, 0, sizeof (mbstate_t));
	    }
	  else if (wc_bytes == (size_t)0)
	    break;			/* Found '\0' */
	  else
	    {
	      temp = wcwidth (wc);
	      wc_width = (temp < 0) ? 1 : temp;
	    }
	}
#endif

      if (out + 8 >= line_size)		/* XXX - 8 for \t */
	{
	  line_size *= 2;
	  visible_line = (char *)xrealloc (visible_line, line_size);
	  invisible_line = (char *)xrealloc (invisible_line, line_size);
	  line = invisible_line;
	}

      if (in == rl_point)
	{
	  c_pos = out;
	  lb_linenum = newlines;
	}

#if defined (HANDLE_MULTIBYTE)
      if (META_CHAR (c) && _rl_output_meta_chars == 0)	/* XXX - clean up */
#else
      if (META_CHAR (c))
#endif
	{
	  if (_rl_output_meta_chars == 0)
	    {
	      snprintf (line + out, line_size - out, "\\%o", c);

	      if (lpos + 4 >= _rl_screenwidth)
		{
		  temp = _rl_screenwidth - lpos;
		  CHECK_INV_LBREAKS ();
		  inv_lbreaks[++newlines] = out + temp;
		  lpos = 4 - temp;
		}
	      else
		lpos += 4;

	      out += 4;
	    }
	  else
	    {
	      line[out++] = c;
	      CHECK_LPOS();
	    }
	}
#if defined (DISPLAY_TABS)
      else if (c == '\t')
	{
	  register int newout;

#if 0
	  newout = (out | (int)7) + 1;
#else
	  newout = out + 8 - lpos % 8;
#endif
	  temp = newout - out;
	  if (lpos + temp >= _rl_screenwidth)
	    {
	      register int temp2;
	      temp2 = _rl_screenwidth - lpos;
	      CHECK_INV_LBREAKS ();
	      inv_lbreaks[++newlines] = out + temp2;
	      lpos = temp - temp2;
	      while (out < newout)
		line[out++] = ' ';
	    }
	  else
	    {
	      while (out < newout)
		line[out++] = ' ';
	      lpos += temp;
	    }
	}
#endif
      else if (c == '\n' && _rl_horizontal_scroll_mode == 0 && _rl_term_up && *_rl_term_up)
	{
	  line[out++] = '\0';	/* XXX - sentinel */
	  CHECK_INV_LBREAKS ();
	  inv_lbreaks[++newlines] = out;
	  lpos = 0;
	}
      else if (CTRL_CHAR (c) || c == RUBOUT)
	{
	  line[out++] = '^';
	  CHECK_LPOS();
	  line[out++] = CTRL_CHAR (c) ? UNCTRL (c) : '?';
	  CHECK_LPOS();
	}
      else
	{
#if defined (HANDLE_MULTIBYTE)
	  if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
	    {
	      register int i;

	      _rl_wrapped_multicolumn = 0;

	      if (_rl_screenwidth < lpos + wc_width)
		for (i = lpos; i < _rl_screenwidth; i++)
		  {
		    /* The space will be removed in update_line() */
		    line[out++] = ' ';
		    _rl_wrapped_multicolumn++;
		    CHECK_LPOS();
		  }
	      if (in == rl_point)
		{
		  c_pos = out;
		  lb_linenum = newlines;
		}
	      for (i = in; i < in+wc_bytes; i++)
		line[out++] = rl_line_buffer[i];
	      for (i = 0; i < wc_width; i++)
		CHECK_LPOS();
	    }
	  else
	    {
	      line[out++] = c;
	      CHECK_LPOS();
	    }
#else
	  line[out++] = c;
	  CHECK_LPOS();
#endif
	}

#if defined (HANDLE_MULTIBYTE)
      if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
	{
	  in += wc_bytes;
	  wc_bytes = mbrtowc (&wc, rl_line_buffer + in, rl_end - in, &ps);
	}
      else
        in++;
#endif

    }
  line[out] = '\0';
  if (c_pos < 0)
    {
      c_pos = out;
      lb_linenum = newlines;
    }

  inv_botlin = lb_botlin = newlines;
  CHECK_INV_LBREAKS ();
  inv_lbreaks[newlines+1] = out;
  cursor_linenum = lb_linenum;

  /* C_POS == position in buffer where cursor should be placed.
     CURSOR_LINENUM == line number where the cursor should be placed. */

  /* PWP: now is when things get a bit hairy.  The visible and invisible
     line buffers are really multiple lines, which would wrap every
     (screenwidth - 1) characters.  Go through each in turn, finding
     the changed region and updating it.  The line order is top to bottom. */

  /* If we can move the cursor up and down, then use multiple lines,
     otherwise, let long lines display in a single terminal line, and
     horizontally scroll it. */

  if (_rl_horizontal_scroll_mode == 0 && _rl_term_up && *_rl_term_up)
    {
      int nleft, pos, changed_screen_line;

      if (!rl_display_fixed || forced_display)
	{
	  forced_display = 0;

	  /* If we have more than a screenful of material to display, then
	     only display a screenful.  We should display the last screen,
	     not the first.  */
	  if (out >= _rl_screenchars)
	    {
	      if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
		out = _rl_find_prev_mbchar (line, _rl_screenchars, MB_FIND_ANY);
	      else
		out = _rl_screenchars - 1;
	    }

	  /* The first line is at character position 0 in the buffer.  The
	     second and subsequent lines start at inv_lbreaks[N], offset by
	     OFFSET (which has already been calculated above).  */

#define W_OFFSET(line, offset) ((line) == 0 ? offset : 0)
#define VIS_LLEN(l)	((l) > _rl_vis_botlin ? 0 : (vis_lbreaks[l+1] - vis_lbreaks[l]))
#define INV_LLEN(l)	(inv_lbreaks[l+1] - inv_lbreaks[l])
#define VIS_CHARS(line) (visible_line + vis_lbreaks[line])
#define VIS_LINE(line) ((line) > _rl_vis_botlin) ? "" : VIS_CHARS(line)
#define INV_LINE(line) (invisible_line + inv_lbreaks[line])

	  /* For each line in the buffer, do the updating display. */
	  for (linenum = 0; linenum <= inv_botlin; linenum++)
	    {
	      update_line (VIS_LINE(linenum), INV_LINE(linenum), linenum,
			   VIS_LLEN(linenum), INV_LLEN(linenum), inv_botlin);

	      /* If this is the line with the prompt, we might need to
		 compensate for invisible characters in the new line. Do
		 this only if there is not more than one new line (which
		 implies that we completely overwrite the old visible line)
		 and the new line is shorter than the old.  Make sure we are
		 at the end of the new line before clearing. */
	      if (linenum == 0 &&
		  inv_botlin == 0 && _rl_last_c_pos == out &&
		  (wrap_offset > visible_wrap_offset) &&
		  (_rl_last_c_pos < visible_first_line_len))
		{
		  nleft = _rl_screenwidth + wrap_offset - _rl_last_c_pos;
		  if (nleft)
		    _rl_clear_to_eol (nleft);
		}

	      /* Since the new first line is now visible, save its length. */
	      if (linenum == 0)
		visible_first_line_len = (inv_botlin > 0) ? inv_lbreaks[1] : out - wrap_offset;
	    }

	  /* We may have deleted some lines.  If so, clear the left over
	     blank ones at the bottom out. */
	  if (_rl_vis_botlin > inv_botlin)
	    {
	      char *tt;
	      for (; linenum <= _rl_vis_botlin; linenum++)
		{
		  tt = VIS_CHARS (linenum);
		  _rl_move_vert (linenum);
		  _rl_move_cursor_relative (0, tt);
		  _rl_clear_to_eol
		    ((linenum == _rl_vis_botlin) ? strlen (tt) : _rl_screenwidth);
		}
	    }
	  _rl_vis_botlin = inv_botlin;

	  /* CHANGED_SCREEN_LINE is set to 1 if we have moved to a
	     different screen line during this redisplay. */
	  changed_screen_line = _rl_last_v_pos != cursor_linenum;
	  if (changed_screen_line)
	    {
	      _rl_move_vert (cursor_linenum);
	      /* If we moved up to the line with the prompt using _rl_term_up,
		 the physical cursor position on the screen stays the same,
		 but the buffer position needs to be adjusted to account
		 for invisible characters. */
	      if (cursor_linenum == 0 && wrap_offset)
		_rl_last_c_pos += wrap_offset;
	    }

	  /* We have to reprint the prompt if it contains invisible
	     characters, since it's not generally OK to just reprint
	     the characters from the current cursor position.  But we
	     only need to reprint it if the cursor is before the last
	     invisible character in the prompt string. */
	  nleft = prompt_visible_length + wrap_offset;
	  if (cursor_linenum == 0 && wrap_offset > 0 && _rl_last_c_pos > 0 &&
	      _rl_last_c_pos <= prompt_last_invisible && local_prompt)
	    {
#if defined (__MSDOS__)
	      putc ('\r', rl_outstream);
#else
	      if (_rl_term_cr)
		tputs (_rl_term_cr, 1, _rl_output_character_function);
#endif
	      _rl_output_some_chars (local_prompt, nleft);
	      if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
		_rl_last_c_pos = _rl_col_width(local_prompt, 0, nleft);
	      else
		_rl_last_c_pos = nleft;
	    }

	  /* Where on that line?  And where does that line start
	     in the buffer? */
	  pos = inv_lbreaks[cursor_linenum];
	  /* nleft == number of characters in the line buffer between the
	     start of the line and the cursor position. */
	  nleft = c_pos - pos;

	  /* Since _rl_backspace() doesn't know about invisible characters in the
	     prompt, and there's no good way to tell it, we compensate for
	     those characters here and call _rl_backspace() directly. */
	  if (wrap_offset && cursor_linenum == 0 && nleft < _rl_last_c_pos)
	    {
	      _rl_backspace (_rl_last_c_pos - nleft);
	      if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
		_rl_last_c_pos = _rl_col_width (&visible_line[pos], 0, nleft);
	      else
		_rl_last_c_pos = nleft;
	    }

	  if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
	    _rl_move_cursor_relative (nleft, &invisible_line[pos]);
	  else if (nleft != _rl_last_c_pos)
	    _rl_move_cursor_relative (nleft, &invisible_line[pos]);
	}
    }
  else				/* Do horizontal scrolling. */
    {
#define M_OFFSET(margin, offset) ((margin) == 0 ? offset : 0)
      int lmargin, ndisp, nleft, phys_c_pos, t;

      /* Always at top line. */
      _rl_last_v_pos = 0;

      /* Compute where in the buffer the displayed line should start.  This
	 will be LMARGIN. */

      /* The number of characters that will be displayed before the cursor. */
      ndisp = c_pos - wrap_offset;
      nleft  = prompt_visible_length + wrap_offset;
      /* Where the new cursor position will be on the screen.  This can be
	 longer than SCREENWIDTH; if it is, lmargin will be adjusted. */
      phys_c_pos = c_pos - (last_lmargin ? last_lmargin : wrap_offset);
      t = _rl_screenwidth / 3;

      /* If the number of characters had already exceeded the screenwidth,
	 last_lmargin will be > 0. */

      /* If the number of characters to be displayed is more than the screen
	 width, compute the starting offset so that the cursor is about
	 two-thirds of the way across the screen. */
      if (phys_c_pos > _rl_screenwidth - 2)
	{
	  lmargin = c_pos - (2 * t);
	  if (lmargin < 0)
	    lmargin = 0;
	  /* If the left margin would be in the middle of a prompt with
	     invisible characters, don't display the prompt at all. */
	  if (wrap_offset && lmargin > 0 && lmargin < nleft)
	    lmargin = nleft;
	}
      else if (ndisp < _rl_screenwidth - 2)		/* XXX - was -1 */
	lmargin = 0;
      else if (phys_c_pos < 1)
	{
	  /* If we are moving back towards the beginning of the line and
	     the last margin is no longer correct, compute a new one. */
	  lmargin = ((c_pos - 1) / t) * t;	/* XXX */
	  if (wrap_offset && lmargin > 0 && lmargin < nleft)
	    lmargin = nleft;
	}
      else
	lmargin = last_lmargin;

      /* If the first character on the screen isn't the first character
	 in the display line, indicate this with a special character. */
      if (lmargin > 0)
	line[lmargin] = '<';

      /* If SCREENWIDTH characters starting at LMARGIN do not encompass
	 the whole line, indicate that with a special character at the
	 right edge of the screen.  If LMARGIN is 0, we need to take the
	 wrap offset into account. */
      t = lmargin + M_OFFSET (lmargin, wrap_offset) + _rl_screenwidth;
      if (t < out)
	line[t - 1] = '>';

      if (!rl_display_fixed || forced_display || lmargin != last_lmargin)
	{
	  forced_display = 0;
	  update_line (&visible_line[last_lmargin],
		       &invisible_line[lmargin],
		       0,
		       _rl_screenwidth + visible_wrap_offset,
		       _rl_screenwidth + (lmargin ? 0 : wrap_offset),
		       0);

	  /* If the visible new line is shorter than the old, but the number
	     of invisible characters is greater, and we are at the end of
	     the new line, we need to clear to eol. */
	  t = _rl_last_c_pos - M_OFFSET (lmargin, wrap_offset);
	  if ((M_OFFSET (lmargin, wrap_offset) > visible_wrap_offset) &&
	      (_rl_last_c_pos == out) &&
	      t < visible_first_line_len)
	    {
	      nleft = _rl_screenwidth - t;
	      _rl_clear_to_eol (nleft);
	    }
	  visible_first_line_len = out - lmargin - M_OFFSET (lmargin, wrap_offset);
	  if (visible_first_line_len > _rl_screenwidth)
	    visible_first_line_len = _rl_screenwidth;

	  _rl_move_cursor_relative (c_pos - lmargin, &invisible_line[lmargin]);
	  last_lmargin = lmargin;
	}
    }
  fflush (rl_outstream);

  /* Swap visible and non-visible lines. */
  {
    char *vtemp = visible_line;
    int *itemp = vis_lbreaks, ntemp = vis_lbsize;

    visible_line = invisible_line;
    invisible_line = vtemp;

    vis_lbreaks = inv_lbreaks;
    inv_lbreaks = itemp;

    vis_lbsize = inv_lbsize;
    inv_lbsize = ntemp;

    rl_display_fixed = 0;
    /* If we are displaying on a single line, and last_lmargin is > 0, we
       are not displaying any invisible characters, so set visible_wrap_offset
       to 0. */
    if (_rl_horizontal_scroll_mode && last_lmargin)
      visible_wrap_offset = 0;
    else
      visible_wrap_offset = wrap_offset;
  }
}

/* PWP: update_line() is based on finding the middle difference of each
   line on the screen; vis:

			     /old first difference
	/beginning of line   |	      /old last same       /old EOL
	v		     v	      v		    v
old:	eddie> Oh, my little gruntle-buggy is to me, as lurgid as
new:	eddie> Oh, my little buggy says to me, as lurgid as
	^		     ^	^			   ^
	\beginning of line   |	\new last same	   \new end of line
			     \new first difference

   All are character pointers for the sake of speed.  Special cases for
   no differences, as well as for end of line additions must be handled.

   Could be made even smarter, but this works well enough */
static void
update_line (old, new, current_line, omax, nmax, inv_botlin)
     register char *old, *new;
     int current_line, omax, nmax, inv_botlin;
{
  register char *ofd, *ols, *oe, *nfd, *nls, *ne;
  int temp, lendiff, wsatend, od, nd;
  int current_invis_chars;
  int col_lendiff, col_temp;
#if defined (HANDLE_MULTIBYTE)
  mbstate_t ps_new, ps_old;
  int new_offset, old_offset, tmp;
#endif

  /* If we're at the right edge of a terminal that supports xn, we're
     ready to wrap around, so do so.  This fixes problems with knowing
     the exact cursor position and cut-and-paste with certain terminal
     emulators.  In this calculation, TEMP is the physical screen
     position of the cursor. */
  temp = _rl_last_c_pos - W_OFFSET(_rl_last_v_pos, visible_wrap_offset);
  if (temp == _rl_screenwidth && _rl_term_autowrap && !_rl_horizontal_scroll_mode
	&& _rl_last_v_pos == current_line - 1)
    {
#if defined (HANDLE_MULTIBYTE)
      if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
	{
	  wchar_t wc;
	  mbstate_t ps;
	  int tempwidth, bytes;
	  size_t ret;

	  /* This fixes only double-column characters, but if the wrapped
	     character comsumes more than three columns, spaces will be
	     inserted in the string buffer. */
	  if (_rl_wrapped_line[current_line] > 0)
	    _rl_clear_to_eol (_rl_wrapped_line[current_line]);

	  memset (&ps, 0, sizeof (mbstate_t));
	  ret = mbrtowc (&wc, new, MB_CUR_MAX, &ps);
	  if (ret == (size_t)-1 || ret == (size_t)-2)
	    {
	      tempwidth = 1;
	      ret = 1;
	    }
	  else if (ret == 0)
	    tempwidth = 0;
	  else
	    tempwidth = wcwidth (wc);

	  if (tempwidth > 0)
	    {
	      int count;
	      bytes = ret;
	      for (count = 0; count < bytes; count++)
		putc (new[count], rl_outstream);
	      _rl_last_c_pos = tempwidth;
	      _rl_last_v_pos++;
	      memset (&ps, 0, sizeof (mbstate_t));
	      ret = mbrtowc (&wc, old, MB_CUR_MAX, &ps);
	      if (ret != 0 && bytes != 0)
		{
		  if (ret == (size_t)-1 || ret == (size_t)-2)
		    memmove (old+bytes, old+1, strlen (old+1));
		  else
		    memmove (old+bytes, old+ret, strlen (old+ret));
		  memcpy (old, new, bytes);
		}
	    }
	  else
	    {
	      putc (' ', rl_outstream);
	      _rl_last_c_pos = 1;
	      _rl_last_v_pos++;
	      if (old[0] && new[0])
		old[0] = new[0];
	    }
	}
      else
#endif
	{
	  if (new[0])
	    putc (new[0], rl_outstream);
	  else
	    putc (' ', rl_outstream);
	  _rl_last_c_pos = 1;		/* XXX */
	  _rl_last_v_pos++;
	  if (old[0] && new[0])
	    old[0] = new[0];
	}
    }


  /* Find first difference. */
#if defined (HANDLE_MULTIBYTE)
  if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
    {
      memset (&ps_new, 0, sizeof(mbstate_t));
      memset (&ps_old, 0, sizeof(mbstate_t));

      new_offset = old_offset = 0;
      for (ofd = old, nfd = new;
	   (ofd - old < omax) && *ofd &&
	     _rl_compare_chars(old, old_offset, &ps_old, new, new_offset, &ps_new); )
	{
	  old_offset = _rl_find_next_mbchar (old, old_offset, 1, MB_FIND_ANY);
	  new_offset = _rl_find_next_mbchar (new, new_offset, 1, MB_FIND_ANY);
	  ofd = old + old_offset;
	  nfd = new + new_offset;
	}
    }
  else
#endif
  for (ofd = old, nfd = new;
       (ofd - old < omax) && *ofd && (*ofd == *nfd);
       ofd++, nfd++)
    ;

  /* Move to the end of the screen line.  ND and OD are used to keep track
     of the distance between ne and new and oe and old, respectively, to
     move a subtraction out of each loop. */
  for (od = ofd - old, oe = ofd; od < omax && *oe; oe++, od++);
  for (nd = nfd - new, ne = nfd; nd < nmax && *ne; ne++, nd++);

  /* If no difference, continue to next line. */
  if (ofd == oe && nfd == ne)
    return;

  wsatend = 1;			/* flag for trailing whitespace */

#if defined (HANDLE_MULTIBYTE)
  if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
    {
      ols = old + _rl_find_prev_mbchar (old, oe - old, MB_FIND_ANY);
      nls = new + _rl_find_prev_mbchar (new, ne - new, MB_FIND_ANY);
      while ((ols > ofd) && (nls > nfd))
	{
	  memset (&ps_old, 0, sizeof (mbstate_t));
	  memset (&ps_new, 0, sizeof (mbstate_t));

	  _rl_adjust_point (old, ols - old, &ps_old);
	  _rl_adjust_point (new, nls - new, &ps_new);

	  if (_rl_compare_chars (old, ols - old, &ps_old, new, nls - new, &ps_new) == 0)
	    break;

	  if (*ols == ' ')
	    wsatend = 0;

	  ols = old + _rl_find_prev_mbchar (old, ols - old, MB_FIND_ANY);
	  nls = new + _rl_find_prev_mbchar (new, nls - new, MB_FIND_ANY);
	}
    }
  else
    {
#endif /* HANDLE_MULTIBYTE */
  ols = oe - 1;			/* find last same */
  nls = ne - 1;
  while ((ols > ofd) && (nls > nfd) && (*ols == *nls))
    {
      if (*ols != ' ')
	wsatend = 0;
      ols--;
      nls--;
    }
#if defined (HANDLE_MULTIBYTE)
    }
#endif

  if (wsatend)
    {
      ols = oe;
      nls = ne;
    }
#if defined (HANDLE_MULTIBYTE)
  /* This may not work for stateful encoding, but who cares?  To handle
     stateful encoding properly, we have to scan each string from the
     beginning and compare. */
  else if (_rl_compare_chars (ols, 0, NULL, nls, 0, NULL) == 0)
#else
  else if (*ols != *nls)
#endif
    {
      if (*ols)			/* don't step past the NUL */
	{
	  if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
	    ols = old + _rl_find_next_mbchar (old, ols - old, 1, MB_FIND_ANY);
	  else
	    ols++;
	}
      if (*nls)
	{
	  if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
	    nls = new + _rl_find_next_mbchar (new, nls - new, 1, MB_FIND_ANY);
	  else
	    nls++;
	}
    }

  /* count of invisible characters in the current invisible line. */
  current_invis_chars = W_OFFSET (current_line, wrap_offset);
  if (_rl_last_v_pos != current_line)
    {
      _rl_move_vert (current_line);
      if (current_line == 0 && visible_wrap_offset)
	_rl_last_c_pos += visible_wrap_offset;
    }

  /* If this is the first line and there are invisible characters in the
     prompt string, and the prompt string has not changed, and the current
     cursor position is before the last invisible character in the prompt,
     and the index of the character to move to is past the end of the prompt
     string, then redraw the entire prompt string.  We can only do this
     reliably if the terminal supports a `cr' capability.

     This is not an efficiency hack -- there is a problem with redrawing
     portions of the prompt string if they contain terminal escape
     sequences (like drawing the `unbold' sequence without a corresponding
     `bold') that manifests itself on certain terminals. */

  lendiff = local_prompt ? strlen (local_prompt) : 0;
  od = ofd - old;	/* index of first difference in visible line */
  if (current_line == 0 && !_rl_horizontal_scroll_mode &&
      _rl_term_cr && lendiff > prompt_visible_length && _rl_last_c_pos > 0 &&
      od >= lendiff && _rl_last_c_pos <= prompt_last_invisible)
    {
#if defined (__MSDOS__)
      putc ('\r', rl_outstream);
#else
      tputs (_rl_term_cr, 1, _rl_output_character_function);
#endif
      _rl_output_some_chars (local_prompt, lendiff);
      if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
	_rl_last_c_pos = _rl_col_width (local_prompt, 0, lendiff);
      else
	_rl_last_c_pos = lendiff;
    }

  _rl_move_cursor_relative (od, old);

  /* if (len (new) > len (old))
     lendiff == difference in buffer
     col_lendiff == difference on screen
     When not using multibyte characters, these are equal */
  lendiff = (nls - nfd) - (ols - ofd);
  if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
    col_lendiff = _rl_col_width (new, nfd - new, nls - new) - _rl_col_width (old, ofd - old, ols - old);
  else
    col_lendiff = lendiff;

  /* If we are changing the number of invisible characters in a line, and
     the spot of first difference is before the end of the invisible chars,
     lendiff needs to be adjusted. */
  if (current_line == 0 && !_rl_horizontal_scroll_mode &&
      current_invis_chars != visible_wrap_offset)
    {
      if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
	{
	  lendiff += visible_wrap_offset - current_invis_chars;
	  col_lendiff += visible_wrap_offset - current_invis_chars;
	}
      else
	{
	  lendiff += visible_wrap_offset - current_invis_chars;
	  col_lendiff = lendiff;
	}
    }

  /* Insert (diff (len (old), len (new)) ch. */
  temp = ne - nfd;
  if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
    col_temp = _rl_col_width (new, nfd - new, ne - new);
  else
    col_temp = temp;

  if (col_lendiff > 0)	/* XXX - was lendiff */
    {
      /* Non-zero if we're increasing the number of lines. */
      int gl = current_line >= _rl_vis_botlin && inv_botlin > _rl_vis_botlin;
      /* Sometimes it is cheaper to print the characters rather than
	 use the terminal's capabilities.  If we're growing the number
	 of lines, make sure we actually cause the new line to wrap
	 around on auto-wrapping terminals. */
      if (_rl_terminal_can_insert && ((2 * col_temp) >= col_lendiff || _rl_term_IC) && (!_rl_term_autowrap || !gl))
	{
	  /* If lendiff > prompt_visible_length and _rl_last_c_pos == 0 and
	     _rl_horizontal_scroll_mode == 1, inserting the characters with
	     _rl_term_IC or _rl_term_ic will screw up the screen because of the
	     invisible characters.  We need to just draw them. */
	  if (*ols && (!_rl_horizontal_scroll_mode || _rl_last_c_pos > 0 ||
			lendiff <= prompt_visible_length || !current_invis_chars))
	    {
	      insert_some_chars (nfd, lendiff, col_lendiff);
	      _rl_last_c_pos += col_lendiff;
	    }
	  else if (*ols == 0)
	    {
	      /* At the end of a line the characters do not have to
		 be "inserted".  They can just be placed on the screen. */
	      /* However, this screws up the rest of this block, which
		 assumes you've done the insert because you can. */
	      _rl_output_some_chars (nfd, lendiff);
	      _rl_last_c_pos += col_lendiff;
	    }
	  else
	    {
	      /* We have horizontal scrolling and we are not inserting at
		 the end.  We have invisible characters in this line.  This
		 is a dumb update. */
	      _rl_output_some_chars (nfd, temp);
	      _rl_last_c_pos += col_temp;
	      return;
	    }
	  /* Copy (new) chars to screen from first diff to last match. */
	  temp = nls - nfd;
	  if ((temp - lendiff) > 0)
	    {
	      _rl_output_some_chars (nfd + lendiff, temp - lendiff);
#if 0
	      _rl_last_c_pos += _rl_col_width (nfd+lendiff, 0, temp-col_lendiff);
#else
	      _rl_last_c_pos += _rl_col_width (nfd+lendiff, 0, temp-lendiff);
#endif
	    }
	}
      else
	{
	  /* cannot insert chars, write to EOL */
	  _rl_output_some_chars (nfd, temp);
	  _rl_last_c_pos += col_temp;
	}
    }
  else				/* Delete characters from line. */
    {
      /* If possible and inexpensive to use terminal deletion, then do so. */
      if (_rl_term_dc && (2 * col_temp) >= -col_lendiff)
	{
	  /* If all we're doing is erasing the invisible characters in the
	     prompt string, don't bother.  It screws up the assumptions
	     about what's on the screen. */
	  if (_rl_horizontal_scroll_mode && _rl_last_c_pos == 0 &&
	      -lendiff == visible_wrap_offset)
	    col_lendiff = 0;

	  if (col_lendiff)
	    delete_chars (-col_lendiff); /* delete (diff) characters */

	  /* Copy (new) chars to screen from first diff to last match */
	  temp = nls - nfd;
	  if (temp > 0)
	    {
	      _rl_output_some_chars (nfd, temp);
	      _rl_last_c_pos += _rl_col_width (nfd, 0, temp);;
	    }
	}
      /* Otherwise, print over the existing material. */
      else
	{
	  if (temp > 0)
	    {
	      _rl_output_some_chars (nfd, temp);
	      _rl_last_c_pos += col_temp;
	    }
	  lendiff = (oe - old) - (ne - new);
	  if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
	    col_lendiff = _rl_col_width (old, 0, oe - old) - _rl_col_width (new, 0, ne - new);
	  else
	    col_lendiff = lendiff;

	  if (col_lendiff)
	    {
	      if (_rl_term_autowrap && current_line < inv_botlin)
		space_to_eol (col_lendiff);
	      else
		_rl_clear_to_eol (col_lendiff);
	    }
	}
    }
}

/* Tell the update routines that we have moved onto a new (empty) line. */
int
rl_on_new_line ()
{
  if (visible_line)
    visible_line[0] = '\0';

  _rl_last_c_pos = _rl_last_v_pos = 0;
  _rl_vis_botlin = last_lmargin = 0;
  if (vis_lbreaks)
    vis_lbreaks[0] = vis_lbreaks[1] = 0;
  visible_wrap_offset = 0;
  return 0;
}

/* Tell the update routines that we have moved onto a new line with the
   prompt already displayed.  Code originally from the version of readline
   distributed with CLISP. */
int
rl_on_new_line_with_prompt ()
{
  int prompt_size, i, l, real_screenwidth, newlines;
  char *prompt_last_line;

  /* Initialize visible_line and invisible_line to ensure that they can hold
     the already-displayed prompt. */
  prompt_size = strlen (rl_prompt) + 1;
  init_line_structures (prompt_size);

  /* Make sure the line structures hold the already-displayed prompt for
     redisplay. */
  strlcpy (visible_line, rl_prompt, line_size);
  strlcpy (invisible_line, rl_prompt, line_size);

  /* If the prompt contains newlines, take the last tail. */
  prompt_last_line = strrchr (rl_prompt, '\n');
  if (!prompt_last_line)
    prompt_last_line = rl_prompt;

  l = strlen (prompt_last_line);
  if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
    _rl_last_c_pos = _rl_col_width (prompt_last_line, 0, l);
  else
    _rl_last_c_pos = l;

  /* Dissect prompt_last_line into screen lines. Note that here we have
     to use the real screenwidth. Readline's notion of screenwidth might be
     one less, see terminal.c. */
  real_screenwidth = _rl_screenwidth + (_rl_term_autowrap ? 0 : 1);
  _rl_last_v_pos = l / real_screenwidth;
  /* If the prompt length is a multiple of real_screenwidth, we don't know
     whether the cursor is at the end of the last line, or already at the
     beginning of the next line. Output a newline just to be safe. */
  if (l > 0 && (l % real_screenwidth) == 0)
    _rl_output_some_chars ("\n", 1);
  last_lmargin = 0;

  newlines = 0; i = 0;
  while (i <= l)
    {
      _rl_vis_botlin = newlines;
      vis_lbreaks[newlines++] = i;
      i += real_screenwidth;
    }
  vis_lbreaks[newlines] = l;
  visible_wrap_offset = 0;

  return 0;
}

/* Actually update the display, period. */
int
rl_forced_update_display ()
{
  if (visible_line)
    {
      register char *temp = visible_line;

      while (*temp)
	*temp++ = '\0';
    }
  rl_on_new_line ();
  forced_display++;
  (*rl_redisplay_function) ();
  return 0;
}

/* Move the cursor from _rl_last_c_pos to NEW, which are buffer indices.
   DATA is the contents of the screen line of interest; i.e., where
   the movement is being done. */
void
_rl_move_cursor_relative (new, data)
     int new;
     const char *data;
{
  register int i;

  /* If we don't have to do anything, then return. */
#if defined (HANDLE_MULTIBYTE)
  /* If we have multibyte characters, NEW is indexed by the buffer point in
     a multibyte string, but _rl_last_c_pos is the display position.  In
     this case, NEW's display position is not obvious and must be
     calculated. */
  if (MB_CUR_MAX == 1 || rl_byte_oriented)
    {
      if (_rl_last_c_pos == new)
	return;
    }
  else if (_rl_last_c_pos == _rl_col_width (data, 0, new))
    return;
#else
  if (_rl_last_c_pos == new) return;
#endif

  /* It may be faster to output a CR, and then move forwards instead
     of moving backwards. */
  /* i == current physical cursor position. */
  i = _rl_last_c_pos - W_OFFSET(_rl_last_v_pos, visible_wrap_offset);
  if (new == 0 || CR_FASTER (new, _rl_last_c_pos) ||
      (_rl_term_autowrap && i == _rl_screenwidth))
    {
#if defined (__MSDOS__)
      putc ('\r', rl_outstream);
#else
      tputs (_rl_term_cr, 1, _rl_output_character_function);
#endif /* !__MSDOS__ */
      _rl_last_c_pos = 0;
    }

  if (_rl_last_c_pos < new)
    {
      /* Move the cursor forward.  We do it by printing the command
	 to move the cursor forward if there is one, else print that
	 portion of the output buffer again.  Which is cheaper? */

      /* The above comment is left here for posterity.  It is faster
	 to print one character (non-control) than to print a control
	 sequence telling the terminal to move forward one character.
	 That kind of control is for people who don't know what the
	 data is underneath the cursor. */
#if defined (HACK_TERMCAP_MOTION)
      if (_rl_term_forward_char)
	{
	  if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
	    {
	      int width;
	      width = _rl_col_width (data, _rl_last_c_pos, new);
	      for (i = 0; i < width; i++)
		tputs (_rl_term_forward_char, 1, _rl_output_character_function);
	    }
	  else
	    {
	      for (i = _rl_last_c_pos; i < new; i++)
		tputs (_rl_term_forward_char, 1, _rl_output_character_function);
	    }
	}
      else if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
	{
	  tputs (_rl_term_cr, 1, _rl_output_character_function);
	  for (i = 0; i < new; i++)
	    putc (data[i], rl_outstream);
	}
      else
	for (i = _rl_last_c_pos; i < new; i++)
	  putc (data[i], rl_outstream);

#else /* !HACK_TERMCAP_MOTION */

      if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
	{
	  tputs (_rl_term_cr, 1, _rl_output_character_function);
	  for (i = 0; i < new; i++)
	    putc (data[i], rl_outstream);
	}
      else
	for (i = _rl_last_c_pos; i < new; i++)
	  putc (data[i], rl_outstream);

#endif /* !HACK_TERMCAP_MOTION */

    }
#if defined (HANDLE_MULTIBYTE)
  /* NEW points to the buffer point, but _rl_last_c_pos is the display point.
     The byte length of the string is probably bigger than the column width
     of the string, which means that if NEW == _rl_last_c_pos, then NEW's
     display point is less than _rl_last_c_pos. */
  else if (_rl_last_c_pos >= new)
#else
  else if (_rl_last_c_pos > new)
#endif
    {
      if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
	_rl_backspace (_rl_last_c_pos - _rl_col_width (data, 0, new));
      else
	_rl_backspace (_rl_last_c_pos - new);
    }

  if (MB_CUR_MAX > 1 && rl_byte_oriented == 0)
    _rl_last_c_pos =  _rl_col_width (data, 0, new);
  else
    _rl_last_c_pos = new;
}

/* PWP: move the cursor up or down. */
void
_rl_move_vert (to)
     int to;
{
  register int delta, i;

  if (_rl_last_v_pos == to || to > _rl_screenheight)
    return;

  if ((delta = to - _rl_last_v_pos) > 0)
    {
      for (i = 0; i < delta; i++)
	putc ('\n', rl_outstream);
#if defined (__MSDOS__)
      putc ('\r', rl_outstream);
#else
      tputs (_rl_term_cr, 1, _rl_output_character_function);
#endif
      _rl_last_c_pos = 0;
    }
  else
    {			/* delta < 0 */
      if (_rl_term_up && *_rl_term_up)
	for (i = 0; i < -delta; i++)
	  tputs (_rl_term_up, 1, _rl_output_character_function);
    }

  _rl_last_v_pos = to;		/* Now TO is here */
}

/* Physically print C on rl_outstream.  This is for functions which know
   how to optimize the display.  Return the number of characters output. */
int
rl_show_char (c)
     int c;
{
  int n = 1;
  if (META_CHAR (c) && (_rl_output_meta_chars == 0))
    {
      fprintf (rl_outstream, "M-");
      n += 2;
      c = UNMETA (c);
    }

#if defined (DISPLAY_TABS)
  if ((CTRL_CHAR (c) && c != '\t') || c == RUBOUT)
#else
  if (CTRL_CHAR (c) || c == RUBOUT)
#endif /* !DISPLAY_TABS */
    {
      fprintf (rl_outstream, "C-");
      n += 2;
      c = CTRL_CHAR (c) ? UNCTRL (c) : '?';
    }

  putc (c, rl_outstream);
  fflush (rl_outstream);
  return n;
}

int
rl_character_len (c, pos)
     register int c, pos;
{
  unsigned char uc;

  uc = (unsigned char)c;

  if (META_CHAR (uc))
    return ((_rl_output_meta_chars == 0) ? 4 : 1);

  if (uc == '\t')
    {
#if defined (DISPLAY_TABS)
      return (((pos | 7) + 1) - pos);
#else
      return (2);
#endif /* !DISPLAY_TABS */
    }

  if (CTRL_CHAR (c) || c == RUBOUT)
    return (2);

  return ((ISPRINT (uc)) ? 1 : 2);
}

/* How to print things in the "echo-area".  The prompt is treated as a
   mini-modeline. */

#if defined (USE_VARARGS)
int
#if defined (PREFER_STDARG)
rl_message (const char *format, ...)
#else
rl_message (va_alist)
     va_dcl
#endif
{
  va_list args;
#if defined (PREFER_VARARGS)
  char *format;
#endif

#if defined (PREFER_STDARG)
  va_start (args, format);
#else
  va_start (args);
  format = va_arg (args, char *);
#endif

#if defined (HAVE_VSNPRINTF)
  vsnprintf (msg_buf, sizeof (msg_buf) - 1, format, args);
#else
  vsprintf (msg_buf, format, args);
  msg_buf[sizeof(msg_buf) - 1] = '\0';	/* overflow? */
#endif
  va_end (args);

  rl_display_prompt = msg_buf;
  (*rl_redisplay_function) ();
  return 0;
}
#else /* !USE_VARARGS */
int
rl_message (format, arg1, arg2)
     char *format;
{
  snprintf (msg_buf, sizeof(msg_buf), format, arg1, arg2);
  rl_display_prompt = msg_buf;
  (*rl_redisplay_function) ();
  return 0;
}
#endif /* !USE_VARARGS */

/* How to clear things from the "echo-area". */
int
rl_clear_message ()
{
  rl_display_prompt = rl_prompt;
  (*rl_redisplay_function) ();
  return 0;
}

int
rl_reset_line_state ()
{
  rl_on_new_line ();

  rl_display_prompt = rl_prompt ? rl_prompt : "";
  forced_display = 1;
  return 0;
}

static char *saved_local_prompt;
static char *saved_local_prefix;
static int saved_last_invisible;
static int saved_visible_length;

void
rl_save_prompt ()
{
  saved_local_prompt = local_prompt;
  saved_local_prefix = local_prompt_prefix;
  saved_last_invisible = prompt_last_invisible;
  saved_visible_length = prompt_visible_length;

  local_prompt = local_prompt_prefix = (char *)0;
  prompt_last_invisible = prompt_visible_length = 0;
}

void
rl_restore_prompt ()
{
  FREE (local_prompt);
  FREE (local_prompt_prefix);

  local_prompt = saved_local_prompt;
  local_prompt_prefix = saved_local_prefix;
  prompt_last_invisible = saved_last_invisible;
  prompt_visible_length = saved_visible_length;
}

char *
_rl_make_prompt_for_search (pchar)
     int pchar;
{
  int len;
  char *pmt;

  rl_save_prompt ();

  if (saved_local_prompt == 0)
    {
      len = (rl_prompt && *rl_prompt) ? strlen (rl_prompt) : 0;
      pmt = (char *)xmalloc (len + 2);
      if (len)
	strlcpy (pmt, rl_prompt, len + 2);
      pmt[len] = pchar;
      pmt[len+1] = '\0';
    }
  else
    {
      len = *saved_local_prompt ? strlen (saved_local_prompt) : 0;
      pmt = (char *)xmalloc (len + 2);
      if (len)
	strlcpy (pmt, saved_local_prompt, len + 2);
      pmt[len] = pchar;
      pmt[len+1] = '\0';
      local_prompt = savestring (pmt);
      prompt_last_invisible = saved_last_invisible;
      prompt_visible_length = saved_visible_length + 1;
    }
  return pmt;
}

/* Quick redisplay hack when erasing characters at the end of the line. */
void
_rl_erase_at_end_of_line (l)
     int l;
{
  register int i;

  _rl_backspace (l);
  for (i = 0; i < l; i++)
    putc (' ', rl_outstream);
  _rl_backspace (l);
  for (i = 0; i < l; i++)
    visible_line[--_rl_last_c_pos] = '\0';
  rl_display_fixed++;
}

/* Clear to the end of the line.  COUNT is the minimum
   number of character spaces to clear, */
void
_rl_clear_to_eol (count)
     int count;
{
  if (_rl_term_clreol)
    tputs (_rl_term_clreol, 1, _rl_output_character_function);
  else if (count)
    space_to_eol (count);
}

/* Clear to the end of the line using spaces.  COUNT is the minimum
   number of character spaces to clear, */
static void
space_to_eol (count)
     int count;
{
  register int i;

  for (i = 0; i < count; i++)
   putc (' ', rl_outstream);

  _rl_last_c_pos += count;
}

void
_rl_clear_screen ()
{
  if (_rl_term_clrpag)
    tputs (_rl_term_clrpag, 1, _rl_output_character_function);
  else
    rl_crlf ();
}

/* Insert COUNT characters from STRING to the output stream at column COL. */
static void
insert_some_chars (string, count, col)
     char *string;
     int count, col;
{
  /* DEBUGGING */
  if (MB_CUR_MAX == 1 || rl_byte_oriented)
    if (count != col)
      fprintf(stderr, "readline: debug: insert_some_chars: count (%d) != col (%d)\n", count, col);

  /* If IC is defined, then we do not have to "enter" insert mode. */
  if (_rl_term_IC)
    {
      char *buffer;

      buffer = tgoto (_rl_term_IC, 0, col);
      tputs (buffer, 1, _rl_output_character_function);
      _rl_output_some_chars (string, count);
    }
  else
    {
      register int i;

      /* If we have to turn on insert-mode, then do so. */
      if (_rl_term_im && *_rl_term_im)
	tputs (_rl_term_im, 1, _rl_output_character_function);

      /* If there is a special command for inserting characters, then
	 use that first to open up the space. */
      if (_rl_term_ic && *_rl_term_ic)
	{
	  for (i = col; i--; )
	    tputs (_rl_term_ic, 1, _rl_output_character_function);
	}

      /* Print the text. */
      _rl_output_some_chars (string, count);

      /* If there is a string to turn off insert mode, we had best use
	 it now. */
      if (_rl_term_ei && *_rl_term_ei)
	tputs (_rl_term_ei, 1, _rl_output_character_function);
    }
}

/* Delete COUNT characters from the display line. */
static void
delete_chars (count)
     int count;
{
  if (count > _rl_screenwidth)	/* XXX */
    return;

  if (_rl_term_DC && *_rl_term_DC)
    {
      char *buffer;
      buffer = tgoto (_rl_term_DC, count, count);
      tputs (buffer, count, _rl_output_character_function);
    }
  else
    {
      if (_rl_term_dc && *_rl_term_dc)
	while (count--)
	  tputs (_rl_term_dc, 1, _rl_output_character_function);
    }
}

void
_rl_update_final ()
{
  int full_lines;

  full_lines = 0;
  /* If the cursor is the only thing on an otherwise-blank last line,
     compensate so we don't print an extra CRLF. */
  if (_rl_vis_botlin && _rl_last_c_pos == 0 &&
	visible_line[vis_lbreaks[_rl_vis_botlin]] == 0)
    {
      _rl_vis_botlin--;
      full_lines = 1;
    }
  _rl_move_vert (_rl_vis_botlin);
  /* If we've wrapped lines, remove the final xterm line-wrap flag. */
  if (full_lines && _rl_term_autowrap && (VIS_LLEN(_rl_vis_botlin) == _rl_screenwidth))
    {
      char *last_line;

      last_line = &visible_line[vis_lbreaks[_rl_vis_botlin]];
      _rl_move_cursor_relative (_rl_screenwidth - 1, last_line);
      _rl_clear_to_eol (0);
      putc (last_line[_rl_screenwidth - 1], rl_outstream);
    }
  _rl_vis_botlin = 0;
  rl_crlf ();
  fflush (rl_outstream);
  rl_display_fixed++;
}

/* Move to the start of the current line. */
static void
cr ()
{
  if (_rl_term_cr)
    {
#if defined (__MSDOS__)
      putc ('\r', rl_outstream);
#else
      tputs (_rl_term_cr, 1, _rl_output_character_function);
#endif
      _rl_last_c_pos = 0;
    }
}

/* Redraw the last line of a multi-line prompt that may possibly contain
   terminal escape sequences.  Called with the cursor at column 0 of the
   line to draw the prompt on. */
static void
redraw_prompt (t)
     char *t;
{
  char *oldp, *oldl, *oldlprefix;
  int oldlen, oldlast, oldplen, oldninvis;

  /* Geez, I should make this a struct. */
  oldp = rl_display_prompt;
  oldl = local_prompt;
  oldlprefix = local_prompt_prefix;
  oldlen = prompt_visible_length;
  oldplen = prompt_prefix_length;
  oldlast = prompt_last_invisible;
  oldninvis = prompt_invis_chars_first_line;

  rl_display_prompt = t;
  local_prompt = expand_prompt (t, &prompt_visible_length,
				   &prompt_last_invisible,
				   &prompt_invis_chars_first_line);
  local_prompt_prefix = (char *)NULL;
  rl_forced_update_display ();

  rl_display_prompt = oldp;
  local_prompt = oldl;
  local_prompt_prefix = oldlprefix;
  prompt_visible_length = oldlen;
  prompt_prefix_length = oldplen;
  prompt_last_invisible = oldlast;
  prompt_invis_chars_first_line = oldninvis;
}

/* Redisplay the current line after a SIGWINCH is received. */
void
_rl_redisplay_after_sigwinch ()
{
  char *t;

  /* Clear the current line and put the cursor at column 0.  Make sure
     the right thing happens if we have wrapped to a new screen line. */
  if (_rl_term_cr)
    {
#if defined (__MSDOS__)
      putc ('\r', rl_outstream);
#else
      tputs (_rl_term_cr, 1, _rl_output_character_function);
#endif
      _rl_last_c_pos = 0;
#if defined (__MSDOS__)
      space_to_eol (_rl_screenwidth);
      putc ('\r', rl_outstream);
#else
      if (_rl_term_clreol)
	tputs (_rl_term_clreol, 1, _rl_output_character_function);
      else
	{
	  space_to_eol (_rl_screenwidth);
	  tputs (_rl_term_cr, 1, _rl_output_character_function);
	}
#endif
      if (_rl_last_v_pos > 0)
	_rl_move_vert (0);
    }
  else
    rl_crlf ();

  /* Redraw only the last line of a multi-line prompt. */
  t = strrchr (rl_display_prompt, '\n');
  if (t)
    redraw_prompt (++t);
  else
    rl_forced_update_display ();
}

void
_rl_clean_up_for_exit ()
{
  if (readline_echoing_p)
    {
      _rl_move_vert (_rl_vis_botlin);
      _rl_vis_botlin = 0;
      fflush (rl_outstream);
      rl_restart_output (1, 0);
    }
}

void
_rl_erase_entire_line ()
{
  cr ();
  _rl_clear_to_eol (0);
  cr ();
  fflush (rl_outstream);
}

/* return the `current display line' of the cursor -- the number of lines to
   move up to get to the first screen line of the current readline line. */
int
_rl_current_display_line ()
{
  int ret, nleft;

  /* Find out whether or not there might be invisible characters in the
     editing buffer. */
  if (rl_display_prompt == rl_prompt)
    nleft = _rl_last_c_pos - _rl_screenwidth - rl_visible_prompt_length;
  else
    nleft = _rl_last_c_pos - _rl_screenwidth;

  if (nleft > 0)
    ret = 1 + nleft / _rl_screenwidth;
  else
    ret = 0;

  return ret;
}

#if defined (HANDLE_MULTIBYTE)
/* Calculate the number of screen columns occupied by STR from START to END.
   In the case of multibyte characters with stateful encoding, we have to
   scan from the beginning of the string to take the state into account. */
static int
_rl_col_width (str, start, end)
     const char *str;
     int start, end;
{
  wchar_t wc;
  mbstate_t ps = {0};
  int tmp, point, width, max;

  if (end <= start)
    return 0;

  point = 0;
  max = end;

  while (point < start)
    {
      tmp = mbrlen (str + point, max, &ps);
      if ((size_t)tmp == (size_t)-1 || (size_t)tmp == (size_t)-2)
	{
	  /* In this case, the bytes are invalid or too short to compose a
	     multibyte character, so we assume that the first byte represents
	     a single character. */
	  point++;
	  max--;

	  /* Clear the state of the byte sequence, because in this case the
	     effect of mbstate is undefined. */
	  memset (&ps, 0, sizeof (mbstate_t));
	}
      else if (tmp == 0)
        break;		/* Found '\0' */
      else
	{
	  point += tmp;
	  max -= tmp;
	}
    }

  /* If START is not a byte that starts a character, then POINT will be
     greater than START.  In this case, assume that (POINT - START) gives
     a byte count that is the number of columns of difference. */
  width = point - start;

  while (point < end)
    {
      tmp = mbrtowc (&wc, str + point, max, &ps);
      if ((size_t)tmp == (size_t)-1 || (size_t)tmp == (size_t)-2)
	{
	  /* In this case, the bytes are invalid or too short to compose a
	     multibyte character, so we assume that the first byte represents
	     a single character. */
	  point++;
	  max--;

	  /* and assume that the byte occupies a single column. */
	  width++;

	  /* Clear the state of the byte sequence, because in this case the
	     effect of mbstate is undefined. */
	  memset (&ps, 0, sizeof (mbstate_t));
	}
      else if (tmp == 0)
        break;			/* Found '\0' */
      else
	{
	  point += tmp;
	  max -= tmp;
	  tmp = wcwidth(wc);
	  width += (tmp >= 0) ? tmp : 1;
	}
    }

  width += point - end;

  return width;
}
#endif /* HANDLE_MULTIBYTE */
