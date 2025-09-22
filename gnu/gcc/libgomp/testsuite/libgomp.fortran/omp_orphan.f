C******************************************************************************
C FILE: omp_orphan.f
C DESCRIPTION:
C   OpenMP Example - Parallel region with an orphaned directive - Fortran
C   Version
C   This example demonstrates a dot product being performed by an orphaned
C   loop reduction construct.  Scoping of the reduction variable is critical.
C AUTHOR: Blaise Barney  5/99
C LAST REVISED:
C******************************************************************************

      PROGRAM ORPHAN
      COMMON /DOTDATA/ A, B, SUM
      INTEGER I, VECLEN
      PARAMETER (VECLEN = 100)
      REAL*8 A(VECLEN), B(VECLEN), SUM

      DO I=1, VECLEN
         A(I) = 1.0 * I
         B(I) = A(I)
      ENDDO
      SUM = 0.0
!$OMP PARALLEL
      CALL DOTPROD
!$OMP END PARALLEL
      WRITE(*,*) "Sum = ", SUM
      END



      SUBROUTINE DOTPROD
      COMMON /DOTDATA/ A, B, SUM
      INTEGER I, TID, OMP_GET_THREAD_NUM, VECLEN
      PARAMETER (VECLEN = 100)
      REAL*8 A(VECLEN), B(VECLEN), SUM

      TID = OMP_GET_THREAD_NUM()
!$OMP DO REDUCTION(+:SUM)
      DO I=1, VECLEN
         SUM = SUM + (A(I)*B(I))
         PRINT *, '  TID= ',TID,'I= ',I
      ENDDO
      RETURN
      END
