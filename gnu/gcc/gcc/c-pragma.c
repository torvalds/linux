/* Handle #pragma, system V.4 style.  Supports #pragma weak and #pragma pack.
   Copyright (C) 1992, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005,
   2006 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "tree.h"
#include "function.h"
#include "cpplib.h"
#include "c-pragma.h"
#include "flags.h"
#include "toplev.h"
#include "ggc.h"
#include "c-common.h"
#include "output.h"
#include "tm_p.h"
#include "vec.h"
#include "target.h"
#include "diagnostic.h"
#include "opts.h"

#define GCC_BAD(gmsgid) \
  do { warning (OPT_Wpragmas, gmsgid); return; } while (0)
#define GCC_BAD2(gmsgid, arg) \
  do { warning (OPT_Wpragmas, gmsgid, arg); return; } while (0)

typedef struct align_stack GTY(())
{
  int		       alignment;
  tree		       id;
  struct align_stack * prev;
} align_stack;

static GTY(()) struct align_stack * alignment_stack;

#ifdef HANDLE_PRAGMA_PACK
static void handle_pragma_pack (cpp_reader *);

#ifdef HANDLE_PRAGMA_PACK_PUSH_POP
/* If we have a "global" #pragma pack(<n>) in effect when the first
   #pragma pack(push,<n>) is encountered, this stores the value of
   maximum_field_alignment in effect.  When the final pop_alignment()
   happens, we restore the value to this, not to a value of 0 for
   maximum_field_alignment.  Value is in bits.  */
static int default_alignment;
#define SET_GLOBAL_ALIGNMENT(ALIGN) (maximum_field_alignment = *(alignment_stack == NULL \
	? &default_alignment \
	: &alignment_stack->alignment) = (ALIGN))

static void push_alignment (int, tree);
static void pop_alignment (tree);

/* Push an alignment value onto the stack.  */
static void
push_alignment (int alignment, tree id)
{
  align_stack * entry;

  entry = GGC_NEW (align_stack);

  entry->alignment  = alignment;
  entry->id	    = id;
  entry->prev	    = alignment_stack;

  /* The current value of maximum_field_alignment is not necessarily
     0 since there may be a #pragma pack(<n>) in effect; remember it
     so that we can restore it after the final #pragma pop().  */
  if (alignment_stack == NULL)
    default_alignment = maximum_field_alignment;

  alignment_stack = entry;

  maximum_field_alignment = alignment;
}

/* Undo a push of an alignment onto the stack.  */
static void
pop_alignment (tree id)
{
  align_stack * entry;

  if (alignment_stack == NULL)
    GCC_BAD ("#pragma pack (pop) encountered without matching #pragma pack (push)");

  /* If we got an identifier, strip away everything above the target
     entry so that the next step will restore the state just below it.  */
  if (id)
    {
      for (entry = alignment_stack; entry; entry = entry->prev)
	if (entry->id == id)
	  {
	    alignment_stack = entry;
	    break;
	  }
      if (entry == NULL)
	warning (OPT_Wpragmas, "\
#pragma pack(pop, %s) encountered without matching #pragma pack(push, %s)"
		 , IDENTIFIER_POINTER (id), IDENTIFIER_POINTER (id));
    }

  entry = alignment_stack->prev;

  maximum_field_alignment = entry ? entry->alignment : default_alignment;

  alignment_stack = entry;
}
#else  /* not HANDLE_PRAGMA_PACK_PUSH_POP */
#define SET_GLOBAL_ALIGNMENT(ALIGN) (maximum_field_alignment = (ALIGN))
#define push_alignment(ID, N) \
    GCC_BAD ("#pragma pack(push[, id], <n>) is not supported on this target")
#define pop_alignment(ID) \
    GCC_BAD ("#pragma pack(pop[, id], <n>) is not supported on this target")
#endif /* HANDLE_PRAGMA_PACK_PUSH_POP */

/* #pragma pack ()
   #pragma pack (N)

   #pragma pack (push)
   #pragma pack (push, N)
   #pragma pack (push, ID)
   #pragma pack (push, ID, N)
   #pragma pack (pop)
   #pragma pack (pop, ID) */
