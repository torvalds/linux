/* bind.c -- key binding and startup file support for the readline library. */

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

#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>
#if defined (HAVE_SYS_FILE_H)
#  include <sys/file.h>
#endif /* HAVE_SYS_FILE_H */

#if defined (HAVE_UNISTD_H)
#  include <unistd.h>
#endif /* HAVE_UNISTD_H */

#if defined (HAVE_STDLIB_H)
#  include <stdlib.h>
#else
#  include "ansi_stdlib.h"
#endif /* HAVE_STDLIB_H */

#include <errno.h>

#if !defined (errno)
extern int errno;
#endif /* !errno */

#include "posixstat.h"

/* System-specific feature definitions and include files. */
#include "rldefs.h"

/* Some standard library routines. */
#include "readline.h"
#include "history.h"

#include "rlprivate.h"
#include "rlshell.h"
#include "xmalloc.h"

#if !defined (strchr) && !defined (__STDC__)
extern char *strchr (), *strrchr ();
#endif /* !strchr && !__STDC__ */

/* Variables exported by this file. */
Keymap rl_binding_keymap;

static char *_rl_read_file PARAMS((char *, size_t *));
static void _rl_init_file_error PARAMS((const char *));
static int _rl_read_init_file PARAMS((const char *, int));
static int glean_key_from_name PARAMS((char *));
static int substring_member_of_array PARAMS((char *, const char **));

static int currently_reading_init_file;

/* used only in this file */
static int _rl_prefer_visible_bell = 1;

/* **************************************************************** */
/*								    */
/*			Binding keys				    */
/*								    */
/* **************************************************************** */

/* rl_add_defun (char *name, rl_command_func_t *function, int key)
   Add NAME to the list of named functions.  Make FUNCTION be the function
   that gets called.  If KEY is not -1, then bind it. */
int
rl_add_defun (name, function, key)
     const char *name;
     rl_command_func_t *function;
     int key;
{
  if (key != -1)
    rl_bind_key (key, function);
  rl_add_funmap_entry (name, function);
  return 0;
}

/* Bind KEY to FUNCTION.  Returns non-zero if KEY is out of range. */
int
rl_bind_key (key, function)
     int key;
     rl_command_func_t *function;
{
  if (key < 0)
    return (key);

  if (META_CHAR (key) && _rl_convert_meta_chars_to_ascii)
    {
      if (_rl_keymap[ESC].type == ISKMAP)
	{
	  Keymap escmap;

	  escmap = FUNCTION_TO_KEYMAP (_rl_keymap, ESC);
	  key = UNMETA (key);
	  escmap[key].type = ISFUNC;
	  escmap[key].function = function;
	  return (0);
	}
      return (key);
    }

  _rl_keymap[key].type = ISFUNC;
  _rl_keymap[key].function = function;
  rl_binding_keymap = _rl_keymap;
  return (0);
}

/* Bind KEY to FUNCTION in MAP.  Returns non-zero in case of invalid
   KEY. */
int
rl_bind_key_in_map (key, function, map)
     int key;
     rl_command_func_t *function;
     Keymap map;
{
  int result;
  Keymap oldmap;

  oldmap = _rl_keymap;
  _rl_keymap = map;
  result = rl_bind_key (key, function);
  _rl_keymap = oldmap;
  return (result);
}

/* Make KEY do nothing in the currently selected keymap.
   Returns non-zero in case of error. */
int
rl_unbind_key (key)
     int key;
{
  return (rl_bind_key (key, (rl_command_func_t *)NULL));
}

/* Make KEY do nothing in MAP.
   Returns non-zero in case of error. */
int
rl_unbind_key_in_map (key, map)
     int key;
     Keymap map;
{
  return (rl_bind_key_in_map (key, (rl_command_func_t *)NULL, map));
}

/* Unbind all keys bound to FUNCTION in MAP. */
int
rl_unbind_function_in_map (func, map)
     rl_command_func_t *func;
     Keymap map;
{
  register int i, rval;

  for (i = rval = 0; i < KEYMAP_SIZE; i++)
    {
      if (map[i].type == ISFUNC && map[i].function == func)
	{
	  map[i].function = (rl_command_func_t *)NULL;
	  rval = 1;
	}
    }
  return rval;
}

int
rl_unbind_command_in_map (command, map)
     const char *command;
     Keymap map;
{
  rl_command_func_t *func;

  func = rl_named_function (command);
  if (func == 0)
    return 0;
  return (rl_unbind_function_in_map (func, map));
}

/* Bind the key sequence represented by the string KEYSEQ to
   FUNCTION.  This makes new keymaps as necessary.  The initial
   place to do bindings is in MAP. */
int
rl_set_key (keyseq, function, map)
     const char *keyseq;
     rl_command_func_t *function;
     Keymap map;
{
  return (rl_generic_bind (ISFUNC, keyseq, (char *)function, map));
}

/* Bind the key sequence represented by the string KEYSEQ to
   the string of characters MACRO.  This makes new keymaps as
   necessary.  The initial place to do bindings is in MAP. */
int
rl_macro_bind (keyseq, macro, map)
     const char *keyseq, *macro;
     Keymap map;
{
  char *macro_keys;
  int macro_keys_len;

  macro_keys = (char *)xmalloc ((2 * strlen (macro)) + 1);

  if (rl_translate_keyseq (macro, macro_keys, &macro_keys_len))
    {
      free (macro_keys);
      return -1;
    }
  rl_generic_bind (ISMACR, keyseq, macro_keys, map);
  return 0;
}

/* Bind the key sequence represented by the string KEYSEQ to
   the arbitrary pointer DATA.  TYPE says what kind of data is
   pointed to by DATA, right now this can be a function (ISFUNC),
   a macro (ISMACR), or a keymap (ISKMAP).  This makes new keymaps
   as necessary.  The initial place to do bindings is in MAP. */
