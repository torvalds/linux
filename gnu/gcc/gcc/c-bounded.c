/* Bounds checker for library functions with buffers and sizes.
 *
 * Copyright (c) 2004 Anil Madhavapeddy <anil@recoil.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "flags.h"
#include "tree.h"
#include "c-tree.h"
#include "flags.h"
#include "toplev.h"
#include "c-common.h"
#include "intl.h"
#include "diagnostic.h"
#include "langhooks.h"

/* Handle attributes associated with bounds checking.  */

/* Bounded attribute types */
enum bounded_type { buffer_bound_type, string_bound_type,
                    minbytes_bound_type, size_bound_type,
                    bounded_type_error, wcstring_bound_type };

typedef struct bound_check_info
{
  enum bounded_type bounded_type;      /* type of bound (string, minsize, etc) */
  unsigned HOST_WIDE_INT bounded_buf;  /* number of buffer pointer arg */
  unsigned HOST_WIDE_INT bounded_num;  /* number of buffer length arg || min size */
  unsigned HOST_WIDE_INT bounded_size; /* number of buffer element size arg */
} function_bounded_info;

tree handle_bounded_attribute(tree *, tree, tree, int, bool *);
void check_function_bounded (int *, tree, tree);

static bool decode_bounded_attr	PARAMS ((tree, function_bounded_info *, int));
static enum bounded_type decode_bounded_type PARAMS ((const char *));

/* Handle a "bounded" attribute; arguments as in
   struct attribute_spec.handler.  */
tree
handle_bounded_attribute (node, name, args, flags, no_add_attrs)
     tree *node;
     tree name ATTRIBUTE_UNUSED;
     tree args;
     int flags ATTRIBUTE_UNUSED;
     bool *no_add_attrs;
{
  tree type = *node;
  function_bounded_info info;
  tree argument, arg_iterate;
  unsigned HOST_WIDE_INT arg_num;

  if (!decode_bounded_attr (args, &info, 0))
    {
      *no_add_attrs = true;
      return NULL_TREE;
    }

  /* `min_size' directly specifies the minimum buffer length */
  if (info.bounded_type == minbytes_bound_type
      && info.bounded_num <= 0)
    {
      error ("`minbytes' bound size must be a positive integer value");
      *no_add_attrs = true;
      return NULL_TREE;
    }

  argument = TYPE_ARG_TYPES (type);
  if (argument)
    {
      arg_iterate = argument;
      for (arg_num = 1; ; ++arg_num)
        {
          if (arg_iterate == 0 || arg_num == info.bounded_buf)
            break;
          arg_iterate = TREE_CHAIN (arg_iterate);
        }
      if (! arg_iterate
          || (TREE_CODE (TREE_VALUE (arg_iterate)) != POINTER_TYPE
          && TREE_CODE (TREE_VALUE (arg_iterate)) != ARRAY_TYPE))
        {
          error ("bound buffer argument not an array or pointer type");
          *no_add_attrs = true;
          return NULL_TREE;
        }

      if (info.bounded_type == size_bound_type
          || info.bounded_type == string_bound_type
          || info.bounded_type == buffer_bound_type
          || info.bounded_type == wcstring_bound_type)
        {
          arg_iterate = argument;
          for (arg_num = 1; ; ++arg_num)
            {
              if (arg_iterate == 0 || arg_num == info.bounded_num)
                break;
              arg_iterate = TREE_CHAIN (arg_iterate);
            }
          if (! arg_iterate
              || TREE_CODE (TREE_VALUE (arg_iterate)) != INTEGER_TYPE)
            {
              error ("bound length argument not an integer type");
              *no_add_attrs = true;
              return NULL_TREE;
            }
        }
      if (info.bounded_type == size_bound_type)
        {
          arg_iterate = argument;
          for (arg_num = 1; ; ++arg_num)
            {
              if (arg_iterate == 0 || arg_num == info.bounded_size)
                break;
              arg_iterate = TREE_CHAIN (arg_iterate);
            }
          if (! arg_iterate
              || TREE_CODE (TREE_VALUE (arg_iterate)) != INTEGER_TYPE)
            {
              error ("bound element size argument not an integer type");
              *no_add_attrs = true;
              return NULL_TREE;
            }
        }
    }

  return NULL_TREE;
}

