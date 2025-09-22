! { dg-do run }
! { dg-require-effective-target tls_runtime }

      PROGRAM A22_7_GOOD
        INTEGER, ALLOCATABLE, SAVE :: A(:)
        INTEGER, POINTER, SAVE :: PTR
        INTEGER, SAVE :: I
        INTEGER, TARGET :: TARG
        LOGICAL :: FIRSTIN = .TRUE.
!$OMP THREADPRIVATE(A, I, PTR)
        ALLOCATE (A(3))
        A = (/1,2,3/)
        PTR => TARG
        I=5
!$OMP PARALLEL COPYIN(I, PTR)
!$OMP CRITICAL
            IF (FIRSTIN) THEN
              TARG = 4           ! Update target of ptr
              I = I + 10
              IF (ALLOCATED(A)) A = A + 10
              FIRSTIN = .FALSE.
            END IF
            IF (ALLOCATED(A)) THEN
              PRINT *, "a = ", A
            ELSE
              PRINT *, "A is not allocated"
            END IF
            PRINT *, "ptr = ", PTR
            PRINT *, "i = ", I
            PRINT *
!$OMP END CRITICAL
!$OMP END PARALLEL
      END PROGRAM A22_7_GOOD
