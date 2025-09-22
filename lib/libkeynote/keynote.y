/* $OpenBSD: keynote.y,v 1.19 2022/12/27 17:10:06 jmc Exp $ */
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
%union {
    char   *string;
    double  doubval;
    int     intval;
    int     bool;
};
%type <bool> stringexp numexp expr floatexp
%type <intval> NUM KOF numex afterhint notemptyprog
%type <intval> notemptykeypredicate prog key keyexp keylist
%type <doubval> FLOAT floatex
%type <string> STRING VARIABLE str strnotconcat 
%token TRUE FALSE NUM FLOAT STRING VARIABLE 
%token OPENPAREN CLOSEPAREN EQQ COMMA ACTSTR LOCINI KOF KEYPRE KNVERSION
%token DOTT SIGNERKEY HINT OPENBLOCK CLOSEBLOCK SIGNATUREENTRY PRIVATEKEY
%token SEMICOLON TRUE FALSE
%nonassoc EQ NE LT GT LE GE REGEXP
%left OR
%left AND
%right NOT
%left PLUS MINUS DOTT
%left MULT DIV MOD
%left EXP
%nonassoc UNARYMINUS DEREF OPENNUM OPENFLT
%start grammarswitch
%{
#include <sys/types.h>

#include <ctype.h>
#include <math.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "keynote.h"
#include "assertion.h"

static int *keynote_kth_array = NULL;
static int keylistcount = 0;

static int   resolve_assertion(char *);
static int   keynote_init_kth(void);
static int   isfloatstring(char *);
static int   checkexception(int);
static char *my_lookup(char *);
static int   intpow(int, int);
static int   get_kth(int);
%}
%%

grammarswitch: LOCINI { keynote_exceptionflag = keynote_donteval = 0; }
                localinit
             | ACTSTR { keynote_exceptionflag = keynote_donteval = 0; } program
   	     | KEYPRE { keynote_exceptionflag = keynote_donteval = 0; }
                keypredicate
             | SIGNERKEY { keynote_exceptionflag = keynote_donteval = 0; } key
             | SIGNATUREENTRY { keynote_exceptionflag = keynote_donteval = 0; }
                key
             | KNVERSION { keynote_exceptionflag = keynote_donteval = 0; }
                        STRING { keynote_lex_remove($3);
				 if (strcmp($3, KEYNOTE_VERSION_STRING))
				   keynote_errno = ERROR_SYNTAX;
				 free($3);
			       }
             | PRIVATEKEY { keynote_exceptionflag = keynote_donteval = 0; }
                        STRING { keynote_lex_remove($3);
			         keynote_privkey = $3;
			       }
    
keypredicate: /* Nothing */   { keynote_returnvalue = 0;
                                return 0; 
                              }
       | notemptykeypredicate { keynote_returnvalue = $1;
				return 0;
                              }

notemptykeypredicate:  key     { $$ = $1; }
       		     | keyexp  { $$ = $1; }

keyexp: notemptykeypredicate AND { if (($1 == 0) && !keynote_justrecord)
                                     keynote_donteval = 1;
                                 } notemptykeypredicate 
                 { if ($1 > $4)
		     $$ = $4;
		   else
	       	     $$ = $1;
		   keynote_donteval = 0;
                 }  /* Min */
      | notemptykeypredicate OR { if (($1 == (keynote_current_session->ks_values_num - 1)) && !keynote_justrecord)
	                             keynote_donteval = 1;
       	                         } notemptykeypredicate
                 { if ($1 >= $4)
		     $$ = $1;
		   else
		     $$ = $4;
		   keynote_donteval = 0;
                 }  /* Max */
       | OPENPAREN keyexp CLOSEPAREN { $$ = $2; }
       | KOF { keylistcount = 0; } OPENPAREN {
			 if (!keynote_justrecord && !keynote_donteval)
 	                   if (keynote_init_kth() == -1)
			     return -1;
                       } keylist CLOSEPAREN 
                          {
			      if (keylistcount < $1)
			      {
				  keynote_errno = ERROR_SYNTAX;
				  return -1;
			      }

			    if (!keynote_justrecord && !keynote_donteval)
			      $$ = get_kth($1);
			    else
			      $$ = 0;
			  }  /* K-th */

