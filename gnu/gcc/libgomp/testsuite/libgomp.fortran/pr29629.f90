! PR fortran/29629
! { dg-do run }

program pr29629
  integer :: n
  n = 10000
  if (any (func(n).ne.10000)) call abort
  contains
    function func(n)
      integer, intent(in) :: n
      integer, dimension(n) :: func
      integer :: k
      func = 0
!$omp parallel do private(k), reduction(+:func), num_threads(4)
      do k = 1, n
        func = func + 1
      end do
!$omp end parallel do
    end function
end program