int
rl_generic_bind (type, keyseq, data, map)
     int type;
     const char *keyseq;
     char *data;
     Keymap map;
{
  char *keys;
  int keys_len;
  register int i;
  KEYMAP_ENTRY k;

  k.function = 0;

  /* If no keys to bind to, exit right away. */
  if (!keyseq || !*keyseq)
    {
      if (type == ISMACR)
	free (data);
      return -1;
    }

  keys = (char *)xmalloc (1 + (2 * strlen (keyseq)));

  /* Translate the ASCII representation of KEYSEQ into an array of
     characters.  Stuff the characters into KEYS, and the length of
     KEYS into KEYS_LEN. */
  if (rl_translate_keyseq (keyseq, keys, &keys_len))
    {
      free (keys);
      return -1;
    }

  /* Bind keys, making new keymaps as necessary. */
  for (i = 0; i < keys_len; i++)
    {
      unsigned char uc = keys[i];
      int ic;

      ic = uc;
      if (ic < 0 || ic >= KEYMAP_SIZE)
	return -1;

      if (_rl_convert_meta_chars_to_ascii && META_CHAR (ic))
	{
	  ic = UNMETA (ic);
	  if (map[ESC].type == ISKMAP)
	    map = FUNCTION_TO_KEYMAP (map, ESC);
	}

      if ((i + 1) < keys_len)
	{
	  if (map[ic].type != ISKMAP)
	    {
	      /* We allow subsequences of keys.  If a keymap is being
		 created that will `shadow' an existing function or macro
		 key binding, we save that keybinding into the ANYOTHERKEY
		 index in the new map.  The dispatch code will look there
		 to find the function to execute if the subsequence is not
		 matched.  ANYOTHERKEY was chosen to be greater than
		 UCHAR_MAX. */
	      k = map[ic];

	      map[ic].type = ISKMAP;
	      map[ic].function = KEYMAP_TO_FUNCTION (rl_make_bare_keymap());
	    }
	  map = FUNCTION_TO_KEYMAP (map, ic);
	  /* The dispatch code will return this function if no matching
	     key sequence is found in the keymap.  This (with a little
	     help from the dispatch code in readline.c) allows `a' to be
	     mapped to something, `abc' to be mapped to something else,
	     and the function bound  to `a' to be executed when the user
	     types `abx', leaving `bx' in the input queue. */
	  if (k.function && ((k.type == ISFUNC && k.function != rl_do_lowercase_version) || k.type == ISMACR))
	    {
	      map[ANYOTHERKEY] = k;
	      k.function = 0;
	    }
	}
      else
	{
	  if (map[ic].type == ISMACR)
	    free ((char *)map[ic].function);
	  else if (map[ic].type == ISKMAP)
	    {
	      map = FUNCTION_TO_KEYMAP (map, ic);
	      ic = ANYOTHERKEY;
	    }

	  map[ic].function = KEYMAP_TO_FUNCTION (data);
	  map[ic].type = type;
	}

      rl_binding_keymap = map;
    }
  free (keys);
  return 0;
}

/* Translate the ASCII representation of SEQ, stuffing the values into ARRAY,
   an array of characters.  LEN gets the final length of ARRAY.  Return
   non-zero if there was an error parsing SEQ. */
int
rl_translate_keyseq (seq, array, len)
     const char *seq;
     char *array;
     int *len;
{
  register int i, c, l, temp;

  for (i = l = 0; (c = seq[i]); i++)
    {
      if (c == '\\')
	{
	  c = seq[++i];

	  if (c == 0)
	    break;

	  /* Handle \C- and \M- prefixes. */
	  if ((c == 'C' || c == 'M') && seq[i + 1] == '-')
	    {
	      /* Handle special case of backwards define. */
	      if (strncmp (&seq[i], "C-\\M-", 5) == 0)
		{
		  array[l++] = ESC;	/* ESC is meta-prefix */
		  i += 5;
		  array[l++] = CTRL (_rl_to_upper (seq[i]));
		  if (seq[i] == '\0')
		    i--;
		}
	      else if (c == 'M')
		{
		  i++;
		  array[l++] = ESC;	/* ESC is meta-prefix */
		}
	      else if (c == 'C')
		{
		  i += 2;
		  /* Special hack for C-?... */
		  array[l++] = (seq[i] == '?') ? RUBOUT : CTRL (_rl_to_upper (seq[i]));
		}
	      continue;
	    }

	  /* Translate other backslash-escaped characters.  These are the
	     same escape sequences that bash's `echo' and `printf' builtins
	     handle, with the addition of \d -> RUBOUT.  A backslash
	     preceding a character that is not special is stripped. */
	  switch (c)
	    {
	    case 'a':
	      array[l++] = '\007';
	      break;
	    case 'b':
	      array[l++] = '\b';
	      break;
	    case 'd':
	      array[l++] = RUBOUT;	/* readline-specific */
	      break;
	    case 'e':
	      array[l++] = ESC;
	      break;
	    case 'f':
	      array[l++] = '\f';
	      break;
	    case 'n':
	      array[l++] = NEWLINE;
	      break;
	    case 'r':
	      array[l++] = RETURN;
	      break;
	    case 't':
	      array[l++] = TAB;
	      break;
	    case 'v':
	      array[l++] = 0x0B;
	      break;
	    case '\\':
	      array[l++] = '\\';
	      break;
	    case '0': case '1': case '2': case '3':
	    case '4': case '5': case '6': case '7':
	      i++;
	      for (temp = 2, c -= '0'; ISOCTAL (seq[i]) && temp--; i++)
	        c = (c * 8) + OCTVALUE (seq[i]);
	      i--;	/* auto-increment in for loop */
	      array[l++] = c & largest_char;
	      break;
	    case 'x':
	      i++;
	      for (temp = 2, c = 0; ISXDIGIT ((unsigned char)seq[i]) && temp--; i++)
	        c = (c * 16) + HEXVALUE (seq[i]);
	      if (temp == 2)
	        c = 'x';
	      i--;	/* auto-increment in for loop */
	      array[l++] = c & largest_char;
	      break;
	    default:	/* backslashes before non-special chars just add the char */
	      array[l++] = c;
	      break;	/* the backslash is stripped */
	    }
	  continue;
	}

      array[l++] = c;
    }

  *len = l;
  array[l] = '\0';
  return (0);
}

char *
rl_untranslate_keyseq (seq)
     int seq;
{
  static char kseq[16];
  int i, c;

  i = 0;
  c = seq;
  if (META_CHAR (c))
    {
      kseq[i++] = '\\';
      kseq[i++] = 'M';
      kseq[i++] = '-';
      c = UNMETA (c);
    }
  else if (CTRL_CHAR (c))
    {
      kseq[i++] = '\\';
      kseq[i++] = 'C';
      kseq[i++] = '-';
      c = _rl_to_lower (UNCTRL (c));
    }
  else if (c == RUBOUT)
    {
      kseq[i++] = '\\';
      kseq[i++] = 'C';
      kseq[i++] = '-';
      c = '?';
    }

  if (c == ESC)
    {
      kseq[i++] = '\\';
      c = 'e';
    }
  else if (c == '\\' || c == '"')
    {
      kseq[i++] = '\\';
    }

  kseq[i++] = (unsigned char) c;
  kseq[i] = '\0';
  return kseq;
}

static char *
_rl_untranslate_macro_value (seq)
     char *seq;
{
  char *ret, *r, *s;
  int c;

  r = ret = (char *)xmalloc (7 * strlen (seq) + 1);
  for (s = seq; *s; s++)
    {
      c = *s;
      if (META_CHAR (c))
	{
	  *r++ = '\\';
	  *r++ = 'M';
	  *r++ = '-';
	  c = UNMETA (c);
	}
      else if (CTRL_CHAR (c) && c != ESC)
	{
	  *r++ = '\\';
	  *r++ = 'C';
	  *r++ = '-';
	  c = _rl_to_lower (UNCTRL (c));
	}
      else if (c == RUBOUT)
	{
	  *r++ = '\\';
	  *r++ = 'C';
	  *r++ = '-';
	  c = '?';
	}

      if (c == ESC)
	{
	  *r++ = '\\';
	  c = 'e';
	}
      else if (c == '\\' || c == '"')
	*r++ = '\\';

      *r++ = (unsigned char)c;
    }
  *r = '\0';
  return ret;
}

/* Return a pointer to the function that STRING represents.
   If STRING doesn't have a matching function, then a NULL pointer
   is returned. */
rl_command_func_t *
rl_named_function (string)
     const char *string;
{
  register int i;

  rl_initialize_funmap ();

  for (i = 0; funmap[i]; i++)
    if (_rl_stricmp (funmap[i]->name, string) == 0)
      return (funmap[i]->function);
  return ((rl_command_func_t *)NULL);
}

