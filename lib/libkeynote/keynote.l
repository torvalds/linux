%{
/* $OpenBSD: keynote.l,v 1.24 2017/08/28 17:07:19 millert Exp $ */
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

#include <sys/time.h>
#include <sys/types.h>

#include <ctype.h>
#include <regex.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "k.tab.h"
#include "keynote.h"
#include "assertion.h"

static void mystrncpy(char *, char *, int);

struct lex_list
{
    int   lex_type;
    void *lex_s;
};

static struct lex_list *keynote_lex_list = NULL;
static int    keynote_max_lex_list = 32;
static int    keynote_lex_counter = 0;
static int    first_tok = 0;
%}
digit		[0-9]
specnumber      [1-9][0-9]*
number		{digit}+
flt		{digit}+"."{digit}+
vstring		[a-zA-Z_][a-zA-Z0-9_]*
litstring	\"(((\\\n)|(\\.)|([^\\\n\"]))*)\"
variable	{vstring}
comment		"#"[^\n]*
%s ACTIONSTRING LOCALINIT KEYPREDICATE SIGNERINIT KEYNOTEVERSION
%pointer
%option noinput noyywrap never-interactive yylineno
%%
%{
    /*
     * Return a preset token, so we can have more than one grammars
     * in yacc.
     */
    extern int first_tok;
    
    if (first_tok)
    {
	int holdtok = first_tok;
	
	first_tok = 0;
	return holdtok;
    }
%}

<KEYPREDICATE>{specnumber}"-of"		{
					  knlval.intval = atoi(kntext);
					  return KOF;
					}
<ACTIONSTRING,KEYPREDICATE>"("		return OPENPAREN;
<ACTIONSTRING,KEYPREDICATE>")"		return CLOSEPAREN;
<ACTIONSTRING,KEYPREDICATE>"&&"		return AND;
<ACTIONSTRING,KEYPREDICATE>"||"		return OR;
<ACTIONSTRING>"+"			return PLUS;
<ACTIONSTRING>"->"              return HINT;
<ACTIONSTRING>"{"               return OPENBLOCK;
<ACTIONSTRING>"}"               return CLOSEBLOCK;
<ACTIONSTRING>";"               return SEMICOLON;
<ACTIONSTRING>"!"		return NOT;
<ACTIONSTRING>"~="		return REGEXP;
<ACTIONSTRING>"=="		return EQ;
<ACTIONSTRING>"!="		return NE;
<ACTIONSTRING>"<"		return LT;
<ACTIONSTRING>">"		return GT;
<ACTIONSTRING>"<="		return LE;
<ACTIONSTRING>">="		return GE;
<ACTIONSTRING>"-"		return MINUS;
<ACTIONSTRING>"*"		return MULT;
<ACTIONSTRING>"/"		return DIV;
<ACTIONSTRING>"%"		return MOD;
<ACTIONSTRING>"^"		return EXP;
"."		                return DOTT;
<ACTIONSTRING>"true"            return TRUE;
<ACTIONSTRING>"false"           return FALSE;
{comment}		/* eat up comments */
<LOCALINIT>"="	                return EQQ;
<KEYPREDICATE>"," 		return COMMA;
<ACTIONSTRING,KEYPREDICATE,SIGNERINIT,LOCALINIT>{variable} {
					int len;
                                        if (keynote_exceptionflag ||
					    keynote_donteval)
					{
					    knlval.string = NULL;
					    return VARIABLE;
					}

					len = strlen(kntext) + 1;
					knlval.string = calloc(len, sizeof(char));
                                        if (knlval.string == NULL)
					{
					    keynote_errno = ERROR_MEMORY;
					    return -1;
					}
		  	          	strlcpy(knlval.string, kntext, len);
					if (keynote_lex_add(knlval.string,
							    LEXTYPE_CHAR) ==
					    -1)
					  return -1;
		  	         	return VARIABLE;
			      	      }
