/* Implements exception handling.
   Copyright (C) 1989, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Mike Stump <mrs@cygnus.com>.

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


/* An exception is an event that can be signaled from within a
   function. This event can then be "caught" or "trapped" by the
   callers of this function. This potentially allows program flow to
   be transferred to any arbitrary code associated with a function call
   several levels up the stack.

   The intended use for this mechanism is for signaling "exceptional
   events" in an out-of-band fashion, hence its name. The C++ language
   (and many other OO-styled or functional languages) practically
   requires such a mechanism, as otherwise it becomes very difficult
   or even impossible to signal failure conditions in complex
   situations.  The traditional C++ example is when an error occurs in
   the process of constructing an object; without such a mechanism, it
   is impossible to signal that the error occurs without adding global
   state variables and error checks around every object construction.

   The act of causing this event to occur is referred to as "throwing
   an exception". (Alternate terms include "raising an exception" or
   "signaling an exception".) The term "throw" is used because control
   is returned to the callers of the function that is signaling the
   exception, and thus there is the concept of "throwing" the
   exception up the call stack.

   [ Add updated documentation on how to use this.  ]  */


#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "tree.h"
#include "flags.h"
#include "function.h"
#include "expr.h"
#include "libfuncs.h"
#include "insn-config.h"
#include "except.h"
#include "integrate.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "output.h"
#include "dwarf2asm.h"
#include "dwarf2out.h"
#include "dwarf2.h"
#include "toplev.h"
#include "hashtab.h"
#include "intl.h"
#include "ggc.h"
#include "tm_p.h"
#include "target.h"
#include "langhooks.h"
#include "cgraph.h"
#include "diagnostic.h"
#include "tree-pass.h"
#include "timevar.h"

/* Provide defaults for stuff that may not be defined when using
   sjlj exceptions.  */
#ifndef EH_RETURN_DATA_REGNO
#define EH_RETURN_DATA_REGNO(N) INVALID_REGNUM
#endif


/* Protect cleanup actions with must-not-throw regions, with a call
   to the given failure handler.  */
tree (*lang_protect_cleanup_actions) (void);

/* Return true if type A catches type B.  */
int (*lang_eh_type_covers) (tree a, tree b);

/* Map a type to a runtime object to match type.  */
tree (*lang_eh_runtime_type) (tree);

/* A hash table of label to region number.  */

struct ehl_map_entry GTY(())
{
  rtx label;
  struct eh_region *region;
};

static GTY(()) int call_site_base;
static GTY ((param_is (union tree_node)))
  htab_t type_to_runtime_map;

/* Describe the SjLj_Function_Context structure.  */
static GTY(()) tree sjlj_fc_type_node;
static int sjlj_fc_call_site_ofs;
static int sjlj_fc_data_ofs;
static int sjlj_fc_personality_ofs;
static int sjlj_fc_lsda_ofs;
static int sjlj_fc_jbuf_ofs;

/* Describes one exception region.  */
struct eh_region GTY(())
{
  /* The immediately surrounding region.  */
  struct eh_region *outer;

  /* The list of immediately contained regions.  */
  struct eh_region *inner;
  struct eh_region *next_peer;

  /* An identifier for this region.  */
  int region_number;

  /* When a region is deleted, its parents inherit the REG_EH_REGION
     numbers already assigned.  */
  bitmap aka;

  /* Each region does exactly one thing.  */
  enum eh_region_type
  {
    ERT_UNKNOWN = 0,
    ERT_CLEANUP,
    ERT_TRY,
    ERT_CATCH,
    ERT_ALLOWED_EXCEPTIONS,
    ERT_MUST_NOT_THROW,
    ERT_THROW
  } type;

  /* Holds the action to perform based on the preceding type.  */
  union eh_region_u {
    /* A list of catch blocks, a surrounding try block,
       and the label for continuing after a catch.  */
    struct eh_region_u_try {
      struct eh_region *catch;
      struct eh_region *last_catch;
    } GTY ((tag ("ERT_TRY"))) try;

    /* The list through the catch handlers, the list of type objects
       matched, and the list of associated filters.  */
    struct eh_region_u_catch {
      struct eh_region *next_catch;
      struct eh_region *prev_catch;
      tree type_list;
      tree filter_list;
    } GTY ((tag ("ERT_CATCH"))) catch;

    /* A tree_list of allowed types.  */
    struct eh_region_u_allowed {
      tree type_list;
      int filter;
    } GTY ((tag ("ERT_ALLOWED_EXCEPTIONS"))) allowed;

    /* The type given by a call to "throw foo();", or discovered
       for a throw.  */
    struct eh_region_u_throw {
      tree type;
    } GTY ((tag ("ERT_THROW"))) throw;

    /* Retain the cleanup expression even after expansion so that
       we can match up fixup regions.  */
    struct eh_region_u_cleanup {
      struct eh_region *prev_try;
    } GTY ((tag ("ERT_CLEANUP"))) cleanup;
  } GTY ((desc ("%0.type"))) u;

  /* Entry point for this region's handler before landing pads are built.  */
  rtx label;
  tree tree_label;

  /* Entry point for this region's handler from the runtime eh library.  */
  rtx landing_pad;

  /* Entry point for this region's handler from an inner region.  */
  rtx post_landing_pad;

  /* The RESX insn for handing off control to the next outermost handler,
     if appropriate.  */
  rtx resume;

  /* True if something in this region may throw.  */
  unsigned may_contain_throw : 1;
};

typedef struct eh_region *eh_region;

struct call_site_record GTY(())
{
  rtx landing_pad;
  int action;
};

DEF_VEC_P(eh_region);
DEF_VEC_ALLOC_P(eh_region, gc);

/* Used to save exception status for each function.  */
struct eh_status GTY(())
{
  /* The tree of all regions for this function.  */
  struct eh_region *region_tree;

  /* The same information as an indexable array.  */
  VEC(eh_region,gc) *region_array;

  /* The most recently open region.  */
  struct eh_region *cur_region;

  /* This is the region for which we are processing catch blocks.  */
  struct eh_region *try_region;

  rtx filter;
  rtx exc_ptr;

  int built_landing_pads;
  int last_region_number;

  VEC(tree,gc) *ttype_data;
  varray_type ehspec_data;
  varray_type action_record_data;

  htab_t GTY ((param_is (struct ehl_map_entry))) exception_handler_label_map;

  struct call_site_record * GTY ((length ("%h.call_site_data_used")))
    call_site_data;
  int call_site_data_used;
  int call_site_data_size;

  rtx ehr_stackadj;
  rtx ehr_handler;
  rtx ehr_label;

  rtx sjlj_fc;
  rtx sjlj_exit_after;

  htab_t GTY((param_is (struct throw_stmt_node))) throw_stmt_table;
};

static int t2r_eq (const void *, const void *);
static hashval_t t2r_hash (const void *);
static void add_type_for_runtime (tree);
static tree lookup_type_for_runtime (tree);

static void remove_unreachable_regions (rtx);

static int ttypes_filter_eq (const void *, const void *);
static hashval_t ttypes_filter_hash (const void *);
static int ehspec_filter_eq (const void *, const void *);
static hashval_t ehspec_filter_hash (const void *);
static int add_ttypes_entry (htab_t, tree);
static int add_ehspec_entry (htab_t, htab_t, tree);
static void assign_filter_values (void);
static void build_post_landing_pads (void);
static void connect_post_landing_pads (void);
static void dw2_build_landing_pads (void);

struct sjlj_lp_info;
static bool sjlj_find_directly_reachable_regions (struct sjlj_lp_info *);
static void sjlj_assign_call_site_values (rtx, struct sjlj_lp_info *);
static void sjlj_mark_call_sites (struct sjlj_lp_info *);
static void sjlj_emit_function_enter (rtx);
static void sjlj_emit_function_exit (void);
static void sjlj_emit_dispatch_table (rtx, struct sjlj_lp_info *);
static void sjlj_build_landing_pads (void);

static hashval_t ehl_hash (const void *);
static int ehl_eq (const void *, const void *);
static void add_ehl_entry (rtx, struct eh_region *);
static void remove_exception_handler_label (rtx);
static void remove_eh_handler (struct eh_region *);
static int for_each_eh_label_1 (void **, void *);

/* The return value of reachable_next_level.  */
enum reachable_code
{
  /* The given exception is not processed by the given region.  */
  RNL_NOT_CAUGHT,
  /* The given exception may need processing by the given region.  */
  RNL_MAYBE_CAUGHT,
  /* The given exception is completely processed by the given region.  */
  RNL_CAUGHT,
  /* The given exception is completely processed by the runtime.  */
  RNL_BLOCKED
};

struct reachable_info;
static enum reachable_code reachable_next_level (struct eh_region *, tree,
						 struct reachable_info *);

static int action_record_eq (const void *, const void *);
static hashval_t action_record_hash (const void *);
static int add_action_record (htab_t, int, int);
static int collect_one_action_chain (htab_t, struct eh_region *);
static int add_call_site (rtx, int);

static void push_uleb128 (varray_type *, unsigned int);
static void push_sleb128 (varray_type *, int);
#ifndef HAVE_AS_LEB128
static int dw2_size_of_call_site_table (void);
static int sjlj_size_of_call_site_table (void);
#endif
static void dw2_output_call_site_table (void);
static void sjlj_output_call_site_table (void);


/* Routine to see if exception handling is turned on.
   DO_WARN is nonzero if we want to inform the user that exception
   handling is turned off.

   This is used to ensure that -fexceptions has been specified if the
   compiler tries to use any exception-specific functions.  */

int
doing_eh (int do_warn)
{
  if (! flag_exceptions)
    {
      static int warned = 0;
      if (! warned && do_warn)
	{
	  error ("exception handling disabled, use -fexceptions to enable");
	  warned = 1;
	}
      return 0;
    }
  return 1;
}


void
init_eh (void)
{
  if (! flag_exceptions)
    return;

  type_to_runtime_map = htab_create_ggc (31, t2r_hash, t2r_eq, NULL);

  /* Create the SjLj_Function_Context structure.  This should match
     the definition in unwind-sjlj.c.  */
  if (USING_SJLJ_EXCEPTIONS)
    {
      tree f_jbuf, f_per, f_lsda, f_prev, f_cs, f_data, tmp;

      sjlj_fc_type_node = lang_hooks.types.make_type (RECORD_TYPE);

      f_prev = build_decl (FIELD_DECL, get_identifier ("__prev"),
			   build_pointer_type (sjlj_fc_type_node));
      DECL_FIELD_CONTEXT (f_prev) = sjlj_fc_type_node;

      f_cs = build_decl (FIELD_DECL, get_identifier ("__call_site"),
			 integer_type_node);
      DECL_FIELD_CONTEXT (f_cs) = sjlj_fc_type_node;

      tmp = build_index_type (build_int_cst (NULL_TREE, 4 - 1));
      tmp = build_array_type (lang_hooks.types.type_for_mode (word_mode, 1),
			      tmp);
      f_data = build_decl (FIELD_DECL, get_identifier ("__data"), tmp);
      DECL_FIELD_CONTEXT (f_data) = sjlj_fc_type_node;

      f_per = build_decl (FIELD_DECL, get_identifier ("__personality"),
			  ptr_type_node);
      DECL_FIELD_CONTEXT (f_per) = sjlj_fc_type_node;

      f_lsda = build_decl (FIELD_DECL, get_identifier ("__lsda"),
			   ptr_type_node);
      DECL_FIELD_CONTEXT (f_lsda) = sjlj_fc_type_node;

#ifdef DONT_USE_BUILTIN_SETJMP
#ifdef JMP_BUF_SIZE
      tmp = build_int_cst (NULL_TREE, JMP_BUF_SIZE - 1);
#else
      /* Should be large enough for most systems, if it is not,
	 JMP_BUF_SIZE should be defined with the proper value.  It will
	 also tend to be larger than necessary for most systems, a more
	 optimal port will define JMP_BUF_SIZE.  */
      tmp = build_int_cst (NULL_TREE, FIRST_PSEUDO_REGISTER + 2 - 1);
#endif
#else
      /* builtin_setjmp takes a pointer to 5 words.  */
      tmp = build_int_cst (NULL_TREE, 5 * BITS_PER_WORD / POINTER_SIZE - 1);
#endif
      tmp = build_index_type (tmp);
      tmp = build_array_type (ptr_type_node, tmp);
      f_jbuf = build_decl (FIELD_DECL, get_identifier ("__jbuf"), tmp);
#ifdef DONT_USE_BUILTIN_SETJMP
      /* We don't know what the alignment requirements of the
	 runtime's jmp_buf has.  Overestimate.  */
      DECL_ALIGN (f_jbuf) = BIGGEST_ALIGNMENT;
      DECL_USER_ALIGN (f_jbuf) = 1;
#endif
      DECL_FIELD_CONTEXT (f_jbuf) = sjlj_fc_type_node;

      TYPE_FIELDS (sjlj_fc_type_node) = f_prev;
      TREE_CHAIN (f_prev) = f_cs;
      TREE_CHAIN (f_cs) = f_data;
      TREE_CHAIN (f_data) = f_per;
      TREE_CHAIN (f_per) = f_lsda;
      TREE_CHAIN (f_lsda) = f_jbuf;

      layout_type (sjlj_fc_type_node);

      /* Cache the interesting field offsets so that we have
	 easy access from rtl.  */
      sjlj_fc_call_site_ofs
	= (tree_low_cst (DECL_FIELD_OFFSET (f_cs), 1)
	   + tree_low_cst (DECL_FIELD_BIT_OFFSET (f_cs), 1) / BITS_PER_UNIT);
      sjlj_fc_data_ofs
	= (tree_low_cst (DECL_FIELD_OFFSET (f_data), 1)
	   + tree_low_cst (DECL_FIELD_BIT_OFFSET (f_data), 1) / BITS_PER_UNIT);
      sjlj_fc_personality_ofs
	= (tree_low_cst (DECL_FIELD_OFFSET (f_per), 1)
	   + tree_low_cst (DECL_FIELD_BIT_OFFSET (f_per), 1) / BITS_PER_UNIT);
      sjlj_fc_lsda_ofs
	= (tree_low_cst (DECL_FIELD_OFFSET (f_lsda), 1)
	   + tree_low_cst (DECL_FIELD_BIT_OFFSET (f_lsda), 1) / BITS_PER_UNIT);
      sjlj_fc_jbuf_ofs
	= (tree_low_cst (DECL_FIELD_OFFSET (f_jbuf), 1)
	   + tree_low_cst (DECL_FIELD_BIT_OFFSET (f_jbuf), 1) / BITS_PER_UNIT);
    }
}

void
init_eh_for_function (void)
{
  cfun->eh = ggc_alloc_cleared (sizeof (struct eh_status));
}

/* Routines to generate the exception tree somewhat directly.
   These are used from tree-eh.c when processing exception related
   nodes during tree optimization.  */

