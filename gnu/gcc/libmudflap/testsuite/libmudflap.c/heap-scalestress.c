/* zz30
 *
 * demonstrate a splay-tree depth problem
*/

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#ifndef SCALE
#define SCALE 100000
#endif


struct list
{
  struct list *next;
};


int
main ()
{
  struct list *head = NULL;
  struct list *tail = NULL;
  struct list *p;
  long n;
  int direction;

  for (direction = 0; direction < 2; direction++)
    {
      fprintf (stdout, "allocating\n");
      fflush (stdout);

      for (n = 0; n < SCALE; ++n)
	{
	  p = malloc (sizeof *p);
	  if (NULL == p)
	    {
	      fprintf (stdout, "malloc failed\n");
	      break;
	    }
	  if (direction == 0)
	    {			/* add at tail */
	      p->next = NULL;
	      if (NULL != tail)
		tail->next = p;
	      else
		head = p;
	      tail = p;
	    }
	  else
	    {			/* add at head */
	      p->next = head;
	      if (NULL == tail)
		tail = p;
	      head = p;
	    }
	}

      fprintf (stdout, "freeing\n");
      fflush (stdout);

      while (NULL != head)
	{
	  p = head;
	  head = head->next;
	  free (p);
	}

    }

  fprintf (stdout, "done\n");
  fflush (stdout);

  return (0);
}

/* { dg-output "allocating.*freeing.*allocating.*freeing.*done" } */