"$"                             return DEREF;
<ACTIONSTRING>"@"	    	return OPENNUM;
<ACTIONSTRING>"&"	    	return OPENFLT;
<ACTIONSTRING>{flt}	      {
				knlval.doubval = atof(kntext);
				return FLOAT;
			      }
<KEYNOTEVERSION>{number}      {
				int len;

                                if (keynote_exceptionflag ||
				    keynote_donteval)
				{
				    knlval.string = NULL;
				    return STRING;
				}

				len = strlen(kntext) + 1;
				knlval.string = calloc(len, sizeof(char));
                                if (knlval.string == NULL)
				{
				    keynote_errno = ERROR_MEMORY;
				    return -1;
				}
		  	        strlcpy(knlval.string, kntext, len);
				if (keynote_lex_add(knlval.string,
						    LEXTYPE_CHAR) == -1)
				  return -1;
		  	        return STRING;
			      }
<ACTIONSTRING>{number}	      {
				knlval.intval = atoi(kntext);
				return NUM;
			      }
{litstring}                   {
                                if (keynote_exceptionflag ||
				    keynote_donteval)
				{
				    knlval.string = NULL;
				    return STRING;
				}
    
				knlval.string = calloc(strlen(kntext) - 1,
						       sizeof(char));
                                if (knlval.string == NULL)
				{
				    keynote_errno = ERROR_MEMORY;
				    return -1;
				}

				mystrncpy(knlval.string, kntext + 1, 
					  strlen(kntext) - 2);

				if (keynote_lex_add(knlval.string,
						    LEXTYPE_CHAR) == -1)
				  return -1;
				return STRING;
                              }
[ \t\n]
.                             { keynote_errno = ERROR_SYNTAX; 
                                return -1;
				REJECT; /* Avoid -Wall warning. Not reached */
                              }
%%

/*
 * Zap everything.
 */
static void
keynote_lex_zap(void)
{
    int i;
    
    if (keynote_lex_counter == 0)
      return;

    for (i = 0; i < keynote_max_lex_list; i++)
      if (keynote_lex_list[i].lex_s != NULL)
      {
	  switch (keynote_lex_list[i].lex_type)
	  {
	      case LEXTYPE_CHAR:
	          free(keynote_lex_list[i].lex_s);
		  break;
	  }

	  keynote_lex_counter--;
	  keynote_lex_list[i].lex_s = NULL;
	  keynote_lex_list[i].lex_type = 0;
      }
}
	
/*
 * Initialize.
 */
static int
keynote_lex_init(void)
{
    if (keynote_lex_list != NULL)
      memset(keynote_lex_list, 0,
	     keynote_max_lex_list * sizeof(struct lex_list));
    else
    {
	keynote_lex_list = calloc(keynote_max_lex_list,
					              sizeof(struct lex_list));
        if (keynote_lex_list == NULL)
	{
	    keynote_errno = ERROR_MEMORY;
	    return -1;
	}
    }

    return RESULT_TRUE;
}

/*
 * Add the string in a list of allocated but "dangling" memory references.
 * If out of memory, free the string and return -1 (and set keynote_errno).
 */
int
keynote_lex_add(void *s, int type)
{
    struct lex_list *p;
    int i;

    if (s == NULL)
      return RESULT_TRUE;

    for (i = 0; i < keynote_max_lex_list; i++)
      if (keynote_lex_list[i].lex_s == NULL)
      {
    	  keynote_lex_list[i].lex_s = (void *) s;
	  keynote_lex_list[i].lex_type = type;
	  keynote_lex_counter++;
     	  return RESULT_TRUE;
      }

    /* Not enough space, increase the size of the array */
    keynote_max_lex_list *= 2;

    p = (struct lex_list *) reallocarray(keynote_lex_list,
					 keynote_max_lex_list,
					 sizeof(struct lex_list));
    if (p == NULL)
    {
	switch (type)
	{
	    case LEXTYPE_CHAR:
	        free(s);
		break;
	}

        keynote_max_lex_list /= 2;
	keynote_errno = ERROR_MEMORY;
        return -1;
    }

    keynote_lex_list = p;
    keynote_lex_list[i].lex_s = s;
    keynote_lex_list[i++].lex_type = type;
    keynote_lex_counter++;

    /* Zero out the rest */
    memset(&(keynote_lex_list[i]), 0,
	   (keynote_max_lex_list - i) * sizeof(struct lex_list));

    return RESULT_TRUE;
}

