! { dg-do run }

      PROGRAM A28_2
        COMMON /BLOCK2/ X
        X = 1.0
!$OMP PARALLEL PRIVATE (X)
          X = 2.0
          CALL SUB()
!$OMP END PARALLEL
       CONTAINS
        SUBROUTINE SUB()
        COMMON /BLOCK2/ Y
        PRINT *,X                 ! X is undefined
        PRINT *,Y                 ! Y is undefined
        END SUBROUTINE SUB
      END PROGRAM A28_2
