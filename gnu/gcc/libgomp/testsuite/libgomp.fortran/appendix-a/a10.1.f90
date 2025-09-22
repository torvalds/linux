! { dg-do run }
      SUBROUTINE WORK1()
      END SUBROUTINE WORK1
      SUBROUTINE WORK2()
      END SUBROUTINE WORK2
      PROGRAM A10
!$OMP PARALLEL
!$OMP SINGLE
        print *, "Beginning work1."
!$OMP END SINGLE
        CALL WORK1()
!$OMP SINGLE
        print *, "Finishing work1."
!$OMP END SINGLE
!$OMP SINGLE
        print *, "Finished work1 and beginning work2."
!$OMP END SINGLE NOWAIT
        CALL WORK2()
!$OMP END PARALLEL
      END PROGRAM A10
