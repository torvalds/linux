/************************************************************************
          Copyright 1988, 1991 by Carnegie Mellon University

                          All Rights Reserved

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose and without fee is hereby granted, provided
that the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation, and that the name of Carnegie Mellon University not be used
in advertising or publicity pertaining to distribution of the software
without specific, written prior permission.

CARNEGIE MELLON UNIVERSITY DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
IN NO EVENT SHALL CMU BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL
DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
SOFTWARE.

 $FreeBSD$

************************************************************************/

/*
 * Generalized hash table ADT
 *
 * Provides multiple, dynamically-allocated, variable-sized hash tables on
 * various data and keys.
 *
 * This package attempts to follow some of the coding conventions suggested
 * by Bob Sidebotham and the AFS Clean Code Committee of the
 * Information Technology Center at Carnegie Mellon.
 */


#include <sys/types.h>
#include <stdlib.h>

#ifndef USE_BFUNCS
#include <memory.h>
/* Yes, memcpy is OK here (no overlapped copies). */
#define bcopy(a,b,c)    memcpy(b,a,c)
#define bzero(p,l)      memset(p,0,l)
#define bcmp(a,b,c)     memcmp(a,b,c)
#endif

#include "hash.h"

#define TRUE		1
#define FALSE		0
#ifndef	NULL
#define NULL		0
#endif

/*
 * This can be changed to make internal routines visible to debuggers, etc.
 */
#ifndef PRIVATE
#define PRIVATE static
#endif

PRIVATE void hashi_FreeMembers(hash_member *, hash_freefp);




/*
 * Hash table initialization routine.
 *
 * This routine creates and intializes a hash table of size "tablesize"
 * entries.  Successful calls return a pointer to the hash table (which must
 * be passed to other hash routines to identify the hash table).  Failed
 * calls return NULL.
 */

hash_tbl *
hash_Init(tablesize)
	unsigned tablesize;
{
	hash_tbl *hashtblptr;
	unsigned totalsize;

	if (tablesize > 0) {
		totalsize = sizeof(hash_tbl)
			+ sizeof(hash_member *) * (tablesize - 1);
		hashtblptr = (hash_tbl *) malloc(totalsize);
		if (hashtblptr) {
			bzero((char *) hashtblptr, totalsize);
			hashtblptr->size = tablesize;	/* Success! */
			hashtblptr->bucketnum = 0;
			hashtblptr->member = (hashtblptr->table)[0];
		}
	} else {
		hashtblptr = NULL;		/* Disallow zero-length tables */
	}
	return hashtblptr;			/* NULL if failure */
}



/*
 * Frees an entire linked list of bucket members (used in the open
 * hashing scheme).  Does nothing if the passed pointer is NULL.
 */

PRIVATE void
hashi_FreeMembers(bucketptr, free_data)
	hash_member *bucketptr;
	hash_freefp free_data;
{
	hash_member *nextbucket;
	while (bucketptr) {
		nextbucket = bucketptr->next;
		(*free_data) (bucketptr->data);
		free((char *) bucketptr);
		bucketptr = nextbucket;
	}
}




/*
 * This routine re-initializes the hash table.  It frees all the allocated
 * memory and resets all bucket pointers to NULL.
 */

void
hash_Reset(hashtable, free_data)
	hash_tbl *hashtable;
	hash_freefp free_data;
{
	hash_member **bucketptr;
	unsigned i;

	bucketptr = hashtable->table;
	for (i = 0; i < hashtable->size; i++) {
		hashi_FreeMembers(*bucketptr, free_data);
		*bucketptr++ = NULL;
	}
	hashtable->bucketnum = 0;
	hashtable->member = (hashtable->table)[0];
}



/*
 * Generic hash function to calculate a hash code from the given string.
 *
 * For each byte of the string, this function left-shifts the value in an
 * accumulator and then adds the byte into the accumulator.  The contents of
 * the accumulator is returned after the entire string has been processed.
 * It is assumed that this result will be used as the "hashcode" parameter in
 * calls to other functions in this package.  These functions automatically
 * adjust the hashcode for the size of each hashtable.
 *
 * This algorithm probably works best when the hash table size is a prime
 * number.
 *
 * Hopefully, this function is better than the previous one which returned
 * the sum of the squares of all the bytes.  I'm still open to other
 * suggestions for a default hash function.  The programmer is more than
 * welcome to supply his/her own hash function as that is one of the design
 * features of this package.
 */

unsigned
hash_HashFunction(string, len)
	unsigned char *string;
	unsigned len;
{
	unsigned accum;

	accum = 0;
	for (; len > 0; len--) {
		accum <<= 1;
		accum += (unsigned) (*string++ & 0xFF);
	}
	return accum;
}



/*
 * Returns TRUE if at least one entry for the given key exists; FALSE
 * otherwise.
 */

int
hash_Exists(hashtable, hashcode, compare, key)
	hash_tbl *hashtable;
	unsigned hashcode;
	hash_cmpfp compare;
	hash_datum *key;
{
	hash_member *memberptr;

	memberptr = (hashtable->table)[hashcode % (hashtable->size)];
	while (memberptr) {
		if ((*compare) (key, memberptr->data)) {
			return TRUE;		/* Entry does exist */
		}
		memberptr = memberptr->next;
	}
	return FALSE;				/* Entry does not exist */
}



