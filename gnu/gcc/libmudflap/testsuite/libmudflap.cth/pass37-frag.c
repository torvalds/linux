#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <sched.h>

static void *
func (void *p)
{
  int *counter = (int *) p;
  unsigned i;
  
  for (i=0; i<100; i++)
    {
      (*counter) ++;
      {
	int array[17];
	unsigned x = i % (sizeof(array)/sizeof(array[0]));
	/* VRP could prove that x is within [0,16], but until then, the
	   following access will ensure that array[] is registered to
	   libmudflap. */
	array[x] = i;
      }
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
