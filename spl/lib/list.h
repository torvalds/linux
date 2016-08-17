/*****************************************************************************
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2001-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is from LSD-Tools, the LLNL Software Development Toolbox.
 *
 *  LSD-Tools is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  LSD-Tools is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with LSD-Tools.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************/

#ifndef LSD_LIST_H
#define LSD_LIST_H


/***********
 *  Notes  *
 ***********/
/*
 *  If NDEBUG is not defined, internal debug code will be enabled.  This is
 *  intended for development use only and production code should define NDEBUG.
 *
 *  If WITH_LSD_FATAL_ERROR_FUNC is defined, the linker will expect to
 *  find an external lsd_fatal_error(file,line,mesg) function.  By default,
 *  lsd_fatal_error(file,line,mesg) is a macro definition that outputs an
 *  error message to stderr.  This macro may be redefined to invoke another
 *  routine instead.
 *
 *  If WITH_LSD_NOMEM_ERROR_FUNC is defined, the linker will expect to
 *  find an external lsd_nomem_error(file,line,mesg) function.  By default,
 *  lsd_nomem_error(file,line,mesg) is a macro definition that returns NULL.
 *  This macro may be redefined to invoke another routine instead.
 *
 *  If WITH_PTHREADS is defined, these routines will be thread-safe.
 */


/****************
 *  Data Types  *
 ****************/

typedef struct list * List;
/*
 *  List opaque data type.
 */

typedef struct listIterator * ListIterator;
/*
 *  List Iterator opaque data type.
 */

typedef void (*ListDelF) (void *x);
/*
 *  Function prototype to deallocate data stored in a list.
 *    This function is responsible for freeing all memory associated
 *    with an item, including all subordinate items (if applicable).
 */

typedef int (*ListCmpF) (void *x, void *y);
/*
 *  Function prototype for comparing two items in a list.
 *  Returns less-than-zero if (x<y), zero if (x==y), and
 *    greather-than-zero if (x>y).
 */

typedef int (*ListFindF) (void *x, void *key);
/*
 *  Function prototype for matching items in a list.
 *  Returns non-zero if (x==key); o/w returns zero.
 */

typedef int (*ListForF) (void *x, void *arg);
/*
 *  Function prototype for operating on each item in a list.
 *  Returns less-than-zero on error.
 */


/*******************************
 *  General-Purpose Functions  *
 *******************************/

List list_create (ListDelF f);
/*
 *  Creates and returns a new empty list, or lsd_nomem_error() on failure.
 *  The deletion function [f] is used to deallocate memory used by items
 *    in the list; if this is NULL, memory associated with these items
 *    will not be freed when the list is destroyed.
 *  Note: Abandoning a list without calling list_destroy() will result
 *    in a memory leak.
 */

void list_destroy (List l);
/*
 *  Destroys list [l], freeing memory used for list iterators and the
 *    list itself; if a deletion function was specified when the list
 *    was created, it will be called for each item in the list.
 */

int list_is_empty (List l);
/*
 *  Returns non-zero if list [l] is empty; o/w returns zero.
 */

int list_count (List l);
/*
 *  Returns the number of items in list [l].
 */


/***************************
 *  List Access Functions  *
 ***************************/

void * list_append (List l, void *x);
/*
 *  Inserts data [x] at the end of list [l].
 *  Returns the data's ptr, or lsd_nomem_error() if insertion failed.
 */

void * list_prepend (List l, void *x);
/*
 *  Inserts data [x] at the beginning of list [l].
 *  Returns the data's ptr, or lsd_nomem_error() if insertion failed.
 */

void * list_find_first (List l, ListFindF f, void *key);
/*
 *  Traverses list [l] using [f] to match each item with [key].
 *  Returns a ptr to the first item for which the function [f]
 *    returns non-zero, or NULL if no such item is found.
 *  Note: This function differs from list_find() in that it does not require
 *    a list iterator; it should only be used when all list items are known
 *    to be unique (according to the function [f]).
 */

