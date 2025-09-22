! { dg-do run }
use omp_lib
  call test_master
  call test_critical
  call test_barrier
  call test_atomic

contains
  subroutine test_master
    logical :: i, j
    i = .false.
    j = .false.
!$omp parallel num_threads (4)
!$omp master
    i = .true.
    j = omp_get_thread_num () .eq. 0
!$omp endmaster
!$omp end parallel
    if (.not. (i .or. j)) call abort
  end subroutine test_master

  subroutine test_critical_1 (i, j)
    integer :: i, j
!$omp critical(critical_foo) 
    i = i + 1
!$omp end critical (critical_foo)
!$omp critical
    j = j + 1
!$omp end critical
    end subroutine test_critical_1

  subroutine test_critical
    integer :: i, j, n
    n = -1
    i = 0
    j = 0
!$omp parallel num_threads (4)
    if (omp_get_thread_num () .eq. 0) n = omp_get_num_threads ()
    call test_critical_1 (i, j)
    call test_critical_1 (i, j)
!$omp critical
    j = j + 1
!$omp end critical
!$omp critical (critical_foo)
    i = i + 1
!$omp endcritical (critical_foo)
!$omp end parallel
    if (n .lt. 1 .or. i .ne. n * 3 .or. j .ne. n * 3) call abort
  end subroutine test_critical

  subroutine test_barrier
    integer :: i
    logical :: j
    i = 23
    j = .false.
!$omp parallel num_threads (4)
    if (omp_get_thread_num () .eq. 0) i = 5
!$omp flush (i)
!$omp barrier
    if (i .ne. 5) then
!$omp atomic
      j = j .or. .true.
    end if
!$omp end parallel
    if (i .ne. 5 .or. j) call abort
  end subroutine test_barrier

  subroutine test_atomic
    integer :: a, b, c, d, e, f, g
    a = 0
    b = 1
    c = 0
    d = 1024
    e = 1024
    f = -1
    g = -1
!$omp parallel num_threads (8)
!$omp atomic
    a = a + 2 + 4
!$omp atomic
    b = 3 * b
!$omp atomic
    c = 8 - c
!$omp atomic
    d = d / 2
!$omp atomic
    e = min (e, omp_get_thread_num ())
!$omp atomic
    f = max (omp_get_thread_num (), f)
    if (omp_get_thread_num () .eq. 0) g = omp_get_num_threads ()
!$omp end parallel
    if (g .le. 0 .or. g .gt. 8) call abort
    if (a .ne. 6 * g .or. b .ne. 3 ** g) call abort
    if (iand (g, 1) .eq. 1) then
      if (c .ne. 8) call abort
    else if (c .ne. 0) then
      call abort
    end if
    if (d .ne. 1024 / (2 ** g)) call abort
    if (e .ne. 0 .or. f .ne. g - 1) call abort
  end subroutine test_atomic
end