/*
 * Insert the data item "element" into the hash table using "hashcode"
 * to determine the bucket number, and "compare" and "key" to determine
 * its uniqueness.
 *
 * If the insertion is successful 0 is returned.  If a matching entry
 * already exists in the given bucket of the hash table, or some other error
 * occurs, -1 is returned and the insertion is not done.
 */

int
hash_Insert(hashtable, hashcode, compare, key, element)
	hash_tbl *hashtable;
	unsigned hashcode;
	hash_cmpfp compare;
	hash_datum *key, *element;
{
	hash_member *temp;

	hashcode %= hashtable->size;
	if (hash_Exists(hashtable, hashcode, compare, key)) {
		return -1;				/* At least one entry already exists */
	}
	temp = (hash_member *) malloc(sizeof(hash_member));
	if (!temp)
		return -1;				/* malloc failed! */

	temp->data = element;
	temp->next = (hashtable->table)[hashcode];
	(hashtable->table)[hashcode] = temp;
	return 0;					/* Success */
}



/*
 * Delete all data elements which match the given key.  If at least one
 * element is found and the deletion is successful, 0 is returned.
 * If no matching elements can be found in the hash table, -1 is returned.
 */

int
hash_Delete(hashtable, hashcode, compare, key, free_data)
	hash_tbl *hashtable;
	unsigned hashcode;
	hash_cmpfp compare;
	hash_datum *key;
	hash_freefp free_data;
{
	hash_member *memberptr, *tempptr;
	hash_member *previous = NULL;
	int retval;

	retval = -1;
	hashcode %= hashtable->size;

	/*
	 * Delete the first member of the list if it matches.  Since this moves
	 * the second member into the first position we have to keep doing this
	 * over and over until it no longer matches.
	 */
	memberptr = (hashtable->table)[hashcode];
	while (memberptr && (*compare) (key, memberptr->data)) {
		(hashtable->table)[hashcode] = memberptr->next;
		/*
		 * Stop hashi_FreeMembers() from deleting the whole list!
		 */
		memberptr->next = NULL;
		hashi_FreeMembers(memberptr, free_data);
		memberptr = (hashtable->table)[hashcode];
		retval = 0;
	}

	/*
	 * Now traverse the rest of the list
	 */
	if (memberptr) {
		previous = memberptr;
		memberptr = memberptr->next;
	}
	while (memberptr) {
		if ((*compare) (key, memberptr->data)) {
			tempptr = memberptr;
			previous->next = memberptr = memberptr->next;
			/*
			 * Put the brakes on hashi_FreeMembers(). . . .
			 */
			tempptr->next = NULL;
			hashi_FreeMembers(tempptr, free_data);
			retval = 0;
		} else {
			previous = memberptr;
			memberptr = memberptr->next;
		}
	}
	return retval;
}



/*
 * Locate and return the data entry associated with the given key.
 *
 * If the data entry is found, a pointer to it is returned.  Otherwise,
 * NULL is returned.
 */

hash_datum *
hash_Lookup(hashtable, hashcode, compare, key)
	hash_tbl *hashtable;
	unsigned hashcode;
	hash_cmpfp compare;
	hash_datum *key;
{
	hash_member *memberptr;

	memberptr = (hashtable->table)[hashcode % (hashtable->size)];
	while (memberptr) {
		if ((*compare) (key, memberptr->data)) {
			return (memberptr->data);
		}
		memberptr = memberptr->next;
	}
	return NULL;
}



/*
 * Return the next available entry in the hashtable for a linear search
 */

hash_datum *
hash_NextEntry(hashtable)
	hash_tbl *hashtable;
{
	unsigned bucket;
	hash_member *memberptr;

	/*
	 * First try to pick up where we left off.
	 */
	memberptr = hashtable->member;
	if (memberptr) {
		hashtable->member = memberptr->next;	/* Set up for next call */
		return memberptr->data;	/* Return the data */
	}
	/*
	 * We hit the end of a chain, so look through the array of buckets
	 * until we find a new chain (non-empty bucket) or run out of buckets.
	 */
	bucket = hashtable->bucketnum + 1;
	while ((bucket < hashtable->size) &&
		   !(memberptr = (hashtable->table)[bucket])) {
		bucket++;
	}

	/*
	 * Check to see if we ran out of buckets.
	 */
	if (bucket >= hashtable->size) {
		/*
		 * Reset to top of table for next call.
		 */
		hashtable->bucketnum = 0;
		hashtable->member = (hashtable->table)[0];
		/*
		 * But return end-of-table indication to the caller this time.
		 */
		return NULL;
	}
	/*
	 * Must have found a non-empty bucket.
	 */
	hashtable->bucketnum = bucket;
	hashtable->member = memberptr->next;	/* Set up for next call */
	return memberptr->data;		/* Return the data */
}



/*
 * Return the first entry in a hash table for a linear search
 */

hash_datum *
hash_FirstEntry(hashtable)
	hash_tbl *hashtable;
{
	hashtable->bucketnum = 0;
	hashtable->member = (hashtable->table)[0];
	return hash_NextEntry(hashtable);
}

/*
 * Local Variables:
 * tab-width: 4
 * c-indent-level: 4
 * c-argdecl-indent: 4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: -4
 * c-label-offset: -4
 * c-brace-offset: 0
 * End:
 */
