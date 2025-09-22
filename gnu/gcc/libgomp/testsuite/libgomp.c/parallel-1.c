/* Trivial test of thread startup.  */

#include <omp.h>
#include <string.h>
#include <assert.h>
#include "libgomp_g.h"


static int nthr;
static int saw[4];

static void function(void *dummy)
{
  int iam = omp_get_thread_num ();

  if (iam == 0)
    nthr = omp_get_num_threads ();

  saw[iam] = 1;
}

int main()
{
  omp_set_dynamic (0);

  GOMP_parallel_start (function, NULL, 2);
  function (NULL);
  GOMP_parallel_end ();

  assert (nthr == 2);
  assert (saw[0] != 0);
  assert (saw[1] != 0);
  assert (saw[2] == 0);

  memset (saw, 0, sizeof (saw));
  
  GOMP_parallel_start (function, NULL, 3);
  function (NULL);
  GOMP_parallel_end ();

  assert (nthr == 3);
  assert (saw[0] != 0);
  assert (saw[1] != 0);
  assert (saw[2] != 0);
  assert (saw[3] == 0);

  return 0;
}