static struct eh_region *
gen_eh_region (enum eh_region_type type, struct eh_region *outer)
{
  struct eh_region *new;

#ifdef ENABLE_CHECKING
  gcc_assert (doing_eh (0));
#endif

  /* Insert a new blank region as a leaf in the tree.  */
  new = ggc_alloc_cleared (sizeof (*new));
  new->type = type;
  new->outer = outer;
  if (outer)
    {
      new->next_peer = outer->inner;
      outer->inner = new;
    }
  else
    {
      new->next_peer = cfun->eh->region_tree;
      cfun->eh->region_tree = new;
    }

  new->region_number = ++cfun->eh->last_region_number;

  return new;
}

struct eh_region *
gen_eh_region_cleanup (struct eh_region *outer, struct eh_region *prev_try)
{
  struct eh_region *cleanup = gen_eh_region (ERT_CLEANUP, outer);
  cleanup->u.cleanup.prev_try = prev_try;
  return cleanup;
}

struct eh_region *
gen_eh_region_try (struct eh_region *outer)
{
  return gen_eh_region (ERT_TRY, outer);
}

struct eh_region *
gen_eh_region_catch (struct eh_region *t, tree type_or_list)
{
  struct eh_region *c, *l;
  tree type_list, type_node;

  /* Ensure to always end up with a type list to normalize further
     processing, then register each type against the runtime types map.  */
  type_list = type_or_list;
  if (type_or_list)
    {
      if (TREE_CODE (type_or_list) != TREE_LIST)
	type_list = tree_cons (NULL_TREE, type_or_list, NULL_TREE);

      type_node = type_list;
      for (; type_node; type_node = TREE_CHAIN (type_node))
	add_type_for_runtime (TREE_VALUE (type_node));
    }

  c = gen_eh_region (ERT_CATCH, t->outer);
  c->u.catch.type_list = type_list;
  l = t->u.try.last_catch;
  c->u.catch.prev_catch = l;
  if (l)
    l->u.catch.next_catch = c;
  else
    t->u.try.catch = c;
  t->u.try.last_catch = c;

  return c;
}

struct eh_region *
gen_eh_region_allowed (struct eh_region *outer, tree allowed)
{
  struct eh_region *region = gen_eh_region (ERT_ALLOWED_EXCEPTIONS, outer);
  region->u.allowed.type_list = allowed;

  for (; allowed ; allowed = TREE_CHAIN (allowed))
    add_type_for_runtime (TREE_VALUE (allowed));

  return region;
}

struct eh_region *
gen_eh_region_must_not_throw (struct eh_region *outer)
{
  return gen_eh_region (ERT_MUST_NOT_THROW, outer);
}

int
get_eh_region_number (struct eh_region *region)
{
  return region->region_number;
}

bool
get_eh_region_may_contain_throw (struct eh_region *region)
{
  return region->may_contain_throw;
}

tree
get_eh_region_tree_label (struct eh_region *region)
{
  return region->tree_label;
}

void
set_eh_region_tree_label (struct eh_region *region, tree lab)
{
  region->tree_label = lab;
}

void
expand_resx_expr (tree exp)
{
  int region_nr = TREE_INT_CST_LOW (TREE_OPERAND (exp, 0));
  struct eh_region *reg = VEC_index (eh_region,
				     cfun->eh->region_array, region_nr);

  gcc_assert (!reg->resume);
  reg->resume = emit_jump_insn (gen_rtx_RESX (VOIDmode, region_nr));
  emit_barrier ();
}

/* Note that the current EH region (if any) may contain a throw, or a
   call to a function which itself may contain a throw.  */

void
note_eh_region_may_contain_throw (struct eh_region *region)
{
  while (region && !region->may_contain_throw)
    {
      region->may_contain_throw = 1;
      region = region->outer;
    }
}

void
note_current_region_may_contain_throw (void)
{
  note_eh_region_may_contain_throw (cfun->eh->cur_region);
}


/* Return an rtl expression for a pointer to the exception object
   within a handler.  */

rtx
get_exception_pointer (struct function *fun)
{
  rtx exc_ptr = fun->eh->exc_ptr;
  if (fun == cfun && ! exc_ptr)
    {
      exc_ptr = gen_reg_rtx (ptr_mode);
      fun->eh->exc_ptr = exc_ptr;
    }
  return exc_ptr;
}

/* Return an rtl expression for the exception dispatch filter
   within a handler.  */

rtx
get_exception_filter (struct function *fun)
{
  rtx filter = fun->eh->filter;
  if (fun == cfun && ! filter)
    {
      filter = gen_reg_rtx (targetm.eh_return_filter_mode ());
      fun->eh->filter = filter;
    }
  return filter;
}

/* This section is for the exception handling specific optimization pass.  */

/* Random access the exception region tree.  */

void
collect_eh_region_array (void)
{
  struct eh_region *i;

  i = cfun->eh->region_tree;
  if (! i)
    return;

  VEC_safe_grow (eh_region, gc, cfun->eh->region_array,
		 cfun->eh->last_region_number + 1);
  VEC_replace (eh_region, cfun->eh->region_array, 0, 0);

  while (1)
    {
      VEC_replace (eh_region, cfun->eh->region_array, i->region_number, i);

      /* If there are sub-regions, process them.  */
      if (i->inner)
	i = i->inner;
      /* If there are peers, process them.  */
      else if (i->next_peer)
	i = i->next_peer;
      /* Otherwise, step back up the tree to the next peer.  */
      else
	{
	  do {
	    i = i->outer;
	    if (i == NULL)
	      return;
	  } while (i->next_peer == NULL);
	  i = i->next_peer;
	}
    }
}

/* Remove all regions whose labels are not reachable from insns.  */

static void
remove_unreachable_regions (rtx insns)
{
  int i, *uid_region_num;
  bool *reachable;
  struct eh_region *r;
  rtx insn;

  uid_region_num = xcalloc (get_max_uid (), sizeof(int));
  reachable = xcalloc (cfun->eh->last_region_number + 1, sizeof(bool));

  for (i = cfun->eh->last_region_number; i > 0; --i)
    {
      r = VEC_index (eh_region, cfun->eh->region_array, i);
      if (!r || r->region_number != i)
	continue;

      if (r->resume)
	{
	  gcc_assert (!uid_region_num[INSN_UID (r->resume)]);
	  uid_region_num[INSN_UID (r->resume)] = i;
	}
      if (r->label)
	{
	  gcc_assert (!uid_region_num[INSN_UID (r->label)]);
	  uid_region_num[INSN_UID (r->label)] = i;
	}
    }

  for (insn = insns; insn; insn = NEXT_INSN (insn))
    reachable[uid_region_num[INSN_UID (insn)]] = true;

  for (i = cfun->eh->last_region_number; i > 0; --i)
    {
      r = VEC_index (eh_region, cfun->eh->region_array, i);
      if (r && r->region_number == i && !reachable[i])
	{
	  bool kill_it = true;
	  switch (r->type)
	    {
	    case ERT_THROW:
	      /* Don't remove ERT_THROW regions if their outer region
		 is reachable.  */
	      if (r->outer && reachable[r->outer->region_number])
		kill_it = false;
	      break;

	    case ERT_MUST_NOT_THROW:
	      /* MUST_NOT_THROW regions are implementable solely in the
		 runtime, but their existence continues to affect calls
		 within that region.  Never delete them here.  */
	      kill_it = false;
	      break;

	    case ERT_TRY:
	      {
		/* TRY regions are reachable if any of its CATCH regions
		   are reachable.  */
		struct eh_region *c;
		for (c = r->u.try.catch; c ; c = c->u.catch.next_catch)
		  if (reachable[c->region_number])
		    {
		      kill_it = false;
		      break;
		    }
		break;
	      }

	    default:
	      break;
	    }

	  if (kill_it)
	    remove_eh_handler (r);
	}
    }

  free (reachable);
  free (uid_region_num);
}

/* Set up EH labels for RTL.  */

void
convert_from_eh_region_ranges (void)
{
  rtx insns = get_insns ();
  int i, n = cfun->eh->last_region_number;

  /* Most of the work is already done at the tree level.  All we need to
     do is collect the rtl labels that correspond to the tree labels that
     collect the rtl labels that correspond to the tree labels
     we allocated earlier.  */
  for (i = 1; i <= n; ++i)
    {
      struct eh_region *region;

      region = VEC_index (eh_region, cfun->eh->region_array, i);
      if (region && region->tree_label)
	region->label = DECL_RTL_IF_SET (region->tree_label);
    }

  remove_unreachable_regions (insns);
}

static void
add_ehl_entry (rtx label, struct eh_region *region)
{
  struct ehl_map_entry **slot, *entry;

  LABEL_PRESERVE_P (label) = 1;

  entry = ggc_alloc (sizeof (*entry));
  entry->label = label;
  entry->region = region;

  slot = (struct ehl_map_entry **)
    htab_find_slot (cfun->eh->exception_handler_label_map, entry, INSERT);

  /* Before landing pad creation, each exception handler has its own
     label.  After landing pad creation, the exception handlers may
     share landing pads.  This is ok, since maybe_remove_eh_handler
     only requires the 1-1 mapping before landing pad creation.  */
  gcc_assert (!*slot || cfun->eh->built_landing_pads);

  *slot = entry;
}

void
find_exception_handler_labels (void)
{
  int i;

  if (cfun->eh->exception_handler_label_map)
    htab_empty (cfun->eh->exception_handler_label_map);
  else
    {
      /* ??? The expansion factor here (3/2) must be greater than the htab
	 occupancy factor (4/3) to avoid unnecessary resizing.  */
      cfun->eh->exception_handler_label_map
        = htab_create_ggc (cfun->eh->last_region_number * 3 / 2,
			   ehl_hash, ehl_eq, NULL);
    }

  if (cfun->eh->region_tree == NULL)
    return;

  for (i = cfun->eh->last_region_number; i > 0; --i)
    {
      struct eh_region *region;
      rtx lab;

      region = VEC_index (eh_region, cfun->eh->region_array, i);
      if (! region || region->region_number != i)
	continue;
      if (cfun->eh->built_landing_pads)
	lab = region->landing_pad;
      else
	lab = region->label;

      if (lab)
	add_ehl_entry (lab, region);
    }

  /* For sjlj exceptions, need the return label to remain live until
     after landing pad generation.  */
  if (USING_SJLJ_EXCEPTIONS && ! cfun->eh->built_landing_pads)
    add_ehl_entry (return_label, NULL);
}

/* Returns true if the current function has exception handling regions.  */

bool
current_function_has_exception_handlers (void)
{
  int i;

  for (i = cfun->eh->last_region_number; i > 0; --i)
    {
      struct eh_region *region;

      region = VEC_index (eh_region, cfun->eh->region_array, i);
      if (region
	  && region->region_number == i
	  && region->type != ERT_THROW)
	return true;
    }

  return false;
}

/* A subroutine of duplicate_eh_regions.  Search the region tree under O
   for the minimum and maximum region numbers.  Update *MIN and *MAX.  */

static void
duplicate_eh_regions_0 (eh_region o, int *min, int *max)
{
  if (o->region_number < *min)
    *min = o->region_number;
  if (o->region_number > *max)
    *max = o->region_number;

  if (o->inner)
    {
      o = o->inner;
      duplicate_eh_regions_0 (o, min, max);
      while (o->next_peer)
	{
	  o = o->next_peer;
	  duplicate_eh_regions_0 (o, min, max);
	}
    }
}

/* A subroutine of duplicate_eh_regions.  Copy the region tree under OLD.
   Root it at OUTER, and apply EH_OFFSET to the region number.  Don't worry
   about the other internal pointers just yet, just the tree-like pointers.  */

static eh_region
duplicate_eh_regions_1 (eh_region old, eh_region outer, int eh_offset)
{
  eh_region ret, n;

  ret = n = ggc_alloc (sizeof (struct eh_region));

  *n = *old;
  n->outer = outer;
  n->next_peer = NULL;
  gcc_assert (!old->aka);

  n->region_number += eh_offset;
  VEC_replace (eh_region, cfun->eh->region_array, n->region_number, n);

  if (old->inner)
    {
      old = old->inner;
      n = n->inner = duplicate_eh_regions_1 (old, ret, eh_offset);
      while (old->next_peer)
	{
	  old = old->next_peer;
	  n = n->next_peer = duplicate_eh_regions_1 (old, ret, eh_offset);
	}
    }

  return ret;
}

/* Duplicate the EH regions of IFUN, rooted at COPY_REGION, into current
   function and root the tree below OUTER_REGION.  Remap labels using MAP
   callback.  The special case of COPY_REGION of 0 means all regions.  */

int
duplicate_eh_regions (struct function *ifun, duplicate_eh_regions_map map,
		      void *data, int copy_region, int outer_region)
{
  eh_region cur, prev_try, outer, *splice;
  int i, min_region, max_region, eh_offset, cfun_last_region_number;
  int num_regions;

  if (!ifun->eh->region_tree)
    return 0;

  /* Find the range of region numbers to be copied.  The interface we 
     provide here mandates a single offset to find new number from old,
     which means we must look at the numbers present, instead of the
     count or something else.  */
  if (copy_region > 0)
    {
      min_region = INT_MAX;
      max_region = 0;

      cur = VEC_index (eh_region, ifun->eh->region_array, copy_region);
      duplicate_eh_regions_0 (cur, &min_region, &max_region);
    }
  else
    min_region = 1, max_region = ifun->eh->last_region_number;
  num_regions = max_region - min_region + 1;
  cfun_last_region_number = cfun->eh->last_region_number;
  eh_offset = cfun_last_region_number + 1 - min_region;

  /* If we've not yet created a region array, do so now.  */
  VEC_safe_grow (eh_region, gc, cfun->eh->region_array,
		 cfun_last_region_number + 1 + num_regions);
  cfun->eh->last_region_number = max_region + eh_offset;

  /* We may have just allocated the array for the first time.
     Make sure that element zero is null.  */
  VEC_replace (eh_region, cfun->eh->region_array, 0, 0);

  /* Zero all entries in the range allocated.  */
  memset (VEC_address (eh_region, cfun->eh->region_array)
	  + cfun_last_region_number + 1, 0, num_regions * sizeof (eh_region));

  /* Locate the spot at which to insert the new tree.  */
  if (outer_region > 0)
    {
      outer = VEC_index (eh_region, cfun->eh->region_array, outer_region);
      splice = &outer->inner;
    }
  else
    {
      outer = NULL;
      splice = &cfun->eh->region_tree;
    }
  while (*splice)
    splice = &(*splice)->next_peer;

  /* Copy all the regions in the subtree.  */
  if (copy_region > 0)
    {
      cur = VEC_index (eh_region, ifun->eh->region_array, copy_region);
      *splice = duplicate_eh_regions_1 (cur, outer, eh_offset);
    }
  else
    {
      eh_region n;

      cur = ifun->eh->region_tree;
      *splice = n = duplicate_eh_regions_1 (cur, outer, eh_offset);
      while (cur->next_peer)
	{
	  cur = cur->next_peer;
	  n = n->next_peer = duplicate_eh_regions_1 (cur, outer, eh_offset);
	}
    }

  /* Remap all the labels in the new regions.  */
  for (i = cfun_last_region_number + 1;
       VEC_iterate (eh_region, cfun->eh->region_array, i, cur); ++i)
    if (cur && cur->tree_label)
      cur->tree_label = map (cur->tree_label, data);

  /* Search for the containing ERT_TRY region to fix up
     the prev_try short-cuts for ERT_CLEANUP regions.  */
  prev_try = NULL;
  if (outer_region > 0)
    for (prev_try = VEC_index (eh_region, cfun->eh->region_array, outer_region);
         prev_try && prev_try->type != ERT_TRY;
	 prev_try = prev_try->outer)
      if (prev_try->type == ERT_MUST_NOT_THROW)
	{
	  prev_try = NULL;
	  break;
	}

  /* Remap all of the internal catch and cleanup linkages.  Since we 
     duplicate entire subtrees, all of the referenced regions will have
     been copied too.  And since we renumbered them as a block, a simple
     bit of arithmetic finds us the index for the replacement region.  */
  for (i = cfun_last_region_number + 1;
       VEC_iterate (eh_region, cfun->eh->region_array, i, cur); ++i)
    {
      if (cur == NULL)
	continue;

#define REMAP(REG) \
	(REG) = VEC_index (eh_region, cfun->eh->region_array, \
			   (REG)->region_number + eh_offset)

      switch (cur->type)
	{
	case ERT_TRY:
	  if (cur->u.try.catch)
	    REMAP (cur->u.try.catch);
	  if (cur->u.try.last_catch)
	    REMAP (cur->u.try.last_catch);
	  break;

	case ERT_CATCH:
	  if (cur->u.catch.next_catch)
	    REMAP (cur->u.catch.next_catch);
	  if (cur->u.catch.prev_catch)
	    REMAP (cur->u.catch.prev_catch);
	  break;

	case ERT_CLEANUP:
	  if (cur->u.cleanup.prev_try)
	    REMAP (cur->u.cleanup.prev_try);
	  else
	    cur->u.cleanup.prev_try = prev_try;
	  break;

	default:
	  break;
	}

#undef REMAP
    }

  return eh_offset;
}

