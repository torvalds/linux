! { dg-do run }

  integer, dimension (6, 6) :: a
  character (36) :: c
  integer nthreads
  a = 9
  nthreads = -1
  call foo (a (2:4, 3:5), nthreads)
  if (nthreads .eq. 3) then
    write (c, '(36i1)') a
    if (c .ne. '999999999999966699966699966699999999') call abort
  end if
contains
  subroutine foo (b, nthreads)
    use omp_lib
    integer, dimension (3:, 5:) :: b
    integer :: err, nthreads
    b = 0
    err = 0
!$omp parallel num_threads (3) reduction (+:b)
    if (any (b .ne. 0)) then
!$omp atomic
      err = err + 1
    end if
!$omp master
    nthreads = omp_get_num_threads ()
!$omp end master
    b = 2
!$omp end parallel
    if (err .gt. 0) call abort
  end subroutine foo
end
