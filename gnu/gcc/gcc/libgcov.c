/* Routines required for instrumenting a program.  */
/* Compile this one with gcc.  */
/* Copyright (C) 1989, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001, 2002, 2003, 2004, 2005  Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combine
executable.)

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

#include "tconfig.h"
#include "tsystem.h"
#include "coretypes.h"
#include "tm.h"

#if defined(inhibit_libc)
#define IN_LIBGCOV (-1)
#else
#undef NULL /* Avoid errors if stdio.h and our stddef.h mismatch.  */
#include <stdio.h>
#define IN_LIBGCOV 1
#if defined(L_gcov)
#define GCOV_LINKAGE /* nothing */
#endif
#endif
#include "gcov-io.h"

#if defined(inhibit_libc)
/* If libc and its header files are not available, provide dummy functions.  */

#ifdef L_gcov
void __gcov_init (struct gcov_info *p __attribute__ ((unused))) {}
void __gcov_flush (void) {}
#endif

#ifdef L_gcov_merge_add
void __gcov_merge_add (gcov_type *counters  __attribute__ ((unused)),
		       unsigned n_counters __attribute__ ((unused))) {}
#endif

#ifdef L_gcov_merge_single
void __gcov_merge_single (gcov_type *counters  __attribute__ ((unused)),
			  unsigned n_counters __attribute__ ((unused))) {}
#endif

#ifdef L_gcov_merge_delta
void __gcov_merge_delta (gcov_type *counters  __attribute__ ((unused)),
			 unsigned n_counters __attribute__ ((unused))) {}
#endif

#else

#include <string.h>
#if GCOV_LOCKED
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#endif

#ifdef L_gcov
#include "gcov-io.c"

/* Chain of per-object gcov structures.  */
static struct gcov_info *gcov_list;

/* A program checksum allows us to distinguish program data for an
   object file included in multiple programs.  */
static gcov_unsigned_t gcov_crc32;

/* Size of the longest file name. */
static size_t gcov_max_filename = 0;

#ifdef TARGET_POSIX_IO
/* Make sure path component of the given FILENAME exists, create 
   missing directories. FILENAME must be writable. 
   Returns zero on success, or -1 if an error occurred.  */

static int
create_file_directory (char *filename)
{
  char *s;

  for (s = filename + 1; *s != '\0'; s++)
    if (IS_DIR_SEPARATOR(*s))
      {
        char sep = *s;
	*s  = '\0';

        /* Try to make directory if it doesn't already exist.  */
        if (access (filename, F_OK) == -1
            && mkdir (filename, 0755) == -1
            /* The directory might have been made by another process.  */
	    && errno != EEXIST)
	  {
            fprintf (stderr, "profiling:%s:Cannot create directory\n",
		     filename);
            *s = sep;
	    return -1;
	  };
        
	*s = sep;
      };
  return 0;
}
#endif

/* Check if VERSION of the info block PTR matches libgcov one.
   Return 1 on success, or zero in case of versions mismatch.
   If FILENAME is not NULL, its value used for reporting purposes 
   instead of value from the info block.  */
   
static int
gcov_version (struct gcov_info *ptr, gcov_unsigned_t version,
	      const char *filename)
{
  if (version != GCOV_VERSION)
    {
      char v[4], e[4];

      GCOV_UNSIGNED2STRING (v, version);
      GCOV_UNSIGNED2STRING (e, GCOV_VERSION);
      
      fprintf (stderr,
	       "profiling:%s:Version mismatch - expected %.4s got %.4s\n",
	       filename? filename : ptr->filename, e, v);
      return 0;
    }
  return 1;
}

/* Dump the coverage counts. We merge with existing counts when
   possible, to avoid growing the .da files ad infinitum. We use this
   program's checksum to make sure we only accumulate whole program
   statistics to the correct summary. An object file might be embedded
   in two separate programs, and we must keep the two program
   summaries separate.  */

