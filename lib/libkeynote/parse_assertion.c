/* $OpenBSD: parse_assertion.c,v 1.17 2022/12/27 17:10:06 jmc Exp $ */
/*
 * The author of this code is Angelos D. Keromytis (angelos@dsl.cis.upenn.edu)
 *
 * This code was written by Angelos D. Keromytis in Philadelphia, PA, USA,
 * in April-May 1998
 *
 * Copyright (C) 1998, 1999 by Angelos D. Keromytis.
 *	
 * Permission to use, copy, and modify this software with or without fee
 * is hereby granted, provided that this entire notice is included in
 * all copies of any software which is or includes a copy or
 * modification of this software. 
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTY. IN PARTICULAR, THE AUTHORS MAKES NO
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE
 * MERCHANTABILITY OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR
 * PURPOSE.
 */

#include <sys/types.h>

#include <ctype.h>
#include <limits.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "keynote.h"
#include "assertion.h"
#include "signature.h"

/*
 * Recurse on graph discovery.
 */
static int
rec_evaluate_query(struct assertion *as)
{
    struct assertion *ast;
    struct keylist *kl;
    int i, s;

    as->as_kresult = KRESULT_IN_PROGRESS;

    /*
     * If we get the minimum result or an error from evaluating this
     * assertion, we don't need to recurse. 
     */
    keynote_evaluate_assertion(as);
    if (keynote_errno != 0)
    {
	as->as_kresult = KRESULT_DONE;
	if (keynote_errno)
	  as->as_error = keynote_errno;
	if (keynote_errno == ERROR_MEMORY)
	  return -1;
	else
	{
	    keynote_errno = 0;  /* Ignore syntax errors for now */
	    return 0;
	}
    }

    if (as->as_result == 0)
    {
        as->as_kresult = KRESULT_DONE;
        return as->as_result;
    }

    for (kl = as->as_keylist;
	 kl != NULL;
	 kl = kl->key_next)
    {
	switch (keynote_in_action_authorizers(kl->key_key, kl->key_alg))
	{
	    case -1:
		as->as_kresult = KRESULT_DONE;
		if (keynote_errno == ERROR_MEMORY)
		{
		    as->as_error = ERROR_MEMORY;
		    return -1;
		}
		else
		{
		    keynote_errno = 0; /* Reset */
		    continue;
		}

	    case RESULT_FALSE:    /* Not there, check for assertions instead */
		break;

	    case RESULT_TRUE:     /* Ok, don't bother with assertions */
		keynote_current_assertion = NULL;
		continue;
	}

	for (i = 0;; i++)
	{
	    ast = keynote_find_assertion(kl->key_key, i, kl->key_alg);
	    if (ast == NULL)
	      break;

	    if (ast->as_kresult == KRESULT_IN_PROGRESS) /* Cycle detected */
	      continue;

	    if (ast->as_kresult == KRESULT_UNTOUCHED)   /* Recurse if needed */
	      rec_evaluate_query(ast);

	    /* Check for errors */
	    if (keynote_errno == ERROR_MEMORY)
	    {
		as->as_error = ERROR_MEMORY;
		as->as_kresult = KRESULT_DONE;
		return -1;
	    }
	    else
	      keynote_errno = 0; /* Reset */
	}
    }

    keynote_current_assertion = as;
    s = keynote_parse_keypred(as, 0);
    keynote_current_assertion = NULL;

    if (keynote_errno == ERROR_MEMORY)
    {
	as->as_error = ERROR_MEMORY;
	as->as_kresult = KRESULT_DONE;
	return -1;
    }
    else
      if (keynote_errno)
      {
	  keynote_errno = 0;
	  s = 0;
      }

    /* Keep lower of two */
    as->as_result = (as->as_result < s ? as->as_result : s);

    /* Check the signature now if we haven't done so already */
    if (as->as_sigresult == SIGRESULT_UNTOUCHED)
    {
	if (!(as->as_flags & ASSERT_FLAG_LOCAL))
	  as->as_sigresult = keynote_sigverify_assertion(as);
	else
	  as->as_sigresult = SIGRESULT_TRUE;    /* Trusted assertion */
    }

    if (as->as_sigresult != SIGRESULT_TRUE)
    {
	as->as_result = 0;
	as->as_sigresult = SIGRESULT_FALSE;
	if (keynote_errno != ERROR_MEMORY)
	  keynote_errno = 0; /* Reset */
	else
	{
	    as->as_error = ERROR_MEMORY;
	    as->as_kresult = KRESULT_DONE;
	    return -1;
	}
    }

    as->as_kresult = KRESULT_DONE;
    return as->as_result;
}