static void
handle_pragma_pack (cpp_reader * ARG_UNUSED (dummy))
{
  tree x, id = 0;
  int align = -1;
  enum cpp_ttype token;
  enum { set, push, pop } action;

  if (pragma_lex (&x) != CPP_OPEN_PAREN)
    GCC_BAD ("missing %<(%> after %<#pragma pack%> - ignored");

  token = pragma_lex (&x);
  if (token == CPP_CLOSE_PAREN)
    {
      action = set;
      align = initial_max_fld_align;
    }
  else if (token == CPP_NUMBER)
    {
      if (TREE_CODE (x) != INTEGER_CST)
	GCC_BAD ("invalid constant in %<#pragma pack%> - ignored");
      align = TREE_INT_CST_LOW (x);
      action = set;
      if (pragma_lex (&x) != CPP_CLOSE_PAREN)
	GCC_BAD ("malformed %<#pragma pack%> - ignored");
    }
  else if (token == CPP_NAME)
    {
#define GCC_BAD_ACTION do { if (action != pop) \
	  GCC_BAD ("malformed %<#pragma pack(push[, id][, <n>])%> - ignored"); \
	else \
	  GCC_BAD ("malformed %<#pragma pack(pop[, id])%> - ignored"); \
	} while (0)

      const char *op = IDENTIFIER_POINTER (x);
      if (!strcmp (op, "push"))
	action = push;
      else if (!strcmp (op, "pop"))
	action = pop;
      else
	GCC_BAD2 ("unknown action %qs for %<#pragma pack%> - ignored", op);

      while ((token = pragma_lex (&x)) == CPP_COMMA)
	{
	  token = pragma_lex (&x);
	  if (token == CPP_NAME && id == 0)
	    {
	      id = x;
	    }
	  else if (token == CPP_NUMBER && action == push && align == -1)
	    {
	      if (TREE_CODE (x) != INTEGER_CST)
		GCC_BAD ("invalid constant in %<#pragma pack%> - ignored");
	      align = TREE_INT_CST_LOW (x);
	      if (align == -1)
		action = set;
	    }
	  else
	    GCC_BAD_ACTION;
	}

      if (token != CPP_CLOSE_PAREN)
	GCC_BAD_ACTION;
#undef GCC_BAD_ACTION
    }
  else
    GCC_BAD ("malformed %<#pragma pack%> - ignored");

  if (pragma_lex (&x) != CPP_EOF)
    warning (OPT_Wpragmas, "junk at end of %<#pragma pack%>");

  if (flag_pack_struct)
    GCC_BAD ("#pragma pack has no effect with -fpack-struct - ignored");

  if (action != pop)
    switch (align)
      {
      case 0:
      case 1:
      case 2:
      case 4:
      case 8:
      case 16:
	align *= BITS_PER_UNIT;
	break;
      case -1:
	if (action == push)
	  {
	    align = maximum_field_alignment;
	    break;
	  }
      default:
	GCC_BAD2 ("alignment must be a small power of two, not %d", align);
      }

  switch (action)
    {
    case set:   SET_GLOBAL_ALIGNMENT (align);  break;
    case push:  push_alignment (align, id);    break;
    case pop:   pop_alignment (id);	       break;
    }
}
#endif  /* HANDLE_PRAGMA_PACK */

static GTY(()) tree pending_weaks;

#ifdef HANDLE_PRAGMA_WEAK
static void apply_pragma_weak (tree, tree);
static void handle_pragma_weak (cpp_reader *);

static void
apply_pragma_weak (tree decl, tree value)
{
  if (value)
    {
      value = build_string (IDENTIFIER_LENGTH (value),
			    IDENTIFIER_POINTER (value));
      decl_attributes (&decl, build_tree_list (get_identifier ("alias"),
					       build_tree_list (NULL, value)),
		       0);
    }

  if (SUPPORTS_WEAK && DECL_EXTERNAL (decl) && TREE_USED (decl)
      && !DECL_WEAK (decl) /* Don't complain about a redundant #pragma.  */
      && TREE_SYMBOL_REFERENCED (DECL_ASSEMBLER_NAME (decl)))
    warning (OPT_Wpragmas, "applying #pragma weak %q+D after first use "
	     "results in unspecified behavior", decl);

  declare_weak (decl);
}

