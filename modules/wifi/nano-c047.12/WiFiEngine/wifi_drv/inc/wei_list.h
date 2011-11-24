#ifndef __WEI_LIST_H
#define __WEI_LIST_H
/* **** */
/* Named and anonymous in-line list implementation */
typedef struct wei_list_head 
{
      struct wei_list_head *next;
      struct wei_list_head *prev;
} wei_list_head_t;

void __wei_list_insert(wei_list_head_t *old_el, struct wei_list_head *new_el);
void __wei_list_remove(wei_list_head_t *el);

#define WEI_LIST_HEAD wei_list_head_t __wei_list_head
#define WEI_INIT_LIST_HEAD(x) ((x)->__wei_list_head.next = (x)->__wei_list_head.prev = NULL)
#define WEI_LIST_INSERT(x, y) __wei_list_insert(&(x)->__wei_list_head, &(y)->__wei_list_head)
#define WEI_LIST_REMOVE(x) __wei_list_remove(&(x)->__wei_list_head)
#define __GET_LIST_ENTRY(ptr, type, member) \
	((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))

#define WEI_GET_NEXT_LIST_ENTRY(ptr, type) \
        ((ptr->__wei_list_head.next) ? __GET_LIST_ENTRY(ptr->__wei_list_head.next, type, __wei_list_head) : NULL)
#define WEI_LIST_IS_UNLINKED(x) ((x)->__wei_list_head.next == NULL && (x)->__wei_list_head.prev == NULL)

#define WEI_LIST_HEAD_NAMED(name) wei_list_head_t name
#define WEI_INIT_LIST_HEAD_NAMED(x, name) ((x)->name.next = (x)->name.prev = NULL)
#define WEI_LIST_INSERT_NAMED(x, y, name) __wei_list_insert(&(x)->name, &(y)->name)
#define WEI_LIST_REMOVE_NAMED(x, name) __wei_list_remove(&(x)->name)

#define WEI_GET_NEXT_LIST_ENTRY_NAMED(ptr, type, name) \
        ((ptr->name.next) ? __GET_LIST_ENTRY(ptr->name.next, type, name) : NULL)
#define WEI_LIST_IS_UNLINKED_NAMED(x, name) ((x)->name.next == NULL && (x)->name.prev == NULL)
/* **** */

#endif