keylist: key
	    { /* Don't do anything if we're just recording */ 
              if (!keynote_justrecord && !keynote_donteval)
		if (($1 < keynote_current_session->ks_values_num) && ($1 >= 0))
		  keynote_kth_array[$1]++;

	      keylistcount++;
            }
        | key COMMA keylist
            { /* Don't do anything if we're just recording */ 
	      if (!keynote_justrecord && !keynote_donteval)
		if (($1 < keynote_current_session->ks_values_num) && ($1 >= 0))
		  keynote_kth_array[$1]++;

	      keylistcount++;
            }

key: str        {
		   if (keynote_donteval)
		     $$ = 0;
		   else
		   {
		       keynote_lex_remove($1);
		       if (keynote_justrecord)
		       {
			   if (keynote_keylist_add(&keynote_keypred_keylist,
						   $1) == -1)
			   {
			       free($1);
			       return -1;
			   }

			   $$ = 0;
		       }
		       else
			 switch (keynote_in_action_authorizers($1, KEYNOTE_ALGORITHM_UNSPEC))
			 {
			     case -1:
				 free($1);
				 return -1;
				 
			     case RESULT_TRUE:
				 free($1);
				 $$ = keynote_current_session->ks_values_num -
				      1;
				 break;
				 
			     default:
				 $$ = resolve_assertion($1);
				 free($1);
				 break;
			 }
		   }
                 }

localinit: /* Nothing */
         | localconstants

localconstants: VARIABLE EQQ STRING 
	  {
            int i;

            keynote_lex_remove($1);
	    keynote_lex_remove($3);
 
	    /*
	     * Variable names starting with underscores are illegal here.
	     */
	    if ($1[0] == '_')
	    {
		free($1);
		free($3);
		keynote_errno = ERROR_SYNTAX;
		return -1;
	    }
	    
	    /* If the identifier already exists, report error. */
	    if (keynote_env_lookup($1, &keynote_init_list, 1) != NULL)
	    {
		free($1);
		free($3);
		keynote_errno = ERROR_SYNTAX;
		return -1;
	    }

	    i = keynote_env_add($1, $3, &keynote_init_list, 1, 0);
	    free($1);
	    free($3);

	    if (i != RESULT_TRUE)
	      return -1;
	  }
         | VARIABLE EQQ STRING
	  {
            int i;

	    keynote_lex_remove($1);
	    keynote_lex_remove($3);

	    /*
	     * Variable names starting with underscores are illegal here.
	     */
	    if ($1[0] == '_')
	    {
		free($1);
		free($3);
		keynote_errno = ERROR_SYNTAX;
		return -1;
	    }
	 
	    /* If the identifier already exists, report error. */
	    if (keynote_env_lookup($1, &keynote_init_list, 1) != NULL)
	    {
		free($1);
		free($3);
		keynote_errno = ERROR_SYNTAX;
		return -1;
	    }

	    i = keynote_env_add($1, $3, &keynote_init_list, 1, 0);
	    free($1);
	    free($3);

	    if (i != RESULT_TRUE)
	      return -1;
	  } localconstants

program: prog { 
	        keynote_returnvalue = $1;
		return 0;
	      }

prog:   /* Nada */ { $$ = 0; }
       | notemptyprog {
			  /* 
			   * Cleanup envlist of additions such as 
			   * regexp results
			   */
			  keynote_env_cleanup(&keynote_temp_list, 1);
                    } SEMICOLON prog
                    {
		      if ($1 > $4)
			$$ = $1;
		      else
			$$ = $4;
                    } 

notemptyprog: expr HINT afterhint
              {
		if (checkexception($1))
		  $$ = $3;
		else
		  $$ = 0;
	      }
       |  expr 
              {
		if (checkexception($1))
		  $$ = keynote_current_session->ks_values_num - 1;
		else
		  $$ = 0;
	      }