/*
 * Remove string.
 */
void
keynote_lex_remove(void *s)
{
    int i;

    for (i = 0; i < keynote_max_lex_list; i++)
      if (keynote_lex_list[i].lex_s == s)
      {
	  memset(&(keynote_lex_list[i]), 0, sizeof(struct lex_list));
	  keynote_lex_counter--;
	  return;
      }
}

/*
 * Return RESULT_TRUE if character is octal digit, RESULT_FALSE otherwise.
 */
static int
is_octal(char c)
{
    switch (c)
    {
	case '0': case '1': case '2': case '3':
	case '4': case '5': case '6': case '7':
	    return RESULT_TRUE;

	default:
	    return RESULT_FALSE;
    }
}

/*
 * Return octal value (non-zero) if argument starts with such a
 * representation, otherwise 0.
 */
static unsigned char
get_octal(char *s, int len, int *adv)
{
    unsigned char res = 0;

    if (*s == '0')
    {
	if (len > 0)
	{
	    if (is_octal(*(s + 1)))
	    {
		res = *(s + 1) - '0';
		*adv = 2;

		if (is_octal(*(s + 2)) && (len - 1 > 0))
		{
		    res = res * 8 + (*(s + 2) - '0');
		    *adv = 3;
		}
	    }
	}
    }
    else
      if (is_octal(*s) && (len - 1 > 0))  /* Non-zero leading */
      {
	  if (is_octal(*(s + 1)) &&
	      is_octal(*(s + 2)))
	  {
	      *adv = 3;
	      res = (((*s) - '0') * 64) +
		    (((*(s + 1)) - '0') * 8) +
		    ((*(s + 2)) - '0');
	  }
      }

    return res;
}

/*
 * Copy at most len characters to string s1 from string s2, taking
 * care of escaped characters in the process. String s1 is assumed
 * to have enough space, and be zero'ed.
 */
static void
mystrncpy(char *s1, char *s2, int len)
{
    unsigned char c;
    int advance;

    if (len == 0)
      return;

    while (len-- > 0)
    {
        if (*s2 == '\\')
	{
	    s2++;

	    if (len-- <= 0)
	      break;

	    if (*s2 == '\n')
	    {
		while (isspace((unsigned char)*(++s2)) && (len-- > 0))
		  ;
	    }
	    else
	      if ((c = get_octal(s2, len, &advance)) != 0)
	      {
		  len -= advance - 1;
		  s2 += advance;
		  *s1++ = c;
	      }
	      else
		if (*s2 == 'n')  /* Newline */
		{
		    *s1++ = '\n';
		    s2++;
		}
		else
		  if (*s2 == 't')  /* Tab */
		  {
		      *s1++ = '\t';
		      s2++;
		  }
		  else
		    if (*s2 == 'r')  /* Linefeed */
		    {
			*s1++ = '\r';
			s2++;
		    }
		    else
		      if (*s2 == 'f')  /* Formfeed */
		      {
			  *s1++ = '\f';
			  s2++;
		      }
		      else
			if ((*s1++ = *s2++) == 0)
			  break;

	    continue;
	}

        if ((*s1++ = *s2++) == 0)
	  break;
     }
}

/* 
 * Evaluate an assertion, with as->as_result holding the result.
 * Return RESULT_TRUE if all ok. Also return the result.
 */