void
maybe_apply_pragma_weak (tree decl)
{
  tree *p, t, id;

  /* Avoid asking for DECL_ASSEMBLER_NAME when it's not needed.  */

  /* No weak symbols pending, take the short-cut.  */
  if (!pending_weaks)
    return;
  /* If it's not visible outside this file, it doesn't matter whether
     it's weak.  */
  if (!DECL_EXTERNAL (decl) && !TREE_PUBLIC (decl))
    return;
  /* If it's not a function or a variable, it can't be weak.
     FIXME: what kinds of things are visible outside this file but
     aren't functions or variables?   Should this be an assert instead?  */
  if (TREE_CODE (decl) != FUNCTION_DECL && TREE_CODE (decl) != VAR_DECL)
    return;

  id = DECL_ASSEMBLER_NAME (decl);

  for (p = &pending_weaks; (t = *p) ; p = &TREE_CHAIN (t))
    if (id == TREE_PURPOSE (t))
      {
	apply_pragma_weak (decl, TREE_VALUE (t));
	*p = TREE_CHAIN (t);
	break;
      }
}

/* Process all "#pragma weak A = B" directives where we have not seen
   a decl for A.  */
void
maybe_apply_pending_pragma_weaks (void)
{
  tree *p, t, alias_id, id, decl, *next;

  for (p = &pending_weaks; (t = *p) ; p = next)
    {
      next = &TREE_CHAIN (t);
      alias_id = TREE_PURPOSE (t);
      id = TREE_VALUE (t);

      if (TREE_VALUE (t) == NULL)
	continue;

      decl = build_decl (FUNCTION_DECL, alias_id, default_function_type);

      DECL_ARTIFICIAL (decl) = 1;
      TREE_PUBLIC (decl) = 1;
      DECL_EXTERNAL (decl) = 1;
      DECL_WEAK (decl) = 1;

      assemble_alias (decl, id);
    }
}

/* #pragma weak name [= value] */
static void
handle_pragma_weak (cpp_reader * ARG_UNUSED (dummy))
{
  tree name, value, x, decl;
  enum cpp_ttype t;

  value = 0;

  if (pragma_lex (&name) != CPP_NAME)
    GCC_BAD ("malformed #pragma weak, ignored");
  t = pragma_lex (&x);
  if (t == CPP_EQ)
    {
      if (pragma_lex (&value) != CPP_NAME)
	GCC_BAD ("malformed #pragma weak, ignored");
      t = pragma_lex (&x);
    }
  if (t != CPP_EOF)
    warning (OPT_Wpragmas, "junk at end of %<#pragma weak%>");

  decl = identifier_global_value (name);
  if (decl && DECL_P (decl))
    {
      apply_pragma_weak (decl, value);
      if (value)
	assemble_alias (decl, value);
    }
  else
    pending_weaks = tree_cons (name, value, pending_weaks);
}
#else
void
maybe_apply_pragma_weak (tree ARG_UNUSED (decl))
{
}

void
maybe_apply_pending_pragma_weaks (void)
{
}
#endif /* HANDLE_PRAGMA_WEAK */

/* GCC supports two #pragma directives for renaming the external
   symbol associated with a declaration (DECL_ASSEMBLER_NAME), for
   compatibility with the Solaris and Tru64 system headers.  GCC also
   has its own notation for this, __asm__("name") annotations.

   Corner cases of these features and their interaction:

   1) Both pragmas silently apply only to declarations with external
      linkage (that is, TREE_PUBLIC || DECL_EXTERNAL).  Asm labels
      do not have this restriction.

   2) In C++, both #pragmas silently apply only to extern "C" declarations.
      Asm labels do not have this restriction.

   3) If any of the three ways of changing DECL_ASSEMBLER_NAME is
      applied to a decl whose DECL_ASSEMBLER_NAME is already set, and the
      new name is different, a warning issues and the name does not change.

   4) The "source name" for #pragma redefine_extname is the DECL_NAME,
      *not* the DECL_ASSEMBLER_NAME.

   5) If #pragma extern_prefix is in effect and a declaration occurs
      with an __asm__ name, the #pragma extern_prefix is silently
      ignored for that declaration.

   6) If #pragma extern_prefix and #pragma redefine_extname apply to
      the same declaration, whichever triggered first wins, and a warning
      is issued.  (We would like to have #pragma redefine_extname always
      win, but it can appear either before or after the declaration, and
      if it appears afterward, we have no way of knowing whether a modified
      DECL_ASSEMBLER_NAME is due to #pragma extern_prefix.)  */