afterhint: str {  if (keynote_exceptionflag || keynote_donteval)
		    $$ = 0;
		  else
		  {
		      keynote_lex_remove($1);

		      $$ = keynote_retindex($1);
		      if ($$ == -1)   /* Invalid return value */
			$$ = 0;

		      free($1);
		  }
                }
         | OPENBLOCK prog CLOSEBLOCK { $$ = $2; }


expr:     OPENPAREN expr CLOSEPAREN 	{ $$ = $2; }
	| expr AND { if ($1 == 0)
	               keynote_donteval = 1;
	           } expr               { $$ = ($1 && $4);
		                          keynote_donteval = 0;
		                        }
	| expr OR { if ($1)
	              keynote_donteval = 1; 
	          } expr 		{ $$ = ($1 || $4);
		                          keynote_donteval = 0;
                                        }
	| NOT expr 			{ $$ = !($2); }
	| numexp 			{ $$ = $1; }
	| floatexp			{ $$ = $1; }
	| stringexp 			{ $$ = $1; }
        | TRUE	  		        { $$ = 1; }
        | FALSE	  		        { $$ = 0; }

numexp:	  numex LT numex { $$ = $1 < $3; }
	| numex GT numex { $$ = $1 > $3; }
	| numex EQ numex { $$ = $1 == $3; }
	| numex LE numex { $$ = $1 <= $3; }
	| numex GE numex { $$ = $1 >= $3; }
	| numex NE numex { $$ = $1 != $3; }

floatexp: floatex LT floatex { $$ = $1 < $3; }
	| floatex GT floatex { $$ = $1 > $3; }
	| floatex LE floatex { $$ = $1 <= $3; }
	| floatex GE floatex { $$ = $1 >= $3; }

numex:	  numex PLUS numex  { $$ = $1 + $3; }
	| numex MINUS numex { $$ = $1 - $3; }
	| numex MULT numex  { $$ = $1 * $3; }
        | numex DIV numex   { if ($3 == 0)
	                      {
				  if (!keynote_donteval)
				    keynote_exceptionflag = 1;
			      }
	                      else
			        $$ = ($1 / $3);
			    }
	| numex MOD numex   { if ($3 == 0)
	                      {
				  if (!keynote_donteval)
				    keynote_exceptionflag = 1;
			      }
	                      else
			        $$ = $1 % $3;
			    }
	| numex EXP numex   		{ $$ = intpow($1, $3); }
	| MINUS numex %prec UNARYMINUS 	{ $$ = -($2); }
	| OPENPAREN numex CLOSEPAREN   	{ $$ = $2; }
	| NUM 			       	{ $$ = $1; }
        | OPENNUM strnotconcat         	{ if (keynote_exceptionflag ||
					      keynote_donteval)
	                                    $$ = 0;
 	                                  else
					  {
					      keynote_lex_remove($2);

					      if (!isfloatstring($2))
						$$ = 0;
					      else
						$$ = (int) floor(atof($2));
					      free($2);
					  }
					}

floatex:  floatex PLUS floatex  	{ $$ = ($1 + $3); }
	| floatex MINUS floatex 	{ $$ = ($1 - $3); }
	| floatex MULT floatex          { $$ = ($1 * $3); }
        | floatex DIV floatex   	{ if ($3 == 0)
	                                  {
					      if (!keynote_donteval)
						keynote_exceptionflag = 1;
					  }
	                                  else
			        	   $$ = ($1 / $3);
					}
	| floatex EXP floatex  			{ if (!keynote_exceptionflag &&
						      !keynote_donteval)
	                                            $$ = pow($1, $3);
	                                        }
	| MINUS floatex %prec UNARYMINUS 	{ $$ = -($2); }
	| OPENPAREN floatex CLOSEPAREN	 	{ $$ = $2; }
	| FLOAT			       		{ $$ = $1; }
        | OPENFLT strnotconcat          {
	                                  if (keynote_exceptionflag ||
					      keynote_donteval)
					    $$ = 0.0;
					  else
					  {
					      keynote_lex_remove($2);
					  
					      if (!isfloatstring($2))
						$$ = 0.0;
					      else
						$$ = atof($2);
					      free($2);
					  }
	                                }