static void
gcov_exit (void)
{
  struct gcov_info *gi_ptr;
  struct gcov_summary this_program;
  struct gcov_summary all;
  struct gcov_ctr_summary *cs_ptr;
  const struct gcov_ctr_info *ci_ptr;
  unsigned t_ix;
  gcov_unsigned_t c_num;
  const char *gcov_prefix;
  int gcov_prefix_strip = 0;
  size_t prefix_length;
  char *gi_filename, *gi_filename_up;

  memset (&all, 0, sizeof (all));
  /* Find the totals for this execution.  */
  memset (&this_program, 0, sizeof (this_program));
  for (gi_ptr = gcov_list; gi_ptr; gi_ptr = gi_ptr->next)
    {
      ci_ptr = gi_ptr->counts;
      for (t_ix = 0; t_ix < GCOV_COUNTERS_SUMMABLE; t_ix++)
	{
	  if (!((1 << t_ix) & gi_ptr->ctr_mask))
	    continue;

	  cs_ptr = &this_program.ctrs[t_ix];
	  cs_ptr->num += ci_ptr->num;
	  for (c_num = 0; c_num < ci_ptr->num; c_num++)
	    {
      	      cs_ptr->sum_all += ci_ptr->values[c_num];
	      if (cs_ptr->run_max < ci_ptr->values[c_num])
		cs_ptr->run_max = ci_ptr->values[c_num];
	    }
	  ci_ptr++;
	}
    }

  /* Get file name relocation prefix.  Non-absolute values are ignored. */
  gcov_prefix = getenv("GCOV_PREFIX");
  if (gcov_prefix && IS_ABSOLUTE_PATH (gcov_prefix))
    {
      /* Check if the level of dirs to strip off specified. */
      char *tmp = getenv("GCOV_PREFIX_STRIP");
      if (tmp)
        {
          gcov_prefix_strip = atoi (tmp);
          /* Do not consider negative values. */
          if (gcov_prefix_strip < 0)
            gcov_prefix_strip = 0;
        }
      
      prefix_length = strlen(gcov_prefix);

      /* Remove an unnecessary trailing '/' */
      if (IS_DIR_SEPARATOR (gcov_prefix[prefix_length - 1]))
	prefix_length--;
    }
  else
    prefix_length = 0;
  
  /* Allocate and initialize the filename scratch space.  */
  gi_filename = (char *) alloca (prefix_length + gcov_max_filename + 1);
  if (prefix_length)
    memcpy (gi_filename, gcov_prefix, prefix_length);
  gi_filename_up = gi_filename + prefix_length;
  
  /* Now merge each file.  */
  for (gi_ptr = gcov_list; gi_ptr; gi_ptr = gi_ptr->next)
    {
      struct gcov_summary this_object;
      struct gcov_summary object, program;
      gcov_type *values[GCOV_COUNTERS];
      const struct gcov_fn_info *fi_ptr;
      unsigned fi_stride;
      unsigned c_ix, f_ix, n_counts;
      struct gcov_ctr_summary *cs_obj, *cs_tobj, *cs_prg, *cs_tprg, *cs_all;
      int error = 0;
      gcov_unsigned_t tag, length;
      gcov_position_t summary_pos = 0;
      gcov_position_t eof_pos = 0;

      memset (&this_object, 0, sizeof (this_object));
      memset (&object, 0, sizeof (object));
      
      /* Build relocated filename, stripping off leading 
         directories from the initial filename if requested. */
      if (gcov_prefix_strip > 0)
        {
          int level = 0;
          const char *fname = gi_ptr->filename;
          const char *s;

          /* Skip selected directory levels. */
	  for (s = fname + 1; (*s != '\0') && (level < gcov_prefix_strip); s++)
	    if (IS_DIR_SEPARATOR(*s))
	      {
		fname = s;
		level++;
	      };

          /* Update complete filename with stripped original. */
          strlcpy (gi_filename_up, fname, gcov_max_filename + 1);
        }
      else
        strlcpy (gi_filename_up, gi_ptr->filename, gcov_max_filename + 1);

      /* Totals for this object file.  */
      ci_ptr = gi_ptr->counts;
      for (t_ix = 0; t_ix < GCOV_COUNTERS_SUMMABLE; t_ix++)
	{
	  if (!((1 << t_ix) & gi_ptr->ctr_mask))
	    continue;

	  cs_ptr = &this_object.ctrs[t_ix];
	  cs_ptr->num += ci_ptr->num;
	  for (c_num = 0; c_num < ci_ptr->num; c_num++)
	    {
	      cs_ptr->sum_all += ci_ptr->values[c_num];
	      if (cs_ptr->run_max < ci_ptr->values[c_num])
		cs_ptr->run_max = ci_ptr->values[c_num];
	    }

	  ci_ptr++;
	}

      c_ix = 0;
      for (t_ix = 0; t_ix < GCOV_COUNTERS; t_ix++)
	if ((1 << t_ix) & gi_ptr->ctr_mask)
	  {
	    values[c_ix] = gi_ptr->counts[c_ix].values;
	    c_ix++;
	  }

      /* Calculate the function_info stride. This depends on the
	 number of counter types being measured.  */
      fi_stride = sizeof (struct gcov_fn_info) + c_ix * sizeof (unsigned);
      if (__alignof__ (struct gcov_fn_info) > sizeof (unsigned))
	{
	  fi_stride += __alignof__ (struct gcov_fn_info) - 1;
	  fi_stride &= ~(__alignof__ (struct gcov_fn_info) - 1);
	}
      
      if (!gcov_open (gi_filename))
	{
#ifdef TARGET_POSIX_IO
	  /* Open failed likely due to missed directory.
	     Create directory and retry to open file. */
          if (create_file_directory (gi_filename))
	    {
	      fprintf (stderr, "profiling:%s:Skip\n", gi_filename);
	      continue;
	    }
#endif
	  if (!gcov_open (gi_filename))
	    {
              fprintf (stderr, "profiling:%s:Cannot open\n", gi_filename);
	      continue;
	    }
	}

      tag = gcov_read_unsigned ();
      if (tag)
	{
	  /* Merge data from file.  */
	  if (tag != GCOV_DATA_MAGIC)
	    {
	      fprintf (stderr, "profiling:%s:Not a gcov data file\n",
		       gi_filename);
	      goto read_fatal;
	    }
	  length = gcov_read_unsigned ();
	  if (!gcov_version (gi_ptr, length, gi_filename))
	    goto read_fatal;

	  length = gcov_read_unsigned ();
	  if (length != gi_ptr->stamp)
	    /* Read from a different compilation. Overwrite the file.  */
	    goto rewrite;
	  
	  /* Merge execution counts for each function.  */
	  for (f_ix = 0; f_ix < gi_ptr->n_functions; f_ix++)
	    {
	      fi_ptr = (const struct gcov_fn_info *)
		      ((const char *) gi_ptr->functions + f_ix * fi_stride);
	      tag = gcov_read_unsigned ();
	      length = gcov_read_unsigned ();

	      /* Check function.  */
	      if (tag != GCOV_TAG_FUNCTION
		  || length != GCOV_TAG_FUNCTION_LENGTH
		  || gcov_read_unsigned () != fi_ptr->ident
		  || gcov_read_unsigned () != fi_ptr->checksum)
		{
		read_mismatch:;
		  fprintf (stderr, "profiling:%s:Merge mismatch for %s\n",
			   gi_filename,
			   f_ix + 1 ? "function" : "summaries");
		  goto read_fatal;
		}

	      c_ix = 0;
	      for (t_ix = 0; t_ix < GCOV_COUNTERS; t_ix++)
		{
		  gcov_merge_fn merge;

		  if (!((1 << t_ix) & gi_ptr->ctr_mask))
		    continue;
		  
		  n_counts = fi_ptr->n_ctrs[c_ix];
		  merge = gi_ptr->counts[c_ix].merge;
		    
		  tag = gcov_read_unsigned ();
		  length = gcov_read_unsigned ();
		  if (tag != GCOV_TAG_FOR_COUNTER (t_ix)
		      || length != GCOV_TAG_COUNTER_LENGTH (n_counts))
		    goto read_mismatch;
		  (*merge) (values[c_ix], n_counts);
		  values[c_ix] += n_counts;
		  c_ix++;
		}
	      if ((error = gcov_is_error ()))
		goto read_error;
	    }

	  f_ix = ~0u;
	  /* Check program & object summary */
	  while (1)
	    {
	      int is_program;
	      
	      eof_pos = gcov_position ();
	      tag = gcov_read_unsigned ();
	      if (!tag)
		break;

	      length = gcov_read_unsigned ();
	      is_program = tag == GCOV_TAG_PROGRAM_SUMMARY;
	      if (length != GCOV_TAG_SUMMARY_LENGTH
		  || (!is_program && tag != GCOV_TAG_OBJECT_SUMMARY))
		goto read_mismatch;
	      gcov_read_summary (is_program ? &program : &object);
	      if ((error = gcov_is_error ()))
		goto read_error;
	      if (is_program && program.checksum == gcov_crc32)
		{
		  summary_pos = eof_pos;
		  goto rewrite;
		}
	    }
	}
      goto rewrite;
      
    read_error:;
      fprintf (stderr, error < 0 ? "profiling:%s:Overflow merging\n"
	       : "profiling:%s:Error merging\n", gi_filename);
	      
    read_fatal:;
      gcov_close ();
      continue;

    rewrite:;
      gcov_rewrite ();
      if (!summary_pos)
	memset (&program, 0, sizeof (program));

      /* Merge the summaries.  */
      f_ix = ~0u;
      for (t_ix = 0; t_ix < GCOV_COUNTERS_SUMMABLE; t_ix++)
	{
	  cs_obj = &object.ctrs[t_ix];
	  cs_tobj = &this_object.ctrs[t_ix];
	  cs_prg = &program.ctrs[t_ix];
	  cs_tprg = &this_program.ctrs[t_ix];
	  cs_all = &all.ctrs[t_ix];

	  if ((1 << t_ix) & gi_ptr->ctr_mask)
	    {
	      if (!cs_obj->runs++)
		cs_obj->num = cs_tobj->num;
	      else if (cs_obj->num != cs_tobj->num)
		goto read_mismatch;
	      cs_obj->sum_all += cs_tobj->sum_all;
	      if (cs_obj->run_max < cs_tobj->run_max)
		cs_obj->run_max = cs_tobj->run_max;
	      cs_obj->sum_max += cs_tobj->run_max;
	      
	      if (!cs_prg->runs++)
		cs_prg->num = cs_tprg->num;
	      else if (cs_prg->num != cs_tprg->num)
		goto read_mismatch;
	      cs_prg->sum_all += cs_tprg->sum_all;
	      if (cs_prg->run_max < cs_tprg->run_max)
		cs_prg->run_max = cs_tprg->run_max;
	      cs_prg->sum_max += cs_tprg->run_max;
	    }
	  else if (cs_obj->num || cs_prg->num)
	    goto read_mismatch;
	  
	  if (!cs_all->runs && cs_prg->runs)
	    memcpy (cs_all, cs_prg, sizeof (*cs_all));
	  else if (!all.checksum
		   && (!GCOV_LOCKED || cs_all->runs == cs_prg->runs)
		   && memcmp (cs_all, cs_prg, sizeof (*cs_all)))
	    {
	      fprintf (stderr, "profiling:%s:Invocation mismatch - some data files may have been removed%s",
		       gi_filename, GCOV_LOCKED
		       ? "" : " or concurrent update without locking support");
	      all.checksum = ~0u;
	    }
	}
      
      c_ix = 0;
      for (t_ix = 0; t_ix < GCOV_COUNTERS; t_ix++)
	if ((1 << t_ix) & gi_ptr->ctr_mask)
	  {
	    values[c_ix] = gi_ptr->counts[c_ix].values;
	    c_ix++;
	  }

      program.checksum = gcov_crc32;
      
      /* Write out the data.  */
      gcov_write_tag_length (GCOV_DATA_MAGIC, GCOV_VERSION);
      gcov_write_unsigned (gi_ptr->stamp);
      
      /* Write execution counts for each function.  */
      for (f_ix = 0; f_ix < gi_ptr->n_functions; f_ix++)
	{
	  fi_ptr = (const struct gcov_fn_info *)
		  ((const char *) gi_ptr->functions + f_ix * fi_stride);

	  /* Announce function.  */
	  gcov_write_tag_length (GCOV_TAG_FUNCTION, GCOV_TAG_FUNCTION_LENGTH);
	  gcov_write_unsigned (fi_ptr->ident);
	  gcov_write_unsigned (fi_ptr->checksum);

	  c_ix = 0;
	  for (t_ix = 0; t_ix < GCOV_COUNTERS; t_ix++)
	    {
	      gcov_type *c_ptr;

	      if (!((1 << t_ix) & gi_ptr->ctr_mask))
		continue;

	      n_counts = fi_ptr->n_ctrs[c_ix];
		    
	      gcov_write_tag_length (GCOV_TAG_FOR_COUNTER (t_ix),
				     GCOV_TAG_COUNTER_LENGTH (n_counts));
	      c_ptr = values[c_ix];
	      while (n_counts--)
		gcov_write_counter (*c_ptr++);

	      values[c_ix] = c_ptr;
	      c_ix++;
	    }
	}

      /* Object file summary.  */
      gcov_write_summary (GCOV_TAG_OBJECT_SUMMARY, &object);

      /* Generate whole program statistics.  */
      if (eof_pos)
	gcov_seek (eof_pos);
      gcov_write_summary (GCOV_TAG_PROGRAM_SUMMARY, &program);
      if (!summary_pos)
	gcov_write_unsigned (0);
      if ((error = gcov_close ()))
	  fprintf (stderr, error  < 0 ?
		   "profiling:%s:Overflow writing\n" :
		   "profiling:%s:Error writing\n",
		   gi_filename);
    }
}