/*
 * Fix the Authorizer/Licencees/Signature fields. If the first argument is
 * empty, fix all assertions. The second argument specifies whether the
 * Signature field should be parsed or not.
 */
static int
keynote_fix_fields(struct assertion *ast, int sigfield)
{
    struct assertion *as;
    int i;
 
    /* Signature generation/verification handling, no need to eval Licensees */
    if (ast != NULL)
    {
	/* Authorizer */
	if (keynote_evaluate_authorizer(ast, 1) != RESULT_TRUE)
	  return -1;

	/* Signature */
	if ((sigfield) && (ast->as_signature_string_s != NULL))
	  if (keynote_evaluate_authorizer(ast, 0) != RESULT_TRUE)
	    return -1;

	return RESULT_TRUE;
    }
    
    for (i = 0; i < HASHTABLESIZE; i++)
      for (as = keynote_current_session->ks_assertion_table[i];
	   as != NULL;
	   as = as->as_next)
      {
	  if (!(as->as_internalflags & ASSERT_IFLAG_NEEDPROC) &&
	      !(as->as_internalflags & ASSERT_IFLAG_WEIRDLICS) &&
	      !(as->as_internalflags & ASSERT_IFLAG_WEIRDAUTH) &&
	      !(as->as_internalflags & ASSERT_IFLAG_WEIRDSIG))
	    continue;
	  
	  /* Parse the Signature field */
	  if (((as->as_internalflags & ASSERT_IFLAG_WEIRDSIG) ||
	       (as->as_internalflags & ASSERT_IFLAG_NEEDPROC)) &&
	      (as->as_signature_string_s != NULL))
	    if (keynote_evaluate_authorizer(as, 0) == -1)
	    {
		if (keynote_errno)
		  as->as_error = keynote_errno;
		if (keynote_errno == ERROR_MEMORY)
		  return -1;
		else
		  keynote_errno = 0;
	    }
	  
	  /* Parse the Licensees field */
	  if ((as->as_internalflags & ASSERT_IFLAG_WEIRDLICS) ||
	      (as->as_internalflags & ASSERT_IFLAG_NEEDPROC))
	    if (keynote_parse_keypred(as, 1) == -1)
	    {
		if (keynote_errno)
		    as->as_error = keynote_errno;
		if (keynote_errno == ERROR_MEMORY)
		  return -1;
		else
		  keynote_errno = 0;
	    }

	  /* Parse the Authorizer field */
	  if ((as->as_internalflags & ASSERT_IFLAG_WEIRDAUTH) ||
	      (as->as_internalflags & ASSERT_IFLAG_NEEDPROC))
	    if (keynote_evaluate_authorizer(as, 1) == -1)
	    {
		if (keynote_errno)
		  as->as_error = keynote_errno;
		if (keynote_errno == ERROR_MEMORY)
		  return -1;
		else
		  keynote_errno = 0;
	    }
      }

    /* Reposition if necessary */
    for (i = 0; i < HASHTABLESIZE; i++)
      for (as = keynote_current_session->ks_assertion_table[i];
	   as != NULL;
	   as = as->as_next)
	if (((as->as_internalflags & ASSERT_IFLAG_WEIRDAUTH) &&
	     !(as->as_internalflags & ASSERT_IFLAG_PROCESSED)) ||
	    (as->as_internalflags & ASSERT_IFLAG_NEEDPROC))
	{
	    as->as_internalflags &= ~ASSERT_IFLAG_NEEDPROC;
	    as->as_internalflags |= ASSERT_IFLAG_PROCESSED;
	    keynote_sremove_assertion(keynote_current_session->ks_id,
				      as->as_id);

	    if (keynote_add_htable(as, 1) != RESULT_TRUE)
	      return -1;

	    /* Point to beginning of the previous list. */
	    i--;
	    break;
	}

    return RESULT_TRUE;
}

/*
 * Find the trust graph. This is a depth-first search, starting at
 * POLICY assertions.
 */
int
keynote_evaluate_query(void)
{
    struct assertion *as;
    int p, prev;
    int i;

    /* Fix the authorizer/licensees/signature fields */
    if (keynote_fix_fields(NULL, 0) != RESULT_TRUE)
      return -1;

    /* Find POLICY assertions and try to evaluate the query. */
    for (i = 0, prev = 0; i < HASHTABLESIZE; i++)
      for (as = keynote_current_session->ks_assertion_table[i];
	   as != NULL;
	   as = as->as_next)
	if ((as->as_authorizer != NULL) &&      /* Paranoid */
            (as->as_signeralgorithm == KEYNOTE_ALGORITHM_NONE))
	  if ((!strcmp("POLICY", as->as_authorizer)) &&
	      (as->as_flags & ASSERT_FLAG_LOCAL))
	  {
	      if ((p = rec_evaluate_query(as)) == -1)
	      {
		  if (keynote_errno)
		    as->as_error = keynote_errno;
		  if (keynote_errno == ERROR_MEMORY)
		    return -1;
		  else
		  {
		      keynote_errno = 0;
		      continue;
		  }
	      }

	      if (p > prev)
		prev = p;

	      /* If we get the highest possible return value, just return */
	      if (prev == (keynote_current_session->ks_values_num - 1))
		return prev;
	  }
    
    return prev;
}

