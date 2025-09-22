! { dg-do run }
  real, dimension (5) :: b
  b = 5
  call foo (b)
contains
  subroutine foo (a)
    real, dimension (5) :: a
    logical :: l
    l = .false.
!$omp parallel private (a) reduction (.or.:l)
    a = 15
    l = bar (a)
!$omp end parallel
    if (l) call abort
  end subroutine
  function bar (a)
    real, dimension (5) :: a
    logical :: bar
    bar = any (a .ne. 15)
  end function
end
