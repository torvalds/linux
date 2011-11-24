#include <stdio.h>
#include <stdlib.h>
#include "wei_tailq.h"

/* This file serves two purposes, one is to give example usage of the
 * TAILQ macros, the other is to provide a test case. Because of the
 * last requirement, an attempt has been made to make use of all
 * macros. */


/* compute number of bits in a given number */
static unsigned int bitcount(unsigned int val)
{
   unsigned int bc = 0;
   while(val != 0) {
      if((val & 1))
         bc++;
      val >>= 1;
   }
   return bc;
}


/* this is the data we wish to put on linked lists */
struct foo {
   unsigned int number;			/* data value */
   unsigned int bitcount;		/* set bits in number */
   WEI_TQ_ENTRY(foo) by_number;		/* list entry sorted by number */
   WEI_TQ_ENTRY(foo) by_bitcount;	/* list entry sorted by bitcount */
   unsigned int usecount;		/* if > 0, then in use somewhere */
   enum {	 			/* allocation information */
      FREE,					/* free in static block */
      USED, 					/* allocated in static block */
      ALLOC 					/* allocated with malloc */
   } status;
};

/* declare a head structure for foo */
WEI_TQ_HEAD(foo_head, foo);

/* and define two heads, one for ordering by number, the other for
 * ordering by bitcount */

/* use static initialisation */
static struct foo_head foo_bynumber = WEI_TQ_HEAD_INITIALIZER(foo_bynumber);

/* will be initialised in main() */
static struct foo_head foo_bybitcount;

/* this is a block of static foo entries, we prefer these to
   allocating new entries with malloc */
#define NSTATIC 4
struct foo foo_block[NSTATIC];

/* get a new foo, and initialise fields */
static struct foo*
new_foo(int number)
{
   int i;
   struct foo *p = NULL;

   /* first try to find a free static entry */
   for(i = 0; i < NSTATIC; i++)
      if(foo_block[i].status == FREE) {
         p = &foo_block[i];
         p->status = USED;
         break;
      }
   if(p == NULL) {
      /* we didn't find any, so allocate a new one */
      p = malloc(sizeof(*p));
      if(p == NULL)
         return NULL;
      p->status = ALLOC;
   }
   p->usecount = 0;
   p->number = number;
   p->bitcount = bitcount(p->number);
   return p;
}

/* decrease usecount, and free if zero */
static void
free_foo(struct foo *p)
{
   if(--p->usecount == 0) {
      if(p->status == ALLOC)
         free(p);
      else if(p->status == USED)
         p->status = FREE;
      else
         abort();
   }
}

static void
print_foo(struct foo *p)
{
   int ch;
   switch(p->status) {
      case FREE:
         ch = 'F';
         break;
      case USED:
         ch = 'U';
         break;
      case ALLOC:
         ch = 'A';
         break;
      default:
         ch = '?';
         break;
   }
   printf("%d/%d[%d%c] ", p->number, p->bitcount, p->usecount, ch);
}

/* print the two queues */
static void
print_queues(void)
{
   struct foo *p;

   printf("by number:   ");
   WEI_TQ_FOREACH(p, &foo_bynumber, by_number) {
      print_foo(p);
   }
   printf("\n");

   printf("by bitcount: ");
   WEI_TQ_FOREACH(p, &foo_bybitcount, by_bitcount) {
      print_foo(p);
   }
   printf("\n");
}

/* insert an entry on the list sorted by number */
void
insert_bynumber(struct foo *elm)
{
   struct foo *q;
   /* find first entry where number is greater than ours */
   WEI_TQ_FOREACH(q, &foo_bynumber, by_number) {
      if(q->number > elm->number)
         break;
   }
   if(q == NULL)
      /* not found, so insert at tail */
      WEI_TQ_INSERT_TAIL(&foo_bynumber, elm, by_number);
   else
      /* found, so insert before that entry */
      WEI_TQ_INSERT_BEFORE(&head, q, elm, by_number);
   elm->usecount++;
}

/* insert an entry on the list sorted by bitcount */
void
insert_bybitcount(struct foo *elm)
{
   struct foo *q;
   WEI_TQ_FOREACH(q, &foo_bybitcount,  by_bitcount) {
      if(q->bitcount > elm->bitcount)
         break;
   }
   if(q == NULL)
      WEI_TQ_INSERT_TAIL(&foo_bybitcount, elm, by_bitcount);
   else
      WEI_TQ_INSERT_BEFORE(&head, q, elm, by_bitcount);
   elm->usecount++;
}

void
new_entry(int number)
{
   struct foo *p;

   p = new_foo(number);

   insert_bynumber(p);
   insert_bybitcount(p);
}

int main(int argc, char **argv)
{
   struct foo *p, *q;
   int i;

   WEI_TQ_INIT(&foo_bybitcount); /* initialise head strucure */
   
   /* this shows how to insert at the head of a list, we know the list
    * is empty, so inserting at head will not break sort order */
   p = new_foo(10);
   WEI_TQ_INSERT_HEAD(&foo_bynumber, p, by_number);
   p->usecount++;

   q = new_foo(20);
   WEI_TQ_INSERT_AFTER(&foo_bynumber, p, q, by_number);
   q->usecount++;

   /* add a few entries to both lists */
   new_entry(0);
   new_entry(1);
   new_entry(2);
   new_entry(3);
   new_entry(4);
   new_entry(5);
   new_entry(6);
   new_entry(7);

   print_queues();

   /* find last element that has a bitcount of 1 */
   WEI_TQ_FOREACH_REVERSE(p, &foo_bybitcount, foo_head, by_bitcount) {
      if(p->bitcount == 1)
         break;
   }
   /* and remove it from the by_number list */
   if(p != NULL) {
      WEI_TQ_REMOVE(&foo_bynumber, p, by_number);
      free_foo(p);
   }
   print_queues();

   /* remove everything from by-number list */
   while(!WEI_TQ_EMPTY(&foo_bynumber)) {
      p = WEI_TQ_FIRST(&foo_bynumber);
      WEI_TQ_REMOVE(&foo_bynumber, p, by_number);
      free_foo(p);
   }
   print_queues();

   /* remove everything from by-bitcount list */
   while(!WEI_TQ_EMPTY(&foo_bybitcount)) {
      p = WEI_TQ_FIRST(&foo_bybitcount);
      WEI_TQ_REMOVE(&foo_bybitcount, p, by_bitcount);
      free_foo(p);
   }
   print_queues();

   /* and for show, print static block, they should all be free'd by now */
   printf("static block: ");
   for(i = 0; i < NSTATIC; i++)
      print_foo(&foo_block[i]);
   printf("\n");

   return 0;
}