/* Return the function (or macro) definition which would be invoked via
   KEYSEQ if executed in MAP.  If MAP is NULL, then the current keymap is
   used.  TYPE, if non-NULL, is a pointer to an int which will receive the
   type of the object pointed to.  One of ISFUNC (function), ISKMAP (keymap),
   or ISMACR (macro). */
rl_command_func_t *
rl_function_of_keyseq (keyseq, map, type)
     const char *keyseq;
     Keymap map;
     int *type;
{
  register int i;

  if (!map)
    map = _rl_keymap;

  for (i = 0; keyseq && keyseq[i]; i++)
    {
      unsigned char ic = keyseq[i];

      if (META_CHAR (ic) && _rl_convert_meta_chars_to_ascii)
	{
	  if (map[ESC].type != ISKMAP)
	    {
	      if (type)
		*type = map[ESC].type;

	      return (map[ESC].function);
	    }
	  else
	    {
	      map = FUNCTION_TO_KEYMAP (map, ESC);
	      ic = UNMETA (ic);
	    }
	}

      if (map[ic].type == ISKMAP)
	{
	  /* If this is the last key in the key sequence, return the
	     map. */
	  if (!keyseq[i + 1])
	    {
	      if (type)
		*type = ISKMAP;

	      return (map[ic].function);
	    }
	  else
	    map = FUNCTION_TO_KEYMAP (map, ic);
	}
      else
	{
	  if (type)
	    *type = map[ic].type;

	  return (map[ic].function);
	}
    }
  return ((rl_command_func_t *) NULL);
}

/* The last key bindings file read. */
static char *last_readline_init_file = (char *)NULL;

/* The file we're currently reading key bindings from. */
static const char *current_readline_init_file;
static int current_readline_init_include_level;
static int current_readline_init_lineno;

/* Read FILENAME into a locally-allocated buffer and return the buffer.
   The size of the buffer is returned in *SIZEP.  Returns NULL if any
   errors were encountered. */
static char *
_rl_read_file (filename, sizep)
     char *filename;
     size_t *sizep;
{
  struct stat finfo;
  size_t file_size;
  char *buffer;
  int i, file;

  if ((stat (filename, &finfo) < 0) || (file = open (filename, O_RDONLY, 0666)) < 0)
    return ((char *)NULL);

  file_size = (size_t)finfo.st_size;

  /* check for overflow on very large files */
  if (file_size != finfo.st_size || file_size + 1 < file_size)
    {
      if (file >= 0)
	close (file);
#if defined (EFBIG)
      errno = EFBIG;
#endif
      return ((char *)NULL);
    }

  /* Read the file into BUFFER. */
  buffer = (char *)xmalloc (file_size + 1);
  i = read (file, buffer, file_size);
  close (file);

  if (i < 0)
    {
      free (buffer);
      return ((char *)NULL);
    }

  buffer[i] = '\0';
  if (sizep)
    *sizep = i;

  return (buffer);
}

/* Re-read the current keybindings file. */
int
rl_re_read_init_file (count, ignore)
     int count, ignore;
{
  int r;
  r = rl_read_init_file ((const char *)NULL);
  rl_set_keymap_from_edit_mode ();
  return r;
}

/* Do key bindings from a file.  If FILENAME is NULL it defaults
   to the first non-null filename from this list:
     1. the filename used for the previous call
     2. the value of the shell variable `INPUTRC'
     3. ~/.inputrc
   If the file existed and could be opened and read, 0 is returned,
   otherwise errno is returned. */
int
rl_read_init_file (filename)
     const char *filename;
{
  /* Default the filename. */
  if (filename == 0)
    {
      filename = last_readline_init_file;
      if (filename == 0)
        filename = sh_get_env_value ("INPUTRC");
      if (filename == 0 || *filename == '\0')
	filename = DEFAULT_INPUTRC;
    }

  if (*filename == 0)
    filename = DEFAULT_INPUTRC;

#if defined (__MSDOS__)
  if (_rl_read_init_file (filename, 0) == 0)
    return 0;
  filename = "~/_inputrc";
#endif
  return (_rl_read_init_file (filename, 0));
}

static int
_rl_read_init_file (filename, include_level)
     const char *filename;
     int include_level;
{
  register int i;
  char *buffer, *openname, *line, *end;
  size_t file_size;

  current_readline_init_file = filename;
  current_readline_init_include_level = include_level;

  openname = tilde_expand (filename);
  buffer = _rl_read_file (openname, &file_size);
  free (openname);

  if (buffer == 0)
    return (errno);

  if (include_level == 0 && filename != last_readline_init_file)
    {
      FREE (last_readline_init_file);
      last_readline_init_file = savestring (filename);
    }

  currently_reading_init_file = 1;

  /* Loop over the lines in the file.  Lines that start with `#' are
     comments; all other lines are commands for readline initialization. */
  current_readline_init_lineno = 1;
  line = buffer;
  end = buffer + file_size;
  while (line < end)
    {
      /* Find the end of this line. */
      for (i = 0; line + i != end && line[i] != '\n'; i++);

#if defined (__CYGWIN__)
      /* ``Be liberal in what you accept.'' */
      if (line[i] == '\n' && line[i-1] == '\r')
	line[i - 1] = '\0';
#endif

      /* Mark end of line. */
      line[i] = '\0';

      /* Skip leading whitespace. */
      while (*line && whitespace (*line))
        {
	  line++;
	  i--;
        }

      /* If the line is not a comment, then parse it. */
      if (*line && *line != '#')
	rl_parse_and_bind (line);

      /* Move to the next line. */
      line += i + 1;
      current_readline_init_lineno++;
    }

  free (buffer);
  currently_reading_init_file = 0;
  return (0);
}

static void
_rl_init_file_error (msg)
     const char *msg;
{
  if (currently_reading_init_file)
    fprintf (stderr, "readline: %s: line %d: %s\n", current_readline_init_file,
		     current_readline_init_lineno, msg);
  else
    fprintf (stderr, "readline: %s\n", msg);
}

/* **************************************************************** */
/*								    */
/*			Parser Directives			    */
/*								    */
/* **************************************************************** */

typedef int _rl_parser_func_t PARAMS((char *));

/* Things that mean `Control'. */
const char *_rl_possible_control_prefixes[] = {
  "Control-", "C-", "CTRL-", (const char *)NULL
};

const char *_rl_possible_meta_prefixes[] = {
  "Meta", "M-", (const char *)NULL
};

/* Conditionals. */

/* Calling programs set this to have their argv[0]. */
const char *rl_readline_name = "other";

/* Stack of previous values of parsing_conditionalized_out. */
static unsigned char *if_stack = (unsigned char *)NULL;
static int if_stack_depth;
static int if_stack_size;

/* Push _rl_parsing_conditionalized_out, and set parser state based
   on ARGS. */
