! { dg-do run }
  real, dimension (20) :: r
  integer, dimension (20) :: d
  integer :: i, j, k, n
  integer (kind = 2) :: a, b, c

  do 10 i = 1, 20
    r(i) = i
10  d(i) = 21 - i

  n = 20
  call foo (r, d, n)

  if (n .ne. 22) call abort
  if (any (r .ne. 33)) call abort

  i = 1
  j = 18
  k = 23
!$omp atomic
  i = min (i, j, k, n)
  if (i .ne. 1) call abort
!$omp atomic
  i = max (j, n, k, i)
  if (i .ne. 23) call abort

  a = 1
  b = 18
  c = 23
!$omp atomic
  a = min (a, b, c)
  if (a .ne. 1) call abort
!$omp atomic
  a = max (a, b, c)
  if (a .ne. 23) call abort

contains
  function bar (i)
    real bar
    integer i
    bar = 12.0 + i
  end function bar

  subroutine foo (x, y, n)
    integer i, y (*), n
    real x (*)
    do i = 1, n
!$omp atomic
      x(y(i)) = x(y(i)) + bar (i)
    end do
!$omp atomic
    n = n + 2
  end subroutine foo
end