/* Return true if REGION_A is outer to REGION_B in IFUN.  */

bool
eh_region_outer_p (struct function *ifun, int region_a, int region_b)
{
  struct eh_region *rp_a, *rp_b;

  gcc_assert (ifun->eh->last_region_number > 0);
  gcc_assert (ifun->eh->region_tree);

  rp_a = VEC_index (eh_region, ifun->eh->region_array, region_a);
  rp_b = VEC_index (eh_region, ifun->eh->region_array, region_b);
  gcc_assert (rp_a != NULL);
  gcc_assert (rp_b != NULL);

  do
    {
      if (rp_a == rp_b)
	return true;
      rp_b = rp_b->outer;
    }
  while (rp_b);

  return false;
}

/* Return region number of region that is outer to both if REGION_A and
   REGION_B in IFUN.  */

int
eh_region_outermost (struct function *ifun, int region_a, int region_b)
{
  struct eh_region *rp_a, *rp_b;
  sbitmap b_outer;

  gcc_assert (ifun->eh->last_region_number > 0);
  gcc_assert (ifun->eh->region_tree);

  rp_a = VEC_index (eh_region, ifun->eh->region_array, region_a);
  rp_b = VEC_index (eh_region, ifun->eh->region_array, region_b);
  gcc_assert (rp_a != NULL);
  gcc_assert (rp_b != NULL);

  b_outer = sbitmap_alloc (ifun->eh->last_region_number + 1);
  sbitmap_zero (b_outer);

  do
    {
      SET_BIT (b_outer, rp_b->region_number);
      rp_b = rp_b->outer;
    }
  while (rp_b);

  do
    {
      if (TEST_BIT (b_outer, rp_a->region_number))
	{
	  sbitmap_free (b_outer);
	  return rp_a->region_number;
	}
      rp_a = rp_a->outer;
    }
  while (rp_a);

  sbitmap_free (b_outer);
  return -1;
}

static int
t2r_eq (const void *pentry, const void *pdata)
{
  tree entry = (tree) pentry;
  tree data = (tree) pdata;

  return TREE_PURPOSE (entry) == data;
}

static hashval_t
t2r_hash (const void *pentry)
{
  tree entry = (tree) pentry;
  return TREE_HASH (TREE_PURPOSE (entry));
}

static void
add_type_for_runtime (tree type)
{
  tree *slot;

  slot = (tree *) htab_find_slot_with_hash (type_to_runtime_map, type,
					    TREE_HASH (type), INSERT);
  if (*slot == NULL)
    {
      tree runtime = (*lang_eh_runtime_type) (type);
      *slot = tree_cons (type, runtime, NULL_TREE);
    }
}

static tree
lookup_type_for_runtime (tree type)
{
  tree *slot;

  slot = (tree *) htab_find_slot_with_hash (type_to_runtime_map, type,
					    TREE_HASH (type), NO_INSERT);

  /* We should have always inserted the data earlier.  */
  return TREE_VALUE (*slot);
}


/* Represent an entry in @TTypes for either catch actions
   or exception filter actions.  */
struct ttypes_filter GTY(())
{
  tree t;
  int filter;
};

/* Compare ENTRY (a ttypes_filter entry in the hash table) with DATA
   (a tree) for a @TTypes type node we are thinking about adding.  */

static int
ttypes_filter_eq (const void *pentry, const void *pdata)
{
  const struct ttypes_filter *entry = (const struct ttypes_filter *) pentry;
  tree data = (tree) pdata;

  return entry->t == data;
}

static hashval_t
ttypes_filter_hash (const void *pentry)
{
  const struct ttypes_filter *entry = (const struct ttypes_filter *) pentry;
  return TREE_HASH (entry->t);
}

/* Compare ENTRY with DATA (both struct ttypes_filter) for a @TTypes
   exception specification list we are thinking about adding.  */
/* ??? Currently we use the type lists in the order given.  Someone
   should put these in some canonical order.  */

static int
ehspec_filter_eq (const void *pentry, const void *pdata)
{
  const struct ttypes_filter *entry = (const struct ttypes_filter *) pentry;
  const struct ttypes_filter *data = (const struct ttypes_filter *) pdata;

  return type_list_equal (entry->t, data->t);
}

/* Hash function for exception specification lists.  */

static hashval_t
ehspec_filter_hash (const void *pentry)
{
  const struct ttypes_filter *entry = (const struct ttypes_filter *) pentry;
  hashval_t h = 0;
  tree list;

  for (list = entry->t; list ; list = TREE_CHAIN (list))
    h = (h << 5) + (h >> 27) + TREE_HASH (TREE_VALUE (list));
  return h;
}

/* Add TYPE (which may be NULL) to cfun->eh->ttype_data, using TYPES_HASH
   to speed up the search.  Return the filter value to be used.  */

static int
add_ttypes_entry (htab_t ttypes_hash, tree type)
{
  struct ttypes_filter **slot, *n;

  slot = (struct ttypes_filter **)
    htab_find_slot_with_hash (ttypes_hash, type, TREE_HASH (type), INSERT);

  if ((n = *slot) == NULL)
    {
      /* Filter value is a 1 based table index.  */

      n = XNEW (struct ttypes_filter);
      n->t = type;
      n->filter = VEC_length (tree, cfun->eh->ttype_data) + 1;
      *slot = n;

      VEC_safe_push (tree, gc, cfun->eh->ttype_data, type);
    }

  return n->filter;
}

/* Add LIST to cfun->eh->ehspec_data, using EHSPEC_HASH and TYPES_HASH
   to speed up the search.  Return the filter value to be used.  */

static int
add_ehspec_entry (htab_t ehspec_hash, htab_t ttypes_hash, tree list)
{
  struct ttypes_filter **slot, *n;
  struct ttypes_filter dummy;

  dummy.t = list;
  slot = (struct ttypes_filter **)
    htab_find_slot (ehspec_hash, &dummy, INSERT);

  if ((n = *slot) == NULL)
    {
      /* Filter value is a -1 based byte index into a uleb128 buffer.  */

      n = XNEW (struct ttypes_filter);
      n->t = list;
      n->filter = -(VARRAY_ACTIVE_SIZE (cfun->eh->ehspec_data) + 1);
      *slot = n;

      /* Generate a 0 terminated list of filter values.  */
      for (; list ; list = TREE_CHAIN (list))
	{
	  if (targetm.arm_eabi_unwinder)
	    VARRAY_PUSH_TREE (cfun->eh->ehspec_data, TREE_VALUE (list));
	  else
	    {
	      /* Look up each type in the list and encode its filter
		 value as a uleb128.  */
	      push_uleb128 (&cfun->eh->ehspec_data,
		  add_ttypes_entry (ttypes_hash, TREE_VALUE (list)));
	    }
	}
      if (targetm.arm_eabi_unwinder)
	VARRAY_PUSH_TREE (cfun->eh->ehspec_data, NULL_TREE);
      else
	VARRAY_PUSH_UCHAR (cfun->eh->ehspec_data, 0);
    }

  return n->filter;
}

/* Generate the action filter values to be used for CATCH and
   ALLOWED_EXCEPTIONS regions.  When using dwarf2 exception regions,
   we use lots of landing pads, and so every type or list can share
   the same filter value, which saves table space.  */

static void
assign_filter_values (void)
{
  int i;
  htab_t ttypes, ehspec;

  cfun->eh->ttype_data = VEC_alloc (tree, gc, 16);
  if (targetm.arm_eabi_unwinder)
    VARRAY_TREE_INIT (cfun->eh->ehspec_data, 64, "ehspec_data");
  else
    VARRAY_UCHAR_INIT (cfun->eh->ehspec_data, 64, "ehspec_data");

  ttypes = htab_create (31, ttypes_filter_hash, ttypes_filter_eq, free);
  ehspec = htab_create (31, ehspec_filter_hash, ehspec_filter_eq, free);

  for (i = cfun->eh->last_region_number; i > 0; --i)
    {
      struct eh_region *r;

      r = VEC_index (eh_region, cfun->eh->region_array, i);

      /* Mind we don't process a region more than once.  */
      if (!r || r->region_number != i)
	continue;

      switch (r->type)
	{
	case ERT_CATCH:
	  /* Whatever type_list is (NULL or true list), we build a list
	     of filters for the region.  */
	  r->u.catch.filter_list = NULL_TREE;

	  if (r->u.catch.type_list != NULL)
	    {
	      /* Get a filter value for each of the types caught and store
		 them in the region's dedicated list.  */
	      tree tp_node = r->u.catch.type_list;

	      for (;tp_node; tp_node = TREE_CHAIN (tp_node))
		{
		  int flt = add_ttypes_entry (ttypes, TREE_VALUE (tp_node));
		  tree flt_node = build_int_cst (NULL_TREE, flt);

		  r->u.catch.filter_list
		    = tree_cons (NULL_TREE, flt_node, r->u.catch.filter_list);
		}
	    }
	  else
	    {
	      /* Get a filter value for the NULL list also since it will need
		 an action record anyway.  */
	      int flt = add_ttypes_entry (ttypes, NULL);
	      tree flt_node = build_int_cst (NULL_TREE, flt);

	      r->u.catch.filter_list
		= tree_cons (NULL_TREE, flt_node, r->u.catch.filter_list);
	    }

	  break;

	case ERT_ALLOWED_EXCEPTIONS:
	  r->u.allowed.filter
	    = add_ehspec_entry (ehspec, ttypes, r->u.allowed.type_list);
	  break;

	default:
	  break;
	}
    }

  htab_delete (ttypes);
  htab_delete (ehspec);
}

/* Emit SEQ into basic block just before INSN (that is assumed to be
   first instruction of some existing BB and return the newly
   produced block.  */
static basic_block
emit_to_new_bb_before (rtx seq, rtx insn)
{
  rtx last;
  basic_block bb;
  edge e;
  edge_iterator ei;

  /* If there happens to be a fallthru edge (possibly created by cleanup_cfg
     call), we don't want it to go into newly created landing pad or other EH
     construct.  */
  for (ei = ei_start (BLOCK_FOR_INSN (insn)->preds); (e = ei_safe_edge (ei)); )
    if (e->flags & EDGE_FALLTHRU)
      force_nonfallthru (e);
    else
      ei_next (&ei);
  last = emit_insn_before (seq, insn);
  if (BARRIER_P (last))
    last = PREV_INSN (last);
  bb = create_basic_block (seq, last, BLOCK_FOR_INSN (insn)->prev_bb);
  update_bb_for_insn (bb);
  bb->flags |= BB_SUPERBLOCK;
  return bb;
}

/* Generate the code to actually handle exceptions, which will follow the
   landing pads.  */

static void
build_post_landing_pads (void)
{
  int i;

  for (i = cfun->eh->last_region_number; i > 0; --i)
    {
      struct eh_region *region;
      rtx seq;

      region = VEC_index (eh_region, cfun->eh->region_array, i);
      /* Mind we don't process a region more than once.  */
      if (!region || region->region_number != i)
	continue;

      switch (region->type)
	{
	case ERT_TRY:
	  /* ??? Collect the set of all non-overlapping catch handlers
	       all the way up the chain until blocked by a cleanup.  */
	  /* ??? Outer try regions can share landing pads with inner
	     try regions if the types are completely non-overlapping,
	     and there are no intervening cleanups.  */

	  region->post_landing_pad = gen_label_rtx ();

	  start_sequence ();

	  emit_label (region->post_landing_pad);

	  /* ??? It is mighty inconvenient to call back into the
	     switch statement generation code in expand_end_case.
	     Rapid prototyping sez a sequence of ifs.  */
	  {
	    struct eh_region *c;
	    for (c = region->u.try.catch; c ; c = c->u.catch.next_catch)
	      {
		if (c->u.catch.type_list == NULL)
		  emit_jump (c->label);
		else
		  {
		    /* Need for one cmp/jump per type caught. Each type
		       list entry has a matching entry in the filter list
		       (see assign_filter_values).  */
		    tree tp_node = c->u.catch.type_list;
		    tree flt_node = c->u.catch.filter_list;

		    for (; tp_node; )
		      {
			emit_cmp_and_jump_insns
			  (cfun->eh->filter,
			   GEN_INT (tree_low_cst (TREE_VALUE (flt_node), 0)),
			   EQ, NULL_RTX,
			   targetm.eh_return_filter_mode (), 0, c->label);

			tp_node = TREE_CHAIN (tp_node);
			flt_node = TREE_CHAIN (flt_node);
		      }
		  }
	      }
	  }

	  /* We delay the generation of the _Unwind_Resume until we generate
	     landing pads.  We emit a marker here so as to get good control
	     flow data in the meantime.  */
	  region->resume
	    = emit_jump_insn (gen_rtx_RESX (VOIDmode, region->region_number));
	  emit_barrier ();

	  seq = get_insns ();
	  end_sequence ();

	  emit_to_new_bb_before (seq, region->u.try.catch->label);

	  break;

	case ERT_ALLOWED_EXCEPTIONS:
	  region->post_landing_pad = gen_label_rtx ();

	  start_sequence ();

	  emit_label (region->post_landing_pad);

	  emit_cmp_and_jump_insns (cfun->eh->filter,
				   GEN_INT (region->u.allowed.filter),
				   EQ, NULL_RTX,
				   targetm.eh_return_filter_mode (), 0, region->label);

	  /* We delay the generation of the _Unwind_Resume until we generate
	     landing pads.  We emit a marker here so as to get good control
	     flow data in the meantime.  */
	  region->resume
	    = emit_jump_insn (gen_rtx_RESX (VOIDmode, region->region_number));
	  emit_barrier ();

	  seq = get_insns ();
	  end_sequence ();

	  emit_to_new_bb_before (seq, region->label);
	  break;

	case ERT_CLEANUP:
	case ERT_MUST_NOT_THROW:
	  region->post_landing_pad = region->label;
	  break;

	case ERT_CATCH:
	case ERT_THROW:
	  /* Nothing to do.  */
	  break;

	default:
	  gcc_unreachable ();
	}
    }
}