static GTY(()) tree pending_redefine_extname;

static void handle_pragma_redefine_extname (cpp_reader *);

/* #pragma redefine_extname oldname newname */
static void
handle_pragma_redefine_extname (cpp_reader * ARG_UNUSED (dummy))
{
  tree oldname, newname, decl, x;
  enum cpp_ttype t;

  if (pragma_lex (&oldname) != CPP_NAME)
    GCC_BAD ("malformed #pragma redefine_extname, ignored");
  if (pragma_lex (&newname) != CPP_NAME)
    GCC_BAD ("malformed #pragma redefine_extname, ignored");
  t = pragma_lex (&x);
  if (t != CPP_EOF)
    warning (OPT_Wpragmas, "junk at end of %<#pragma redefine_extname%>");

  if (!flag_mudflap && !targetm.handle_pragma_redefine_extname)
    {
      if (warn_unknown_pragmas > in_system_header)
	warning (OPT_Wunknown_pragmas,
		 "#pragma redefine_extname not supported on this target");
      return;
    }

  decl = identifier_global_value (oldname);
  if (decl
      && (TREE_PUBLIC (decl) || DECL_EXTERNAL (decl))
      && (TREE_CODE (decl) == FUNCTION_DECL
	  || TREE_CODE (decl) == VAR_DECL)
      && has_c_linkage (decl))
    {
      if (DECL_ASSEMBLER_NAME_SET_P (decl))
	{
	  const char *name = IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (decl));
	  name = targetm.strip_name_encoding (name);

	  if (strcmp (name, IDENTIFIER_POINTER (newname)))
	    warning (OPT_Wpragmas, "#pragma redefine_extname ignored due to "
		     "conflict with previous rename");
	}
      else
	change_decl_assembler_name (decl, newname);
    }
  else
    /* We have to add this to the rename list even if there's already
       a global value that doesn't meet the above criteria, because in
       C++ "struct foo {...};" puts "foo" in the current namespace but
       does *not* conflict with a subsequent declaration of a function
       or variable foo.  See g++.dg/other/pragma-re-2.C.  */
    add_to_renaming_pragma_list (oldname, newname);
}

/* This is called from here and from ia64.c.  */
void
add_to_renaming_pragma_list (tree oldname, tree newname)
{
  tree previous = purpose_member (oldname, pending_redefine_extname);
  if (previous)
    {
      if (TREE_VALUE (previous) != newname)
	warning (OPT_Wpragmas, "#pragma redefine_extname ignored due to "
		 "conflict with previous #pragma redefine_extname");
      return;
    }

  pending_redefine_extname
    = tree_cons (oldname, newname, pending_redefine_extname);
}

static GTY(()) tree pragma_extern_prefix;

/* #pragma extern_prefix "prefix" */
static void
handle_pragma_extern_prefix (cpp_reader * ARG_UNUSED (dummy))
{
  tree prefix, x;
  enum cpp_ttype t;

  if (pragma_lex (&prefix) != CPP_STRING)
    GCC_BAD ("malformed #pragma extern_prefix, ignored");
  t = pragma_lex (&x);
  if (t != CPP_EOF)
    warning (OPT_Wpragmas, "junk at end of %<#pragma extern_prefix%>");

  if (targetm.handle_pragma_extern_prefix)
    /* Note that the length includes the null terminator.  */
    pragma_extern_prefix = (TREE_STRING_LENGTH (prefix) > 1 ? prefix : NULL);
  else if (warn_unknown_pragmas > in_system_header)
    warning (OPT_Wunknown_pragmas,
	     "#pragma extern_prefix not supported on this target");
}