int
keynote_evaluate_assertion(struct assertion *as)
{
    YY_BUFFER_STATE keynote_bs;

    /* Non-existent Conditions field means highest return value */
    if (as->as_conditions_s == NULL)
    {
	as->as_result = keynote_current_session->ks_values_num - 1;
	return RESULT_TRUE;
    }

    if (keynote_lex_init() != RESULT_TRUE)
      return -1;

    keynote_used_variable = 0;
    keynote_init_list = as->as_env;     /* Setup the local-init var list */

    keynote_bs = kn_scan_bytes(as->as_conditions_s,
			       as->as_conditions_e - as->as_conditions_s);
    BEGIN(ACTIONSTRING);	/* We're doing conditions-string parsing */
    first_tok = ACTSTR;
    as->as_result = 0;
    keynote_returnvalue = 0;
    
    switch (knparse())
    {
	case 1:  /* Fall through */
	    keynote_errno = ERROR_SYNTAX;
	case -1:
	    as->as_result = 0;
	    break;
	    
	case 0:
	    as->as_result = keynote_returnvalue;
	    break;
    }

    keynote_env_cleanup(&keynote_temp_list, 1);
    keynote_lex_zap();
    kn_delete_buffer(keynote_bs);

    keynote_used_variable = 0;
    keynote_returnvalue = 0;
    keynote_temp_list = NULL;
    keynote_init_list = NULL;

    if (keynote_errno != 0)
      return -1;
    else
      return RESULT_TRUE;
}

/* 
 * Parse/evaluate a key predicate field. 
 * Store keys in key predicate as keylist in as->as_keylist, if second
 * argument is true.
 */
int
keynote_parse_keypred(struct assertion *as, int record)
{
    YY_BUFFER_STATE keypred_state;
    int p = 0, err;

    if (as->as_keypred_s == NULL)
      return keynote_current_session->ks_values_num - 1;

    if (keynote_lex_init() != RESULT_TRUE)
      return -1;

    keynote_used_variable = 0;
    keynote_returnvalue = 0;
    keynote_justrecord = record; /* Just want the list of keys in predicate */
    keynote_init_list = as->as_env;

    keypred_state = kn_scan_bytes(as->as_keypred_s,
				  as->as_keypred_e - as->as_keypred_s);
    BEGIN(KEYPREDICATE);
    first_tok = KEYPRE;

    err = knparse();
    if (err != 0)
      if (keynote_errno == 0)
	keynote_errno = ERROR_SYNTAX;

    kn_delete_buffer(keypred_state);
    keynote_lex_zap(); 
    keynote_cleanup_kth();

    keynote_init_list = NULL;
    keynote_justrecord = 0;
    p = keynote_returnvalue;
    keynote_returnvalue = 0;

    if (record)
    {
	if (keynote_errno != 0)
	{
	    keynote_keylist_free(keynote_keypred_keylist);
	    keynote_keypred_keylist = NULL;
	    return -1;
	}
	else
	{
	    /* Mark for re-processing if/when environment changes */
	    if (keynote_used_variable)
	    {
		keynote_used_variable = 0;
		as->as_internalflags |= ASSERT_IFLAG_WEIRDLICS;
	    }

	    if (as->as_keylist)
              keynote_keylist_free(as->as_keylist);
	    as->as_keylist = keynote_keypred_keylist;
	    keynote_keypred_keylist = NULL;
	    return RESULT_TRUE;
	}
    }
    else
      return p;
}

/* Evaluate an authorizer or signature field. Return RESULT_TRUE on success.
 * Store key in as->as_authorizer. Second argument is set only for Authorizer
 * field parsing.
 */
