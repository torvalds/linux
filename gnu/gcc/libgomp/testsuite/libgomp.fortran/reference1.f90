! { dg-do run }
!$ use omp_lib

  integer :: i, j, k
  double precision :: d
  i = 6
  j = 19
  k = 0
  d = 24.5
  call test (i, j, k, d)
  if (i .ne. 38) call abort
  if (iand (k, 255) .ne. 0) call abort
  if (iand (k, 65280) .eq. 0) then
    if (k .ne. 65536 * 4) call abort
  end if
contains
  subroutine test (i, j, k, d)
    integer :: i, j, k
    double precision :: d

!$omp parallel firstprivate (d) private (j) num_threads (4) reduction (+:k)
    if (i .ne. 6 .or. d .ne. 24.5 .or. k .ne. 0) k = k + 1
    if (omp_get_num_threads () .ne. 4) k = k + 256
    d = d / 2
    j = 8
    k = k + 65536
!$omp barrier
    if (d .ne. 12.25 .or. j .ne. 8) k = k + 1
!$omp single
    i = i + 32
!$omp end single nowait
!$omp end parallel
  end subroutine test
end