/* Replace RESX patterns with jumps to the next handler if any, or calls to
   _Unwind_Resume otherwise.  */

static void
connect_post_landing_pads (void)
{
  int i;

  for (i = cfun->eh->last_region_number; i > 0; --i)
    {
      struct eh_region *region;
      struct eh_region *outer;
      rtx seq;
      rtx barrier;

      region = VEC_index (eh_region, cfun->eh->region_array, i);
      /* Mind we don't process a region more than once.  */
      if (!region || region->region_number != i)
	continue;

      /* If there is no RESX, or it has been deleted by flow, there's
	 nothing to fix up.  */
      if (! region->resume || INSN_DELETED_P (region->resume))
	continue;

      /* Search for another landing pad in this function.  */
      for (outer = region->outer; outer ; outer = outer->outer)
	if (outer->post_landing_pad)
	  break;

      start_sequence ();

      if (outer)
	{
	  edge e;
	  basic_block src, dest;

	  emit_jump (outer->post_landing_pad);
	  src = BLOCK_FOR_INSN (region->resume);
	  dest = BLOCK_FOR_INSN (outer->post_landing_pad);
	  while (EDGE_COUNT (src->succs) > 0)
	    remove_edge (EDGE_SUCC (src, 0));
	  e = make_edge (src, dest, 0);
	  e->probability = REG_BR_PROB_BASE;
	  e->count = src->count;
	}
      else
	{
	  emit_library_call (unwind_resume_libfunc, LCT_THROW,
			     VOIDmode, 1, cfun->eh->exc_ptr, ptr_mode);

	  /* What we just emitted was a throwing libcall, so it got a
	     barrier automatically added after it.  If the last insn in
	     the libcall sequence isn't the barrier, it's because the
	     target emits multiple insns for a call, and there are insns
	     after the actual call insn (which are redundant and would be
	     optimized away).  The barrier is inserted exactly after the
	     call insn, so let's go get that and delete the insns after
	     it, because below we need the barrier to be the last insn in
	     the sequence.  */
	  delete_insns_since (NEXT_INSN (last_call_insn ()));
	}

      seq = get_insns ();
      end_sequence ();
      barrier = emit_insn_before (seq, region->resume);
      /* Avoid duplicate barrier.  */
      gcc_assert (BARRIER_P (barrier));
      delete_insn (barrier);
      delete_insn (region->resume);

      /* ??? From tree-ssa we can wind up with catch regions whose
	 label is not instantiated, but whose resx is present.  Now
	 that we've dealt with the resx, kill the region.  */
      if (region->label == NULL && region->type == ERT_CLEANUP)
	remove_eh_handler (region);
    }
}


static void
dw2_build_landing_pads (void)
{
  int i;

  for (i = cfun->eh->last_region_number; i > 0; --i)
    {
      struct eh_region *region;
      rtx seq;
      basic_block bb;
      edge e;

      region = VEC_index (eh_region, cfun->eh->region_array, i);
      /* Mind we don't process a region more than once.  */
      if (!region || region->region_number != i)
	continue;

      if (region->type != ERT_CLEANUP
	  && region->type != ERT_TRY
	  && region->type != ERT_ALLOWED_EXCEPTIONS)
	continue;

      start_sequence ();

      region->landing_pad = gen_label_rtx ();
      emit_label (region->landing_pad);

#ifdef HAVE_exception_receiver
      if (HAVE_exception_receiver)
	emit_insn (gen_exception_receiver ());
      else
#endif
#ifdef HAVE_nonlocal_goto_receiver
	if (HAVE_nonlocal_goto_receiver)
	  emit_insn (gen_nonlocal_goto_receiver ());
	else
#endif
	  { /* Nothing */ }

      emit_move_insn (cfun->eh->exc_ptr,
		      gen_rtx_REG (ptr_mode, EH_RETURN_DATA_REGNO (0)));
      emit_move_insn (cfun->eh->filter,
		      gen_rtx_REG (targetm.eh_return_filter_mode (),
				   EH_RETURN_DATA_REGNO (1)));

      seq = get_insns ();
      end_sequence ();

      bb = emit_to_new_bb_before (seq, region->post_landing_pad);
      e = make_edge (bb, bb->next_bb, EDGE_FALLTHRU);
      e->count = bb->count;
      e->probability = REG_BR_PROB_BASE;
    }
}


struct sjlj_lp_info
{
  int directly_reachable;
  int action_index;
  int dispatch_index;
  int call_site_index;
};

static bool
sjlj_find_directly_reachable_regions (struct sjlj_lp_info *lp_info)
{
  rtx insn;
  bool found_one = false;

  for (insn = get_insns (); insn ; insn = NEXT_INSN (insn))
    {
      struct eh_region *region;
      enum reachable_code rc;
      tree type_thrown;
      rtx note;

      if (! INSN_P (insn))
	continue;

      note = find_reg_note (insn, REG_EH_REGION, NULL_RTX);
      if (!note || INTVAL (XEXP (note, 0)) <= 0)
	continue;

      region = VEC_index (eh_region, cfun->eh->region_array, INTVAL (XEXP (note, 0)));

      type_thrown = NULL_TREE;
      if (region->type == ERT_THROW)
	{
	  type_thrown = region->u.throw.type;
	  region = region->outer;
	}

      /* Find the first containing region that might handle the exception.
	 That's the landing pad to which we will transfer control.  */
      rc = RNL_NOT_CAUGHT;
      for (; region; region = region->outer)
	{
	  rc = reachable_next_level (region, type_thrown, NULL);
	  if (rc != RNL_NOT_CAUGHT)
	    break;
	}
      if (rc == RNL_MAYBE_CAUGHT || rc == RNL_CAUGHT)
	{
	  lp_info[region->region_number].directly_reachable = 1;
	  found_one = true;
	}
    }

  return found_one;
}

static void
sjlj_assign_call_site_values (rtx dispatch_label, struct sjlj_lp_info *lp_info)
{
  htab_t ar_hash;
  int i, index;

  /* First task: build the action table.  */

  VARRAY_UCHAR_INIT (cfun->eh->action_record_data, 64, "action_record_data");
  ar_hash = htab_create (31, action_record_hash, action_record_eq, free);

  for (i = cfun->eh->last_region_number; i > 0; --i)
    if (lp_info[i].directly_reachable)
      {
	struct eh_region *r = VEC_index (eh_region, cfun->eh->region_array, i);

	r->landing_pad = dispatch_label;
	lp_info[i].action_index = collect_one_action_chain (ar_hash, r);
	if (lp_info[i].action_index != -1)
	  cfun->uses_eh_lsda = 1;
      }

  htab_delete (ar_hash);

  /* Next: assign dispatch values.  In dwarf2 terms, this would be the
     landing pad label for the region.  For sjlj though, there is one
     common landing pad from which we dispatch to the post-landing pads.

     A region receives a dispatch index if it is directly reachable
     and requires in-function processing.  Regions that share post-landing
     pads may share dispatch indices.  */
  /* ??? Post-landing pad sharing doesn't actually happen at the moment
     (see build_post_landing_pads) so we don't bother checking for it.  */

  index = 0;
  for (i = cfun->eh->last_region_number; i > 0; --i)
    if (lp_info[i].directly_reachable)
      lp_info[i].dispatch_index = index++;

  /* Finally: assign call-site values.  If dwarf2 terms, this would be
     the region number assigned by convert_to_eh_region_ranges, but
     handles no-action and must-not-throw differently.  */

  call_site_base = 1;
  for (i = cfun->eh->last_region_number; i > 0; --i)
    if (lp_info[i].directly_reachable)
      {
	int action = lp_info[i].action_index;

	/* Map must-not-throw to otherwise unused call-site index 0.  */
	if (action == -2)
	  index = 0;
	/* Map no-action to otherwise unused call-site index -1.  */
	else if (action == -1)
	  index = -1;
	/* Otherwise, look it up in the table.  */
	else
	  index = add_call_site (GEN_INT (lp_info[i].dispatch_index), action);

	lp_info[i].call_site_index = index;
      }
}

static void
sjlj_mark_call_sites (struct sjlj_lp_info *lp_info)
{
  int last_call_site = -2;
  rtx insn, mem;

  for (insn = get_insns (); insn ; insn = NEXT_INSN (insn))
    {
      struct eh_region *region;
      int this_call_site;
      rtx note, before, p;

      /* Reset value tracking at extended basic block boundaries.  */
      if (LABEL_P (insn))
	last_call_site = -2;

      if (! INSN_P (insn))
	continue;

      note = find_reg_note (insn, REG_EH_REGION, NULL_RTX);
      if (!note)
	{
	  /* Calls (and trapping insns) without notes are outside any
	     exception handling region in this function.  Mark them as
	     no action.  */
	  if (CALL_P (insn)
	      || (flag_non_call_exceptions
		  && may_trap_p (PATTERN (insn))))
	    this_call_site = -1;
	  else
	    continue;
	}
      else
	{
	  /* Calls that are known to not throw need not be marked.  */
	  if (INTVAL (XEXP (note, 0)) <= 0)
	    continue;

	  region = VEC_index (eh_region, cfun->eh->region_array, INTVAL (XEXP (note, 0)));
	  this_call_site = lp_info[region->region_number].call_site_index;
	}

      if (this_call_site == last_call_site)
	continue;

      /* Don't separate a call from it's argument loads.  */
      before = insn;
      if (CALL_P (insn))
	before = find_first_parameter_load (insn, NULL_RTX);

      start_sequence ();
      mem = adjust_address (cfun->eh->sjlj_fc, TYPE_MODE (integer_type_node),
			    sjlj_fc_call_site_ofs);
      emit_move_insn (mem, GEN_INT (this_call_site));
      p = get_insns ();
      end_sequence ();

      emit_insn_before (p, before);
      last_call_site = this_call_site;
    }
}

/* Construct the SjLj_Function_Context.  */

static void
sjlj_emit_function_enter (rtx dispatch_label)
{
  rtx fn_begin, fc, mem, seq;
  bool fn_begin_outside_block;

  fc = cfun->eh->sjlj_fc;

  start_sequence ();

  /* We're storing this libcall's address into memory instead of
     calling it directly.  Thus, we must call assemble_external_libcall
     here, as we can not depend on emit_library_call to do it for us.  */
  assemble_external_libcall (eh_personality_libfunc);
  mem = adjust_address (fc, Pmode, sjlj_fc_personality_ofs);
  emit_move_insn (mem, eh_personality_libfunc);

  mem = adjust_address (fc, Pmode, sjlj_fc_lsda_ofs);
  if (cfun->uses_eh_lsda)
    {
      char buf[20];
      rtx sym;

      ASM_GENERATE_INTERNAL_LABEL (buf, "LLSDA", current_function_funcdef_no);
      sym = gen_rtx_SYMBOL_REF (Pmode, ggc_strdup (buf));
      SYMBOL_REF_FLAGS (sym) = SYMBOL_FLAG_LOCAL;
      emit_move_insn (mem, sym);
    }
  else
    emit_move_insn (mem, const0_rtx);

#ifdef DONT_USE_BUILTIN_SETJMP
  {
    rtx x, note;
    x = emit_library_call_value (setjmp_libfunc, NULL_RTX, LCT_RETURNS_TWICE,
				 TYPE_MODE (integer_type_node), 1,
				 plus_constant (XEXP (fc, 0),
						sjlj_fc_jbuf_ofs), Pmode);

    note = emit_note (NOTE_INSN_EXPECTED_VALUE);
    NOTE_EXPECTED_VALUE (note) = gen_rtx_EQ (VOIDmode, x, const0_rtx);

    emit_cmp_and_jump_insns (x, const0_rtx, NE, 0,
			     TYPE_MODE (integer_type_node), 0, dispatch_label);
  }
#else
  expand_builtin_setjmp_setup (plus_constant (XEXP (fc, 0), sjlj_fc_jbuf_ofs),
			       dispatch_label);
#endif

  emit_library_call (unwind_sjlj_register_libfunc, LCT_NORMAL, VOIDmode,
		     1, XEXP (fc, 0), Pmode);

  seq = get_insns ();
  end_sequence ();

  /* ??? Instead of doing this at the beginning of the function,
     do this in a block that is at loop level 0 and dominates all
     can_throw_internal instructions.  */

  fn_begin_outside_block = true;
  for (fn_begin = get_insns (); ; fn_begin = NEXT_INSN (fn_begin))
    if (NOTE_P (fn_begin))
      {
	if (NOTE_LINE_NUMBER (fn_begin) == NOTE_INSN_FUNCTION_BEG)
	  break;
	else if (NOTE_LINE_NUMBER (fn_begin) == NOTE_INSN_BASIC_BLOCK)
	  fn_begin_outside_block = false;
      }

  if (fn_begin_outside_block)
    insert_insn_on_edge (seq, single_succ_edge (ENTRY_BLOCK_PTR));
  else
    emit_insn_after (seq, fn_begin);
}

/* Call back from expand_function_end to know where we should put
   the call to unwind_sjlj_unregister_libfunc if needed.  */

void
sjlj_emit_function_exit_after (rtx after)
{
  cfun->eh->sjlj_exit_after = after;
}

static void
sjlj_emit_function_exit (void)
{
  rtx seq;
  edge e;
  edge_iterator ei;

  start_sequence ();

  emit_library_call (unwind_sjlj_unregister_libfunc, LCT_NORMAL, VOIDmode,
		     1, XEXP (cfun->eh->sjlj_fc, 0), Pmode);

  seq = get_insns ();
  end_sequence ();

  /* ??? Really this can be done in any block at loop level 0 that
     post-dominates all can_throw_internal instructions.  This is
     the last possible moment.  */

  FOR_EACH_EDGE (e, ei, EXIT_BLOCK_PTR->preds)
    if (e->flags & EDGE_FALLTHRU)
      break;
  if (e)
    {
      rtx insn;

      /* Figure out whether the place we are supposed to insert libcall
         is inside the last basic block or after it.  In the other case
         we need to emit to edge.  */
      gcc_assert (e->src->next_bb == EXIT_BLOCK_PTR);
      for (insn = BB_HEAD (e->src); ; insn = NEXT_INSN (insn))
	{
	  if (insn == cfun->eh->sjlj_exit_after)
	    {
	      if (LABEL_P (insn))
		insn = NEXT_INSN (insn);
	      emit_insn_after (seq, insn);
	      return;
	    }
	  if (insn == BB_END (e->src))
	    break;
	}
      insert_insn_on_edge (seq, e);
    }
}