/* Add a new object file onto the bb chain.  Invoked automatically
   when running an object file's global ctors.  */

void
__gcov_init (struct gcov_info *info)
{
  if (!info->version)
    return;
  if (gcov_version (info, info->version, 0))
    {
      const char *ptr = info->filename;
      gcov_unsigned_t crc32 = gcov_crc32;
      size_t filename_length =  strlen(info->filename);

      /* Refresh the longest file name information */
      if (filename_length > gcov_max_filename)
        gcov_max_filename = filename_length;
      
      do
	{
	  unsigned ix;
	  gcov_unsigned_t value = *ptr << 24;

	  for (ix = 8; ix--; value <<= 1)
	    {
	      gcov_unsigned_t feedback;

	      feedback = (value ^ crc32) & 0x80000000 ? 0x04c11db7 : 0;
	      crc32 <<= 1;
	      crc32 ^= feedback;
	    }
	}
      while (*ptr++);
      
      gcov_crc32 = crc32;
      
      if (!gcov_list)
	atexit (gcov_exit);
      
      info->next = gcov_list;
      gcov_list = info;
    }
  info->version = 0;
}

/* Called before fork or exec - write out profile information gathered so
   far and reset it to zero.  This avoids duplication or loss of the
   profile information gathered so far.  */

void
__gcov_flush (void)
{
  const struct gcov_info *gi_ptr;

  gcov_exit ();
  for (gi_ptr = gcov_list; gi_ptr; gi_ptr = gi_ptr->next)
    {
      unsigned t_ix;
      const struct gcov_ctr_info *ci_ptr;
      
      for (t_ix = 0, ci_ptr = gi_ptr->counts; t_ix != GCOV_COUNTERS; t_ix++)
	if ((1 << t_ix) & gi_ptr->ctr_mask)
	  {
	    memset (ci_ptr->values, 0, sizeof (gcov_type) * ci_ptr->num);
	    ci_ptr++;
	  }
    }
}

