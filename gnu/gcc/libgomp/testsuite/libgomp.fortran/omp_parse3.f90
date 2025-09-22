! { dg-do run }
! { dg-require-effective-target tls_runtime }
use omp_lib
  common /tlsblock/ x, y
  integer :: x, y, z
  save z
!$omp threadprivate (/tlsblock/, z)

  call test_flush
  call test_ordered
  call test_threadprivate

contains
  subroutine test_flush
    integer :: i, j
    i = 0
    j = 0
!$omp parallel num_threads (4)
    if (omp_get_thread_num () .eq. 0) i = omp_get_num_threads ()
    if (omp_get_thread_num () .eq. 0) j = j + 1
!$omp flush (i, j)
!$omp barrier
    if (omp_get_thread_num () .eq. 1) j = j + 2
!$omp flush
!$omp barrier
    if (omp_get_thread_num () .eq. 2) j = j + 3
!$omp flush (i)
!$omp flush (j)
!$omp barrier
    if (omp_get_thread_num () .eq. 3) j = j + 4
!$omp end parallel
  end subroutine test_flush

  subroutine test_ordered
    integer :: i, j
    integer, dimension (100) :: d
    d(:) = -1
!$omp parallel do ordered schedule (dynamic) num_threads (4)
    do i = 1, 100, 5
!$omp ordered
      d(i) = i
!$omp end ordered
    end do
    j = 1
    do 100 i = 1, 100
      if (i .eq. j) then
	if (d(i) .ne. i) call abort
	j = i + 5
      else
	if (d(i) .ne. -1) call abort
      end if
100   d(i) = -1
  end subroutine test_ordered

  subroutine test_threadprivate
    common /tlsblock/ x, y
!$omp threadprivate (/tlsblock/)
    integer :: i, j
    logical :: m, n
    call omp_set_num_threads (4)
    call omp_set_dynamic (.false.)
    i = -1
    x = 6
    y = 7
    z = 8
    n = .false.
    m = .false.
!$omp parallel copyin (/tlsblock/, z) reduction (.or.:m) &
!$omp& num_threads (4)
    if (omp_get_thread_num () .eq. 0) i = omp_get_num_threads ()
    if (x .ne. 6 .or. y .ne. 7 .or. z .ne. 8) call abort
    x = omp_get_thread_num ()
    y = omp_get_thread_num () + 1024
    z = omp_get_thread_num () + 4096
!$omp end parallel
    if (x .ne. 0 .or. y .ne. 1024 .or. z .ne. 4096) call abort
!$omp parallel num_threads (4), private (j) reduction (.or.:n)
    if (omp_get_num_threads () .eq. i) then
      j = omp_get_thread_num ()
      if (x .ne. j .or. y .ne. j + 1024 .or. z .ne. j + 4096) &
&       call abort
    end if
!$omp end parallel
    m = m .or. n
    n = .false.
!$omp parallel num_threads (4), copyin (z) reduction (.or. : n)
    if (z .ne. 4096) n = .true.
    if (omp_get_num_threads () .eq. i) then
      j = omp_get_thread_num ()
      if (x .ne. j .or. y .ne. j + 1024) call abort
    end if
!$omp end parallel
    if (m .or. n) call abort
  end subroutine test_threadprivate
end