static void
sjlj_emit_dispatch_table (rtx dispatch_label, struct sjlj_lp_info *lp_info)
{
  int i, first_reachable;
  rtx mem, dispatch, seq, fc;
  rtx before;
  basic_block bb;
  edge e;

  fc = cfun->eh->sjlj_fc;

  start_sequence ();

  emit_label (dispatch_label);

#ifndef DONT_USE_BUILTIN_SETJMP
  expand_builtin_setjmp_receiver (dispatch_label);
#endif

  /* Load up dispatch index, exc_ptr and filter values from the
     function context.  */
  mem = adjust_address (fc, TYPE_MODE (integer_type_node),
			sjlj_fc_call_site_ofs);
  dispatch = copy_to_reg (mem);

  mem = adjust_address (fc, word_mode, sjlj_fc_data_ofs);
  if (word_mode != ptr_mode)
    {
#ifdef POINTERS_EXTEND_UNSIGNED
      mem = convert_memory_address (ptr_mode, mem);
#else
      mem = convert_to_mode (ptr_mode, mem, 0);
#endif
    }
  emit_move_insn (cfun->eh->exc_ptr, mem);

  mem = adjust_address (fc, word_mode, sjlj_fc_data_ofs + UNITS_PER_WORD);
  emit_move_insn (cfun->eh->filter, mem);

  /* Jump to one of the directly reachable regions.  */
  /* ??? This really ought to be using a switch statement.  */

  first_reachable = 0;
  for (i = cfun->eh->last_region_number; i > 0; --i)
    {
      if (! lp_info[i].directly_reachable)
	continue;

      if (! first_reachable)
	{
	  first_reachable = i;
	  continue;
	}

      emit_cmp_and_jump_insns (dispatch, GEN_INT (lp_info[i].dispatch_index),
			       EQ, NULL_RTX, TYPE_MODE (integer_type_node), 0,
	                       ((struct eh_region *)VEC_index (eh_region, cfun->eh->region_array, i))
				->post_landing_pad);
    }

  seq = get_insns ();
  end_sequence ();

  before = (((struct eh_region *)VEC_index (eh_region, cfun->eh->region_array, first_reachable))
	    ->post_landing_pad);

  bb = emit_to_new_bb_before (seq, before);
  e = make_edge (bb, bb->next_bb, EDGE_FALLTHRU);
  e->count = bb->count;
  e->probability = REG_BR_PROB_BASE;
}

static void
sjlj_build_landing_pads (void)
{
  struct sjlj_lp_info *lp_info;

  lp_info = XCNEWVEC (struct sjlj_lp_info, cfun->eh->last_region_number + 1);

  if (sjlj_find_directly_reachable_regions (lp_info))
    {
      rtx dispatch_label = gen_label_rtx ();

      cfun->eh->sjlj_fc
	= assign_stack_local (TYPE_MODE (sjlj_fc_type_node),
			      int_size_in_bytes (sjlj_fc_type_node),
			      TYPE_ALIGN (sjlj_fc_type_node));

      sjlj_assign_call_site_values (dispatch_label, lp_info);
      sjlj_mark_call_sites (lp_info);

      sjlj_emit_function_enter (dispatch_label);
      sjlj_emit_dispatch_table (dispatch_label, lp_info);
      sjlj_emit_function_exit ();
    }

  free (lp_info);
}

void
finish_eh_generation (void)
{
  basic_block bb;

  /* Nothing to do if no regions created.  */
  if (cfun->eh->region_tree == NULL)
    return;

  /* The object here is to provide find_basic_blocks with detailed
     information (via reachable_handlers) on how exception control
     flows within the function.  In this first pass, we can include
     type information garnered from ERT_THROW and ERT_ALLOWED_EXCEPTIONS
     regions, and hope that it will be useful in deleting unreachable
     handlers.  Subsequently, we will generate landing pads which will
     connect many of the handlers, and then type information will not
     be effective.  Still, this is a win over previous implementations.  */

  /* These registers are used by the landing pads.  Make sure they
     have been generated.  */
  get_exception_pointer (cfun);
  get_exception_filter (cfun);

  /* Construct the landing pads.  */

  assign_filter_values ();
  build_post_landing_pads ();
  connect_post_landing_pads ();
  if (USING_SJLJ_EXCEPTIONS)
    sjlj_build_landing_pads ();
  else
    dw2_build_landing_pads ();

  cfun->eh->built_landing_pads = 1;

  /* We've totally changed the CFG.  Start over.  */
  find_exception_handler_labels ();
  break_superblocks ();
  if (USING_SJLJ_EXCEPTIONS)
    commit_edge_insertions ();
  FOR_EACH_BB (bb)
    {
      edge e;
      edge_iterator ei;
      bool eh = false;
      for (ei = ei_start (bb->succs); (e = ei_safe_edge (ei)); )
	{
	  if (e->flags & EDGE_EH)
	    {
	      remove_edge (e);
	      eh = true;
	    }
	  else
	    ei_next (&ei);
	}
      if (eh)
	rtl_make_eh_edge (NULL, bb, BB_END (bb));
    }
}

static hashval_t
ehl_hash (const void *pentry)
{
  struct ehl_map_entry *entry = (struct ehl_map_entry *) pentry;

  /* 2^32 * ((sqrt(5) - 1) / 2) */
  const hashval_t scaled_golden_ratio = 0x9e3779b9;
  return CODE_LABEL_NUMBER (entry->label) * scaled_golden_ratio;
}

static int
ehl_eq (const void *pentry, const void *pdata)
{
  struct ehl_map_entry *entry = (struct ehl_map_entry *) pentry;
  struct ehl_map_entry *data = (struct ehl_map_entry *) pdata;

  return entry->label == data->label;
}

/* This section handles removing dead code for flow.  */

/* Remove LABEL from exception_handler_label_map.  */

static void
remove_exception_handler_label (rtx label)
{
  struct ehl_map_entry **slot, tmp;

  /* If exception_handler_label_map was not built yet,
     there is nothing to do.  */
  if (cfun->eh->exception_handler_label_map == NULL)
    return;

  tmp.label = label;
  slot = (struct ehl_map_entry **)
    htab_find_slot (cfun->eh->exception_handler_label_map, &tmp, NO_INSERT);
  gcc_assert (slot);

  htab_clear_slot (cfun->eh->exception_handler_label_map, (void **) slot);
}

/* Splice REGION from the region tree etc.  */

static void
remove_eh_handler (struct eh_region *region)
{
  struct eh_region **pp, **pp_start, *p, *outer, *inner;
  rtx lab;

  /* For the benefit of efficiently handling REG_EH_REGION notes,
     replace this region in the region array with its containing
     region.  Note that previous region deletions may result in
     multiple copies of this region in the array, so we have a
     list of alternate numbers by which we are known.  */

  outer = region->outer;
  VEC_replace (eh_region, cfun->eh->region_array, region->region_number, outer);
  if (region->aka)
    {
      unsigned i;
      bitmap_iterator bi;

      EXECUTE_IF_SET_IN_BITMAP (region->aka, 0, i, bi)
	{
          VEC_replace (eh_region, cfun->eh->region_array, i, outer);
	}
    }

  if (outer)
    {
      if (!outer->aka)
        outer->aka = BITMAP_GGC_ALLOC ();
      if (region->aka)
	bitmap_ior_into (outer->aka, region->aka);
      bitmap_set_bit (outer->aka, region->region_number);
    }

  if (cfun->eh->built_landing_pads)
    lab = region->landing_pad;
  else
    lab = region->label;
  if (lab)
    remove_exception_handler_label (lab);

  if (outer)
    pp_start = &outer->inner;
  else
    pp_start = &cfun->eh->region_tree;
  for (pp = pp_start, p = *pp; p != region; pp = &p->next_peer, p = *pp)
    continue;
  *pp = region->next_peer;

  inner = region->inner;
  if (inner)
    {
      for (p = inner; p->next_peer ; p = p->next_peer)
	p->outer = outer;
      p->outer = outer;

      p->next_peer = *pp_start;
      *pp_start = inner;
    }

  if (region->type == ERT_CATCH)
    {
      struct eh_region *try, *next, *prev;

      for (try = region->next_peer;
	   try->type == ERT_CATCH;
	   try = try->next_peer)
	continue;
      gcc_assert (try->type == ERT_TRY);

      next = region->u.catch.next_catch;
      prev = region->u.catch.prev_catch;

      if (next)
	next->u.catch.prev_catch = prev;
      else
	try->u.try.last_catch = prev;
      if (prev)
	prev->u.catch.next_catch = next;
      else
	{
	  try->u.try.catch = next;
	  if (! next)
	    remove_eh_handler (try);
	}
    }
}

/* LABEL heads a basic block that is about to be deleted.  If this
   label corresponds to an exception region, we may be able to
   delete the region.  */

void
maybe_remove_eh_handler (rtx label)
{
  struct ehl_map_entry **slot, tmp;
  struct eh_region *region;

  /* ??? After generating landing pads, it's not so simple to determine
     if the region data is completely unused.  One must examine the
     landing pad and the post landing pad, and whether an inner try block
     is referencing the catch handlers directly.  */
  if (cfun->eh->built_landing_pads)
    return;

  tmp.label = label;
  slot = (struct ehl_map_entry **)
    htab_find_slot (cfun->eh->exception_handler_label_map, &tmp, NO_INSERT);
  if (! slot)
    return;
  region = (*slot)->region;
  if (! region)
    return;

  /* Flow will want to remove MUST_NOT_THROW regions as unreachable
     because there is no path to the fallback call to terminate.
     But the region continues to affect call-site data until there
     are no more contained calls, which we don't see here.  */
  if (region->type == ERT_MUST_NOT_THROW)
    {
      htab_clear_slot (cfun->eh->exception_handler_label_map, (void **) slot);
      region->label = NULL_RTX;
    }
  else
    remove_eh_handler (region);
}

/* Invokes CALLBACK for every exception handler label.  Only used by old
   loop hackery; should not be used by new code.  */

void
for_each_eh_label (void (*callback) (rtx))
{
  htab_traverse (cfun->eh->exception_handler_label_map, for_each_eh_label_1,
		 (void *) &callback);
}

static int
for_each_eh_label_1 (void **pentry, void *data)
{
  struct ehl_map_entry *entry = *(struct ehl_map_entry **)pentry;
  void (*callback) (rtx) = *(void (**) (rtx)) data;

  (*callback) (entry->label);
  return 1;
}

/* Invoke CALLBACK for every exception region in the current function.  */

void
for_each_eh_region (void (*callback) (struct eh_region *))
{
  int i, n = cfun->eh->last_region_number;
  for (i = 1; i <= n; ++i)
    {
      struct eh_region *region;

      region = VEC_index (eh_region, cfun->eh->region_array, i);
      if (region)
	(*callback) (region);
    }
}

/* This section describes CFG exception edges for flow.  */

/* For communicating between calls to reachable_next_level.  */
struct reachable_info
{
  tree types_caught;
  tree types_allowed;
  void (*callback) (struct eh_region *, void *);
  void *callback_data;
  bool saw_any_handlers;
};

/* A subroutine of reachable_next_level.  Return true if TYPE, or a
   base class of TYPE, is in HANDLED.  */

static int
check_handled (tree handled, tree type)
{
  tree t;

  /* We can check for exact matches without front-end help.  */
  if (! lang_eh_type_covers)
    {
      for (t = handled; t ; t = TREE_CHAIN (t))
	if (TREE_VALUE (t) == type)
	  return 1;
    }
  else
    {
      for (t = handled; t ; t = TREE_CHAIN (t))
	if ((*lang_eh_type_covers) (TREE_VALUE (t), type))
	  return 1;
    }

  return 0;
}

/* A subroutine of reachable_next_level.  If we are collecting a list
   of handlers, add one.  After landing pad generation, reference
   it instead of the handlers themselves.  Further, the handlers are
   all wired together, so by referencing one, we've got them all.
   Before landing pad generation we reference each handler individually.

   LP_REGION contains the landing pad; REGION is the handler.  */

static void
add_reachable_handler (struct reachable_info *info,
		       struct eh_region *lp_region, struct eh_region *region)
{
  if (! info)
    return;

  info->saw_any_handlers = true;

  if (cfun->eh->built_landing_pads)
    info->callback (lp_region, info->callback_data);
  else
    info->callback (region, info->callback_data);
}

/* Process one level of exception regions for reachability.
   If TYPE_THROWN is non-null, then it is the *exact* type being
   propagated.  If INFO is non-null, then collect handler labels
   and caught/allowed type information between invocations.  */