#endif /* L_gcov */

#ifdef L_gcov_merge_add
/* The profile merging function that just adds the counters.  It is given
   an array COUNTERS of N_COUNTERS old counters and it reads the same number
   of counters from the gcov file.  */
void
__gcov_merge_add (gcov_type *counters, unsigned n_counters)
{
  for (; n_counters; counters++, n_counters--)
    *counters += gcov_read_counter ();
}
#endif /* L_gcov_merge_add */

#ifdef L_gcov_merge_single
/* The profile merging function for choosing the most common value.
   It is given an array COUNTERS of N_COUNTERS old counters and it
   reads the same number of counters from the gcov file.  The counters
   are split into 3-tuples where the members of the tuple have
   meanings:
   
   -- the stored candidate on the most common value of the measured entity
   -- counter
   -- total number of evaluations of the value  */
void
__gcov_merge_single (gcov_type *counters, unsigned n_counters)
{
  unsigned i, n_measures;
  gcov_type value, counter, all;

  gcc_assert (!(n_counters % 3));
  n_measures = n_counters / 3;
  for (i = 0; i < n_measures; i++, counters += 3)
    {
      value = gcov_read_counter ();
      counter = gcov_read_counter ();
      all = gcov_read_counter ();

      if (counters[0] == value)
	counters[1] += counter;
      else if (counter > counters[1])
	{
	  counters[0] = value;
	  counters[1] = counter - counters[1];
	}
      else
	counters[1] -= counter;
      counters[2] += all;
    }
}
#endif /* L_gcov_merge_single */

