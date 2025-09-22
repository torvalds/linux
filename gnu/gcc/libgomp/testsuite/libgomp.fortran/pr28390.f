! PR fortran/28390
      program pr28390
      integer i
!$omp parallel do lastprivate(i)
      do i=1,100
      end do
      if (i.ne.101) call abort
      end