stringexp: str EQ str {
                        if (keynote_exceptionflag || keynote_donteval)
			  $$ = 0;
			else
			{
			    $$ = strcmp($1, $3) == 0 ? 1 : 0;
			    keynote_lex_remove($1);
			    keynote_lex_remove($3);
			    free($1);
			    free($3);
			}
		      }
	 | str NE str {
	                if (keynote_exceptionflag || keynote_donteval)
			  $$ = 0;
			else
			{
			    $$ = strcmp($1, $3) != 0 ? 1 : 0;
			    keynote_lex_remove($1);
			    keynote_lex_remove($3);
			    free($1);
			    free($3);
			}
		      }
	 | str LT str {
	                if (keynote_exceptionflag || keynote_donteval)
			  $$ = 0;
			else
			{
			    $$ = strcmp($1, $3) < 0 ? 1 : 0;
			    keynote_lex_remove($1);
			    keynote_lex_remove($3);
			    free($1);
			    free($3);
			}
		      }
	 | str GT str {
	                if (keynote_exceptionflag || keynote_donteval)
			  $$ = 0;
			else
			{
			    $$ = strcmp($1, $3) > 0 ? 1 : 0;
			    keynote_lex_remove($1);
			    keynote_lex_remove($3);
			    free($1);
			    free($3);
			}
		      }
	 | str LE str {
	                if (keynote_exceptionflag || keynote_donteval)
			  $$ = 0;
			else
			{
			    $$ = strcmp($1, $3) <= 0 ? 1 : 0;
			    keynote_lex_remove($1);
			    keynote_lex_remove($3);
			    free($1);
			    free($3);
			}
		      }
	 | str GE str {
	                if (keynote_exceptionflag || keynote_donteval)
			  $$ = 0;
			else
			{
			    $$ = strcmp($1, $3) >= 0 ? 1 : 0;
			    keynote_lex_remove($1);
			    keynote_lex_remove($3);
			    free($1);
			    free($3);
			}
		      }
	 | str REGEXP str 
            {
	      regmatch_t pmatch[32];
	      char grp[10], *gr;
	      regex_t preg;
	      int i;

	      if (keynote_exceptionflag || keynote_donteval)
		$$ = 0;
	      else
	      {
		  keynote_lex_remove($1);
		  keynote_lex_remove($3);

		  memset(pmatch, 0, sizeof(pmatch));
		  memset(grp, 0, sizeof(grp));

		  if (regcomp(&preg, $3, REG_EXTENDED))
		  {
		      free($1);
		      free($3);
		      keynote_exceptionflag = 1;
		  }
		  else
		  {
		      /* Clean-up residuals from previous regexps */
		      keynote_env_cleanup(&keynote_temp_list, 1);

		      free($3);
		      i = regexec(&preg, $1, 32, pmatch, 0);
		      $$ = (i == 0 ? 1 : 0);
		      if (i == 0)
		      {
			  snprintf(grp, sizeof grp, "%lu",
			        (unsigned long)preg.re_nsub);
			  if (keynote_env_add("_0", grp, &keynote_temp_list,
					      1, 0) != RESULT_TRUE)
			  {
			      free($1);
			      regfree(&preg);
			      return -1;
			  }

			  for (i = 1; i < 32 && pmatch[i].rm_so != -1; i++)
			  {
			      gr = calloc(pmatch[i].rm_eo - pmatch[i].rm_so +
					  1, sizeof(char));
			      if (gr == NULL)
			      {
				  free($1);
				  regfree(&preg);
				  keynote_errno = ERROR_MEMORY;
				  return -1;
			      }

			      strncpy(gr, $1 + pmatch[i].rm_so,
				      pmatch[i].rm_eo - pmatch[i].rm_so);
			      gr[pmatch[i].rm_eo - pmatch[i].rm_so] = '\0';
			      snprintf(grp, sizeof grp, "_%d", i);
			      if (keynote_env_add(grp, gr, &keynote_temp_list,
						  1, 0) == -1)
			      {
				  free($1);
				  regfree(&preg);
				  free(gr);
				  return -1;
			      }
			      else
				free(gr);
			  }
		      }

		      regfree(&preg);
		      free($1);
		  }
	      }
	    }

