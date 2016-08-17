/*****************************************************************************
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2001-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is from LSD-Tools, the LLNL Software Development Toolbox.
 *
 *  LSD-Tools is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  LSD-Tools is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with LSD-Tools.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************
 *  Refer to "list.h" for documentation on public functions.
 *****************************************************************************/

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif /* WITH_PTHREADS */

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "list.h"


/*********************
 *  lsd_fatal_error  *
 *********************/

#ifdef WITH_LSD_FATAL_ERROR_FUNC
#  undef lsd_fatal_error
   extern void lsd_fatal_error(char *file, int line, char *mesg);
#else /* !WITH_LSD_FATAL_ERROR_FUNC */
#  ifndef lsd_fatal_error
#    include <errno.h>
#    include <stdio.h>
#    include <string.h>
#    define lsd_fatal_error(file, line, mesg)                                 \
       do {                                                                   \
           fprintf(stderr, "ERROR: [%s:%d] %s: %s\n",                         \
                   file, line, mesg, strerror(errno));                        \
       } while (0)
#  endif /* !lsd_fatal_error */
#endif /* !WITH_LSD_FATAL_ERROR_FUNC */


/*********************
 *  lsd_nomem_error  *
 *********************/

#ifdef WITH_LSD_NOMEM_ERROR_FUNC
#  undef lsd_nomem_error
   extern void * lsd_nomem_error(char *file, int line, char *mesg);
#else /* !WITH_LSD_NOMEM_ERROR_FUNC */
#  ifndef lsd_nomem_error
#    define lsd_nomem_error(file, line, mesg) (NULL)
#  endif /* !lsd_nomem_error */
#endif /* !WITH_LSD_NOMEM_ERROR_FUNC */


/***************
 *  Constants  *
 ***************/

#define LIST_ALLOC 32
#define LIST_MAGIC 0xDEADBEEF


/****************
 *  Data Types  *
 ****************/

struct listNode {
    void                 *data;         /* node's data                       */
    struct listNode      *next;         /* next node in list                 */
};

struct listIterator {
    struct list          *list;         /* the list being iterated           */
    struct listNode      *pos;          /* the next node to be iterated      */
    struct listNode     **prev;         /* addr of 'next' ptr to prv It node */
    struct listIterator  *iNext;        /* iterator chain for list_destroy() */
#ifndef NDEBUG
    unsigned int          magic;        /* sentinel for asserting validity   */
#endif /* !NDEBUG */
};

struct list {
    struct listNode      *head;         /* head of the list                  */
    struct listNode     **tail;         /* addr of last node's 'next' ptr    */
    struct listIterator  *iNext;        /* iterator chain for list_destroy() */
    ListDelF              fDel;         /* function to delete node data      */
    int                   count;        /* number of nodes in list           */
#ifdef WITH_PTHREADS
    pthread_mutex_t       mutex;        /* mutex to protect access to list   */
#endif /* WITH_PTHREADS */
#ifndef NDEBUG
    unsigned int          magic;        /* sentinel for asserting validity   */
#endif /* !NDEBUG */
};

typedef struct listNode * ListNode;


/****************
 *  Prototypes  *
 ****************/

static void * list_node_create (List l, ListNode *pp, void *x);
static void * list_node_destroy (List l, ListNode *pp);
static List list_alloc (void);
static void list_free (List l);
static ListNode list_node_alloc (void);
static void list_node_free (ListNode p);
static ListIterator list_iterator_alloc (void);
static void list_iterator_free (ListIterator i);
static void * list_alloc_aux (int size, void *pfreelist);
static void list_free_aux (void *x, void *pfreelist);


/***************
 *  Variables  *
 ***************/

static List list_free_lists = NULL;
static ListNode list_free_nodes = NULL;
static ListIterator list_free_iterators = NULL;

#ifdef WITH_PTHREADS
static pthread_mutex_t list_free_lock = PTHREAD_MUTEX_INITIALIZER;
#endif /* WITH_PTHREADS */


/************
 *  Macros  *
 ************/

#ifdef WITH_PTHREADS

