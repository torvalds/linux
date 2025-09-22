/* $OpenBSD: assertion.h,v 1.6 2024/05/21 11:13:08 jsg Exp $ */
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

#ifndef __ASSERTION_H__
#define __ASSERTION_H__

/*
 * These can be changed to reflect more assertions/session or more
 * sessions respectively
 */
#define HASHTABLESIZE                   37
#define SESSIONTABLESIZE                37

struct keynote_session   
{
    int                     ks_id;
    int                     ks_assertioncounter;
    int                     ks_values_num;
    struct environment     *ks_env_table[HASHTABLESIZE];
    struct environment     *ks_env_regex;
    struct keylist         *ks_action_authorizers;
    struct assertion       *ks_assertion_table[HASHTABLESIZE];
    char                  **ks_values;
    char                   *ks_authorizers_cache;
    char                   *ks_values_cache;
    struct keynote_session *ks_prev;
    struct keynote_session *ks_next;
};

struct keylist
{
    int             key_alg;
    void           *key_key;
    char           *key_stringkey;
    struct keylist *key_next;
};

struct assertion 
{
    void               *as_authorizer;
    char               *as_buf;
    char               *as_signature;
    char	       *as_authorizer_string_s;
    char               *as_authorizer_string_e;
    char               *as_keypred_s; 
    char               *as_keypred_e;
    char               *as_conditions_s;
    char               *as_conditions_e;
    char               *as_signature_string_s;
    char               *as_signature_string_e;
    char	       *as_comment_s;
    char	       *as_comment_e;
    char	       *as_startofsignature;
    char	       *as_allbutsignature;
    int                 as_id;
    int			as_signeralgorithm;
    int                 as_result;
    int			as_error;
    unsigned char	as_flags;
    unsigned char	as_internalflags;
    char		as_kresult;
    char                as_sigresult;
    struct keylist     *as_keylist;
    struct environment *as_env;
    struct assertion   *as_next;
};

/* Internal flags */
#define ASSERT_IFLAG_WEIRDLICS   0x0001  /* Needs Licensees re-processing */
#define ASSERT_IFLAG_WEIRDAUTH   0x0002  /* Needs Authorizer re-processing */
#define ASSERT_IFLAG_WEIRDSIG	 0x0004  /* Needs Signature re-processing */
#define ASSERT_IFLAG_NEEDPROC    0x0008  /* Needs "key field" processing */
#define ASSERT_IFLAG_PROCESSED   0x0010  /* Handled repositioning already */

#define KRESULT_UNTOUCHED	0
#define KRESULT_IN_PROGRESS	1	/* For cycle detection */
#define KRESULT_DONE            2

#define KEYWORD_VERSION		1
#define KEYWORD_LOCALINIT      	2
#define KEYWORD_AUTHORIZER     	3
#define KEYWORD_LICENSEES	4
#define KEYWORD_CONDITIONS	5
#define KEYWORD_SIGNATURE	6
#define KEYWORD_COMMENT		7

#define KEYNOTE_FLAG_EXPORTALL	0x1

/* List types for cleanup */
#define LEXTYPE_CHAR		0x1

/* Length of random initializer */
#define KEYNOTE_RAND_INIT_LEN           1024

/* Variables */
extern char **keynote_values;
extern char *keynote_privkey;

extern struct assertion *keynote_current_assertion;

extern struct environment *keynote_init_list;
extern struct environment *keynote_temp_list;

extern struct keylist *keynote_keypred_keylist;

extern struct keynote_session *keynote_sessions[SESSIONTABLESIZE];
extern struct keynote_session *keynote_current_session;

extern int keynote_exceptionflag;
extern int keynote_used_variable;
extern int keynote_returnvalue;
extern int keynote_justrecord;
extern int keynote_donteval;
extern int keynote_errno;

/* Extern definitions */
extern int knlineno;

/* Function prototypes */
extern int keynote_env_add(char *, char *, struct environment **,
                           unsigned int, int);
extern char *keynote_env_lookup(char *, struct environment **, unsigned int);
extern int keynote_env_delete(char *, struct environment **, unsigned int);
extern struct keylist *keynote_keylist_find(struct keylist *, char *);
extern struct environment *keynote_get_envlist(char *, char *, int);
extern struct assertion *keynote_parse_assertion(char *, int, int);
extern int keynote_evaluate_authorizer(struct assertion *, int);
extern struct assertion *keynote_find_assertion(void *, int, int);
extern void keynote_env_cleanup(struct environment **, unsigned int);
extern int keynote_get_key_algorithm(char *, int *, int *);
extern int keynote_sigverify_assertion(struct assertion *);
extern int keynote_evaluate_assertion(struct assertion *);
extern int keynote_parse_keypred(struct assertion *, int);
extern int keynote_keylist_add(struct keylist **, char *);
extern int keynote_add_htable(struct assertion *, int);
extern void keynote_free_assertion(struct assertion *);
extern int keynote_in_action_authorizers(void *, int);
extern struct keynote_session *keynote_find_session(int);
extern void keynote_keylist_free(struct keylist *);
extern void keynote_free_env(struct environment *);
extern int  keynote_sremove_assertion(int, int);
extern unsigned int keynote_stringhash(char *, unsigned int);
extern char *keynote_get_private_key(char *);
extern void keynote_free_key(void *, int);
extern int keynote_evaluate_query(void);
extern int keynote_lex_add(void *, int);
extern void keynote_lex_remove(void *);
extern void keynote_cleanup_kth(void);
extern int keynote_retindex(char *);
extern void knerror(char *);
extern int knparse(void);
extern int knlex(void);
#endif /* __ASSERTION_H__ */
