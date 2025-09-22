! PR fortran/27395
! { dg-do run }

program pr27395_1
  implicit none
  integer, parameter :: n=10,m=1001
  integer :: i
  integer, dimension(n) :: sumarray
  call foo(n,m,sumarray)
  do i=1,n
    if (sumarray(i).ne.m*i) call abort
  end do
end program pr27395_1

subroutine foo(n,m,sumarray)
  use omp_lib, only : omp_get_thread_num
  implicit none
  integer, intent(in) :: n,m
  integer, dimension(n), intent(out) :: sumarray
  integer :: i,j
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
end subroutine foo