int
keynote_evaluate_authorizer(struct assertion *as, int flag)
{
    YY_BUFFER_STATE authorizer_state;
    int err;

    if (keynote_lex_init() != RESULT_TRUE)
      return -1;

    keynote_init_list = as->as_env;
    keynote_justrecord = 1;
    keynote_used_variable = 0;

    if ((flag) && (as->as_authorizer != NULL))
    {
	keynote_free_key(as->as_authorizer, as->as_signeralgorithm);
	as->as_authorizer = NULL;
    }

    if (flag)
      authorizer_state = kn_scan_bytes(as->as_authorizer_string_s,
				       as->as_authorizer_string_e -
				       as->as_authorizer_string_s);
    else
      authorizer_state = kn_scan_bytes(as->as_signature_string_s,
				       as->as_signature_string_e -
				       as->as_signature_string_s);

    BEGIN(SIGNERINIT);
    if (flag)
      first_tok = SIGNERKEY;
    else
      first_tok = SIGNATUREENTRY;

    err = knparse();
    if ((err != 0) && (keynote_errno == 0))
      keynote_errno = ERROR_SYNTAX;

    kn_delete_buffer(authorizer_state);
    keynote_lex_zap();

    keynote_justrecord = 0;
    keynote_init_list = NULL;
    keynote_returnvalue = 0;

    if (keynote_keypred_keylist != NULL)
    {
	if (flag)
	{
	    if (keynote_used_variable)
	      as->as_internalflags |= ASSERT_IFLAG_WEIRDAUTH;

	    as->as_authorizer = keynote_keypred_keylist->key_key;
	    as->as_signeralgorithm = keynote_keypred_keylist->key_alg;
	}
	else
	{
	    if (keynote_used_variable)
	      as->as_internalflags |= ASSERT_IFLAG_WEIRDSIG;

	    as->as_signature = keynote_keypred_keylist->key_key;
	}
	
	keynote_keypred_keylist->key_key = NULL;
	keynote_keylist_free(keynote_keypred_keylist);
	keynote_keypred_keylist = NULL;
    }

    keynote_used_variable = 0;

    if (keynote_errno != 0)
      return -1;
    else
      return RESULT_TRUE;
}

/*
 * Exportable front-end to keynote_get_private_key().
 */
char *
kn_get_string(char *buf)
{
    return keynote_get_private_key(buf);
}

/*
 * Parse a private key -- actually, it can deal with any kind of string.
 */
char *
keynote_get_private_key(char *buf)
{
    YY_BUFFER_STATE pkey;
    char *s;
    int err;

    if (keynote_lex_init() != RESULT_TRUE)
      return NULL;

    keynote_privkey = NULL;
    pkey = kn_scan_bytes(buf, strlen(buf));
    first_tok = PRIVATEKEY;
    err = knparse();
    kn_delete_buffer(pkey);
    keynote_lex_zap();

    if (err != 0)
    {
	if (keynote_privkey != NULL)
	{
	    free(keynote_privkey);
	    keynote_privkey = NULL;
	}

	if (keynote_errno == 0)
	  keynote_errno = ERROR_SYNTAX;

	return NULL;
    }

    s = keynote_privkey;
    keynote_privkey = NULL;
    return s;
}

/*
 * Parse Local-Constants and KeyNote-Version fields.
 */
struct environment *
keynote_get_envlist(char *buf, char *bufend, int whichfield)
{
    struct environment *en = NULL;
    YY_BUFFER_STATE localinit_state;
    int err;

    if (keynote_lex_init() != RESULT_TRUE)
      return NULL;

    localinit_state = kn_scan_bytes(buf, bufend - buf);
    if (whichfield == 0)
    {
	BEGIN(LOCALINIT);	/* We're doing Local-Constants parsing */
	first_tok = LOCINI;
    }
    else
    {
	BEGIN(KEYNOTEVERSION);	/* KeyNote-Version parsing */
      	first_tok = KNVERSION;
    }

    err = knparse();
    if (err != 0)
      if (keynote_errno == 0)
	keynote_errno = ERROR_SYNTAX;

    kn_delete_buffer(localinit_state);
    keynote_lex_zap();

    if (!whichfield)
    {
	if (keynote_errno != 0)
	  keynote_env_cleanup(&keynote_init_list, 1);
	else
	  en = keynote_init_list;

    	keynote_init_list = NULL;
    }

    /* Avoid compiler (-Wall) warnings. Never reached. */
    if (0)
    {
	yyunput(0, NULL);
    }

    return en;
}