/* Decode the arguments to a "bounded" attribute into a function_bounded_info
   structure.  It is already known that the list is of the right length.
   If VALIDATED_P is true, then these attributes have already been validated
   and this function will abort if they are erroneous; if false, it
   will give an error message.  Returns true if the attributes are
   successfully decoded, false otherwise.  */

static bool
decode_bounded_attr (args, info, validated_p)
     tree args;
     function_bounded_info *info;
     int validated_p;
{
  int  bounded_num;
  tree bounded_type_id = TREE_VALUE (args);
  tree bounded_buf_expr = TREE_VALUE (TREE_CHAIN (args));
  tree bounded_num_expr = TREE_VALUE (TREE_CHAIN (TREE_CHAIN (args)));
  tree bounded_size_expr = TREE_CHAIN (TREE_CHAIN (TREE_CHAIN (args)));

  if (TREE_CODE (bounded_type_id) != IDENTIFIER_NODE)
    {
      if (validated_p)
        abort ();
      error ("unrecognized bounded type specifier");
      return false;
    }
  else
    {
      const char *p = IDENTIFIER_POINTER (bounded_type_id);

      info->bounded_type = decode_bounded_type (p);

      if (info->bounded_type == bounded_type_error)
	{
	  if (validated_p)
	    abort ();
	  warning (OPT_Wbounded,
	  	   "`%s' is an unrecognized bounded function type", p);
	  return false;
	}
    }

  /* Extract the third argument if its appropriate */
  switch (info->bounded_type)
    {
    case bounded_type_error:
      /* should never happen */
      internal_error ("unexpected bounded_type_error in decode_bounded_attr");
      break;
    case string_bound_type:
      if (bounded_size_expr)
        warning (OPT_Wbounded, "`string' bound type only takes 2 parameters");
      bounded_size_expr = size_int (0);
      break;
    case buffer_bound_type:
      if (bounded_size_expr)
        warning (OPT_Wbounded, "`buffer' bound type only takes 2 parameters");
      bounded_size_expr = size_int (0);
      break;
    case minbytes_bound_type:
      if (bounded_size_expr)
        warning (OPT_Wbounded, "`minbytes' bound type only takes 2 parameters");
      bounded_size_expr = size_int (0);
      break;
    case size_bound_type:
      if (bounded_size_expr)
        bounded_size_expr = TREE_VALUE (bounded_size_expr);
      else
        {
          error ("parameter 3 not specified for `size' bounded function");
          return false;
        }
      break;
    case wcstring_bound_type:
      if (bounded_size_expr)
        warning (
            OPT_Wbounded, "`wcstring' bound type only takes 2 parameters");
      bounded_size_expr = size_int (0);
      break;
    }

  /* Strip any conversions from the buffer parameters and verify they
     are constants */
  while (TREE_CODE (bounded_num_expr) == NOP_EXPR
         || TREE_CODE (bounded_num_expr) == CONVERT_EXPR
         || TREE_CODE (bounded_num_expr) == NON_LVALUE_EXPR)
    bounded_num_expr = TREE_OPERAND (bounded_num_expr, 0);

  while (TREE_CODE (bounded_buf_expr) == NOP_EXPR
         || TREE_CODE (bounded_buf_expr) == CONVERT_EXPR
         || TREE_CODE (bounded_buf_expr) == NON_LVALUE_EXPR)
    bounded_buf_expr = TREE_OPERAND (bounded_buf_expr, 0);

  while (TREE_CODE (bounded_size_expr) == NOP_EXPR
         || TREE_CODE (bounded_size_expr) == CONVERT_EXPR
         || TREE_CODE (bounded_size_expr) == NON_LVALUE_EXPR)
    bounded_size_expr = TREE_OPERAND (bounded_size_expr, 0);

  if (TREE_CODE (bounded_num_expr) != INTEGER_CST)
    {
      if (validated_p)
	abort ();
      error ("bound length operand number is not an integer constant");
      return false;
    }

  if (TREE_CODE (bounded_buf_expr) != INTEGER_CST)
    {
      if (validated_p)
	abort ();
      error ("bound buffer operand number is not an integer constant");
      return false;
    }

  if (TREE_CODE (bounded_size_expr) != INTEGER_CST)
    {
      if (validated_p)
	abort ();
      error ("bound element size operand number is not an integer constant");
      return false;
    }

  info->bounded_buf = TREE_INT_CST_LOW (bounded_buf_expr);
  info->bounded_size = TREE_INT_CST_LOW (bounded_size_expr);
  bounded_num = TREE_INT_CST_LOW (bounded_num_expr);

  /* `minbytes' directly specifies the minimum buffer length */
  if (info->bounded_type == minbytes_bound_type
      && bounded_num <= 0)
    {
      if (validated_p)
	abort ();
      error ("`minbytes' bound size must be a positive integer value");
      return false;
    }

  info->bounded_num = (unsigned HOST_WIDE_INT) bounded_num;
  return true;
}

