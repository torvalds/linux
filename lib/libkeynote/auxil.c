/* $OpenBSD: auxil.c,v 1.12 2022/01/14 09:08:03 tb Exp $ */
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

#include <openssl/dsa.h>
#include <openssl/rsa.h>

#include "keynote.h"
#include "assertion.h"
#include "signature.h"

/*
 * Get some sort of key-hash for hash table indexing purposes.
 */
static int
keynote_keyhash(void *key, int alg)
{
    struct keynote_binary *bn;
    unsigned int res = 0, i;
    DSA *dsa;
    RSA *rsa;

    if (key == NULL)
      return 0;

    switch (alg)
    {
	case KEYNOTE_ALGORITHM_DSA:
	    dsa = (DSA *) key;
	    res += BN_mod_word(DSA_get0_p(dsa), HASHTABLESIZE);
	    res += BN_mod_word(DSA_get0_q(dsa), HASHTABLESIZE);
	    res += BN_mod_word(DSA_get0_g(dsa), HASHTABLESIZE);
	    res += BN_mod_word(DSA_get0_pub_key(dsa), HASHTABLESIZE);
	    return res % HASHTABLESIZE;

        case KEYNOTE_ALGORITHM_RSA:
	    rsa = (RSA *) key;
            res += BN_mod_word(RSA_get0_n(rsa), HASHTABLESIZE);
            res += BN_mod_word(RSA_get0_e(rsa), HASHTABLESIZE);
	    return res % HASHTABLESIZE;

	case KEYNOTE_ALGORITHM_X509: /* RSA-specific */
	    rsa = (RSA *) key;
            res += BN_mod_word(RSA_get0_n(rsa), HASHTABLESIZE);
            res += BN_mod_word(RSA_get0_e(rsa), HASHTABLESIZE);
	    return res % HASHTABLESIZE;

	case KEYNOTE_ALGORITHM_BINARY:
	    bn = (struct keynote_binary *) key;
	    for (i = 0; i < bn->bn_len; i++)
	      res = (res + ((unsigned char) bn->bn_key[i])) % HASHTABLESIZE;

	    return res;

	case KEYNOTE_ALGORITHM_NONE:
	    return keynote_stringhash(key, HASHTABLESIZE);

	default:
	    return 0;
    }
}

/*
 * Return RESULT_TRUE if key appears in the action authorizers.
 */
int
keynote_in_action_authorizers(void *key, int algorithm)
{
    struct keylist *kl, *kl2;
    void *s;
    int alg;

    if (algorithm == KEYNOTE_ALGORITHM_UNSPEC)
    {
	kl2 = keynote_keylist_find(keynote_current_assertion->as_keylist, key);
	if (kl2 == NULL)
	  return RESULT_FALSE;   /* Shouldn't ever happen */

	s = kl2->key_key;
	alg = kl2->key_alg;
    }
    else
    {
	s = key;
	alg = algorithm;
    }

    for (kl = keynote_current_session->ks_action_authorizers;
	 kl != NULL;
	 kl = kl->key_next)
      if ((kl->key_alg == alg) ||
	  ((kl->key_alg == KEYNOTE_ALGORITHM_RSA) &&
	   (alg == KEYNOTE_ALGORITHM_X509)) ||
	  ((kl->key_alg == KEYNOTE_ALGORITHM_X509) &&
	   (alg == KEYNOTE_ALGORITHM_RSA)))
	if (kn_keycompare(kl->key_key, s, alg) == RESULT_TRUE)
	  return RESULT_TRUE;

    return RESULT_FALSE;
}

/*
 * Add a key to the keylist. Return RESULT_TRUE on success, -1 (and set
 * keynote_errno) otherwise. We are not supposed to make a copy of the 
 * argument.
 */
int
keynote_keylist_add(struct keylist **keylist, char *key)
{
    struct keynote_deckey dc;
    struct keylist *kl;
    
    if (keylist == NULL)
    {
	keynote_errno = ERROR_MEMORY;
	return -1;
    }
    
    kl = calloc(1, sizeof(struct keylist));
    if (kl == NULL)
    {
	keynote_errno = ERROR_MEMORY;
	return -1;
    }

    if (kn_decode_key(&dc, key, KEYNOTE_PUBLIC_KEY) != 0)
    {
	free(kl);
	return -1;
    }

    kl->key_key = dc.dec_key;
    kl->key_alg = dc.dec_algorithm;
    kl->key_stringkey = key;
    kl->key_next = *keylist;
    *keylist = kl;
    return RESULT_TRUE;
}

