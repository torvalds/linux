! { dg-do run }
PROGRAM A2
  INCLUDE "omp_lib.h"      ! or USE OMP_LIB
  INTEGER X
  X=2
!$OMP PARALLEL NUM_THREADS(2) SHARED(X)
    IF (OMP_GET_THREAD_NUM() .EQ. 0) THEN
       X=5
    ELSE
    ! PRINT 1: The following read of x has a race
      PRINT *,"1: THREAD# ", OMP_GET_THREAD_NUM(), "X = ", X
    ENDIF
!$OMP BARRIER
    IF (OMP_GET_THREAD_NUM() .EQ. 0) THEN
    ! PRINT 2
      PRINT *,"2: THREAD# ", OMP_GET_THREAD_NUM(), "X = ", X
    ELSE
    ! PRINT 3
      PRINT *,"3: THREAD# ", OMP_GET_THREAD_NUM(), "X = ", X
    ENDIF
!$OMP END PARALLEL
END PROGRAM A2