static void check_bounded_info PARAMS ((int *, function_bounded_info *, tree));

/* Decode a bounded type from a string, returning the type, or
   bounded_type_error if not valid, in which case the caller should print an
   error message.  */
static enum bounded_type
decode_bounded_type (s)
     const char *s;
{
  if (!strcmp (s, "string") || !strcmp (s, "__string__"))
    return string_bound_type;
  else if (!strcmp (s, "buffer") || !strcmp (s, "__buffer__"))
    return buffer_bound_type;
  else if (!strcmp (s, "minbytes") || !strcmp (s, "__minbytes__"))
    return minbytes_bound_type;
  else if (!strcmp (s, "size") || !strcmp (s, "__size__"))
    return size_bound_type;
  else if (!strcmp (s, "wcstring") || !strcmp (s, "__wcstring__"))
    return wcstring_bound_type;
  else
    return bounded_type_error;
}

/* Check the argument list of a call to memcpy, bzero, etc.
   ATTRS are the attributes on the function type.
   PARAMS is the list of argument values.  */

void
check_function_bounded (status, attrs, params)
     int *status;
     tree attrs;
     tree params;
{
  tree a;
  /* See if this function has any bounded attributes.  */
  for (a = attrs; a; a = TREE_CHAIN (a))
    {
      if (is_attribute_p ("bounded", TREE_PURPOSE (a)))
	{
	  /* Yup; check it.  */
	  function_bounded_info info;
	  decode_bounded_attr (TREE_VALUE (a), &info, 1);
	  check_bounded_info (status, &info, params);
	}
    }
}

/* This function replaces `warning' inside the bounds checking
   functions.  If the `status' parameter is non-NULL, then it is
   dereferenced and set to 1 whenever a warning is caught.  Otherwise
   it warns as usual by replicating the innards of the warning
   function from diagnostic.c.  */
static void
status_warning (int *status, const char *msgid, ...)
{
  diagnostic_info diagnostic;
  va_list ap;
  
  va_start (ap, msgid);

  if (status)
    *status = 1;
  else
    {
      /* This duplicates the warning function behavior.  */
      diagnostic_set_info (&diagnostic, _(msgid), &ap, input_location,
	                   DK_WARNING);
      report_diagnostic (&diagnostic);
    }
  
  va_end (ap);
}

/* Check the argument list of a call to memcpy, bzero, etc.
   INFO points to the function_bounded_info structure.
   PARAMS is the list of argument values.  */