str: str DOTT str    {  if (keynote_exceptionflag || keynote_donteval)
			  $$ = NULL;
			else
			{
			    int len = strlen($1) + strlen($3) + 1;
			    $$ = calloc(len, sizeof(char));
			    keynote_lex_remove($1);
			    keynote_lex_remove($3);
			    if ($$ == NULL)
			    {
				free($1);
				free($3);
				keynote_errno = ERROR_MEMORY;
				return -1;
			    }
			    snprintf($$, len, "%s%s", $1, $3);
			    free($1);
			    free($3);
			    if (keynote_lex_add($$, LEXTYPE_CHAR) == -1)
			      return -1;
			}
		      }
	| strnotconcat { $$ = $1; }

strnotconcat: STRING 	                { $$ = $1; }
        | OPENPAREN str CLOSEPAREN 	{ $$ = $2; }
        | VARIABLE      {  if (keynote_exceptionflag || keynote_donteval)
	                     $$ = NULL;
 	                   else
			   {
			       $$ = my_lookup($1);
			       keynote_lex_remove($1);
			       free($1);
			       if ($$ == NULL)
			       {
				   if (keynote_errno)
				     return -1;
				   $$ = strdup("");
			       }
			       else
				 $$ = strdup($$);

			       if ($$ == NULL)
			       {
				   keynote_errno = ERROR_MEMORY;
				   return -1;
			       }

			       if (keynote_lex_add($$, LEXTYPE_CHAR) == -1)
				 return -1;
			   }
	                 }
	| DEREF str      {  if (keynote_exceptionflag || keynote_donteval)
			      $$ = NULL;
			    else
			    {
				$$ = my_lookup($2);
				keynote_lex_remove($2);
				free($2);
				if ($$ == NULL)
				{
				    if (keynote_errno)
				      return -1;
				    $$ = strdup("");
				}
				else
				  $$ = strdup($$);

				if ($$ == NULL)
				{
				    keynote_errno = ERROR_MEMORY;
				    return -1;
				}

				if (keynote_lex_add($$, LEXTYPE_CHAR) == -1)
				  return -1;
			    }
			 }
%%

/*
 * Find all assertions signed by s and give us the one with the highest
 * return value.
 */
static int
resolve_assertion(char *s)
{
    int i, alg = KEYNOTE_ALGORITHM_NONE, p = 0;
    void *key = (void *) s;
    struct assertion *as;
    struct keylist *kl;

    kl = keynote_keylist_find(keynote_current_assertion->as_keylist, s);
    if (kl != NULL)
    {
	alg = kl->key_alg;
	key = kl->key_key;
    }

    for (i = 0;; i++)
    {
	as = keynote_find_assertion(key, i, alg);
	if (as == NULL)  /* Gone through all of them */
	  return p;

	if (as->as_kresult == KRESULT_DONE)
	  if (p < as->as_result)
	    p = as->as_result;

	/* Short circuit if we find an assertion with maximum return value */
	if (p == (keynote_current_session->ks_values_num - 1))
	  return p;
    }

    return 0;
}

/* 
 * Environment variable lookup. 
 */