static int
parser_if (args)
     char *args;
{
  register int i;

  /* Push parser state. */
  if (if_stack_depth + 1 >= if_stack_size)
    {
      if (!if_stack)
	if_stack = (unsigned char *)xmalloc (if_stack_size = 20);
      else
	if_stack = (unsigned char *)xrealloc (if_stack, if_stack_size += 20);
    }
  if_stack[if_stack_depth++] = _rl_parsing_conditionalized_out;

  /* If parsing is turned off, then nothing can turn it back on except
     for finding the matching endif.  In that case, return right now. */
  if (_rl_parsing_conditionalized_out)
    return 0;

  /* Isolate first argument. */
  for (i = 0; args[i] && !whitespace (args[i]); i++);

  if (args[i])
    args[i++] = '\0';

  /* Handle "$if term=foo" and "$if mode=emacs" constructs.  If this
     isn't term=foo, or mode=emacs, then check to see if the first
     word in ARGS is the same as the value stored in rl_readline_name. */
  if (rl_terminal_name && _rl_strnicmp (args, "term=", 5) == 0)
    {
      char *tem, *tname;

      /* Terminals like "aaa-60" are equivalent to "aaa". */
      tname = savestring (rl_terminal_name);
      tem = strchr (tname, '-');
      if (tem)
	*tem = '\0';

      /* Test the `long' and `short' forms of the terminal name so that
	 if someone has a `sun-cmd' and does not want to have bindings
	 that will be executed if the terminal is a `sun', they can put
	 `$if term=sun-cmd' into their .inputrc. */
      _rl_parsing_conditionalized_out = _rl_stricmp (args + 5, tname) &&
					_rl_stricmp (args + 5, rl_terminal_name);
      free (tname);
    }
#if defined (VI_MODE)
  else if (_rl_strnicmp (args, "mode=", 5) == 0)
    {
      int mode;

      if (_rl_stricmp (args + 5, "emacs") == 0)
	mode = emacs_mode;
      else if (_rl_stricmp (args + 5, "vi") == 0)
	mode = vi_mode;
      else
	mode = no_mode;

      _rl_parsing_conditionalized_out = mode != rl_editing_mode;
    }
#endif /* VI_MODE */
  /* Check to see if the first word in ARGS is the same as the
     value stored in rl_readline_name. */
  else if (_rl_stricmp (args, rl_readline_name) == 0)
    _rl_parsing_conditionalized_out = 0;
  else
    _rl_parsing_conditionalized_out = 1;
  return 0;
}

/* Invert the current parser state if there is anything on the stack. */
static int
parser_else (args)
     char *args;
{
  register int i;

  if (if_stack_depth == 0)
    {
      _rl_init_file_error ("$else found without matching $if");
      return 0;
    }

  /* Check the previous (n - 1) levels of the stack to make sure that
     we haven't previously turned off parsing. */
  for (i = 0; i < if_stack_depth - 1; i++)
    if (if_stack[i] == 1)
      return 0;

  /* Invert the state of parsing if at top level. */
  _rl_parsing_conditionalized_out = !_rl_parsing_conditionalized_out;
  return 0;
}

/* Terminate a conditional, popping the value of
   _rl_parsing_conditionalized_out from the stack. */
static int
parser_endif (args)
     char *args;
{
  if (if_stack_depth)
    _rl_parsing_conditionalized_out = if_stack[--if_stack_depth];
  else
    _rl_init_file_error ("$endif without matching $if");
  return 0;
}

static int
parser_include (args)
     char *args;
{
  const char *old_init_file;
  char *e;
  int old_line_number, old_include_level, r;

  if (_rl_parsing_conditionalized_out)
    return (0);

  old_init_file = current_readline_init_file;
  old_line_number = current_readline_init_lineno;
  old_include_level = current_readline_init_include_level;

  e = strchr (args, '\n');
  if (e)
    *e = '\0';
  r = _rl_read_init_file ((const char *)args, old_include_level + 1);

  current_readline_init_file = old_init_file;
  current_readline_init_lineno = old_line_number;
  current_readline_init_include_level = old_include_level;

  return r;
}

/* Associate textual names with actual functions. */
static struct {
  const char *name;
  _rl_parser_func_t *function;
} parser_directives [] = {
  { "if", parser_if },
  { "endif", parser_endif },
  { "else", parser_else },
  { "include", parser_include },
  { (char *)0x0, (_rl_parser_func_t *)0x0 }
};

/* Handle a parser directive.  STATEMENT is the line of the directive
   without any leading `$'. */
static int
handle_parser_directive (statement)
     char *statement;
{
  register int i;
  char *directive, *args;

  /* Isolate the actual directive. */

  /* Skip whitespace. */
  for (i = 0; whitespace (statement[i]); i++);

  directive = &statement[i];

  for (; statement[i] && !whitespace (statement[i]); i++);

  if (statement[i])
    statement[i++] = '\0';

  for (; statement[i] && whitespace (statement[i]); i++);

  args = &statement[i];

  /* Lookup the command, and act on it. */
  for (i = 0; parser_directives[i].name; i++)
    if (_rl_stricmp (directive, parser_directives[i].name) == 0)
      {
	(*parser_directives[i].function) (args);
	return (0);
      }

  /* display an error message about the unknown parser directive */
  _rl_init_file_error ("unknown parser directive");
  return (1);
}

/* Read the binding command from STRING and perform it.
   A key binding command looks like: Keyname: function-name\0,
   a variable binding command looks like: set variable value.
   A new-style keybinding looks like "\C-x\C-x": exchange-point-and-mark. */
