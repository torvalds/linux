/* Trivial test of barrier.  */

#include <omp.h>
#include <sys/time.h>
#include <unistd.h>
#include <assert.h>
#include "libgomp_g.h"


struct timeval stamps[3][3];

static void function(void *dummy)
{
  int iam = omp_get_thread_num ();

  gettimeofday (&stamps[iam][0], NULL);
  if (iam == 0)
    usleep (10);

  GOMP_barrier ();

  if (iam == 0)
    {
      gettimeofday (&stamps[0][1], NULL);
      usleep (10);
    }

  GOMP_barrier ();
  
  gettimeofday (&stamps[iam][2], NULL);
}

int main()
{
  omp_set_dynamic (0);

  GOMP_parallel_start (function, NULL, 3);
  function (NULL);
  GOMP_parallel_end ();

  assert (!timercmp (&stamps[0][0], &stamps[0][1], >));
  assert (!timercmp (&stamps[1][0], &stamps[0][1], >));
  assert (!timercmp (&stamps[2][0], &stamps[0][1], >));

  assert (!timercmp (&stamps[0][1], &stamps[0][2], >));
  assert (!timercmp (&stamps[0][1], &stamps[1][2], >));
  assert (!timercmp (&stamps[0][1], &stamps[2][2], >));

  return 0;
}