static enum reachable_code
reachable_next_level (struct eh_region *region, tree type_thrown,
		      struct reachable_info *info)
{
  switch (region->type)
    {
    case ERT_CLEANUP:
      /* Before landing-pad generation, we model control flow
	 directly to the individual handlers.  In this way we can
	 see that catch handler types may shadow one another.  */
      add_reachable_handler (info, region, region);
      return RNL_MAYBE_CAUGHT;

    case ERT_TRY:
      {
	struct eh_region *c;
	enum reachable_code ret = RNL_NOT_CAUGHT;

	for (c = region->u.try.catch; c ; c = c->u.catch.next_catch)
	  {
	    /* A catch-all handler ends the search.  */
	    if (c->u.catch.type_list == NULL)
	      {
		add_reachable_handler (info, region, c);
		return RNL_CAUGHT;
	      }

	    if (type_thrown)
	      {
		/* If we have at least one type match, end the search.  */
		tree tp_node = c->u.catch.type_list;

		for (; tp_node; tp_node = TREE_CHAIN (tp_node))
		  {
		    tree type = TREE_VALUE (tp_node);

		    if (type == type_thrown
			|| (lang_eh_type_covers
			    && (*lang_eh_type_covers) (type, type_thrown)))
		      {
			add_reachable_handler (info, region, c);
			return RNL_CAUGHT;
		      }
		  }

		/* If we have definitive information of a match failure,
		   the catch won't trigger.  */
		if (lang_eh_type_covers)
		  return RNL_NOT_CAUGHT;
	      }

	    /* At this point, we either don't know what type is thrown or
	       don't have front-end assistance to help deciding if it is
	       covered by one of the types in the list for this region.

	       We'd then like to add this region to the list of reachable
	       handlers since it is indeed potentially reachable based on the
	       information we have.

	       Actually, this handler is for sure not reachable if all the
	       types it matches have already been caught. That is, it is only
	       potentially reachable if at least one of the types it catches
	       has not been previously caught.  */

	    if (! info)
	      ret = RNL_MAYBE_CAUGHT;
	    else
	      {
		tree tp_node = c->u.catch.type_list;
		bool maybe_reachable = false;

		/* Compute the potential reachability of this handler and
		   update the list of types caught at the same time.  */
		for (; tp_node; tp_node = TREE_CHAIN (tp_node))
		  {
		    tree type = TREE_VALUE (tp_node);

		    if (! check_handled (info->types_caught, type))
		      {
			info->types_caught
			  = tree_cons (NULL, type, info->types_caught);

			maybe_reachable = true;
		      }
		  }

		if (maybe_reachable)
		  {
		    add_reachable_handler (info, region, c);

		    /* ??? If the catch type is a base class of every allowed
		       type, then we know we can stop the search.  */
		    ret = RNL_MAYBE_CAUGHT;
		  }
	      }
	  }

	return ret;
      }

    case ERT_ALLOWED_EXCEPTIONS:
      /* An empty list of types definitely ends the search.  */
      if (region->u.allowed.type_list == NULL_TREE)
	{
	  add_reachable_handler (info, region, region);
	  return RNL_CAUGHT;
	}

      /* Collect a list of lists of allowed types for use in detecting
	 when a catch may be transformed into a catch-all.  */
      if (info)
	info->types_allowed = tree_cons (NULL_TREE,
					 region->u.allowed.type_list,
					 info->types_allowed);

      /* If we have definitive information about the type hierarchy,
	 then we can tell if the thrown type will pass through the
	 filter.  */
      if (type_thrown && lang_eh_type_covers)
	{
	  if (check_handled (region->u.allowed.type_list, type_thrown))
	    return RNL_NOT_CAUGHT;
	  else
	    {
	      add_reachable_handler (info, region, region);
	      return RNL_CAUGHT;
	    }
	}

      add_reachable_handler (info, region, region);
      return RNL_MAYBE_CAUGHT;

    case ERT_CATCH:
      /* Catch regions are handled by their controlling try region.  */
      return RNL_NOT_CAUGHT;

    case ERT_MUST_NOT_THROW:
      /* Here we end our search, since no exceptions may propagate.
	 If we've touched down at some landing pad previous, then the
	 explicit function call we generated may be used.  Otherwise
	 the call is made by the runtime.

         Before inlining, do not perform this optimization.  We may
	 inline a subroutine that contains handlers, and that will
	 change the value of saw_any_handlers.  */

      if ((info && info->saw_any_handlers) || !cfun->after_inlining)
	{
	  add_reachable_handler (info, region, region);
	  return RNL_CAUGHT;
	}
      else
	return RNL_BLOCKED;

    case ERT_THROW:
    case ERT_UNKNOWN:
      /* Shouldn't see these here.  */
      gcc_unreachable ();
      break;
    default:
      gcc_unreachable ();
    }
}

/* Invoke CALLBACK on each region reachable from REGION_NUMBER.  */

void
foreach_reachable_handler (int region_number, bool is_resx,
			   void (*callback) (struct eh_region *, void *),
			   void *callback_data)
{
  struct reachable_info info;
  struct eh_region *region;
  tree type_thrown;

  memset (&info, 0, sizeof (info));
  info.callback = callback;
  info.callback_data = callback_data;

  region = VEC_index (eh_region, cfun->eh->region_array, region_number);

  type_thrown = NULL_TREE;
  if (is_resx)
    {
      /* A RESX leaves a region instead of entering it.  Thus the
	 region itself may have been deleted out from under us.  */
      if (region == NULL)
	return;
      region = region->outer;
    }
  else if (region->type == ERT_THROW)
    {
      type_thrown = region->u.throw.type;
      region = region->outer;
    }

  while (region)
    {
      if (reachable_next_level (region, type_thrown, &info) >= RNL_CAUGHT)
	break;
      /* If we have processed one cleanup, there is no point in
	 processing any more of them.  Each cleanup will have an edge
	 to the next outer cleanup region, so the flow graph will be
	 accurate.  */
      if (region->type == ERT_CLEANUP)
	region = region->u.cleanup.prev_try;
      else
	region = region->outer;
    }
}

/* Retrieve a list of labels of exception handlers which can be
   reached by a given insn.  */

static void
arh_to_landing_pad (struct eh_region *region, void *data)
{
  rtx *p_handlers = data;
  if (! *p_handlers)
    *p_handlers = alloc_INSN_LIST (region->landing_pad, NULL_RTX);
}

static void
arh_to_label (struct eh_region *region, void *data)
{
  rtx *p_handlers = data;
  *p_handlers = alloc_INSN_LIST (region->label, *p_handlers);
}

rtx
reachable_handlers (rtx insn)
{
  bool is_resx = false;
  rtx handlers = NULL;
  int region_number;

  if (JUMP_P (insn)
      && GET_CODE (PATTERN (insn)) == RESX)
    {
      region_number = XINT (PATTERN (insn), 0);
      is_resx = true;
    }
  else
    {
      rtx note = find_reg_note (insn, REG_EH_REGION, NULL_RTX);
      if (!note || INTVAL (XEXP (note, 0)) <= 0)
	return NULL;
      region_number = INTVAL (XEXP (note, 0));
    }

  foreach_reachable_handler (region_number, is_resx,
			     (cfun->eh->built_landing_pads
			      ? arh_to_landing_pad
			      : arh_to_label),
			     &handlers);

  return handlers;
}

/* Determine if the given INSN can throw an exception that is caught
   within the function.  */

bool
can_throw_internal_1 (int region_number, bool is_resx)
{
  struct eh_region *region;
  tree type_thrown;

  region = VEC_index (eh_region, cfun->eh->region_array, region_number);

  type_thrown = NULL_TREE;
  if (is_resx)
    region = region->outer;
  else if (region->type == ERT_THROW)
    {
      type_thrown = region->u.throw.type;
      region = region->outer;
    }

  /* If this exception is ignored by each and every containing region,
     then control passes straight out.  The runtime may handle some
     regions, which also do not require processing internally.  */
  for (; region; region = region->outer)
    {
      enum reachable_code how = reachable_next_level (region, type_thrown, 0);
      if (how == RNL_BLOCKED)
	return false;
      if (how != RNL_NOT_CAUGHT)
	return true;
    }

  return false;
}

bool
can_throw_internal (rtx insn)
{
  rtx note;

  if (! INSN_P (insn))
    return false;

  if (JUMP_P (insn)
      && GET_CODE (PATTERN (insn)) == RESX
      && XINT (PATTERN (insn), 0) > 0)
    return can_throw_internal_1 (XINT (PATTERN (insn), 0), true);

  if (NONJUMP_INSN_P (insn)
      && GET_CODE (PATTERN (insn)) == SEQUENCE)
    insn = XVECEXP (PATTERN (insn), 0, 0);

  /* Every insn that might throw has an EH_REGION note.  */
  note = find_reg_note (insn, REG_EH_REGION, NULL_RTX);
  if (!note || INTVAL (XEXP (note, 0)) <= 0)
    return false;

  return can_throw_internal_1 (INTVAL (XEXP (note, 0)), false);
}

/* Determine if the given INSN can throw an exception that is
   visible outside the function.  */

bool
can_throw_external_1 (int region_number, bool is_resx)
{
  struct eh_region *region;
  tree type_thrown;

  region = VEC_index (eh_region, cfun->eh->region_array, region_number);

  type_thrown = NULL_TREE;
  if (is_resx)
    region = region->outer;
  else if (region->type == ERT_THROW)
    {
      type_thrown = region->u.throw.type;
      region = region->outer;
    }

  /* If the exception is caught or blocked by any containing region,
     then it is not seen by any calling function.  */
  for (; region ; region = region->outer)
    if (reachable_next_level (region, type_thrown, NULL) >= RNL_CAUGHT)
      return false;

  return true;
}

bool
can_throw_external (rtx insn)
{
  rtx note;

  if (! INSN_P (insn))
    return false;

  if (JUMP_P (insn)
      && GET_CODE (PATTERN (insn)) == RESX
      && XINT (PATTERN (insn), 0) > 0)
    return can_throw_external_1 (XINT (PATTERN (insn), 0), true);

  if (NONJUMP_INSN_P (insn)
      && GET_CODE (PATTERN (insn)) == SEQUENCE)
    insn = XVECEXP (PATTERN (insn), 0, 0);

  note = find_reg_note (insn, REG_EH_REGION, NULL_RTX);
  if (!note)
    {
      /* Calls (and trapping insns) without notes are outside any
	 exception handling region in this function.  We have to
	 assume it might throw.  Given that the front end and middle
	 ends mark known NOTHROW functions, this isn't so wildly
	 inaccurate.  */
      return (CALL_P (insn)
	      || (flag_non_call_exceptions
		  && may_trap_p (PATTERN (insn))));
    }
  if (INTVAL (XEXP (note, 0)) <= 0)
    return false;

  return can_throw_external_1 (INTVAL (XEXP (note, 0)), false);
}

/* Set TREE_NOTHROW and cfun->all_throwers_are_sibcalls.  */

unsigned int
set_nothrow_function_flags (void)
{
  rtx insn;

  /* If we don't know that this implementation of the function will
     actually be used, then we must not set TREE_NOTHROW, since
     callers must not assume that this function does not throw.  */
  if (DECL_REPLACEABLE_P (current_function_decl))
    return 0;

  TREE_NOTHROW (current_function_decl) = 1;

  /* Assume cfun->all_throwers_are_sibcalls until we encounter
     something that can throw an exception.  We specifically exempt
     CALL_INSNs that are SIBLING_CALL_P, as these are really jumps,
     and can't throw.  Most CALL_INSNs are not SIBLING_CALL_P, so this
     is optimistic.  */

  cfun->all_throwers_are_sibcalls = 1;

  if (! flag_exceptions)
    return 0;

  for (insn = get_insns (); insn; insn = NEXT_INSN (insn))
    if (can_throw_external (insn))
      {
        TREE_NOTHROW (current_function_decl) = 0;

	if (!CALL_P (insn) || !SIBLING_CALL_P (insn))
	  {
	    cfun->all_throwers_are_sibcalls = 0;
	    return 0;
	  }
      }

  for (insn = current_function_epilogue_delay_list; insn;
       insn = XEXP (insn, 1))
    if (can_throw_external (insn))
      {
        TREE_NOTHROW (current_function_decl) = 0;

	if (!CALL_P (insn) || !SIBLING_CALL_P (insn))
	  {
	    cfun->all_throwers_are_sibcalls = 0;
	    return 0;
	  }
      }
  return 0;
}

struct tree_opt_pass pass_set_nothrow_function_flags =
{
  NULL,                                 /* name */
  NULL,                                 /* gate */
  set_nothrow_function_flags,           /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  0,                                    /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  0,                                    /* todo_flags_finish */
  0                                     /* letter */
};


/* Various hooks for unwind library.  */

/* Do any necessary initialization to access arbitrary stack frames.
   On the SPARC, this means flushing the register windows.  */

void
expand_builtin_unwind_init (void)
{
  /* Set this so all the registers get saved in our frame; we need to be
     able to copy the saved values for any registers from frames we unwind.  */
  current_function_has_nonlocal_label = 1;

#ifdef SETUP_FRAME_ADDRESSES
  SETUP_FRAME_ADDRESSES ();
#endif
}

rtx
expand_builtin_eh_return_data_regno (tree arglist)
{
  tree which = TREE_VALUE (arglist);
  unsigned HOST_WIDE_INT iwhich;

  if (TREE_CODE (which) != INTEGER_CST)
    {
      error ("argument of %<__builtin_eh_return_regno%> must be constant");
      return constm1_rtx;
    }

  iwhich = tree_low_cst (which, 1);
  iwhich = EH_RETURN_DATA_REGNO (iwhich);
  if (iwhich == INVALID_REGNUM)
    return constm1_rtx;

#ifdef DWARF_FRAME_REGNUM
  iwhich = DWARF_FRAME_REGNUM (iwhich);
#else
  iwhich = DBX_REGISTER_NUMBER (iwhich);
#endif

  return GEN_INT (iwhich);
}

/* Given a value extracted from the return address register or stack slot,
   return the actual address encoded in that value.  */

rtx
expand_builtin_extract_return_addr (tree addr_tree)
{
  rtx addr = expand_expr (addr_tree, NULL_RTX, Pmode, 0);

  if (GET_MODE (addr) != Pmode
      && GET_MODE (addr) != VOIDmode)
    {
#ifdef POINTERS_EXTEND_UNSIGNED
      addr = convert_memory_address (Pmode, addr);
#else
      addr = convert_to_mode (Pmode, addr, 0);
#endif
    }

  /* First mask out any unwanted bits.  */
#ifdef MASK_RETURN_ADDR
  expand_and (Pmode, addr, MASK_RETURN_ADDR, addr);
#endif

  /* Then adjust to find the real return address.  */
#if defined (RETURN_ADDR_OFFSET)
  addr = plus_constant (addr, RETURN_ADDR_OFFSET);
#endif

  return addr;
}

/* Given an actual address in addr_tree, do any necessary encoding
   and return the value to be stored in the return address register or
   stack slot so the epilogue will return to that address.  */

rtx
expand_builtin_frob_return_addr (tree addr_tree)
{
  rtx addr = expand_expr (addr_tree, NULL_RTX, ptr_mode, 0);

  addr = convert_memory_address (Pmode, addr);

#ifdef RETURN_ADDR_OFFSET
  addr = force_reg (Pmode, addr);
  addr = plus_constant (addr, -RETURN_ADDR_OFFSET);
#endif

  return addr;
}

/* Set up the epilogue with the magic bits we'll need to return to the
   exception handler.  */

void
expand_builtin_eh_return (tree stackadj_tree ATTRIBUTE_UNUSED,
			  tree handler_tree)
{
  rtx tmp;

#ifdef EH_RETURN_STACKADJ_RTX
  tmp = expand_expr (stackadj_tree, cfun->eh->ehr_stackadj, VOIDmode, 0);
  tmp = convert_memory_address (Pmode, tmp);
  if (!cfun->eh->ehr_stackadj)
    cfun->eh->ehr_stackadj = copy_to_reg (tmp);
  else if (tmp != cfun->eh->ehr_stackadj)
    emit_move_insn (cfun->eh->ehr_stackadj, tmp);
#endif

  tmp = expand_expr (handler_tree, cfun->eh->ehr_handler, VOIDmode, 0);
  tmp = convert_memory_address (Pmode, tmp);
  if (!cfun->eh->ehr_handler)
    cfun->eh->ehr_handler = copy_to_reg (tmp);
  else if (tmp != cfun->eh->ehr_handler)
    emit_move_insn (cfun->eh->ehr_handler, tmp);

  if (!cfun->eh->ehr_label)
    cfun->eh->ehr_label = gen_label_rtx ();
  emit_jump (cfun->eh->ehr_label);
}

void
expand_eh_return (void)
{
  rtx around_label;

  if (! cfun->eh->ehr_label)
    return;

  current_function_calls_eh_return = 1;

#ifdef EH_RETURN_STACKADJ_RTX
  emit_move_insn (EH_RETURN_STACKADJ_RTX, const0_rtx);
#endif

  around_label = gen_label_rtx ();
  emit_jump (around_label);

  emit_label (cfun->eh->ehr_label);
  clobber_return_register ();

#ifdef EH_RETURN_STACKADJ_RTX
  emit_move_insn (EH_RETURN_STACKADJ_RTX, cfun->eh->ehr_stackadj);
#endif

#ifdef HAVE_eh_return
  if (HAVE_eh_return)
    emit_insn (gen_eh_return (cfun->eh->ehr_handler));
  else
#endif
    {
#ifdef EH_RETURN_HANDLER_RTX
      emit_move_insn (EH_RETURN_HANDLER_RTX, cfun->eh->ehr_handler);
#else
      error ("__builtin_eh_return not supported on this target");
#endif
    }

  emit_label (around_label);
}