#ifdef L_gcov_merge_delta
/* The profile merging function for choosing the most common
   difference between two consecutive evaluations of the value.  It is
   given an array COUNTERS of N_COUNTERS old counters and it reads the
   same number of counters from the gcov file.  The counters are split
   into 4-tuples where the members of the tuple have meanings:
   
   -- the last value of the measured entity
   -- the stored candidate on the most common difference
   -- counter
   -- total number of evaluations of the value  */
void
__gcov_merge_delta (gcov_type *counters, unsigned n_counters)
{
  unsigned i, n_measures;
  gcov_type last, value, counter, all;

  gcc_assert (!(n_counters % 4));
  n_measures = n_counters / 4;
  for (i = 0; i < n_measures; i++, counters += 4)
    {
      last = gcov_read_counter ();
      value = gcov_read_counter ();
      counter = gcov_read_counter ();
      all = gcov_read_counter ();

      if (counters[1] == value)
	counters[2] += counter;
      else if (counter > counters[2])
	{
	  counters[1] = value;
	  counters[2] = counter - counters[2];
	}
      else
	counters[2] -= counter;
      counters[3] += all;
    }
}
#endif /* L_gcov_merge_delta */

#ifdef L_gcov_interval_profiler
/* If VALUE is in interval <START, START + STEPS - 1>, then increases the
   corresponding counter in COUNTERS.  If the VALUE is above or below
   the interval, COUNTERS[STEPS] or COUNTERS[STEPS + 1] is increased
   instead.  */