int list_delete_all (List l, ListFindF f, void *key);
/*
 *  Traverses list [l] using [f] to match each item with [key].
 *  Removes all items from the list for which the function [f] returns
 *    non-zero; if a deletion function was specified when the list was
 *    created, it will be called to deallocate each item being removed.
 *  Returns a count of the number of items removed from the list.
 */

int list_for_each (List l, ListForF f, void *arg);
/*
 *  For each item in list [l], invokes the function [f] with [arg].
 *  Returns a count of the number of items on which [f] was invoked.
 *  If [f] returns <0 for a given item, the iteration is aborted and the
 *    function returns the negative of that item's position in the list.
 */

void list_sort (List l, ListCmpF f);
/*
 *  Sorts list [l] into ascending order according to the function [f].
 *  Note: Sorting a list resets all iterators associated with the list.
 *  Note: The sort algorithm is stable.
 */


/****************************
 *  Stack Access Functions  *
 ****************************/

void * list_push (List l, void *x);
/*
 *  Pushes data [x] onto the top of stack [l].
 *  Returns the data's ptr, or lsd_nomem_error() if insertion failed.
 */

void * list_pop (List l);
/*
 *  Pops the data item at the top of the stack [l].
 *  Returns the data's ptr, or NULL if the stack is empty.
 */

void * list_peek (List l);
/*
 *  Peeks at the data item at the top of the stack (or head of the queue) [l].
 *  Returns the data's ptr, or NULL if the stack (or queue) is empty.
 *  Note: The item is not removed from the list.
 */


/****************************
 *  Queue Access Functions  *
 ****************************/

void * list_enqueue (List l, void *x);
/*
 *  Enqueues data [x] at the tail of queue [l].
 *  Returns the data's ptr, or lsd_nomem_error() if insertion failed.
 */

void * list_dequeue (List l);
/*
 *  Dequeues the data item at the head of the queue [l].
 *  Returns the data's ptr, or NULL if the queue is empty.
 */


/*****************************
 *  List Iterator Functions  *
 *****************************/

ListIterator list_iterator_create (List l);
/*
 *  Creates and returns a list iterator for non-destructively traversing
 *    list [l], or lsd_nomem_error() on failure.
 */

void list_iterator_reset (ListIterator i);
/*
 *  Resets the list iterator [i] to start traversal at the beginning
 *    of the list.
 */

void list_iterator_destroy (ListIterator i);
/*
 *  Destroys the list iterator [i]; list iterators not explicitly destroyed
 *    in this manner will be destroyed when the list is deallocated via
 *    list_destroy().
 */

void * list_next (ListIterator i);
/*
 *  Returns a ptr to the next item's data,
 *    or NULL once the end of the list is reached.
 *  Example: i=list_iterator_create(i); while ((x=list_next(i))) {...}
 */

void * list_insert (ListIterator i, void *x);
/*
 *  Inserts data [x] immediately before the last item returned via list
 *    iterator [i]; once the list iterator reaches the end of the list,
 *    insertion is made at the list's end.
 *  Returns the data's ptr, or lsd_nomem_error() if insertion failed.
 */

void * list_find (ListIterator i, ListFindF f, void *key);
/*
 *  Traverses the list from the point of the list iterator [i]
 *    using [f] to match each item with [key].
 *  Returns a ptr to the next item for which the function [f]
 *    returns non-zero, or NULL once the end of the list is reached.
 *  Example: i=list_iterator_reset(i); while ((x=list_find(i,f,k))) {...}
 */

void * list_remove (ListIterator i);
/*
 *  Removes from the list the last item returned via list iterator [i]
 *    and returns the data's ptr.
 *  Note: The client is responsible for freeing the returned data.
 */

int list_delete (ListIterator i);
/*
 *  Removes from the list the last item returned via list iterator [i];
 *    if a deletion function was specified when the list was created,
 *    it will be called to deallocate the item being removed.
 *  Returns a count of the number of items removed from the list
 *    (ie, '1' if the item was removed, and '0' otherwise).
 */


#endif /* !LSD_LIST_H */