/* Convert a ptr_mode address ADDR_TREE to a Pmode address controlled by
   POINTERS_EXTEND_UNSIGNED and return it.  */

rtx
expand_builtin_extend_pointer (tree addr_tree)
{
  rtx addr = expand_expr (addr_tree, NULL_RTX, ptr_mode, 0);
  int extend;

#ifdef POINTERS_EXTEND_UNSIGNED
  extend = POINTERS_EXTEND_UNSIGNED;
#else
  /* The previous EH code did an unsigned extend by default, so we do this also
     for consistency.  */
  extend = 1;
#endif

  return convert_modes (word_mode, ptr_mode, addr, extend);
}

/* In the following functions, we represent entries in the action table
   as 1-based indices.  Special cases are:

	 0:	null action record, non-null landing pad; implies cleanups
	-1:	null action record, null landing pad; implies no action
	-2:	no call-site entry; implies must_not_throw
	-3:	we have yet to process outer regions

   Further, no special cases apply to the "next" field of the record.
   For next, 0 means end of list.  */

struct action_record
{
  int offset;
  int filter;
  int next;
};

static int
action_record_eq (const void *pentry, const void *pdata)
{
  const struct action_record *entry = (const struct action_record *) pentry;
  const struct action_record *data = (const struct action_record *) pdata;
  return entry->filter == data->filter && entry->next == data->next;
}

static hashval_t
action_record_hash (const void *pentry)
{
  const struct action_record *entry = (const struct action_record *) pentry;
  return entry->next * 1009 + entry->filter;
}

static int
add_action_record (htab_t ar_hash, int filter, int next)
{
  struct action_record **slot, *new, tmp;

  tmp.filter = filter;
  tmp.next = next;
  slot = (struct action_record **) htab_find_slot (ar_hash, &tmp, INSERT);

  if ((new = *slot) == NULL)
    {
      new = xmalloc (sizeof (*new));
      new->offset = VARRAY_ACTIVE_SIZE (cfun->eh->action_record_data) + 1;
      new->filter = filter;
      new->next = next;
      *slot = new;

      /* The filter value goes in untouched.  The link to the next
	 record is a "self-relative" byte offset, or zero to indicate
	 that there is no next record.  So convert the absolute 1 based
	 indices we've been carrying around into a displacement.  */

      push_sleb128 (&cfun->eh->action_record_data, filter);
      if (next)
	next -= VARRAY_ACTIVE_SIZE (cfun->eh->action_record_data) + 1;
      push_sleb128 (&cfun->eh->action_record_data, next);
    }

  return new->offset;
}

static int
collect_one_action_chain (htab_t ar_hash, struct eh_region *region)
{
  struct eh_region *c;
  int next;

  /* If we've reached the top of the region chain, then we have
     no actions, and require no landing pad.  */
  if (region == NULL)
    return -1;

  switch (region->type)
    {
    case ERT_CLEANUP:
      /* A cleanup adds a zero filter to the beginning of the chain, but
	 there are special cases to look out for.  If there are *only*
	 cleanups along a path, then it compresses to a zero action.
	 Further, if there are multiple cleanups along a path, we only
	 need to represent one of them, as that is enough to trigger
	 entry to the landing pad at runtime.  */
      next = collect_one_action_chain (ar_hash, region->outer);
      if (next <= 0)
	return 0;
      for (c = region->outer; c ; c = c->outer)
	if (c->type == ERT_CLEANUP)
	  return next;
      return add_action_record (ar_hash, 0, next);

    case ERT_TRY:
      /* Process the associated catch regions in reverse order.
	 If there's a catch-all handler, then we don't need to
	 search outer regions.  Use a magic -3 value to record
	 that we haven't done the outer search.  */
      next = -3;
      for (c = region->u.try.last_catch; c ; c = c->u.catch.prev_catch)
	{
	  if (c->u.catch.type_list == NULL)
	    {
	      /* Retrieve the filter from the head of the filter list
		 where we have stored it (see assign_filter_values).  */
	      int filter
		= TREE_INT_CST_LOW (TREE_VALUE (c->u.catch.filter_list));

	      next = add_action_record (ar_hash, filter, 0);
	    }
	  else
	    {
	      /* Once the outer search is done, trigger an action record for
                 each filter we have.  */
	      tree flt_node;

	      if (next == -3)
		{
		  next = collect_one_action_chain (ar_hash, region->outer);

		  /* If there is no next action, terminate the chain.  */
		  if (next == -1)
		    next = 0;
		  /* If all outer actions are cleanups or must_not_throw,
		     we'll have no action record for it, since we had wanted
		     to encode these states in the call-site record directly.
		     Add a cleanup action to the chain to catch these.  */
		  else if (next <= 0)
		    next = add_action_record (ar_hash, 0, 0);
		}

	      flt_node = c->u.catch.filter_list;
	      for (; flt_node; flt_node = TREE_CHAIN (flt_node))
		{
		  int filter = TREE_INT_CST_LOW (TREE_VALUE (flt_node));
		  next = add_action_record (ar_hash, filter, next);
		}
	    }
	}
      return next;

    case ERT_ALLOWED_EXCEPTIONS:
      /* An exception specification adds its filter to the
	 beginning of the chain.  */
      next = collect_one_action_chain (ar_hash, region->outer);

      /* If there is no next action, terminate the chain.  */
      if (next == -1)
	next = 0;
      /* If all outer actions are cleanups or must_not_throw,
	 we'll have no action record for it, since we had wanted
	 to encode these states in the call-site record directly.
	 Add a cleanup action to the chain to catch these.  */
      else if (next <= 0)
	next = add_action_record (ar_hash, 0, 0);

      return add_action_record (ar_hash, region->u.allowed.filter, next);

    case ERT_MUST_NOT_THROW:
      /* A must-not-throw region with no inner handlers or cleanups
	 requires no call-site entry.  Note that this differs from
	 the no handler or cleanup case in that we do require an lsda
	 to be generated.  Return a magic -2 value to record this.  */
      return -2;

    case ERT_CATCH:
    case ERT_THROW:
      /* CATCH regions are handled in TRY above.  THROW regions are
	 for optimization information only and produce no output.  */
      return collect_one_action_chain (ar_hash, region->outer);

    default:
      gcc_unreachable ();
    }
}

static int
add_call_site (rtx landing_pad, int action)
{
  struct call_site_record *data = cfun->eh->call_site_data;
  int used = cfun->eh->call_site_data_used;
  int size = cfun->eh->call_site_data_size;

  if (used >= size)
    {
      size = (size ? size * 2 : 64);
      data = ggc_realloc (data, sizeof (*data) * size);
      cfun->eh->call_site_data = data;
      cfun->eh->call_site_data_size = size;
    }

  data[used].landing_pad = landing_pad;
  data[used].action = action;

  cfun->eh->call_site_data_used = used + 1;

  return used + call_site_base;
}

/* Turn REG_EH_REGION notes back into NOTE_INSN_EH_REGION notes.
   The new note numbers will not refer to region numbers, but
   instead to call site entries.  */

unsigned int
convert_to_eh_region_ranges (void)
{
  rtx insn, iter, note;
  htab_t ar_hash;
  int last_action = -3;
  rtx last_action_insn = NULL_RTX;
  rtx last_landing_pad = NULL_RTX;
  rtx first_no_action_insn = NULL_RTX;
  int call_site = 0;

  if (USING_SJLJ_EXCEPTIONS || cfun->eh->region_tree == NULL)
    return 0;

  VARRAY_UCHAR_INIT (cfun->eh->action_record_data, 64, "action_record_data");

  ar_hash = htab_create (31, action_record_hash, action_record_eq, free);

  for (iter = get_insns (); iter ; iter = NEXT_INSN (iter))
    if (INSN_P (iter))
      {
	struct eh_region *region;
	int this_action;
	rtx this_landing_pad;

	insn = iter;
	if (NONJUMP_INSN_P (insn)
	    && GET_CODE (PATTERN (insn)) == SEQUENCE)
	  insn = XVECEXP (PATTERN (insn), 0, 0);

	note = find_reg_note (insn, REG_EH_REGION, NULL_RTX);
	if (!note)
	  {
	    if (! (CALL_P (insn)
		   || (flag_non_call_exceptions
		       && may_trap_p (PATTERN (insn)))))
	      continue;
	    this_action = -1;
	    region = NULL;
	  }
	else
	  {
	    if (INTVAL (XEXP (note, 0)) <= 0)
	      continue;
	    region = VEC_index (eh_region, cfun->eh->region_array, INTVAL (XEXP (note, 0)));
	    this_action = collect_one_action_chain (ar_hash, region);
	  }

	/* Existence of catch handlers, or must-not-throw regions
	   implies that an lsda is needed (even if empty).  */
	if (this_action != -1)
	  cfun->uses_eh_lsda = 1;

	/* Delay creation of region notes for no-action regions
	   until we're sure that an lsda will be required.  */
	else if (last_action == -3)
	  {
	    first_no_action_insn = iter;
	    last_action = -1;
	  }

	/* Cleanups and handlers may share action chains but not
	   landing pads.  Collect the landing pad for this region.  */
	if (this_action >= 0)
	  {
	    struct eh_region *o;
	    for (o = region; ! o->landing_pad ; o = o->outer)
	      continue;
	    this_landing_pad = o->landing_pad;
	  }
	else
	  this_landing_pad = NULL_RTX;

	/* Differing actions or landing pads implies a change in call-site
	   info, which implies some EH_REGION note should be emitted.  */
	if (last_action != this_action
	    || last_landing_pad != this_landing_pad)
	  {
	    /* If we'd not seen a previous action (-3) or the previous
	       action was must-not-throw (-2), then we do not need an
	       end note.  */
	    if (last_action >= -1)
	      {
		/* If we delayed the creation of the begin, do it now.  */
		if (first_no_action_insn)
		  {
		    call_site = add_call_site (NULL_RTX, 0);
		    note = emit_note_before (NOTE_INSN_EH_REGION_BEG,
					     first_no_action_insn);
		    NOTE_EH_HANDLER (note) = call_site;
		    first_no_action_insn = NULL_RTX;
		  }

		note = emit_note_after (NOTE_INSN_EH_REGION_END,
					last_action_insn);
		NOTE_EH_HANDLER (note) = call_site;
	      }

	    /* If the new action is must-not-throw, then no region notes
	       are created.  */
	    if (this_action >= -1)
	      {
		call_site = add_call_site (this_landing_pad,
					   this_action < 0 ? 0 : this_action);
		note = emit_note_before (NOTE_INSN_EH_REGION_BEG, iter);
		NOTE_EH_HANDLER (note) = call_site;
	      }

	    last_action = this_action;
	    last_landing_pad = this_landing_pad;
	  }
	last_action_insn = iter;
      }

  if (last_action >= -1 && ! first_no_action_insn)
    {
      note = emit_note_after (NOTE_INSN_EH_REGION_END, last_action_insn);
      NOTE_EH_HANDLER (note) = call_site;
    }

  htab_delete (ar_hash);
  return 0;
}

struct tree_opt_pass pass_convert_to_eh_region_ranges =
{
  "eh-ranges",                          /* name */
  NULL,                                 /* gate */
  convert_to_eh_region_ranges,          /* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  0,                                    /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_dump_func,			/* todo_flags_finish */
  0                                     /* letter */
};


static void
push_uleb128 (varray_type *data_area, unsigned int value)
{
  do
    {
      unsigned char byte = value & 0x7f;
      value >>= 7;
      if (value)
	byte |= 0x80;
      VARRAY_PUSH_UCHAR (*data_area, byte);
    }
  while (value);
}

static void
push_sleb128 (varray_type *data_area, int value)
{
  unsigned char byte;
  int more;

  do
    {
      byte = value & 0x7f;
      value >>= 7;
      more = ! ((value == 0 && (byte & 0x40) == 0)
		|| (value == -1 && (byte & 0x40) != 0));
      if (more)
	byte |= 0x80;
      VARRAY_PUSH_UCHAR (*data_area, byte);
    }
  while (more);
}


#ifndef HAVE_AS_LEB128
static int
dw2_size_of_call_site_table (void)
{
  int n = cfun->eh->call_site_data_used;
  int size = n * (4 + 4 + 4);
  int i;

  for (i = 0; i < n; ++i)
    {
      struct call_site_record *cs = &cfun->eh->call_site_data[i];
      size += size_of_uleb128 (cs->action);
    }

  return size;
}

static int
sjlj_size_of_call_site_table (void)
{
  int n = cfun->eh->call_site_data_used;
  int size = 0;
  int i;

  for (i = 0; i < n; ++i)
    {
      struct call_site_record *cs = &cfun->eh->call_site_data[i];
      size += size_of_uleb128 (INTVAL (cs->landing_pad));
      size += size_of_uleb128 (cs->action);
    }

  return size;
}
#endif

static void
dw2_output_call_site_table (void)
{
  int n = cfun->eh->call_site_data_used;
  int i;

  for (i = 0; i < n; ++i)
    {
      struct call_site_record *cs = &cfun->eh->call_site_data[i];
      char reg_start_lab[32];
      char reg_end_lab[32];
      char landing_pad_lab[32];

      ASM_GENERATE_INTERNAL_LABEL (reg_start_lab, "LEHB", call_site_base + i);
      ASM_GENERATE_INTERNAL_LABEL (reg_end_lab, "LEHE", call_site_base + i);

      if (cs->landing_pad)
	ASM_GENERATE_INTERNAL_LABEL (landing_pad_lab, "L",
				     CODE_LABEL_NUMBER (cs->landing_pad));

      /* ??? Perhaps use insn length scaling if the assembler supports
	 generic arithmetic.  */
      /* ??? Perhaps use attr_length to choose data1 or data2 instead of
	 data4 if the function is small enough.  */
#ifdef HAVE_AS_LEB128
      dw2_asm_output_delta_uleb128 (reg_start_lab,
				    current_function_func_begin_label,
				    "region %d start", i);
      dw2_asm_output_delta_uleb128 (reg_end_lab, reg_start_lab,
				    "length");
      if (cs->landing_pad)
	dw2_asm_output_delta_uleb128 (landing_pad_lab,
				      current_function_func_begin_label,
				      "landing pad");
      else
	dw2_asm_output_data_uleb128 (0, "landing pad");
#else
      dw2_asm_output_delta (4, reg_start_lab,
			    current_function_func_begin_label,
			    "region %d start", i);
      dw2_asm_output_delta (4, reg_end_lab, reg_start_lab, "length");
      if (cs->landing_pad)
	dw2_asm_output_delta (4, landing_pad_lab,
			      current_function_func_begin_label,
			      "landing pad");
      else
	dw2_asm_output_data (4, 0, "landing pad");
#endif
      dw2_asm_output_data_uleb128 (cs->action, "action");
    }

  call_site_base += n;
}

