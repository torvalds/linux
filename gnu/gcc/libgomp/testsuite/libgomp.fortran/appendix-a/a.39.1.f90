! { dg-do run }

      SUBROUTINE SKIP(ID)
      END SUBROUTINE SKIP
      SUBROUTINE WORK(ID)
      END SUBROUTINE WORK
      PROGRAM A39
        INCLUDE "omp_lib.h"      ! or USE OMP_LIB
        INTEGER(OMP_LOCK_KIND) LCK
        INTEGER ID
        CALL OMP_INIT_LOCK(LCK)
!$OMP PARALLEL SHARED(LCK) PRIVATE(ID)
          ID = OMP_GET_THREAD_NUM()
          CALL OMP_SET_LOCK(LCK)
          PRINT *, "My thread id is ", ID
          CALL OMP_UNSET_LOCK(LCK)
          DO WHILE (.NOT. OMP_TEST_LOCK(LCK))
            CALL SKIP(ID)     ! We do not yet have the lock
                              ! so we must do something else
          END DO
          CALL WORK(ID)       ! We now have the lock
                              ! and can do the work
          CALL OMP_UNSET_LOCK( LCK )
!$OMP END PARALLEL
        CALL OMP_DESTROY_LOCK( LCK )
        END PROGRAM A39