void
__gcov_interval_profiler (gcov_type *counters, gcov_type value,
			  int start, unsigned steps)
{
  gcov_type delta = value - start;
  if (delta < 0)
    counters[steps + 1]++;
  else if (delta >= steps)
    counters[steps]++;
  else
    counters[delta]++;
}
#endif

#ifdef L_gcov_pow2_profiler
/* If VALUE is a power of two, COUNTERS[1] is incremented.  Otherwise
   COUNTERS[0] is incremented.  */

void
__gcov_pow2_profiler (gcov_type *counters, gcov_type value)
{
  if (value & (value - 1))
    counters[0]++;
  else
    counters[1]++;
}
#endif

#ifdef L_gcov_one_value_profiler
/* Tries to determine the most common value among its inputs.  Checks if the
   value stored in COUNTERS[0] matches VALUE.  If this is the case, COUNTERS[1]
   is incremented.  If this is not the case and COUNTERS[1] is not zero,
   COUNTERS[1] is decremented.  Otherwise COUNTERS[1] is set to one and
   VALUE is stored to COUNTERS[0].  This algorithm guarantees that if this
   function is called more than 50% of the time with one value, this value
   will be in COUNTERS[0] in the end.

   In any case, COUNTERS[2] is incremented.  */

void
__gcov_one_value_profiler (gcov_type *counters, gcov_type value)
{
  if (value == counters[0])
    counters[1]++;
  else if (counters[1] == 0)
    {
      counters[1] = 1;
      counters[0] = value;
    }
  else
    counters[1]--;
  counters[2]++;
}
#endif