static void
check_bounded_info (status, info, params)
     int *status; 
     function_bounded_info *info;
     tree params;
{
  tree buf_expr, length_expr, size_expr;
  unsigned HOST_WIDE_INT arg_num;

  /* Extract the buffer expression from the arguments */
  buf_expr = params;
  for (arg_num = 1; ; ++arg_num)
    {
        if (buf_expr == 0)
          return;
        if (arg_num == info->bounded_buf)
          break;
        buf_expr = TREE_CHAIN (buf_expr);
    }
  buf_expr = TREE_VALUE (buf_expr);
                             
  /* Get the buffer length, either directly from the function attribute
     info, or from the parameter pointed to */
  if (info->bounded_type == minbytes_bound_type)
    length_expr = size_int (info->bounded_num);
  else
    {
      /* Extract the buffer length expression from the arguments */
      length_expr = params;
      for (arg_num = 1; ; ++arg_num)
        {
          if (length_expr == 0)
            return;
          if (arg_num == info->bounded_num)
            break;
          length_expr = TREE_CHAIN (length_expr);
        }
      length_expr = TREE_VALUE (length_expr);
    }

  /* If the bound type is `size', resolve the third parameter */
  if (info->bounded_type == size_bound_type)
    {
      size_expr = params;
      for (arg_num = 1; ; ++arg_num)
        {
          if (size_expr == 0)
            return;
          if (arg_num == info->bounded_size)
            break;
          size_expr = TREE_CHAIN (size_expr);
        }
      size_expr = TREE_VALUE (size_expr);
    }
  else
    size_expr = size_int (0);

  STRIP_NOPS (buf_expr);

  /* We only need to check if the buffer expression is a static
   * array (which is inside an ADDR_EXPR) */
  if (TREE_CODE (buf_expr) != ADDR_EXPR)
    return;
  buf_expr = TREE_OPERAND (buf_expr, 0);

  if (TREE_CODE (TREE_TYPE (buf_expr)) == ARRAY_TYPE
      && TYPE_DOMAIN (TREE_TYPE (buf_expr)))
    {
      int array_size, length, elem_size, type_size;
      tree array_size_expr = TYPE_MAX_VALUE (TYPE_DOMAIN (TREE_TYPE (buf_expr)));
      tree array_type = TREE_TYPE (TREE_TYPE (buf_expr));
      tree array_type_size_expr = TYPE_SIZE (array_type);

      /* Can't deal with variable-sized arrays yet */
      if (TREE_CODE (array_type_size_expr) != INTEGER_CST)
          return;

      /* Get the size of the type of the array and sanity check it */
      type_size = TREE_INT_CST_LOW (array_type_size_expr);
      if ((type_size % 8) != 0)
        {
          error ("found non-byte aligned type while checking bounds");
          return;
        }
      type_size /= 8;

      /* Both the size of the static buffer and the length should be
       * integer constants by now */
      if ((array_size_expr && TREE_CODE (array_size_expr) != INTEGER_CST)
          || TREE_CODE (length_expr) != INTEGER_CST
          || TREE_CODE (size_expr) != INTEGER_CST)
        return;

      /* array_size_expr contains maximum array index, so add one for size */
      if (array_size_expr)
        array_size = (TREE_INT_CST_LOW (array_size_expr) + 1) * type_size;
      else
        array_size = 0;
      length = TREE_INT_CST_LOW (length_expr);

      /* XXX - warn about a too-small buffer? */
      if (array_size < 1)
        return;

      switch (info->bounded_type)
        {
	case bounded_type_error:
	  /* should never happen */
	  internal_error ("unexpected bounded_type_error");
	  break;
        case string_bound_type:
        case buffer_bound_type:
          /* warn about illegal bounds value */
          if (length < 0)
            status_warning (status, "non-positive bounds length (%d) detected", length);
          /* check if the static buffer is smaller than bound length */
          if (array_size < length)
            status_warning(status, "array size (%d) smaller than bound length (%d)",
                array_size, length);
          break;
        case minbytes_bound_type:
          /* check if array is smaller than the minimum allowed */
          if (array_size < length)
            status_warning (status, "array size (%d) is smaller than minimum required (%d)",
                array_size, length);
          break;
        case size_bound_type:
          elem_size = TREE_INT_CST_LOW (size_expr);
          /* warn about illegal bounds value */
          if (length < 1)
            status_warning (status, "non-positive bounds length (%d) detected", length);
          /* check if the static buffer is smaller than bound length */
          if (array_size < (length * elem_size))
            status_warning(status, "array size (%d) smaller than required length (%d * %d)",
                array_size, length, elem_size);
          break;
        case wcstring_bound_type:
          /* warn about illegal bounds value */
          if (length < 0)
            status_warning (
                status, "non-positive bounds length (%d) detected", length);
          /* check if the static buffer is smaller than bound length */
          if (array_size / type_size < length)
            status_warning(status, "array size (%d) smaller than bound length"
                " (%d) * sizeof(type)", array_size, length);
          break;
        }
    }
}
