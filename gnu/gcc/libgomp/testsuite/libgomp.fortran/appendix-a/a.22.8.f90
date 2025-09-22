! { dg-do run }
! { dg-require-effective-target tls_runtime }
       MODULE A22_MODULE8
         REAL, POINTER :: WORK(:)
         SAVE WORK
!$OMP THREADPRIVATE(WORK)
       END MODULE A22_MODULE8
       SUBROUTINE SUB1(N)
       USE A22_MODULE8
!$OMP PARALLEL PRIVATE(THE_SUM)
         ALLOCATE(WORK(N))
         CALL SUB2(THE_SUM)
        WRITE(*,*)THE_SUM
!$OMP END PARALLEL
       END SUBROUTINE SUB1
       SUBROUTINE SUB2(THE_SUM)
        USE A22_MODULE8
        WORK(:) = 10
        THE_SUM=SUM(WORK)
        END SUBROUTINE SUB2
        PROGRAM A22_8_GOOD
        N = 10
        CALL SUB1(N)
        END PROGRAM A22_8_GOOD