#  define list_mutex_init(mutex)                                              \
     do {                                                                     \
         int e = pthread_mutex_init(mutex, NULL);                             \
         if (e != 0) {                                                        \
             errno = e;                                                       \
             lsd_fatal_error(__FILE__, __LINE__, "list mutex init");          \
             abort();                                                         \
         }                                                                    \
     } while (0)

#  define list_mutex_lock(mutex)                                              \
     do {                                                                     \
         int e = pthread_mutex_lock(mutex);                                   \
         if (e != 0) {                                                        \
             errno = e;                                                       \
             lsd_fatal_error(__FILE__, __LINE__, "list mutex lock");          \
             abort();                                                         \
         }                                                                    \
     } while (0)

#  define list_mutex_unlock(mutex)                                            \
     do {                                                                     \
         int e = pthread_mutex_unlock(mutex);                                 \
         if (e != 0) {                                                        \
             errno = e;                                                       \
             lsd_fatal_error(__FILE__, __LINE__, "list mutex unlock");        \
             abort();                                                         \
         }                                                                    \
     } while (0)

#  define list_mutex_destroy(mutex)                                           \
     do {                                                                     \
         int e = pthread_mutex_destroy(mutex);                                \
         if (e != 0) {                                                        \
             errno = e;                                                       \
             lsd_fatal_error(__FILE__, __LINE__, "list mutex destroy");       \
             abort();                                                         \
         }                                                                    \
     } while (0)

#  ifndef NDEBUG
     static int list_mutex_is_locked (pthread_mutex_t *mutex);
#  endif /* !NDEBUG */

#else /* !WITH_PTHREADS */

#  define list_mutex_init(mutex)
#  define list_mutex_lock(mutex)
#  define list_mutex_unlock(mutex)
#  define list_mutex_destroy(mutex)
#  define list_mutex_is_locked(mutex) (1)

#endif /* !WITH_PTHREADS */


/***************
 *  Functions  *
 ***************/

List
list_create (ListDelF f)
{
    List l;

    if (!(l = list_alloc()))
        return(lsd_nomem_error(__FILE__, __LINE__, "list create"));
    l->head = NULL;
    l->tail = &l->head;
    l->iNext = NULL;
    l->fDel = f;
    l->count = 0;
    list_mutex_init(&l->mutex);
#ifndef NDEBUG
    l->magic = LIST_MAGIC;
#endif
    return(l);
}