static void
sjlj_output_call_site_table (void)
{
  int n = cfun->eh->call_site_data_used;
  int i;

  for (i = 0; i < n; ++i)
    {
      struct call_site_record *cs = &cfun->eh->call_site_data[i];

      dw2_asm_output_data_uleb128 (INTVAL (cs->landing_pad),
				   "region %d landing pad", i);
      dw2_asm_output_data_uleb128 (cs->action, "action");
    }

  call_site_base += n;
}

#ifndef TARGET_UNWIND_INFO
/* Switch to the section that should be used for exception tables.  */

static void
switch_to_exception_section (void)
{
  if (exception_section == 0)
    {
      if (targetm.have_named_sections)
	{
	  int flags;

	  if (EH_TABLES_CAN_BE_READ_ONLY)
	    {
	      int tt_format =
		ASM_PREFERRED_EH_DATA_FORMAT (/*code=*/0, /*global=*/1);
	      flags = ((! flag_pic
			|| ((tt_format & 0x70) != DW_EH_PE_absptr
			    && (tt_format & 0x70) != DW_EH_PE_aligned))
		       ? 0 : SECTION_WRITE);
	    }
	  else
	    flags = SECTION_WRITE;
	  exception_section = get_section (".gcc_except_table", flags, NULL);
	}
      else
	exception_section = flag_pic ? data_section : readonly_data_section;
    }
  switch_to_section (exception_section);
}
#endif


/* Output a reference from an exception table to the type_info object TYPE.
   TT_FORMAT and TT_FORMAT_SIZE describe the DWARF encoding method used for
   the value.  */

static void
output_ttype (tree type, int tt_format, int tt_format_size)
{
  rtx value;
  bool public = true;

  if (type == NULL_TREE)
    value = const0_rtx;
  else
    {
      struct cgraph_varpool_node *node;

      type = lookup_type_for_runtime (type);
      value = expand_expr (type, NULL_RTX, VOIDmode, EXPAND_INITIALIZER);

      /* Let cgraph know that the rtti decl is used.  Not all of the
	 paths below go through assemble_integer, which would take
	 care of this for us.  */
      STRIP_NOPS (type);
      if (TREE_CODE (type) == ADDR_EXPR)
	{
	  type = TREE_OPERAND (type, 0);
	  if (TREE_CODE (type) == VAR_DECL)
	    {
	      node = cgraph_varpool_node (type);
	      if (node)
		cgraph_varpool_mark_needed_node (node);
	      public = TREE_PUBLIC (type);
	    }
	}
      else
	gcc_assert (TREE_CODE (type) == INTEGER_CST);
    }

  /* Allow the target to override the type table entry format.  */
  if (targetm.asm_out.ttype (value))
    return;

  if (tt_format == DW_EH_PE_absptr || tt_format == DW_EH_PE_aligned)
    assemble_integer (value, tt_format_size,
		      tt_format_size * BITS_PER_UNIT, 1);
  else
    dw2_asm_output_encoded_addr_rtx (tt_format, value, public, NULL);
}

void
output_function_exception_table (void)
{
  int tt_format, cs_format, lp_format, i, n;
#ifdef HAVE_AS_LEB128
  char ttype_label[32];
  char cs_after_size_label[32];
  char cs_end_label[32];
#else
  int call_site_len;
#endif
  int have_tt_data;
  int tt_format_size = 0;

  if (eh_personality_libfunc)
    assemble_external_libcall (eh_personality_libfunc);

  /* Not all functions need anything.  */
  if (! cfun->uses_eh_lsda)
    return;

#ifdef TARGET_UNWIND_INFO
  /* TODO: Move this into target file.  */
  fputs ("\t.personality\t", asm_out_file);
  output_addr_const (asm_out_file, eh_personality_libfunc);
  fputs ("\n\t.handlerdata\n", asm_out_file);
  /* Note that varasm still thinks we're in the function's code section.
     The ".endp" directive that will immediately follow will take us back.  */
#else
  switch_to_exception_section ();
#endif

  /* If the target wants a label to begin the table, emit it here.  */
  targetm.asm_out.except_table_label (asm_out_file);

  have_tt_data = (VEC_length (tree, cfun->eh->ttype_data) > 0
		  || VARRAY_ACTIVE_SIZE (cfun->eh->ehspec_data) > 0);

  /* Indicate the format of the @TType entries.  */
  if (! have_tt_data)
    tt_format = DW_EH_PE_omit;
  else
    {
      tt_format = ASM_PREFERRED_EH_DATA_FORMAT (/*code=*/0, /*global=*/1);
#ifdef HAVE_AS_LEB128
      ASM_GENERATE_INTERNAL_LABEL (ttype_label, "LLSDATT",
				   current_function_funcdef_no);
#endif
      tt_format_size = size_of_encoded_value (tt_format);

      assemble_align (tt_format_size * BITS_PER_UNIT);
    }

  targetm.asm_out.internal_label (asm_out_file, "LLSDA",
			     current_function_funcdef_no);

  /* The LSDA header.  */

  /* Indicate the format of the landing pad start pointer.  An omitted
     field implies @LPStart == @Start.  */
  /* Currently we always put @LPStart == @Start.  This field would
     be most useful in moving the landing pads completely out of
     line to another section, but it could also be used to minimize
     the size of uleb128 landing pad offsets.  */
  lp_format = DW_EH_PE_omit;
  dw2_asm_output_data (1, lp_format, "@LPStart format (%s)",
		       eh_data_format_name (lp_format));

  /* @LPStart pointer would go here.  */

  dw2_asm_output_data (1, tt_format, "@TType format (%s)",
		       eh_data_format_name (tt_format));

#ifndef HAVE_AS_LEB128
  if (USING_SJLJ_EXCEPTIONS)
    call_site_len = sjlj_size_of_call_site_table ();
  else
    call_site_len = dw2_size_of_call_site_table ();
#endif

  /* A pc-relative 4-byte displacement to the @TType data.  */
  if (have_tt_data)
    {
#ifdef HAVE_AS_LEB128
      char ttype_after_disp_label[32];
      ASM_GENERATE_INTERNAL_LABEL (ttype_after_disp_label, "LLSDATTD",
				   current_function_funcdef_no);
      dw2_asm_output_delta_uleb128 (ttype_label, ttype_after_disp_label,
				    "@TType base offset");
      ASM_OUTPUT_LABEL (asm_out_file, ttype_after_disp_label);
#else
      /* Ug.  Alignment queers things.  */
      unsigned int before_disp, after_disp, last_disp, disp;

      before_disp = 1 + 1;
      after_disp = (1 + size_of_uleb128 (call_site_len)
		    + call_site_len
		    + VARRAY_ACTIVE_SIZE (cfun->eh->action_record_data)
		    + (VEC_length (tree, cfun->eh->ttype_data)
		       * tt_format_size));

      disp = after_disp;
      do
	{
	  unsigned int disp_size, pad;

	  last_disp = disp;
	  disp_size = size_of_uleb128 (disp);
	  pad = before_disp + disp_size + after_disp;
	  if (pad % tt_format_size)
	    pad = tt_format_size - (pad % tt_format_size);
	  else
	    pad = 0;
	  disp = after_disp + pad;
	}
      while (disp != last_disp);

      dw2_asm_output_data_uleb128 (disp, "@TType base offset");
#endif
    }

  /* Indicate the format of the call-site offsets.  */
#ifdef HAVE_AS_LEB128
  cs_format = DW_EH_PE_uleb128;
#else
  cs_format = DW_EH_PE_udata4;
#endif
  dw2_asm_output_data (1, cs_format, "call-site format (%s)",
		       eh_data_format_name (cs_format));

#ifdef HAVE_AS_LEB128
  ASM_GENERATE_INTERNAL_LABEL (cs_after_size_label, "LLSDACSB",
			       current_function_funcdef_no);
  ASM_GENERATE_INTERNAL_LABEL (cs_end_label, "LLSDACSE",
			       current_function_funcdef_no);
  dw2_asm_output_delta_uleb128 (cs_end_label, cs_after_size_label,
				"Call-site table length");
  ASM_OUTPUT_LABEL (asm_out_file, cs_after_size_label);
  if (USING_SJLJ_EXCEPTIONS)
    sjlj_output_call_site_table ();
  else
    dw2_output_call_site_table ();
  ASM_OUTPUT_LABEL (asm_out_file, cs_end_label);
#else
  dw2_asm_output_data_uleb128 (call_site_len,"Call-site table length");
  if (USING_SJLJ_EXCEPTIONS)
    sjlj_output_call_site_table ();
  else
    dw2_output_call_site_table ();
#endif

  /* ??? Decode and interpret the data for flag_debug_asm.  */
  n = VARRAY_ACTIVE_SIZE (cfun->eh->action_record_data);
  for (i = 0; i < n; ++i)
    dw2_asm_output_data (1, VARRAY_UCHAR (cfun->eh->action_record_data, i),
			 (i ? NULL : "Action record table"));

  if (have_tt_data)
    assemble_align (tt_format_size * BITS_PER_UNIT);

  i = VEC_length (tree, cfun->eh->ttype_data);
  while (i-- > 0)
    {
      tree type = VEC_index (tree, cfun->eh->ttype_data, i);
      output_ttype (type, tt_format, tt_format_size);
    }

#ifdef HAVE_AS_LEB128
  if (have_tt_data)
      ASM_OUTPUT_LABEL (asm_out_file, ttype_label);
#endif

  /* ??? Decode and interpret the data for flag_debug_asm.  */
  n = VARRAY_ACTIVE_SIZE (cfun->eh->ehspec_data);
  for (i = 0; i < n; ++i)
    {
      if (targetm.arm_eabi_unwinder)
	{
	  tree type = VARRAY_TREE (cfun->eh->ehspec_data, i);
	  output_ttype (type, tt_format, tt_format_size);
	}
      else
	dw2_asm_output_data (1, VARRAY_UCHAR (cfun->eh->ehspec_data, i),
			     (i ? NULL : "Exception specification table"));
    }

  switch_to_section (current_function_section ());
}

void
set_eh_throw_stmt_table (struct function *fun, struct htab *table)
{
  fun->eh->throw_stmt_table = table;
}

htab_t
get_eh_throw_stmt_table (struct function *fun)
{
  return fun->eh->throw_stmt_table;
}

/* Dump EH information to OUT.  */
void
dump_eh_tree (FILE *out, struct function *fun)
{
  struct eh_region *i;
  int depth = 0;
  static const char * const type_name[] = {"unknown", "cleanup", "try", "catch",
					   "allowed_exceptions", "must_not_throw",
					   "throw"};

  i = fun->eh->region_tree;
  if (! i)
    return;

  fprintf (out, "Eh tree:\n");
  while (1)
    {
      fprintf (out, "  %*s %i %s", depth * 2, "",
	       i->region_number, type_name [(int)i->type]);
      if (i->tree_label)
	{
          fprintf (out, " tree_label:");
	  print_generic_expr (out, i->tree_label, 0);
	}
      fprintf (out, "\n");
      /* If there are sub-regions, process them.  */
      if (i->inner)
	i = i->inner, depth++;
      /* If there are peers, process them.  */
      else if (i->next_peer)
	i = i->next_peer;
      /* Otherwise, step back up the tree to the next peer.  */
      else
	{
	  do {
	    i = i->outer;
	    depth--;
	    if (i == NULL)
	      return;
	  } while (i->next_peer == NULL);
	  i = i->next_peer;
	}
    }
}

/* Verify some basic invariants on EH datastructures.  Could be extended to
   catch more.  */
void
verify_eh_tree (struct function *fun)
{
  struct eh_region *i, *outer = NULL;
  bool err = false;
  int nvisited = 0;
  int count = 0;
  int j;
  int depth = 0;

  i = fun->eh->region_tree;
  if (! i)
    return;
  for (j = fun->eh->last_region_number; j > 0; --j)
    if ((i = VEC_index (eh_region, cfun->eh->region_array, j)))
      {
	count++;
	if (i->region_number != j)
	  {
	    error ("region_array is corrupted for region %i", i->region_number);
	    err = true;
	  }
      }

  while (1)
    {
      if (VEC_index (eh_region, cfun->eh->region_array, i->region_number) != i)
	{
	  error ("region_array is corrupted for region %i", i->region_number);
	  err = true;
	}
      if (i->outer != outer)
	{
	  error ("outer block of region %i is wrong", i->region_number);
	  err = true;
	}
      if (i->may_contain_throw && outer && !outer->may_contain_throw)
	{
	  error ("region %i may contain throw and is contained in region that may not",
		 i->region_number);
	  err = true;
	}
      if (depth < 0)
	{
	  error ("negative nesting depth of region %i", i->region_number);
	  err = true;
	}
      nvisited ++;
      /* If there are sub-regions, process them.  */
      if (i->inner)
	outer = i, i = i->inner, depth++;
      /* If there are peers, process them.  */
      else if (i->next_peer)
	i = i->next_peer;
      /* Otherwise, step back up the tree to the next peer.  */
      else
	{
	  do {
	    i = i->outer;
	    depth--;
	    if (i == NULL)
	      {
		if (depth != -1)
		  {
		    error ("tree list ends on depth %i", depth + 1);
		    err = true;
		  }
		if (count != nvisited)
		  {
		    error ("array does not match the region tree");
		    err = true;
		  }
		if (err)
		  {
		    dump_eh_tree (stderr, fun);
		    internal_error ("verify_eh_tree failed");
		  }
	        return;
	      }
	    outer = i->outer;
	  } while (i->next_peer == NULL);
	  i = i->next_peer;
	}
    }
}

/* Initialize unwind_resume_libfunc.  */

void
default_init_unwind_resume_libfunc (void)
{
  /* The default c++ routines aren't actually c++ specific, so use those.  */
  unwind_resume_libfunc =
    init_one_libfunc ( USING_SJLJ_EXCEPTIONS ? "_Unwind_SjLj_Resume"
					     : "_Unwind_Resume");
}


static bool
gate_handle_eh (void)
{
  return doing_eh (0);
}

/* Complete generation of exception handling code.  */
static unsigned int
rest_of_handle_eh (void)
{
  cleanup_cfg (CLEANUP_NO_INSN_DEL);
  finish_eh_generation ();
  cleanup_cfg (CLEANUP_NO_INSN_DEL);
  return 0;
}

struct tree_opt_pass pass_rtl_eh =
{
  "eh",                                 /* name */
  gate_handle_eh,                       /* gate */
  rest_of_handle_eh,			/* execute */
  NULL,                                 /* sub */
  NULL,                                 /* next */
  0,                                    /* static_pass_number */
  TV_JUMP,                              /* tv_id */
  0,                                    /* properties_required */
  0,                                    /* properties_provided */
  0,                                    /* properties_destroyed */
  0,                                    /* todo_flags_start */
  TODO_dump_func,                       /* todo_flags_finish */
  'h'                                   /* letter */
};

#include "gt-except.h"
