! { dg-do run }

       SUBROUTINE SUB()
       COMMON /BLOCK/ X
       PRINT *,X              ! X is undefined
       END SUBROUTINE SUB
       PROGRAM A28_1
         COMMON /BLOCK/ X
         X = 1.0
!$OMP PARALLEL PRIVATE (X)
         X = 2.0
         CALL SUB()
!$OMP END PARALLEL
      END PROGRAM A28_1