/*
 * Remove an action authorizer.
 */
int
kn_remove_authorizer(int sessid, char *key)
{
    struct keynote_session *ks;
    struct keylist *kl, *kl2;

    keynote_errno = 0;
    if ((keynote_current_session == NULL) ||
	(keynote_current_session->ks_id != sessid))
    {
	keynote_current_session = keynote_find_session(sessid);
	if (keynote_current_session == NULL)
	{
	    keynote_errno = ERROR_NOTFOUND;
	    return -1;
	}
    }

    ks = keynote_current_session;

    /* If no action authorizers present */
    if ((kl = ks->ks_action_authorizers) == NULL)
    {
	keynote_errno = ERROR_NOTFOUND;
	return -1;
    }

    /* First in list */
    if (!strcmp(kl->key_stringkey, key))
    {
	ks->ks_action_authorizers = kl->key_next;
	kl->key_next = NULL;
	keynote_keylist_free(kl);
	return 0;
    }

    for (; kl->key_next != NULL; kl = kl->key_next)
      if (!strcmp(kl->key_next->key_stringkey, key))
      {
	  kl2 = kl->key_next;
	  kl->key_next = kl2->key_next;
	  kl2->key_next = NULL;
	  keynote_keylist_free(kl2);
	  return 0;
      }
    
    keynote_errno = ERROR_NOTFOUND;
    return -1;
}

/*
 * Add an action authorizer.
 */
int
kn_add_authorizer(int sessid, char *key)
{
    char *stringkey;

    keynote_errno = 0;
    if ((keynote_current_session == NULL) ||
	(keynote_current_session->ks_id != sessid))
    {
	keynote_current_session = keynote_find_session(sessid);
	if (keynote_current_session == NULL)
	{
	    keynote_errno = ERROR_NOTFOUND;
	    return -1;
	}
    }

    stringkey = strdup(key);
    if (stringkey == NULL)
    {
	keynote_errno = ERROR_MEMORY;
	return -1;
    }

    if (keynote_keylist_add(&(keynote_current_session->ks_action_authorizers),
			    stringkey) == -1)
    {
	free(stringkey);
	return -1;
    }

    return 0;
}

/*
 * Find a keylist entry based on the key_stringkey entry.
 */
struct keylist *
keynote_keylist_find(struct keylist *kl, char *s)
{
    for (; kl != NULL; kl = kl->key_next)
      if (!strcmp(kl->key_stringkey, s))
	return kl;

    return kl;
}

/*
 * Free keylist list.
 */
void
keynote_keylist_free(struct keylist *kl)
{
    struct keylist *kl2;

    while (kl != NULL)
    {
	kl2 = kl->key_next;
	free(kl->key_stringkey);
	keynote_free_key(kl->key_key, kl->key_alg);
	free(kl);
	kl = kl2;
    }
}

/*
 * Free a key.
 */
void
kn_free_key(struct keynote_deckey *dc)
{
    if (dc)
      keynote_free_key(dc->dec_key, dc->dec_algorithm);
}

/*
 * Find the num-th assertion given the authorizer. Return NULL if not found.
 */
struct assertion *
keynote_find_assertion(void *authorizer, int num, int algorithm)
{
    struct assertion *as;
    unsigned int h;

    if (authorizer == NULL)
      return NULL;

    h = keynote_keyhash(authorizer, algorithm);
    for (as = keynote_current_session->ks_assertion_table[h];
	 as != NULL;
	 as = as->as_next)
      if ((as->as_authorizer != NULL) &&
	  ((as->as_signeralgorithm == algorithm) ||
	   ((as->as_signeralgorithm == KEYNOTE_ALGORITHM_RSA) &&
	    (algorithm == KEYNOTE_ALGORITHM_X509)) ||
	   ((as->as_signeralgorithm == KEYNOTE_ALGORITHM_X509) &&
	    (algorithm == KEYNOTE_ALGORITHM_RSA))))
	if (kn_keycompare(authorizer, as->as_authorizer, algorithm) ==
	    RESULT_TRUE)
	  if (num-- == 0)
	    return as;

    return NULL;
}

/*
 * Add an assertion to the hash table. Return RESULT_TRUE on success,
 * ERROR_MEMORY for memory failure, ERROR_SYNTAX if some problem with
 * the assertion is detected.
 */