/* Hook from the front ends to apply the results of one of the preceding
   pragmas that rename variables.  */

tree
maybe_apply_renaming_pragma (tree decl, tree asmname)
{
  tree *p, t;

  /* The renaming pragmas are only applied to declarations with
     external linkage.  */
  if ((TREE_CODE (decl) != FUNCTION_DECL && TREE_CODE (decl) != VAR_DECL)
      || (!TREE_PUBLIC (decl) && !DECL_EXTERNAL (decl))
      || !has_c_linkage (decl))
    return asmname;

  /* If the DECL_ASSEMBLER_NAME is already set, it does not change,
     but we may warn about a rename that conflicts.  */
  if (DECL_ASSEMBLER_NAME_SET_P (decl))
    {
      const char *oldname = IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (decl));
      oldname = targetm.strip_name_encoding (oldname);

      if (asmname && strcmp (TREE_STRING_POINTER (asmname), oldname))
	  warning (OPT_Wpragmas, "asm declaration ignored due to "
		   "conflict with previous rename");

      /* Take any pending redefine_extname off the list.  */
      for (p = &pending_redefine_extname; (t = *p); p = &TREE_CHAIN (t))
	if (DECL_NAME (decl) == TREE_PURPOSE (t))
	  {
	    /* Only warn if there is a conflict.  */
	    if (strcmp (IDENTIFIER_POINTER (TREE_VALUE (t)), oldname))
	      warning (OPT_Wpragmas, "#pragma redefine_extname ignored due to "
		       "conflict with previous rename");

	    *p = TREE_CHAIN (t);
	    break;
	  }
      return 0;
    }

  /* Find out if we have a pending #pragma redefine_extname.  */
  for (p = &pending_redefine_extname; (t = *p); p = &TREE_CHAIN (t))
    if (DECL_NAME (decl) == TREE_PURPOSE (t))
      {
	tree newname = TREE_VALUE (t);
	*p = TREE_CHAIN (t);

	/* If we already have an asmname, #pragma redefine_extname is
	   ignored (with a warning if it conflicts).  */
	if (asmname)
	  {
	    if (strcmp (TREE_STRING_POINTER (asmname),
			IDENTIFIER_POINTER (newname)) != 0)
	      warning (OPT_Wpragmas, "#pragma redefine_extname ignored due to "
		       "conflict with __asm__ declaration");
	    return asmname;
	  }

	/* Otherwise we use what we've got; #pragma extern_prefix is
	   silently ignored.  */
	return build_string (IDENTIFIER_LENGTH (newname),
			     IDENTIFIER_POINTER (newname));
      }

  /* If we've got an asmname, #pragma extern_prefix is silently ignored.  */
  if (asmname)
    return asmname;

  /* If #pragma extern_prefix is in effect, apply it.  */
  if (pragma_extern_prefix)
    {
      const char *prefix = TREE_STRING_POINTER (pragma_extern_prefix);
      size_t plen = TREE_STRING_LENGTH (pragma_extern_prefix) - 1;

      const char *id = IDENTIFIER_POINTER (DECL_NAME (decl));
      size_t ilen = IDENTIFIER_LENGTH (DECL_NAME (decl));

      char *newname = (char *) alloca (plen + ilen + 1);

      memcpy (newname,        prefix, plen);
      memcpy (newname + plen, id, ilen + 1);

      return build_string (plen + ilen, newname);
    }

  /* Nada.  */
  return 0;
}


#ifdef HANDLE_PRAGMA_VISIBILITY
static void handle_pragma_visibility (cpp_reader *);

typedef enum symbol_visibility visibility;
DEF_VEC_I (visibility);
DEF_VEC_ALLOC_I (visibility, heap);
static VEC (visibility, heap) *visstack;

/* Push the visibility indicated by STR onto the top of the #pragma
   visibility stack.  */