#ifdef L_gcov_fork
/* A wrapper for the fork function.  Flushes the accumulated profiling data, so
   that they are not counted twice.  */

pid_t
__gcov_fork (void)
{
  __gcov_flush ();
  return fork ();
}
#endif

#ifdef L_gcov_execl
/* A wrapper for the execl function.  Flushes the accumulated profiling data, so
   that they are not lost.  */

int
__gcov_execl (const char *path, const char *arg, ...)
{
  va_list ap, aq;
  unsigned i, length;
  char **args;

  __gcov_flush ();

  va_start (ap, arg);
  va_copy (aq, ap);

  length = 2;
  while (va_arg (ap, char *))
    length++;
  va_end (ap);

  args = (char **) alloca (length * sizeof (void *));
  args[0] = (char *) arg;
  for (i = 1; i < length; i++)
    args[i] = va_arg (aq, char *);
  va_end (aq);

  return execv (path, args);
}
#endif

#ifdef L_gcov_execlp
/* A wrapper for the execlp function.  Flushes the accumulated profiling data, so
   that they are not lost.  */

int
__gcov_execlp (const char *path, const char *arg, ...)
{
  va_list ap, aq;
  unsigned i, length;
  char **args;

  __gcov_flush ();

  va_start (ap, arg);
  va_copy (aq, ap);

  length = 2;
  while (va_arg (ap, char *))
    length++;
  va_end (ap);

  args = (char **) alloca (length * sizeof (void *));
  args[0] = (char *) arg;
  for (i = 1; i < length; i++)
    args[i] = va_arg (aq, char *);
  va_end (aq);

  return execvp (path, args);
}
#endif

#ifdef L_gcov_execle
/* A wrapper for the execle function.  Flushes the accumulated profiling data, so
   that they are not lost.  */

int
__gcov_execle (const char *path, const char *arg, ...)
{
  va_list ap, aq;
  unsigned i, length;
  char **args;
  char **envp;

  __gcov_flush ();

  va_start (ap, arg);
  va_copy (aq, ap);

  length = 2;
  while (va_arg (ap, char *))
    length++;
  va_end (ap);

  args = (char **) alloca (length * sizeof (void *));
  args[0] = (char *) arg;
  for (i = 1; i < length; i++)
    args[i] = va_arg (aq, char *);
  envp = va_arg (aq, char **);
  va_end (aq);

  return execve (path, args, envp);
}
#endif

#ifdef L_gcov_execv
/* A wrapper for the execv function.  Flushes the accumulated profiling data, so
   that they are not lost.  */

int
__gcov_execv (const char *path, char *const argv[])
{
  __gcov_flush ();
  return execv (path, argv);
}
#endif

#ifdef L_gcov_execvp
/* A wrapper for the execvp function.  Flushes the accumulated profiling data, so
   that they are not lost.  */

int
__gcov_execvp (const char *path, char *const argv[])
{
  __gcov_flush ();
  return execvp (path, argv);
}
#endif

#ifdef L_gcov_execve
/* A wrapper for the execve function.  Flushes the accumulated profiling data, so
   that they are not lost.  */

int
__gcov_execve (const char *path, char *const argv[], char *const envp[])
{
  __gcov_flush ();
  return execve (path, argv, envp);
}
#endif
#endif /* inhibit_libc */
