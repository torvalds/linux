C******************************************************************************
C FILE: omp_reduction.f
C DESCRIPTION:
C   OpenMP Example - Combined Parallel Loop Reduction - Fortran Version
C   This example demonstrates a sum reduction within a combined parallel loop
C   construct.  Notice that default data element scoping is assumed - there
C   are no clauses specifying shared or private variables.  OpenMP will
C   automatically make loop index variables private within team threads, and
C   global variables shared.
C AUTHOR: Blaise Barney  5/99
C LAST REVISED:
C******************************************************************************

      PROGRAM REDUCTION

      INTEGER I, N
      REAL A(100), B(100), SUM

!     Some initializations
      N = 100
      DO I = 1, N
        A(I) = I *1.0
        B(I) = A(I)
      ENDDO
      SUM = 0.0

!$OMP PARALLEL DO REDUCTION(+:SUM)
      DO I = 1, N
        SUM = SUM + (A(I) * B(I))
      ENDDO

      PRINT *, '   Sum = ', SUM
      END