void
push_visibility (const char *str)
{
  VEC_safe_push (visibility, heap, visstack,
		 default_visibility);
  if (!strcmp (str, "default"))
    default_visibility = VISIBILITY_DEFAULT;
  else if (!strcmp (str, "internal"))
    default_visibility = VISIBILITY_INTERNAL;
  else if (!strcmp (str, "hidden"))
    default_visibility = VISIBILITY_HIDDEN;
  else if (!strcmp (str, "protected"))
    default_visibility = VISIBILITY_PROTECTED;
  else
    GCC_BAD ("#pragma GCC visibility push() must specify default, internal, hidden or protected");
  visibility_options.inpragma = 1;
}

/* Pop a level of the #pragma visibility stack.  */

void
pop_visibility (void)
{
  default_visibility = VEC_pop (visibility, visstack);
  visibility_options.inpragma
    = VEC_length (visibility, visstack) != 0;
}

/* Sets the default visibility for symbols to something other than that
   specified on the command line.  */

static void
handle_pragma_visibility (cpp_reader *dummy ATTRIBUTE_UNUSED)
{
  /* Form is #pragma GCC visibility push(hidden)|pop */
  tree x;
  enum cpp_ttype token;
  enum { bad, push, pop } action = bad;

  token = pragma_lex (&x);
  if (token == CPP_NAME)
    {
      const char *op = IDENTIFIER_POINTER (x);
      if (!strcmp (op, "push"))
	action = push;
      else if (!strcmp (op, "pop"))
	action = pop;
    }
  if (bad == action)
    GCC_BAD ("#pragma GCC visibility must be followed by push or pop");
  else
    {
      if (pop == action)
	{
	  if (!VEC_length (visibility, visstack))
	    GCC_BAD ("no matching push for %<#pragma GCC visibility pop%>");
	  else
	    pop_visibility ();
	}
      else
	{
	  if (pragma_lex (&x) != CPP_OPEN_PAREN)
	    GCC_BAD ("missing %<(%> after %<#pragma GCC visibility push%> - ignored");
	  token = pragma_lex (&x);
	  if (token != CPP_NAME)
	    GCC_BAD ("malformed #pragma GCC visibility push");
	  else
	    push_visibility (IDENTIFIER_POINTER (x));
	  if (pragma_lex (&x) != CPP_CLOSE_PAREN)
	    GCC_BAD ("missing %<(%> after %<#pragma GCC visibility push%> - ignored");
	}
    }
  if (pragma_lex (&x) != CPP_EOF)
    warning (OPT_Wpragmas, "junk at end of %<#pragma GCC visibility%>");
}

#endif

static void
handle_pragma_diagnostic(cpp_reader *ARG_UNUSED(dummy))
{
  const char *kind_string, *option_string;
  unsigned int option_index;
  enum cpp_ttype token;
  diagnostic_t kind;
  tree x;

  if (cfun)
    {
      warning (OPT_Wpragmas, "#pragma GCC diagnostic ignored inside functions");
      return;
    }

  token = pragma_lex (&x);
  if (token != CPP_NAME)
    GCC_BAD ("missing [error|warning|ignored] after %<#pragma GCC diagnostic%>");
  kind_string = IDENTIFIER_POINTER (x);
  if (strcmp (kind_string, "error") == 0)
    kind = DK_ERROR;
  else if (strcmp (kind_string, "warning") == 0)
    kind = DK_WARNING;
  else if (strcmp (kind_string, "ignored") == 0)
    kind = DK_IGNORED;
  else
    GCC_BAD ("expected [error|warning|ignored] after %<#pragma GCC diagnostic%>");

  token = pragma_lex (&x);
  if (token != CPP_STRING)
    GCC_BAD ("missing option after %<#pragma GCC diagnostic%> kind");
  option_string = TREE_STRING_POINTER (x);
  for (option_index = 0; option_index < cl_options_count; option_index++)
    if (strcmp (cl_options[option_index].opt_text, option_string) == 0)
      {
	/* This overrides -Werror, for example.  */
	diagnostic_classify_diagnostic (global_dc, option_index, kind);
	/* This makes sure the option is enabled, like -Wfoo would do.  */
	if (cl_options[option_index].var_type == CLVC_BOOLEAN
	    && cl_options[option_index].flag_var
	    && kind != DK_IGNORED)
	    *(int *) cl_options[option_index].flag_var = 1;
	return;
      }
  GCC_BAD ("unknown option after %<#pragma GCC diagnostic%> kind");
}

