! { dg-do compile }
! { dg-options "-ffixed-form" }
        MODULE DATA
        USE OMP_LIB, ONLY: OMP_NEST_LOCK_KIND
        TYPE LOCKED_PAIR
        INTEGER A
        INTEGER B
        INTEGER (OMP_NEST_LOCK_KIND) LCK
        END TYPE
            END MODULE DATA
        SUBROUTINE INCR_A(P, A)
            ! called only from INCR_PAIR, no need to lock
            USE DATA
            TYPE(LOCKED_PAIR) :: P
            INTEGER A
            P%A = P%A + A
        END SUBROUTINE INCR_A
        SUBROUTINE INCR_B(P, B)
            ! called from both INCR_PAIR and elsewhere,
            ! so we need a nestable lock
            USE OMP_LIB       ! or INCLUDE "omp_lib.h"
            USE DATA
            TYPE(LOCKED_PAIR) :: P
            INTEGER B
            CALL OMP_SET_NEST_LOCK(P%LCK)
            P%B = P%B + B
            CALL OMP_UNSET_NEST_LOCK(P%LCK)
        END SUBROUTINE INCR_B
        SUBROUTINE INCR_PAIR(P, A, B)
            USE OMP_LIB         ! or INCLUDE "omp_lib.h"
            USE DATA
            TYPE(LOCKED_PAIR) :: P
            INTEGER A
            INTEGER B
        CALL OMP_SET_NEST_LOCK(P%LCK)
        CALL INCR_A(P, A)
        CALL INCR_B(P, B)
        CALL OMP_UNSET_NEST_LOCK(P%LCK)
      END SUBROUTINE INCR_PAIR
      SUBROUTINE A40(P)
        USE OMP_LIB        ! or INCLUDE "omp_lib.h"
        USE DATA
        TYPE(LOCKED_PAIR) :: P
        INTEGER WORK1, WORK2, WORK3
        EXTERNAL WORK1, WORK2, WORK3
!$OMP PARALLEL SECTIONS
!$OMP SECTION
          CALL INCR_PAIR(P, WORK1(), WORK2())
!$OMP SECTION
          CALL INCR_B(P, WORK3())
!$OMP END PARALLEL SECTIONS
      END SUBROUTINE A40