int
keynote_add_htable(struct assertion *as, int which)
{
    char *hashname;
    unsigned int i;

    if (as == NULL)
    {
	keynote_errno = ERROR_MEMORY;
	return -1;
    }

    if (!which)
      hashname = as->as_authorizer_string_s;
    else
      hashname = as->as_authorizer;

    if (hashname == NULL)
    {
	keynote_errno = ERROR_SYNTAX;
	return -1;
    }

    i = keynote_keyhash(hashname, as->as_signeralgorithm);
    as->as_next = keynote_current_session->ks_assertion_table[i];
    keynote_current_session->ks_assertion_table[i] = as;
    return RESULT_TRUE;
}

/*
 * Parse and store an assertion in the internal hash table. 
 * Return the result of the evaluation, if doing early evaluation.
 * If an error was encountered, set keynote_errno.
 */
int
kn_add_assertion(int sessid, char *asrt, int len, int assertion_flags)
{
    struct assertion *as;

    keynote_errno = 0;
    if ((keynote_current_session == NULL) ||
	(keynote_current_session->ks_id != sessid))
    {
	keynote_current_session = keynote_find_session(sessid);
	if (keynote_current_session == NULL)
	{
	    keynote_errno = ERROR_NOTFOUND;
	    return -1;
	}
    }

    as = keynote_parse_assertion(asrt, len, assertion_flags);
    if ((as == NULL) || (keynote_errno != 0))
    {
	if (keynote_errno == 0)
	  keynote_errno = ERROR_SYNTAX;

	return -1;
    }

    as->as_id = keynote_current_session->ks_assertioncounter++;

    /* Check for wrap around...there has to be a better solution to this */
    if (keynote_current_session->ks_assertioncounter < 0)
    {
	keynote_free_assertion(as);
	keynote_errno = ERROR_SYNTAX;
	return -1;
    }
    
    if (keynote_add_htable(as, 0) != RESULT_TRUE)
    {
	keynote_free_assertion(as);
	return -1;
    }

    as->as_internalflags |= ASSERT_IFLAG_NEEDPROC;
    return as->as_id;
}

/*
 * Remove an assertion from the hash table.
 */
static int
keynote_remove_assertion(int sessid, int assertid, int deleteflag)
{
    struct assertion *ht, *ht2;
    int i;

    if ((keynote_current_session == NULL) ||
	(keynote_current_session->ks_id != sessid))
    {
	keynote_current_session = keynote_find_session(sessid);
	if (keynote_current_session == NULL)
	{
	    keynote_errno = ERROR_NOTFOUND;
	    return -1;
	}
    }

    for (i = 0; i < HASHTABLESIZE; i++)
    {
	ht = keynote_current_session->ks_assertion_table[i];
	if (ht == NULL)
	  continue;

	/* If first entry in bucket */
	if (ht->as_id == assertid)
	{
	    keynote_current_session->ks_assertion_table[i] = ht->as_next;
	    if (deleteflag)
	      keynote_free_assertion(ht);
	    return 0;
	}

	for (; ht->as_next != NULL; ht = ht->as_next)
	  if (ht->as_next->as_id == assertid)  /* Got it */
	  {
	      ht2 = ht->as_next;
	      ht->as_next = ht2->as_next;
	      if (deleteflag)
		keynote_free_assertion(ht2);
	      return 0;
	  }
    }

    keynote_errno = ERROR_NOTFOUND;
    return -1;
}

/*
 * API wrapper for deleting assertions.
 */
int
kn_remove_assertion(int sessid, int assertid)
{
    keynote_errno = 0;
    return keynote_remove_assertion(sessid, assertid, 1);
}

/*
 * Internally-used wrapper for removing but not deleting assertions.
 */
int
keynote_sremove_assertion(int sessid, int assertid)
{
    return keynote_remove_assertion(sessid, assertid, 0);
}

/* 
 * Free an assertion structure.
 */
void
keynote_free_assertion(struct assertion *as)
{
    if (as == NULL)
      return;

    free(as->as_buf);

    free(as->as_signature);

    if (as->as_env != NULL)
      keynote_env_cleanup(&(as->as_env), 1);

    if (as->as_keylist != NULL)
      keynote_keylist_free(as->as_keylist);

    if (as->as_authorizer != NULL)
      keynote_free_key(as->as_authorizer, as->as_signeralgorithm);

    free(as);
}

unsigned int 
keynote_stringhash(char *name, unsigned int size)
{
    unsigned int hash_val = 0;
    unsigned int i;

    if ((size == 0) || (size == 1))
      return 0;

    for (; *name; name++) 
    {
        hash_val = (hash_val << 2) + *name;
        if ((i = hash_val & 0x3fff) != 0)
	  hash_val = ((hash_val ^ (i >> 12)) & 0x3fff);
    }

    return hash_val % size;
}