void
list_destroy (List l)
{
    ListIterator i, iTmp;
    ListNode p, pTmp;

    assert(l != NULL);
    list_mutex_lock(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    i = l->iNext;
    while (i) {
        assert(i->magic == LIST_MAGIC);
        iTmp = i->iNext;
#ifndef NDEBUG
        i->magic = ~LIST_MAGIC;
#endif /* !NDEBUG */
        list_iterator_free(i);
        i = iTmp;
    }
    p = l->head;
    while (p) {
        pTmp = p->next;
        if (p->data && l->fDel)
            l->fDel(p->data);
        list_node_free(p);
        p = pTmp;
    }
#ifndef NDEBUG
    l->magic = ~LIST_MAGIC;
#endif /* !NDEBUG */
    list_mutex_unlock(&l->mutex);
    list_mutex_destroy(&l->mutex);
    list_free(l);
    return;
}


int
list_is_empty (List l)
{
    int n;

    assert(l != NULL);
    list_mutex_lock(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    n = l->count;
    list_mutex_unlock(&l->mutex);
    return(n == 0);
}


int
list_count (List l)
{
    int n;

    assert(l != NULL);
    list_mutex_lock(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    n = l->count;
    list_mutex_unlock(&l->mutex);
    return(n);
}


void *
list_append (List l, void *x)
{
    void *v;

    assert(l != NULL);
    assert(x != NULL);
    list_mutex_lock(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    v = list_node_create(l, l->tail, x);
    list_mutex_unlock(&l->mutex);
    return(v);
}


void *
list_prepend (List l, void *x)
{
    void *v;

    assert(l != NULL);
    assert(x != NULL);
    list_mutex_lock(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    v = list_node_create(l, &l->head, x);
    list_mutex_unlock(&l->mutex);
    return(v);
}


void *
list_find_first (List l, ListFindF f, void *key)
{
    ListNode p;
    void *v = NULL;

    assert(l != NULL);
    assert(f != NULL);
    list_mutex_lock(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    for (p=l->head; p; p=p->next) {
        if (f(p->data, key)) {
            v = p->data;
            break;
        }
    }
    list_mutex_unlock(&l->mutex);
    return(v);
}


int
list_delete_all (List l, ListFindF f, void *key)
{
    ListNode *pp;
    void *v;
    int n = 0;

    assert(l != NULL);
    assert(f != NULL);
    list_mutex_lock(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    pp = &l->head;
    while (*pp) {
        if (f((*pp)->data, key)) {
            if ((v = list_node_destroy(l, pp))) {
                if (l->fDel)
                    l->fDel(v);
                n++;
            }
        }
        else {
            pp = &(*pp)->next;
        }
    }
    list_mutex_unlock(&l->mutex);
    return(n);
}


int
list_for_each (List l, ListForF f, void *arg)
{
    ListNode p;
    int n = 0;

    assert(l != NULL);
    assert(f != NULL);
    list_mutex_lock(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    for (p=l->head; p; p=p->next) {
        n++;
        if (f(p->data, arg) < 0) {
            n = -n;
            break;
        }
    }
    list_mutex_unlock(&l->mutex);
    return(n);
}


void
list_sort (List l, ListCmpF f)
{
/*  Note: Time complexity O(n^2).
 */
    ListNode *pp, *ppPrev, *ppPos, pTmp;
    ListIterator i;

    assert(l != NULL);
    assert(f != NULL);
    list_mutex_lock(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    if (l->count > 1) {
        ppPrev = &l->head;
        pp = &(*ppPrev)->next;
        while (*pp) {
            if (f((*pp)->data, (*ppPrev)->data) < 0) {
                ppPos = &l->head;
                while (f((*pp)->data, (*ppPos)->data) >= 0)
                    ppPos = &(*ppPos)->next;
                pTmp = (*pp)->next;
                (*pp)->next = *ppPos;
                *ppPos = *pp;
                *pp = pTmp;
                if (ppPrev == ppPos)
                    ppPrev = &(*ppPrev)->next;
            }
            else {
                ppPrev = pp;
                pp = &(*pp)->next;
            }
        }
        l->tail = pp;

        for (i=l->iNext; i; i=i->iNext) {
            assert(i->magic == LIST_MAGIC);
            i->pos = i->list->head;
            i->prev = &i->list->head;
        }
    }
    list_mutex_unlock(&l->mutex);
    return;
}


void *
list_push (List l, void *x)
{
    void *v;

    assert(l != NULL);
    assert(x != NULL);
    list_mutex_lock(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    v = list_node_create(l, &l->head, x);
    list_mutex_unlock(&l->mutex);
    return(v);
}


void *
list_pop (List l)
{
    void *v;

    assert(l != NULL);
    list_mutex_lock(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    v = list_node_destroy(l, &l->head);
    list_mutex_unlock(&l->mutex);
    return(v);
}


void *
list_peek (List l)
{
    void *v;

    assert(l != NULL);
    list_mutex_lock(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    v = (l->head) ? l->head->data : NULL;
    list_mutex_unlock(&l->mutex);
    return(v);
}


void *
list_enqueue (List l, void *x)
{
    void *v;

    assert(l != NULL);
    assert(x != NULL);
    list_mutex_lock(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    v = list_node_create(l, l->tail, x);
    list_mutex_unlock(&l->mutex);
    return(v);
}


void *
list_dequeue (List l)
{
    void *v;

    assert(l != NULL);
    list_mutex_lock(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    v = list_node_destroy(l, &l->head);
    list_mutex_unlock(&l->mutex);
    return(v);
}


ListIterator
list_iterator_create (List l)
{
    ListIterator i;

    assert(l != NULL);
    if (!(i = list_iterator_alloc()))
        return(lsd_nomem_error(__FILE__, __LINE__, "list iterator create"));
    i->list = l;
    list_mutex_lock(&l->mutex);
    assert(l->magic == LIST_MAGIC);
    i->pos = l->head;
    i->prev = &l->head;
    i->iNext = l->iNext;
    l->iNext = i;
#ifndef NDEBUG
    i->magic = LIST_MAGIC;
#endif /* !NDEBUG */
    list_mutex_unlock(&l->mutex);
    return(i);
}


void
list_iterator_reset (ListIterator i)
{
    assert(i != NULL);
    assert(i->magic == LIST_MAGIC);
    list_mutex_lock(&i->list->mutex);
    assert(i->list->magic == LIST_MAGIC);
    i->pos = i->list->head;
    i->prev = &i->list->head;
    list_mutex_unlock(&i->list->mutex);
    return;
}


void
list_iterator_destroy (ListIterator i)
{
    ListIterator *pi;

    assert(i != NULL);
    assert(i->magic == LIST_MAGIC);
    list_mutex_lock(&i->list->mutex);
    assert(i->list->magic == LIST_MAGIC);
    for (pi=&i->list->iNext; *pi; pi=&(*pi)->iNext) {
        assert((*pi)->magic == LIST_MAGIC);
        if (*pi == i) {
            *pi = (*pi)->iNext;
            break;
        }
    }
    list_mutex_unlock(&i->list->mutex);
#ifndef NDEBUG
    i->magic = ~LIST_MAGIC;
#endif /* !NDEBUG */
    list_iterator_free(i);
    return;
}


void *
list_next (ListIterator i)
{
    ListNode p;

    assert(i != NULL);
    assert(i->magic == LIST_MAGIC);
    list_mutex_lock(&i->list->mutex);
    assert(i->list->magic == LIST_MAGIC);
    if ((p = i->pos))
        i->pos = p->next;
    if (*i->prev != p)
        i->prev = &(*i->prev)->next;
    list_mutex_unlock(&i->list->mutex);
    return(p ? p->data : NULL);
}


void *
list_insert (ListIterator i, void *x)
{
    void *v;

    assert(i != NULL);
    assert(x != NULL);
    assert(i->magic == LIST_MAGIC);
    list_mutex_lock(&i->list->mutex);
    assert(i->list->magic == LIST_MAGIC);
    v = list_node_create(i->list, i->prev, x);
    list_mutex_unlock(&i->list->mutex);
    return(v);
}


void *
list_find (ListIterator i, ListFindF f, void *key)
{
    void *v;

    assert(i != NULL);
    assert(f != NULL);
    assert(i->magic == LIST_MAGIC);
    while ((v=list_next(i)) && !f(v,key)) {;}
    return(v);
}


void *
list_remove (ListIterator i)
{
    void *v = NULL;

    assert(i != NULL);
    assert(i->magic == LIST_MAGIC);
    list_mutex_lock(&i->list->mutex);
    assert(i->list->magic == LIST_MAGIC);
    if (*i->prev != i->pos)
        v = list_node_destroy(i->list, i->prev);
    list_mutex_unlock(&i->list->mutex);
    return(v);
}


int
list_delete (ListIterator i)
{
    void *v;

    assert(i != NULL);
    assert(i->magic == LIST_MAGIC);
    if ((v = list_remove(i))) {
        if (i->list->fDel)
            i->list->fDel(v);
        return(1);
    }
    return(0);
}


static void *
list_node_create (List l, ListNode *pp, void *x)
{
/*  Inserts data pointed to by [x] into list [l] after [pp],
 *    the address of the previous node's "next" ptr.
 *  Returns a ptr to data [x], or NULL if insertion fails.
 *  This routine assumes the list is already locked upon entry.
 */
    ListNode p;
    ListIterator i;

    assert(l != NULL);
    assert(l->magic == LIST_MAGIC);
    assert(list_mutex_is_locked(&l->mutex));
    assert(pp != NULL);
    assert(x != NULL);
    if (!(p = list_node_alloc()))
        return(lsd_nomem_error(__FILE__, __LINE__, "list node create"));
    p->data = x;
    if (!(p->next = *pp))
        l->tail = &p->next;
    *pp = p;
    l->count++;
    for (i=l->iNext; i; i=i->iNext) {
        assert(i->magic == LIST_MAGIC);
        if (i->prev == pp)
            i->prev = &p->next;
        else if (i->pos == p->next)
            i->pos = p;
        assert((i->pos == *i->prev) || (i->pos == (*i->prev)->next));
    }
    return(x);
}


static void *
list_node_destroy (List l, ListNode *pp)
{
/*  Removes the node pointed to by [*pp] from from list [l],
 *    where [pp] is the address of the previous node's "next" ptr.
 *  Returns the data ptr associated with list item being removed,
 *    or NULL if [*pp] points to the NULL element.
 *  This routine assumes the list is already locked upon entry.
 */
    void *v;
    ListNode p;
    ListIterator i;

    assert(l != NULL);
    assert(l->magic == LIST_MAGIC);
    assert(list_mutex_is_locked(&l->mutex));
    assert(pp != NULL);
    if (!(p = *pp))
        return(NULL);
    v = p->data;
    if (!(*pp = p->next))
        l->tail = pp;
    l->count--;
    for (i=l->iNext; i; i=i->iNext) {
        assert(i->magic == LIST_MAGIC);
        if (i->pos == p)
            i->pos = p->next, i->prev = pp;
        else if (i->prev == &p->next)
            i->prev = pp;
        assert((i->pos == *i->prev) || (i->pos == (*i->prev)->next));
    }
    list_node_free(p);
    return(v);
}


static List
list_alloc (void)
{
    return(list_alloc_aux(sizeof(struct list), &list_free_lists));
}


static void
list_free (List l)
{
    list_free_aux(l, &list_free_lists);
    return;
}


static ListNode
list_node_alloc (void)
{
    return(list_alloc_aux(sizeof(struct listNode), &list_free_nodes));
}


static void
list_node_free (ListNode p)
{
    list_free_aux(p, &list_free_nodes);
    return;
}


static ListIterator
list_iterator_alloc (void)
{
    return(list_alloc_aux(sizeof(struct listIterator), &list_free_iterators));
}


static void
list_iterator_free (ListIterator i)
{
    list_free_aux(i, &list_free_iterators);
    return;
}


static void *
list_alloc_aux (int size, void *pfreelist)
{
/*  Allocates an object of [size] bytes from the freelist [*pfreelist].
 *  Memory is added to the freelist in chunks of size LIST_ALLOC.
 *  Returns a ptr to the object, or NULL if the memory request fails.
 */
    void **px;
    void **pfree = pfreelist;
    void **plast;

    assert(sizeof(char) == 1);
    assert(size >= (int)sizeof(void *));
    assert(pfreelist != NULL);
    assert(LIST_ALLOC > 0);
    list_mutex_lock(&list_free_lock);
    if (!*pfree) {
        if ((*pfree = malloc(LIST_ALLOC * size))) {
            px = *pfree;
            plast = (void **) ((char *) *pfree + ((LIST_ALLOC - 1) * size));
            while (px < plast)
                *px = (char *) px + size, px = *px;
            *plast = NULL;
        }
    }
    if ((px = *pfree))
        *pfree = *px;
    else
        errno = ENOMEM;
    list_mutex_unlock(&list_free_lock);
    return(px);
}


static void
list_free_aux (void *x, void *pfreelist)
{
/*  Frees the object [x], returning it to the freelist [*pfreelist].
 */
    void **px = x;
    void **pfree = pfreelist;

    assert(x != NULL);
    assert(pfreelist != NULL);
    list_mutex_lock(&list_free_lock);
    *px = *pfree;
    *pfree = px;
    list_mutex_unlock(&list_free_lock);
    return;
}


#ifndef NDEBUG
#ifdef WITH_PTHREADS
static int
list_mutex_is_locked (pthread_mutex_t *mutex)
{
/*  Returns true if the mutex is locked; o/w, returns false.
 */
    int rc;

    assert(mutex != NULL);
    rc = pthread_mutex_trylock(mutex);
    return(rc == EBUSY ? 1 : 0);
}
#endif /* WITH_PTHREADS */
#endif /* !NDEBUG */