int
rl_parse_and_bind (string)
     char *string;
{
  char *funname, *kname;
  register int c, i;
  int key, equivalency;

  while (string && whitespace (*string))
    string++;

  if (!string || !*string || *string == '#')
    return 0;

  /* If this is a parser directive, act on it. */
  if (*string == '$')
    {
      handle_parser_directive (&string[1]);
      return 0;
    }

  /* If we aren't supposed to be parsing right now, then we're done. */
  if (_rl_parsing_conditionalized_out)
    return 0;

  i = 0;
  /* If this keyname is a complex key expression surrounded by quotes,
     advance to after the matching close quote.  This code allows the
     backslash to quote characters in the key expression. */
  if (*string == '"')
    {
      int passc = 0;

      for (i = 1; (c = string[i]); i++)
	{
	  if (passc)
	    {
	      passc = 0;
	      continue;
	    }

	  if (c == '\\')
	    {
	      passc++;
	      continue;
	    }

	  if (c == '"')
	    break;
	}
      /* If we didn't find a closing quote, abort the line. */
      if (string[i] == '\0')
        {
          _rl_init_file_error ("no closing `\"' in key binding");
          return 1;
        }
    }

  /* Advance to the colon (:) or whitespace which separates the two objects. */
  for (; (c = string[i]) && c != ':' && c != ' ' && c != '\t'; i++ );

  equivalency = (c == ':' && string[i + 1] == '=');

  /* Mark the end of the command (or keyname). */
  if (string[i])
    string[i++] = '\0';

  /* If doing assignment, skip the '=' sign as well. */
  if (equivalency)
    string[i++] = '\0';

  /* If this is a command to set a variable, then do that. */
  if (_rl_stricmp (string, "set") == 0)
    {
      char *var = string + i;
      char *value;

      /* Make VAR point to start of variable name. */
      while (*var && whitespace (*var)) var++;

      /* Make VALUE point to start of value string. */
      value = var;
      while (*value && !whitespace (*value)) value++;
      if (*value)
	*value++ = '\0';
      while (*value && whitespace (*value)) value++;

      rl_variable_bind (var, value);
      return 0;
    }

  /* Skip any whitespace between keyname and funname. */
  for (; string[i] && whitespace (string[i]); i++);
  funname = &string[i];

  /* Now isolate funname.
     For straight function names just look for whitespace, since
     that will signify the end of the string.  But this could be a
     macro definition.  In that case, the string is quoted, so skip
     to the matching delimiter.  We allow the backslash to quote the
     delimiter characters in the macro body. */
  /* This code exists to allow whitespace in macro expansions, which
     would otherwise be gobbled up by the next `for' loop.*/
  /* XXX - it may be desirable to allow backslash quoting only if " is
     the quoted string delimiter, like the shell. */
  if (*funname == '\'' || *funname == '"')
    {
      int delimiter = string[i++], passc;

      for (passc = 0; (c = string[i]); i++)
	{
	  if (passc)
	    {
	      passc = 0;
	      continue;
	    }

	  if (c == '\\')
	    {
	      passc = 1;
	      continue;
	    }

	  if (c == delimiter)
	    break;
	}
      if (c)
	i++;
    }

  /* Advance to the end of the string.  */
  for (; string[i] && !whitespace (string[i]); i++);

  /* No extra whitespace at the end of the string. */
  string[i] = '\0';

  /* Handle equivalency bindings here.  Make the left-hand side be exactly
     whatever the right-hand evaluates to, including keymaps. */
  if (equivalency)
    {
      return 0;
    }

  /* If this is a new-style key-binding, then do the binding with
     rl_set_key ().  Otherwise, let the older code deal with it. */
  if (*string == '"')
    {
      char *seq;
      register int j, k, passc;

      seq = (char *)xmalloc (1 + strlen (string));
      for (j = 1, k = passc = 0; string[j]; j++)
	{
	  /* Allow backslash to quote characters, but leave them in place.
	     This allows a string to end with a backslash quoting another
	     backslash, or with a backslash quoting a double quote.  The
	     backslashes are left in place for rl_translate_keyseq (). */
	  if (passc || (string[j] == '\\'))
	    {
	      seq[k++] = string[j];
	      passc = !passc;
	      continue;
	    }

	  if (string[j] == '"')
	    break;

	  seq[k++] = string[j];
	}
      seq[k] = '\0';

      /* Binding macro? */
      if (*funname == '\'' || *funname == '"')
	{
	  j = strlen (funname);

	  /* Remove the delimiting quotes from each end of FUNNAME. */
	  if (j && funname[j - 1] == *funname)
	    funname[j - 1] = '\0';

	  rl_macro_bind (seq, &funname[1], _rl_keymap);
	}
      else
	rl_set_key (seq, rl_named_function (funname), _rl_keymap);

      free (seq);
      return 0;
    }

  /* Get the actual character we want to deal with. */
  kname = strrchr (string, '-');
  if (!kname)
    kname = string;
  else
    kname++;

  key = glean_key_from_name (kname);

  /* Add in control and meta bits. */
  if (substring_member_of_array (string, _rl_possible_control_prefixes))
    key = CTRL (_rl_to_upper (key));

  if (substring_member_of_array (string, _rl_possible_meta_prefixes))
    key = META (key);

  /* Temporary.  Handle old-style keyname with macro-binding. */
  if (*funname == '\'' || *funname == '"')
    {
      char useq[2];
      int fl = strlen (funname);

      useq[0] = key; useq[1] = '\0';
      if (fl && funname[fl - 1] == *funname)
	funname[fl - 1] = '\0';

      rl_macro_bind (useq, &funname[1], _rl_keymap);
    }
#if defined (PREFIX_META_HACK)
  /* Ugly, but working hack to keep prefix-meta around. */
  else if (_rl_stricmp (funname, "prefix-meta") == 0)
    {
      char seq[2];

      seq[0] = key;
      seq[1] = '\0';
      rl_generic_bind (ISKMAP, seq, (char *)emacs_meta_keymap, _rl_keymap);
    }
#endif /* PREFIX_META_HACK */
  else
    rl_bind_key (key, rl_named_function (funname));
  return 0;
}

/* Simple structure for boolean readline variables (i.e., those that can
   have one of two values; either "On" or 1 for truth, or "Off" or 0 for
   false. */

#define V_SPECIAL	0x1

static struct {
  const char *name;
  int *value;
  int flags;
} boolean_varlist [] = {
  { "blink-matching-paren",	&rl_blink_matching_paren,	V_SPECIAL },
  { "byte-oriented",		&rl_byte_oriented,		0 },
  { "completion-ignore-case",	&_rl_completion_case_fold,	0 },
  { "convert-meta",		&_rl_convert_meta_chars_to_ascii, 0 },
  { "disable-completion",	&rl_inhibit_completion,		0 },
  { "enable-keypad",		&_rl_enable_keypad,		0 },
  { "expand-tilde",		&rl_complete_with_tilde_expansion, 0 },
  { "history-preserve-point",	&_rl_history_preserve_point,	0 },
  { "horizontal-scroll-mode",	&_rl_horizontal_scroll_mode,	0 },
  { "input-meta",		&_rl_meta_flag,			0 },
  { "mark-directories",		&_rl_complete_mark_directories,	0 },
  { "mark-modified-lines",	&_rl_mark_modified_lines,	0 },
  { "mark-symlinked-directories", &_rl_complete_mark_symlink_dirs, 0 },
  { "match-hidden-files",	&_rl_match_hidden_files,	0 },
  { "meta-flag",		&_rl_meta_flag,			0 },
  { "output-meta",		&_rl_output_meta_chars,		0 },
  { "page-completions",		&_rl_page_completions,		0 },
  { "prefer-visible-bell",	&_rl_prefer_visible_bell,	V_SPECIAL },
  { "print-completions-horizontally", &_rl_print_completions_horizontally, 0 },
  { "show-all-if-ambiguous",	&_rl_complete_show_all,		0 },
#if defined (VISIBLE_STATS)
  { "visible-stats",		&rl_visible_stats,		0 },
#endif /* VISIBLE_STATS */
  { (char *)NULL, (int *)NULL }
};

static int
find_boolean_var (name)
     const char *name;
{
  register int i;

  for (i = 0; boolean_varlist[i].name; i++)
    if (_rl_stricmp (name, boolean_varlist[i].name) == 0)
      return i;
  return -1;
}

/* Hooks for handling special boolean variables, where a
   function needs to be called or another variable needs
   to be changed when they're changed. */
static void
hack_special_boolean_var (i)
     int i;
{
  const char *name;

  name = boolean_varlist[i].name;

  if (_rl_stricmp (name, "blink-matching-paren") == 0)
    _rl_enable_paren_matching (rl_blink_matching_paren);
  else if (_rl_stricmp (name, "prefer-visible-bell") == 0)
    {
      if (_rl_prefer_visible_bell)
	_rl_bell_preference = VISIBLE_BELL;
      else
	_rl_bell_preference = AUDIBLE_BELL;
    }
}

typedef int _rl_sv_func_t PARAMS((const char *));

/* These *must* correspond to the array indices for the appropriate
   string variable.  (Though they're not used right now.) */
#define V_BELLSTYLE	0
#define V_COMBEGIN	1
#define V_EDITMODE	2
#define V_ISRCHTERM	3
#define V_KEYMAP	4

