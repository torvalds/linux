! PR fortran/27395
! { dg-do run }

program pr27395_2
  implicit none
  integer, parameter :: n=10,m=1001
  integer :: i
  call foo(n,m)
end program pr27395_2

subroutine foo(n,m)
  use omp_lib, only : omp_get_thread_num
  implicit none
  integer, intent(in) :: n,m
  integer :: i,j
  integer, dimension(n) :: sumarray
  sumarray(:)=0
!$OMP PARALLEL DEFAULT(shared) NUM_THREADS(4)
!$OMP DO PRIVATE(j,i), REDUCTION(+:sumarray)
  do j=1,m
    do i=1,n
      sumarray(i)=sumarray(i)+i
    end do
  end do
!$OMP END DO
!$OMP END PARALLEL
  do i=1,n
    if (sumarray(i).ne.m*i) call abort
  end do
end subroutine foo