/* A vector of registered pragma callbacks.  */

DEF_VEC_O (pragma_handler);
DEF_VEC_ALLOC_O (pragma_handler, heap);

static VEC(pragma_handler, heap) *registered_pragmas;

/* Front-end wrappers for pragma registration to avoid dragging
   cpplib.h in almost everywhere.  */

static void
c_register_pragma_1 (const char *space, const char *name,
		     pragma_handler handler, bool allow_expansion)
{
  unsigned id;

  VEC_safe_push (pragma_handler, heap, registered_pragmas, &handler);
  id = VEC_length (pragma_handler, registered_pragmas);
  id += PRAGMA_FIRST_EXTERNAL - 1;

  /* The C++ front end allocates 6 bits in cp_token; the C front end
     allocates 7 bits in c_token.  At present this is sufficient.  */
  gcc_assert (id < 64);

  cpp_register_deferred_pragma (parse_in, space, name, id,
				allow_expansion, false);
}

void
c_register_pragma (const char *space, const char *name, pragma_handler handler)
{
  c_register_pragma_1 (space, name, handler, false);
}

void
c_register_pragma_with_expansion (const char *space, const char *name,
				  pragma_handler handler)
{
  c_register_pragma_1 (space, name, handler, true);
}

void
c_invoke_pragma_handler (unsigned int id)
{
  pragma_handler handler;

  id -= PRAGMA_FIRST_EXTERNAL;
  handler = *VEC_index (pragma_handler, registered_pragmas, id);

  handler (parse_in);
}

/* Set up front-end pragmas.  */
void
init_pragma (void)
{
  if (flag_openmp && !flag_preprocess_only)
    {
      struct omp_pragma_def { const char *name; unsigned int id; };
      static const struct omp_pragma_def omp_pragmas[] = {
	{ "atomic", PRAGMA_OMP_ATOMIC },
	{ "barrier", PRAGMA_OMP_BARRIER },
	{ "critical", PRAGMA_OMP_CRITICAL },
	{ "flush", PRAGMA_OMP_FLUSH },
	{ "for", PRAGMA_OMP_FOR },
	{ "master", PRAGMA_OMP_MASTER },
	{ "ordered", PRAGMA_OMP_ORDERED },
	{ "parallel", PRAGMA_OMP_PARALLEL },
	{ "section", PRAGMA_OMP_SECTION },
	{ "sections", PRAGMA_OMP_SECTIONS },
	{ "single", PRAGMA_OMP_SINGLE },
	{ "threadprivate", PRAGMA_OMP_THREADPRIVATE }
      };

      const int n_omp_pragmas = sizeof (omp_pragmas) / sizeof (*omp_pragmas);
      int i;

      for (i = 0; i < n_omp_pragmas; ++i)
	cpp_register_deferred_pragma (parse_in, "omp", omp_pragmas[i].name,
				      omp_pragmas[i].id, true, true);
    }

  cpp_register_deferred_pragma (parse_in, "GCC", "pch_preprocess",
				PRAGMA_GCC_PCH_PREPROCESS, false, false);

#ifdef HANDLE_PRAGMA_PACK
#ifdef HANDLE_PRAGMA_PACK_WITH_EXPANSION
  c_register_pragma_with_expansion (0, "pack", handle_pragma_pack);
#else
  c_register_pragma (0, "pack", handle_pragma_pack);
#endif
#endif
#ifdef HANDLE_PRAGMA_WEAK
  c_register_pragma (0, "weak", handle_pragma_weak);
#endif
#ifdef HANDLE_PRAGMA_VISIBILITY
  c_register_pragma ("GCC", "visibility", handle_pragma_visibility);
#endif

  c_register_pragma ("GCC", "diagnostic", handle_pragma_diagnostic);

  c_register_pragma_with_expansion (0, "redefine_extname", handle_pragma_redefine_extname);
  c_register_pragma (0, "extern_prefix", handle_pragma_extern_prefix);

#ifdef REGISTER_TARGET_PRAGMAS
  REGISTER_TARGET_PRAGMAS ();
#endif
}

#include "gt-c-pragma.h"