#define	V_STRING	1
#define V_INT		2

/* Forward declarations */
static int sv_bell_style PARAMS((const char *));
static int sv_combegin PARAMS((const char *));
static int sv_compquery PARAMS((const char *));
static int sv_editmode PARAMS((const char *));
static int sv_isrchterm PARAMS((const char *));
static int sv_keymap PARAMS((const char *));

static struct {
  const char *name;
  int flags;
  _rl_sv_func_t *set_func;
} string_varlist[] = {
  { "bell-style",	V_STRING,	sv_bell_style },
  { "comment-begin",	V_STRING,	sv_combegin },
  { "completion-query-items", V_INT,	sv_compquery },
  { "editing-mode",	V_STRING,	sv_editmode },
  { "isearch-terminators", V_STRING,	sv_isrchterm },
  { "keymap",		V_STRING,	sv_keymap },
  { (char *)NULL,	0 }
};

static int
find_string_var (name)
     const char *name;
{
  register int i;

  for (i = 0; string_varlist[i].name; i++)
    if (_rl_stricmp (name, string_varlist[i].name) == 0)
      return i;
  return -1;
}

/* A boolean value that can appear in a `set variable' command is true if
   the value is null or empty, `on' (case-insenstive), or "1".  Any other
   values result in 0 (false). */
static int
bool_to_int (value)
     const char *value;
{
  return (value == 0 || *value == '\0' ||
		(_rl_stricmp (value, "on") == 0) ||
		(value[0] == '1' && value[1] == '\0'));
}

int
rl_variable_bind (name, value)
     const char *name, *value;
{
  register int i;
  int	v;

  /* Check for simple variables first. */
  i = find_boolean_var (name);
  if (i >= 0)
    {
      *boolean_varlist[i].value = bool_to_int (value);
      if (boolean_varlist[i].flags & V_SPECIAL)
	hack_special_boolean_var (i);
      return 0;
    }

  i = find_string_var (name);

  /* For the time being, unknown variable names or string names without a
     handler function are simply ignored. */
  if (i < 0 || string_varlist[i].set_func == 0)
    return 0;

  v = (*string_varlist[i].set_func) (value);
  return v;
}

static int
sv_editmode (value)
     const char *value;
{
  if (_rl_strnicmp (value, "vi", 2) == 0)
    {
#if defined (VI_MODE)
      _rl_keymap = vi_insertion_keymap;
      rl_editing_mode = vi_mode;
#endif /* VI_MODE */
      return 0;
    }
  else if (_rl_strnicmp (value, "emacs", 5) == 0)
    {
      _rl_keymap = emacs_standard_keymap;
      rl_editing_mode = emacs_mode;
      return 0;
    }
  return 1;
}

static int
sv_combegin (value)
     const char *value;
{
  if (value && *value)
    {
      FREE (_rl_comment_begin);
      _rl_comment_begin = savestring (value);
      return 0;
    }
  return 1;
}

static int
sv_compquery (value)
     const char *value;
{
  int nval = 100;

  if (value && *value)
    {
      nval = atoi (value);
      if (nval < 0)
	nval = 0;
    }
  rl_completion_query_items = nval;
  return 0;
}

static int
sv_keymap (value)
     const char *value;
{
  Keymap kmap;

  kmap = rl_get_keymap_by_name (value);
  if (kmap)
    {
      rl_set_keymap (kmap);
      return 0;
    }
  return 1;
}

static int
sv_bell_style (value)
     const char *value;
{
  if (value == 0 || *value == '\0')
    _rl_bell_preference = AUDIBLE_BELL;
  else if (_rl_stricmp (value, "none") == 0 || _rl_stricmp (value, "off") == 0)
    _rl_bell_preference = NO_BELL;
  else if (_rl_stricmp (value, "audible") == 0 || _rl_stricmp (value, "on") == 0)
    _rl_bell_preference = AUDIBLE_BELL;
  else if (_rl_stricmp (value, "visible") == 0)
    _rl_bell_preference = VISIBLE_BELL;
  else
    return 1;
  return 0;
}

static int
sv_isrchterm (value)
     const char *value;
{
  int beg, end, delim;
  char *v;

  if (value == 0)
    return 1;

  /* Isolate the value and translate it into a character string. */
  v = savestring (value);
  FREE (_rl_isearch_terminators);
  if (v[0] == '"' || v[0] == '\'')
    {
      delim = v[0];
      for (beg = end = 1; v[end] && v[end] != delim; end++)
	;
    }
  else
    {
      for (beg = end = 0; whitespace (v[end]) == 0; end++)
	;
    }

  v[end] = '\0';

  /* The value starts at v + beg.  Translate it into a character string. */
  _rl_isearch_terminators = (char *)xmalloc (2 * strlen (v) + 1);
  rl_translate_keyseq (v + beg, _rl_isearch_terminators, &end);
  _rl_isearch_terminators[end] = '\0';

  free (v);
  return 0;
}

/* Return the character which matches NAME.
   For example, `Space' returns ' '. */

typedef struct {
  const char *name;
  int value;
} assoc_list;

static assoc_list name_key_alist[] = {
  { "DEL", 0x7f },
  { "ESC", '\033' },
  { "Escape", '\033' },
  { "LFD", '\n' },
  { "Newline", '\n' },
  { "RET", '\r' },
  { "Return", '\r' },
  { "Rubout", 0x7f },
  { "SPC", ' ' },
  { "Space", ' ' },
  { "Tab", 0x09 },
  { (char *)0x0, 0 }
};

static int
glean_key_from_name (name)
     char *name;
{
  register int i;

  for (i = 0; name_key_alist[i].name; i++)
    if (_rl_stricmp (name, name_key_alist[i].name) == 0)
      return (name_key_alist[i].value);

  return (*(unsigned char *)name);	/* XXX was return (*name) */
}

/* Auxiliary functions to manage keymaps. */
static struct {
  const char *name;
  Keymap map;
} keymap_names[] = {
  { "emacs", emacs_standard_keymap },
  { "emacs-standard", emacs_standard_keymap },
  { "emacs-meta", emacs_meta_keymap },
  { "emacs-ctlx", emacs_ctlx_keymap },
#if defined (VI_MODE)
  { "vi", vi_movement_keymap },
  { "vi-move", vi_movement_keymap },
  { "vi-command", vi_movement_keymap },
  { "vi-insert", vi_insertion_keymap },
#endif /* VI_MODE */
  { (char *)0x0, (Keymap)0x0 }
};

Keymap
rl_get_keymap_by_name (name)
     const char *name;
{
  register int i;

  for (i = 0; keymap_names[i].name; i++)
    if (_rl_stricmp (name, keymap_names[i].name) == 0)
      return (keymap_names[i].map);
  return ((Keymap) NULL);
}

char *
rl_get_keymap_name (map)
     Keymap map;
{
  register int i;
  for (i = 0; keymap_names[i].name; i++)
    if (map == keymap_names[i].map)
      return ((char *)keymap_names[i].name);
  return ((char *)NULL);
}

void
rl_set_keymap (map)
     Keymap map;
{
  if (map)
    _rl_keymap = map;
}

Keymap
rl_get_keymap ()
{
  return (_rl_keymap);
}

