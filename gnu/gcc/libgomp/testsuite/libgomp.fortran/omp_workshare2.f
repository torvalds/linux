C******************************************************************************
C FILE: omp_workshare2.f
C DESCRIPTION:
C   OpenMP Example - Sections Work-sharing - Fortran Version
C   In this example, the OpenMP SECTION directive is used to assign
C   different array operations to threads that execute a SECTION. Each
C   thread receives its own copy of the result array to work with.
C AUTHOR: Blaise Barney  5/99
C LAST REVISED: 01/09/04
C******************************************************************************

      PROGRAM WORKSHARE2

      INTEGER N, I, NTHREADS, TID, OMP_GET_NUM_THREADS,
     +        OMP_GET_THREAD_NUM
      PARAMETER (N=50)
      REAL A(N), B(N), C(N)

!     Some initializations
      DO I = 1, N
        A(I) = I * 1.0
        B(I) = A(I)
      ENDDO

!$OMP PARALLEL SHARED(A,B,NTHREADS), PRIVATE(C,I,TID)
      TID = OMP_GET_THREAD_NUM()
      IF (TID .EQ. 0) THEN
        NTHREADS = OMP_GET_NUM_THREADS()
        PRINT *, 'Number of threads =', NTHREADS
      END IF
      PRINT *, 'Thread',TID,' starting...'

!$OMP SECTIONS

!$OMP SECTION
      PRINT *, 'Thread',TID,' doing section 1'
      DO I = 1, N
         C(I) = A(I) + B(I)
         WRITE(*,100) TID,I,C(I)
 100     FORMAT(' Thread',I2,': C(',I2,')=',F8.2)
      ENDDO

!$OMP SECTION
      PRINT *, 'Thread',TID,' doing section 2'
      DO I = 1+N/2, N
         C(I) = A(I) * B(I)
         WRITE(*,100) TID,I,C(I)
      ENDDO

!$OMP END SECTIONS NOWAIT

      PRINT *, 'Thread',TID,' done.'

!$OMP END PARALLEL

      END