static char *
my_lookup(char *s)
{
    struct keynote_session *ks = keynote_current_session;
    char *ret;

    if (!strcmp(s, "_MIN_TRUST"))
    {
	keynote_used_variable = 1;
	return ks->ks_values[0];
    }
    else
    {
	if (!strcmp(s, "_MAX_TRUST"))
	{
	    keynote_used_variable = 1;
	    return ks->ks_values[ks->ks_values_num - 1];
	}
	else
	{
	    if (!strcmp(s, "_VALUES"))
	    {
		keynote_used_variable = 1;
		return keynote_env_lookup("_VALUES", ks->ks_env_table,
					  HASHTABLESIZE);
	    }
	    else
	    {
		if (!strcmp(s, "_ACTION_AUTHORIZERS"))
		{
		    keynote_used_variable = 1;
		    return keynote_env_lookup("_ACTION_AUTHORIZERS",
					      ks->ks_env_table, HASHTABLESIZE);
		}
	    }
	}
    }

    /* Temporary list (regexp results) */
    if (keynote_temp_list != NULL)
    {
	ret = keynote_env_lookup(s, &keynote_temp_list, 1);
	if (ret != NULL)
	  return ret;
	else
	  if (keynote_errno != 0)
	    return NULL;
    }

    /* Local-Constants */
    if (keynote_init_list != NULL)
    {
	ret = keynote_env_lookup(s, &keynote_init_list, 1);
	if (ret != NULL)
	  return ret;
	else
	  if (keynote_errno != 0)
	    return NULL;
    }

    if (ks != NULL)
    {
	/* Action environment */
	ret = keynote_env_lookup(s, ks->ks_env_table, HASHTABLESIZE);
	if (ret != NULL)
	{
	    keynote_used_variable = 1;
	    return ret;
	}
	else
	  if (keynote_errno != 0)
	    return NULL;
    }

    /* Regex table */
    if ((ks != NULL) && (ks->ks_env_regex != NULL))
    {
	ret = keynote_env_lookup(s, &(ks->ks_env_regex), 1);
	if (ret != NULL)
	{
	    keynote_used_variable = 1;
	    return ret;
	}

	return NULL;
    }

    return NULL;
}

/*
 * If we had an exception, the boolean expression should return false.
 * Otherwise, return the result of the expression (the argument).
 */
static int
checkexception(int i)
{
    if (keynote_exceptionflag)
    {
	keynote_exceptionflag = 0;
	return 0;
    }
    else
      return i;
}


/* 
 * Integer exponentiation -- copied from Schneier's AC2, page 244. 
 */
static int
intpow(int x, int y)
{
    int s = 1;
    
    /* 
     * x^y with y < 0 is equivalent to 1/(x^y), which for
     * integer arithmetic is 0.
     */
    if (y < 0)
      return 0;

    while (y)
    {
	if (y & 1)
	  s *= x;
	
	y >>= 1;
	x *= x;
    }

    return s;
}

/* 
 * Check whether the string is a floating point number. 
 */
static int
isfloatstring(char *s)
{
    int i, point = 0;
    
    for (i = strlen(s) - 1; i >= 0; i--)
      if (!isdigit((unsigned char)s[i]))
      {
	  if (s[i] == '.')
	  {
	      if (point == 1)
	        return 0;
	      else
	        point = 1;
	  }
	  else
	    return 0;
      }

    return 1;
}

/*
 * Initialize array for threshold search.
 */
static int
keynote_init_kth(void)
{
    int i = keynote_current_session->ks_values_num;
    
    if (i == -1)
      return -1;
    
    keynote_kth_array = calloc(i, sizeof(int));
    if (keynote_kth_array == NULL)
    {
	keynote_errno = ERROR_MEMORY;
	return -1;
    }

    return RESULT_TRUE;
}

/*
 * Get the k-th best return value.
 */
static int
get_kth(int k)
{
    int i;

    for (i = keynote_current_session->ks_values_num - 1; i >= 0; i--)
    {
	k -= keynote_kth_array[i];
	
	if (k <= 0)
	  return i;
    }

    return 0;
}

/*
 * Cleanup array.
 */
void
keynote_cleanup_kth(void)
{
    if (keynote_kth_array != NULL)
    {
	free(keynote_kth_array);
	keynote_kth_array = NULL;
    }
}

void
knerror(char *s)
{}