/*
 * Return keyword type.
 */
static int
whichkeyword(char *start, char *end)
{
    int len = (end - start);

    if (len <= 0)
    {
	keynote_errno = ERROR_MEMORY;
	return -1;
    }

    if (!strncasecmp("keynote-version:", start, len))
      return KEYWORD_VERSION;

    if (!strncasecmp("local-constants:", start, len))
      return KEYWORD_LOCALINIT;

    if (!strncasecmp("authorizer:", start, len))
      return KEYWORD_AUTHORIZER;

    if (!strncasecmp("licensees:", start, len))
      return KEYWORD_LICENSEES;

    if (!strncasecmp("conditions:", start, len))
      return KEYWORD_CONDITIONS;

    if (!strncasecmp("signature:", start, len))
      return KEYWORD_SIGNATURE;

    if (!strncasecmp("comment:", start, len))
      return KEYWORD_COMMENT;

    keynote_errno = ERROR_SYNTAX;
    return -1;
}

/* 
 * Parse an assertion. Set keynote_errno to ERROR_SYNTAX if parsing
 * failed due to certificate badness, and ERROR_MEMORY if memory
 * problem. If more than one assertions have been passed in the
 * buffer, they will be linked.
 */
struct assertion *
keynote_parse_assertion(char *buf, int len, int assertion_flags)
{
    int k, i, j, seen_field = 0, ver = 0, end_of_assertion = 0;
    char *ks, *ke, *ts, *te = NULL;
    struct assertion *as;

    /* Allocate memory for assertion */
    as = calloc(1, sizeof(struct assertion));
    if (as == NULL)
    {
	keynote_errno = ERROR_MEMORY;
	return NULL;
    }

    /* Keep a copy of the assertion around */
    as->as_buf = strdup(buf);
    if (as->as_buf == NULL)
    {
	keynote_errno = ERROR_MEMORY;
	keynote_free_assertion(as);
	return NULL;
    }

    as->as_flags = assertion_flags & ~(ASSERT_FLAG_SIGGEN |
				       ASSERT_FLAG_SIGVER);

    /* Skip any leading whitespace */
    for (i = 0, j = len; i < j && isspace((unsigned char)as->as_buf[i]); i++)
     ;

    /* Keyword must start at beginning of buffer or line */
    if ((i >= j) || ((i != 0) && (as->as_buf[i - 1] != '\n')))
    {
	keynote_free_assertion(as);
	keynote_errno = ERROR_SYNTAX;
	return NULL;
    }

    while (i < j)			/* Decomposition loop */
    {
	ks = as->as_buf + i;

	/* Mark beginning of assertion for signature purposes */
	if (as->as_startofsignature == NULL)
	  as->as_startofsignature = ks;

	/* This catches comments at the beginning of an assertion only */
	if (as->as_buf[i] == '#')	/* Comment */
	{
	    seen_field = 1;

   	    /* Skip until the end of line */
	    while ((i< j) && as->as_buf[++i] != '\n')
	      ;

	    i++;
	    continue;  /* Loop */
	}

	/* Advance until we find a keyword separator */
	for (; (as->as_buf[i] != ':') && (i < j); i++)
	  ;

	if (i + 1 > j)
	{
	    keynote_free_assertion(as);
	    keynote_errno = ERROR_SYNTAX;
	    return NULL;
	}

	/* ks points at beginning of keyword, ke points at end */
	ke = as->as_buf + i;

	/* ts points at beginning of value field */
	ts = as->as_buf + i + 1;	/* Skip ':' */

	/*
	 * Find the end of the field -- means end of buffer,
	 * a newline followed by a non-whitespace character,
	 * or two newlines.
	 */
	while (++i <= j)
	{
	    /* If end of buffer, we're at the end of the field */
	    if (i == j)
	    {
		end_of_assertion = 1;
		te = as->as_buf + i;
		break;
	    }

	    /* If two newlines, end of assertion */
	    if ((as->as_buf[i] == '\n') && (i + 1 < j) && 
		(as->as_buf[i + 1] == '\n'))
	    {
		end_of_assertion = 1;
		te = as->as_buf + i;
		break;
	    }

	    /* If newline followed by non-whitespace or comment character */
	    if ((as->as_buf[i] == '\n') && 
		(!isspace((unsigned char)as->as_buf[i + 1])) &&
                (as->as_buf[i + 1] != '#'))
	    {
	        te = as->as_buf + i;
	        break;
	    }
	}

	i++;

	/* 
	 * On each of the cases (except the first), we check that:
	 *  - we've already seen a keynote-version field (and that
	 *    it's the first one that appears in the assertion)
	 *  - the signature field, if present, is the last one
	 *  - no field appears more than once
	 */
	switch (whichkeyword(ks, ke))
	{
	    case -1:
		keynote_free_assertion(as);
		return NULL;

	    case KEYWORD_VERSION:
		if ((ver == 1) || (seen_field == 1))
		{
		    keynote_free_assertion(as);
		    keynote_errno = ERROR_SYNTAX;
		    return NULL;
		}

		/* Test for version correctness */
		keynote_get_envlist(ts, te, 1);
		if (keynote_errno != 0)
		{
		    keynote_free_assertion(as);
		    return NULL;
		}

		ver = 1;
		break;

	    case KEYWORD_LOCALINIT:
		if (as->as_env != NULL)
		{
		    keynote_free_assertion(as);
		    keynote_errno = ERROR_SYNTAX;
		    return NULL;
		}

		as->as_env = keynote_get_envlist(ts, te, 0);
		if (keynote_errno != 0)
		{
		    keynote_free_assertion(as);
		    return NULL;
		}
		break;

	    case KEYWORD_AUTHORIZER:
		if (as->as_authorizer_string_s != NULL)
		{
		    keynote_free_assertion(as);
		    keynote_errno = ERROR_SYNTAX;
		    return NULL;
		}

		as->as_authorizer_string_s = ts;
		as->as_authorizer_string_e = te;
		break;

	    case KEYWORD_LICENSEES:
		if (as->as_keypred_s != NULL)
		{
		    keynote_free_assertion(as);
		    keynote_errno = ERROR_SYNTAX;
		    return NULL;
		}

		as->as_keypred_s = ts;
		as->as_keypred_e = te;
		break;

	    case KEYWORD_CONDITIONS:
		if (as->as_conditions_s != NULL)
		{
		    keynote_free_assertion(as);
		    keynote_errno = ERROR_SYNTAX;
		    return NULL;
		}

		as->as_conditions_s = ts;
		as->as_conditions_e = te;
		break;

	    case KEYWORD_SIGNATURE:
		if (as->as_signature_string_s != NULL)
		{
		    keynote_free_assertion(as);
		    keynote_errno = ERROR_SYNTAX;
		    return NULL;
		}

		end_of_assertion = 1;
		as->as_allbutsignature = ks;
		as->as_signature_string_s = ts;
		as->as_signature_string_e = te;
		break;

	    case KEYWORD_COMMENT:
		if (as->as_comment_s != NULL)
		{
		    keynote_free_assertion(as);
		    keynote_errno = ERROR_SYNTAX;
		    return NULL;
		}

		as->as_comment_s = ts;
		as->as_comment_e = te;
		break;
	}

	seen_field = 1;
	if (end_of_assertion == 1)
	{
	    /* End of buffer, good termination */
	    if ((te == as->as_buf + len) || (te + 1 == as->as_buf + len) ||
		(*(te) == '\0') || (*(te + 1) == '\0'))
	      break;

	    /* Check whether there's something else following */
	    for (k = 1; te + k < as->as_buf + len && *(te + k) != '\n'; k++)   
	      if (!isspace((unsigned char)*(te + k)))
	      {
		  keynote_free_assertion(as);
		  keynote_errno = ERROR_SYNTAX;
		  return NULL;
	      }

	    break; /* Assertion is "properly" terminated */
	}
    }

    /* Check that the basic fields are there */
    if (as->as_authorizer_string_s == NULL)
    {
	keynote_free_assertion(as);
	keynote_errno = ERROR_SYNTAX;
	return NULL;
    }

    /* Signature generation/verification handling */
    if (assertion_flags & ASSERT_FLAG_SIGGEN)
    {
        if (keynote_fix_fields(as, 0) != RESULT_TRUE)
        {
	    keynote_free_assertion(as);
	    return NULL;
        }
    }
    else
      if (assertion_flags & ASSERT_FLAG_SIGVER)
	if (keynote_fix_fields(as, 1) != RESULT_TRUE)
	{
	    keynote_free_assertion(as);
	    return NULL;
	}

    return as;
}
