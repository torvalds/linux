#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <sched.h>
#include <assert.h>

static void *
func (void *p)
{
  int *counter = (int *) p;
  unsigned i;
  enum { numarrays = 100, numels = 17 };
  char *arrays [numarrays];

  for (i=0; i<numarrays; i++)
    {
      (*counter) ++;
      unsigned x = i % numels;
      arrays[i] = calloc (numels, sizeof(arrays[i][0]));
      assert (arrays[i] != NULL);
      arrays[i][x] = i;
      free (arrays[i]);
      sched_yield (); /* sleep (1); */
    }

  return (NULL);
}


int main ()
{
  int rc;
  unsigned i;
  enum foo { NT=10 };
  pthread_t threads[NT];
  int counts[NT];


  for (i=0; i<NT; i++)
    {
      counts[i] = 0;
      rc = pthread_create (& threads[i], NULL, func, (void *) & counts[i]);
      if (rc) abort();
    }

  for (i=0; i<NT; i++)
    {
      rc = pthread_join (threads[i], NULL);
      if (rc) abort();       
     printf ("%d%s", counts[i], (i==NT-1) ? "\n" : " ");
    }

  return 0;
}
/* { dg-output "100 100 100 100 100 100 100 100 100 100" } */
/* { dg-repetitions 20 } */
/* { dg-timeout 10 } */
