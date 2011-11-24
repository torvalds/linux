/* This file defines a set of macros for tailq lists (doubly linked
 * list with tail pointer). There are operations for inserting at head
 * and tail of a list, inserting before and after certain entries,
 * removing entries, and looping over lists.
 *
 * This implementation is derived from 
 * the TAILQ implementation in 4.4BSD.
 */

/* $Id: wei_tailq.h,v 1.1 2006-12-07 16:08:22 joda Exp $ */

#ifndef __wei_tailq_h__
#define __wei_tailq_h__

/*! Evaluates to the first entry on the list or NULL if empty 
 *  @param head A pointer to the list head.
 */
#define WEI_TQ_FIRST(head) ((head)->tqh_first)

/*! Evaluates to the last entry on the list or NULL if empty 
 *  @param head A pointer to the list head.
 *  @param headname The name of the head struct (~typeof(head), if that had existed in C).
 */
#define WEI_TQ_LAST(head, headname)                     \
   (*(((struct headname*)(head)->tqh_last)->tqh_last))

/*! Evaluates to true, if the list is empty.
 *  @param head A pointer to the list head.
 */
#define WEI_TQ_EMPTY(head) (WEI_TQ_FIRST(head) == NULL)

/*! Evaluates to the next entry on the list, or NULL if last.
 *  @param elm  An entry on the list.
 *  @param field The name of the list entry field in elm.
 */
#define WEI_TQ_NEXT(elm, field) ((elm)->field.tqe_next)

/*! Evaluates to the previous entry on the list, or NULL if last.
 *  @param elm  An entry on the list.
 *  @param field The name of the list entry field in elm.
 *  @param headname The name of the head struct.
 */
#define WEI_TQ_PREV(elm, headname, field)                       \
   (*(((struct headname*)(elm)->field.tqe_prev)->tqh_last))

/*! Declare a head structure for a given type.
 *  @param name  The name of the head structure.
 *  @param type The type of entries put on the list.
 */
#define WEI_TQ_HEAD(name, type) struct name {   \
   struct type *tqh_first;                      \
   struct type **tqh_last;                      \
}

/*! Statically inits a list head.
 *  @param head The name of the list head to initialise.
 */
#define	WEI_TQ_HEAD_INITIALIZER(head)           \
	{ NULL, &(head).tqh_first }


/*! Dynamically inits a list head.
 *  @param head A pointer to the list head to initialise.
 */
#define	WEI_TQ_INIT(head) do {                  \
	(head)->tqh_first = NULL;               \
	(head)->tqh_last = &(head)->tqh_first;  \
} while (0)

/*! Declare a list entry structure, include one in your data structure
 *  for each list it could be on at the same time.
 *  @param type The type of entries put on the list.
 */
#define WEI_TQ_ENTRY(type) struct {             \
   struct type *tqe_next;                       \
   struct type **tqe_prev;                      \
}

/*! Insert a list entry at the head of the list.
 *  @param head A pointer to the list head.
 *  @param elm  The entry to insert.
 *  @param field The name of the list entry field in elm.
 */
#define WEI_TQ_INSERT_HEAD(head, elm, field) do {                       \
   if((WEI_TQ_NEXT(elm, field) = WEI_TQ_FIRST(head)) == NULL)           \
      (head)->tqh_last = &WEI_TQ_NEXT(elm, field);                      \
   else                                                                 \
      WEI_TQ_FIRST(head)->field.tqe_prev = &WEI_TQ_NEXT(elm, field);    \
   WEI_TQ_FIRST(head) = (elm);                                          \
   (elm)->field.tqe_prev = &(head)->tqh_first;                          \
} while(0)

/*! Insert a list entry at the tail of the list.
 *  @param head A pointer to the list head.
 *  @param elm  The entry to insert.
 *  @param field The name of the list entry field in elm.
 */
#define WEI_TQ_INSERT_TAIL(head, elm, field) do {        \
   WEI_TQ_NEXT(elm, field) = NULL;                              \
   (elm)->field.tqe_prev = (head)->tqh_last;            \
   *(head)->tqh_last = (elm);                           \
   (head)->tqh_last = &WEI_TQ_NEXT(elm, field);                 \
} while(0)


/*! Insert a list entry after a given entry.
 *  @param head A pointer to the list head.
 *  @param listelm  An entry that is already on the list.
 *  @param elm  The entry to insert.
 *  @param field The name of the list entry field in elm.
 */
#define WEI_TQ_INSERT_AFTER(head, listelm, elm, field) do {               \
   if((WEI_TQ_NEXT(elm, field) = WEI_TQ_NEXT(listelm, field)) == NULL)    \
      (head)->tqh_last = &WEI_TQ_NEXT(elm, field);                        \
   else                                                                   \
      WEI_TQ_NEXT(elm, field)->field.tqe_prev = &WEI_TQ_NEXT(elm, field); \
   WEI_TQ_NEXT(listelm, field) = (elm);                                   \
   (elm)->field.tqe_prev = &WEI_TQ_NEXT(elm, field);                      \
} while(0)

/*! Insert a list entry before a given entry.
 *  @param head A pointer to the list head.
 *  @param listelm  An entry that is already on the list.
 *  @param elm  The entry to insert.
 *  @param field The name of the list entry field in elm.
 */
#define WEI_TQ_INSERT_BEFORE(head, listelm, elm, field) do {    \
   (elm)->field.tqe_prev = (listelm)->field.tqe_prev;           \
   WEI_TQ_NEXT(elm, field) = (listelm);                         \
   *(listelm)->field.tqe_prev = (elm);                          \
   (listelm)->field.tqe_prev = &WEI_TQ_NEXT(elm, field);        \
} while(0)

/*! Remove an entry from a list.
 *  @param head A pointer to the list head.
 *  @param elm  The entry to remove.
 *  @param field The name of the list entry field in elm.
 */
#define WEI_TQ_REMOVE(head, elm, field) do {                            \
   if(WEI_TQ_NEXT(elm, field) == NULL)                                  \
      (head)->tqh_last = (elm)->field.tqe_prev;                         \
   else                                                                 \
      WEI_TQ_NEXT(elm, field)->field.tqe_prev = (elm)->field.tqe_prev;  \
   *(elm)->field.tqe_prev = WEI_TQ_NEXT(elm, field);                    \
} while(0)

/*! Loop over all entries in a list.
 *  @param var A variable used for each entry.
 *  @param head A pointer to the list head.
 *  @param field The name of the list entry field in elm.
 */
#define WEI_TQ_FOREACH(var, head, field)        \
   for((var) = WEI_TQ_FIRST(head);              \
       (var) != NULL;                           \
       (var) = WEI_TQ_NEXT(var, field))

/*! Loop over all entries in a list.
 *  @param var A variable used for each entry.
 *  @param head A pointer to the list head.
 *  @param headname The name of the head struct (~typeof(head), if that had existed in C).
 *  @param field The name of the list entry field in elm.
 */
#define WEI_TQ_FOREACH_REVERSE(var, head, headname, field)      \
   for (var = WEI_TQ_LAST(head, headname);                      \
        var;                                                    \
        var = WEI_TQ_PREV((var), headname, field))

#endif /* __wei_tailq_h__ */