void
rl_set_keymap_from_edit_mode ()
{
  if (rl_editing_mode == emacs_mode)
    _rl_keymap = emacs_standard_keymap;
#if defined (VI_MODE)
  else if (rl_editing_mode == vi_mode)
    _rl_keymap = vi_insertion_keymap;
#endif /* VI_MODE */
}

char *
rl_get_keymap_name_from_edit_mode ()
{
  if (rl_editing_mode == emacs_mode)
    return "emacs";
#if defined (VI_MODE)
  else if (rl_editing_mode == vi_mode)
    return "vi";
#endif /* VI_MODE */
  else
    return "none";
}

/* **************************************************************** */
/*								    */
/*		  Key Binding and Function Information		    */
/*								    */
/* **************************************************************** */

/* Each of the following functions produces information about the
   state of keybindings and functions known to Readline.  The info
   is always printed to rl_outstream, and in such a way that it can
   be read back in (i.e., passed to rl_parse_and_bind (). */

/* Print the names of functions known to Readline. */
void
rl_list_funmap_names ()
{
  register int i;
  const char **funmap_names;

  funmap_names = rl_funmap_names ();

  if (!funmap_names)
    return;

  for (i = 0; funmap_names[i]; i++)
    fprintf (rl_outstream, "%s\n", funmap_names[i]);

  free (funmap_names);
}

static char *
_rl_get_keyname (key)
     int key;
{
  char *keyname;
  int i, c;

  keyname = (char *)xmalloc (8);

  c = key;
  /* Since this is going to be used to write out keysequence-function
     pairs for possible inclusion in an inputrc file, we don't want to
     do any special meta processing on KEY. */

#if 1
  /* XXX - Experimental */
  /* We might want to do this, but the old version of the code did not. */

  /* If this is an escape character, we don't want to do any more processing.
     Just add the special ESC key sequence and return. */
  if (c == ESC)
    {
      keyname[0] = '\\';
      keyname[1] = 'e';
      keyname[2] = '\0';
      return keyname;
    }
#endif

  /* RUBOUT is translated directly into \C-? */
  if (key == RUBOUT)
    {
      keyname[0] = '\\';
      keyname[1] = 'C';
      keyname[2] = '-';
      keyname[3] = '?';
      keyname[4] = '\0';
      return keyname;
    }

  i = 0;
  /* Now add special prefixes needed for control characters.  This can
     potentially change C. */
  if (CTRL_CHAR (c))
    {
      keyname[i++] = '\\';
      keyname[i++] = 'C';
      keyname[i++] = '-';
      c = _rl_to_lower (UNCTRL (c));
    }

  /* XXX experimental code.  Turn the characters that are not ASCII or
     ISO Latin 1 (128 - 159) into octal escape sequences (\200 - \237).
     This changes C. */
  if (c >= 128 && c <= 159)
    {
      keyname[i++] = '\\';
      keyname[i++] = '2';
      c -= 128;
      keyname[i++] = (c / 8) + '0';
      c = (c % 8) + '0';
    }

  /* Now, if the character needs to be quoted with a backslash, do that. */
  if (c == '\\' || c == '"')
    keyname[i++] = '\\';

  /* Now add the key, terminate the string, and return it. */
  keyname[i++] = (char) c;
  keyname[i] = '\0';

  return keyname;
}

/* Return a NULL terminated array of strings which represent the key
   sequences that are used to invoke FUNCTION in MAP. */
char **
rl_invoking_keyseqs_in_map (function, map)
     rl_command_func_t *function;
     Keymap map;
{
  register int key;
  char **result;
  int result_index, result_size;

  result = (char **)NULL;
  result_index = result_size = 0;

  for (key = 0; key < KEYMAP_SIZE; key++)
    {
      switch (map[key].type)
	{
	case ISMACR:
	  /* Macros match, if, and only if, the pointers are identical.
	     Thus, they are treated exactly like functions in here. */
	case ISFUNC:
	  /* If the function in the keymap is the one we are looking for,
	     then add the current KEY to the list of invoking keys. */
	  if (map[key].function == function)
	    {
	      char *keyname;

	      keyname = _rl_get_keyname (key);

	      if (result_index + 2 > result_size)
	        {
	          result_size += 10;
		  result = (char **)xrealloc (result, result_size * sizeof (char *));
	        }

	      result[result_index++] = keyname;
	      result[result_index] = (char *)NULL;
	    }
	  break;

	case ISKMAP:
	  {
	    char **seqs;
	    register int i;

	    /* Find the list of keyseqs in this map which have FUNCTION as
	       their target.  Add the key sequences found to RESULT. */
	    if (map[key].function)
	      seqs =
	        rl_invoking_keyseqs_in_map (function, FUNCTION_TO_KEYMAP (map, key));
	    else
	      break;

	    if (seqs == 0)
	      break;

	    for (i = 0; seqs[i]; i++)
	      {
		int len = 6 + strlen(seqs[i]);
		char *keyname = (char *)xmalloc (len);

		if (key == ESC)
#if 0
		  snprintf(keyname, len, "\\e");
#else
		/* XXX - experimental */
		  snprintf(keyname, len, "\\M-");
#endif
		else if (CTRL_CHAR (key))
		  snprintf(keyname, len, "\\C-%c", _rl_to_lower (UNCTRL (key)));
		else if (key == RUBOUT)
		  snprintf(keyname, len, "\\C-?");
		else if (key == '\\' || key == '"')
		  {
		    keyname[0] = '\\';
		    keyname[1] = (char) key;
		    keyname[2] = '\0';
		  }
		else
		  {
		    keyname[0] = (char) key;
		    keyname[1] = '\0';
		  }

		strlcat (keyname, seqs[i], len);
		free (seqs[i]);

		if (result_index + 2 > result_size)
		  {
		    result_size += 10;
		    result = (char **)xrealloc (result, result_size * sizeof (char *));
		  }

		result[result_index++] = keyname;
		result[result_index] = (char *)NULL;
	      }

	    free (seqs);
	  }
	  break;
	}
    }
  return (result);
}

/* Return a NULL terminated array of strings which represent the key
   sequences that can be used to invoke FUNCTION using the current keymap. */
char **
rl_invoking_keyseqs (function)
     rl_command_func_t *function;
{
  return (rl_invoking_keyseqs_in_map (function, _rl_keymap));
}

/* Print all of the functions and their bindings to rl_outstream.  If
   PRINT_READABLY is non-zero, then print the output in such a way
   that it can be read back in. */
void
rl_function_dumper (print_readably)
     int print_readably;
{
  register int i;
  const char **names;
  const char *name;

  names = rl_funmap_names ();

  fprintf (rl_outstream, "\n");

  for (i = 0; (name = names[i]); i++)
    {
      rl_command_func_t *function;
      char **invokers;

      function = rl_named_function (name);
      invokers = rl_invoking_keyseqs_in_map (function, _rl_keymap);

      if (print_readably)
	{
	  if (!invokers)
	    fprintf (rl_outstream, "# %s (not bound)\n", name);
	  else
	    {
	      register int j;

	      for (j = 0; invokers[j]; j++)
		{
		  fprintf (rl_outstream, "\"%s\": %s\n",
			   invokers[j], name);
		  free (invokers[j]);
		}

	      free (invokers);
	    }
	}
      else
	{
	  if (!invokers)
	    fprintf (rl_outstream, "%s is not bound to any keys\n",
		     name);
	  else
	    {
	      register int j;

	      fprintf (rl_outstream, "%s can be found on ", name);

	      for (j = 0; invokers[j] && j < 5; j++)
		{
		  fprintf (rl_outstream, "\"%s\"%s", invokers[j],
			   invokers[j + 1] ? ", " : ".\n");
		}

	      if (j == 5 && invokers[j])
		fprintf (rl_outstream, "...\n");

	      for (j = 0; invokers[j]; j++)
		free (invokers[j]);

	      free (invokers);
	    }
	}
    }
}

