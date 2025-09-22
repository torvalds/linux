! { dg-do run }
      PROGRAM A5
        INCLUDE "omp_lib.h"      ! or USE OMP_LIB
        CALL OMP_SET_DYNAMIC(.TRUE.)
!$OMP PARALLEL NUM_THREADS(10)
            ! do work here
!$OMP END PARALLEL
      END PROGRAM A5