/* Print all of the current functions and their bindings to
   rl_outstream.  If an explicit argument is given, then print
   the output in such a way that it can be read back in. */
int
rl_dump_functions (count, key)
     int count, key;
{
  if (rl_dispatching)
    fprintf (rl_outstream, "\r\n");
  rl_function_dumper (rl_explicit_arg);
  rl_on_new_line ();
  return (0);
}

static void
_rl_macro_dumper_internal (print_readably, map, prefix)
     int print_readably;
     Keymap map;
     char *prefix;
{
  register int key;
  char *keyname, *out;
  int prefix_len;

  for (key = 0; key < KEYMAP_SIZE; key++)
    {
      switch (map[key].type)
	{
	case ISMACR:
	  keyname = _rl_get_keyname (key);
	  out = _rl_untranslate_macro_value ((char *)map[key].function);

	  if (print_readably)
	    fprintf (rl_outstream, "\"%s%s\": \"%s\"\n", prefix ? prefix : "",
						         keyname,
						         out ? out : "");
	  else
	    fprintf (rl_outstream, "%s%s outputs %s\n", prefix ? prefix : "",
							keyname,
							out ? out : "");
	  free (keyname);
	  free (out);
	  break;
	case ISFUNC:
	  break;
	case ISKMAP:
	  prefix_len = prefix ? strlen (prefix) : 0;
	  if (key == ESC)
	    {
	      int len = 3 + prefix_len;
	      keyname = (char *)xmalloc (len);
	      if (prefix)
		strlcpy (keyname, prefix, len);
	      keyname[prefix_len] = '\\';
	      keyname[prefix_len + 1] = 'e';
	      keyname[prefix_len + 2] = '\0';
	    }
	  else
	    {
	      keyname = _rl_get_keyname (key);
	      if (prefix)
		{
		  if (asprintf(&out, "%s%s", prefix, keyname) == -1)
		    memory_error_and_abort("asprintf");
		  free (keyname);
		  keyname = out;
		}
	    }

	  _rl_macro_dumper_internal (print_readably, FUNCTION_TO_KEYMAP (map, key), keyname);
	  free (keyname);
	  break;
	}
    }
}

void
rl_macro_dumper (print_readably)
     int print_readably;
{
  _rl_macro_dumper_internal (print_readably, _rl_keymap, (char *)NULL);
}

int
rl_dump_macros (count, key)
     int count, key;
{
  if (rl_dispatching)
    fprintf (rl_outstream, "\r\n");
  rl_macro_dumper (rl_explicit_arg);
  rl_on_new_line ();
  return (0);
}

void
rl_variable_dumper (print_readably)
     int print_readably;
{
  int i;
  const char *kname;

  for (i = 0; boolean_varlist[i].name; i++)
    {
      if (print_readably)
        fprintf (rl_outstream, "set %s %s\n", boolean_varlist[i].name,
			       *boolean_varlist[i].value ? "on" : "off");
      else
        fprintf (rl_outstream, "%s is set to `%s'\n", boolean_varlist[i].name,
			       *boolean_varlist[i].value ? "on" : "off");
    }

  /* bell-style */
  switch (_rl_bell_preference)
    {
    case NO_BELL:
      kname = "none"; break;
    case VISIBLE_BELL:
      kname = "visible"; break;
    case AUDIBLE_BELL:
    default:
      kname = "audible"; break;
    }
  if (print_readably)
    fprintf (rl_outstream, "set bell-style %s\n", kname);
  else
    fprintf (rl_outstream, "bell-style is set to `%s'\n", kname);

  /* comment-begin */
  if (print_readably)
    fprintf (rl_outstream, "set comment-begin %s\n", _rl_comment_begin ? _rl_comment_begin : RL_COMMENT_BEGIN_DEFAULT);
  else
    fprintf (rl_outstream, "comment-begin is set to `%s'\n", _rl_comment_begin ? _rl_comment_begin : RL_COMMENT_BEGIN_DEFAULT);

  /* completion-query-items */
  if (print_readably)
    fprintf (rl_outstream, "set completion-query-items %d\n", rl_completion_query_items);
  else
    fprintf (rl_outstream, "completion-query-items is set to `%d'\n", rl_completion_query_items);

  /* editing-mode */
  if (print_readably)
    fprintf (rl_outstream, "set editing-mode %s\n", (rl_editing_mode == emacs_mode) ? "emacs" : "vi");
  else
    fprintf (rl_outstream, "editing-mode is set to `%s'\n", (rl_editing_mode == emacs_mode) ? "emacs" : "vi");

  /* isearch-terminators */
  if (_rl_isearch_terminators)
    {
      char *disp;

      disp = _rl_untranslate_macro_value (_rl_isearch_terminators);

      if (print_readably)
	fprintf (rl_outstream, "set isearch-terminators \"%s\"\n", disp);
      else
	fprintf (rl_outstream, "isearch-terminators is set to \"%s\"\n", disp);

      free (disp);
    }

  /* keymap */
  kname = rl_get_keymap_name (_rl_keymap);
  if (kname == 0)
    kname = rl_get_keymap_name_from_edit_mode ();
  if (print_readably)
    fprintf (rl_outstream, "set keymap %s\n", kname ? kname : "none");
  else
    fprintf (rl_outstream, "keymap is set to `%s'\n", kname ? kname : "none");
}

/* Print all of the current variables and their values to
   rl_outstream.  If an explicit argument is given, then print
   the output in such a way that it can be read back in. */
int
rl_dump_variables (count, key)
     int count, key;
{
  if (rl_dispatching)
    fprintf (rl_outstream, "\r\n");
  rl_variable_dumper (rl_explicit_arg);
  rl_on_new_line ();
  return (0);
}

/* Bind key sequence KEYSEQ to DEFAULT_FUNC if KEYSEQ is unbound.  Right
   now, this is always used to attempt to bind the arrow keys, hence the
   check for rl_vi_movement_mode. */
void
_rl_bind_if_unbound (keyseq, default_func)
     const char *keyseq;
     rl_command_func_t *default_func;
{
  rl_command_func_t *func;

  if (keyseq)
    {
      func = rl_function_of_keyseq (keyseq, _rl_keymap, (int *)NULL);
#if defined (VI_MODE)
      if (!func || func == rl_do_lowercase_version || func == rl_vi_movement_mode)
#else
      if (!func || func == rl_do_lowercase_version)
#endif
	rl_set_key (keyseq, default_func, _rl_keymap);
    }
}

/* Return non-zero if any members of ARRAY are a substring in STRING. */
static int
substring_member_of_array (string, array)
     char *string;
     const char **array;
{
  while (*array)
    {
      if (_rl_strindex (string, *array))
	return (1);
      array++;
    }
  return (0);
}
