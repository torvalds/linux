! { dg-do run }
use omp_lib
  call test_parallel
  call test_do
  call test_sections
  call test_single

contains
  subroutine test_parallel
    integer :: a, b, c, e, f, g, i, j
    integer, dimension (20) :: d
    logical :: h
    a = 6
    b = 8
    c = 11
    d(:) = -1
    e = 13
    f = 24
    g = 27
    h = .false.
    i = 1
    j = 16
!$omp para&
!$omp&llel &
!$omp if (a .eq. 6) private (b, c) shared (d) private (e) &
  !$omp firstprivate(f) num_threads (a - 1) first&
!$ompprivate(g)default (shared) reduction (.or. : h) &
!$omp reduction(*:i)
    if (i .ne. 1) h = .true.
    i = 2
    if (f .ne. 24) h = .true.
    if (g .ne. 27) h = .true.
    e = 7
    b = omp_get_thread_num ()
    if (b .eq. 0) j = 24
    f = b
    g = f
    c = omp_get_num_threads ()
    if (c .gt. a - 1 .or. c .le. 0) h = .true.
    if (b .ge. c) h = .true.
    d(b + 1) = c
    if (f .ne. g .or. f .ne. b) h = .true.
!$omp endparallel
    if (h) call abort
    if (a .ne. 6) call abort
    if (j .ne. 24) call abort
    if (d(1) .eq. -1) call abort
    e = 1
    do g = 1, d(1)
      if (d(g) .ne. d(1)) call abort
      e = e * 2
    end do
    if (e .ne. i) call abort
  end subroutine test_parallel

  subroutine test_do_orphan
    integer :: k, l
!$omp parallel do private (l)
    do 600 k = 1, 16, 2
600   l = k
  end subroutine test_do_orphan

  subroutine test_do
    integer :: i, j, k, l, n
    integer, dimension (64) :: d
    logical :: m

    j = 16
    d(:) = -1
    m = .true.
    n = 24
!$omp parallel num_threads (4) shared (i, k, d) private (l) &
!$omp&reduction (.and. : m)
    if (omp_get_thread_num () .eq. 0) then
      k = omp_get_num_threads ()
    end if
    call test_do_orphan
!$omp do schedule (static) firstprivate (n)
    do 200 i = 1, j
      if (i .eq. 1 .and. n .ne. 24) call abort
      n = i
200   d(n) = omp_get_thread_num ()
!$omp enddo nowait

!$omp do lastprivate (i) schedule (static, 5)
    do 201 i = j + 1, 2 * j
201   d(i) = omp_get_thread_num () + 1024
    ! Implied omp end do here

    if (i .ne. 33) m = .false.

!$omp do private (j) schedule (dynamic)
    do i = 33, 48
      d(i) = omp_get_thread_num () + 2048
    end do
!$omp end do nowait

!$omp do schedule (runtime)
    do i = 49, 4 * j
      d(i) = omp_get_thread_num () + 4096
    end do
    ! Implied omp end do here
!$omp end parallel
    if (.not. m) call abort

    j = 0
    do i = 1, 64
      if (d(i) .lt. j .or. d(i) .ge. j + k) call abort
      if (i .eq. 16) j = 1024
      if (i .eq. 32) j = 2048
      if (i .eq. 48) j = 4096
    end do
  end subroutine test_do

  subroutine test_sections
    integer :: i, j, k, l, m, n
    i = 9
    j = 10
    k = 11
    l = 0
    m = 0
    n = 30
    call omp_set_dynamic (.false.)
    call omp_set_num_threads (4)
!$omp parallel num_threads (4)
!$omp sections private (i) firstprivate (j, k) lastprivate (j) &
!$omp& reduction (+ : l, m)
!$omp section
    i = 24
    if (j .ne. 10 .or. k .ne. 11 .or. m .ne. 0) l = 1
    m = m + 4
!$omp section
    i = 25
    if (j .ne. 10 .or. k .ne. 11) l = 1
    m = m + 6
!$omp section
    i = 26
    if (j .ne. 10 .or. k .ne. 11) l = 1
    m = m + 8
!$omp section
    i = 27
    if (j .ne. 10 .or. k .ne. 11) l = 1
    m = m + 10
    j = 271
!$omp end sections nowait
!$omp sections lastprivate (n)
!$omp section
    n = 6
!$omp section
    n = 7
!$omp endsections
!$omp end parallel
    if (j .ne. 271 .or. l .ne. 0) call abort
    if (m .ne. 4 + 6 + 8 + 10) call abort
    if (n .ne. 7) call abort
  end subroutine test_sections

  subroutine test_single
    integer :: i, j, k, l
    logical :: m
    i = 200
    j = 300
    k = 400
    l = 500
    m = .false.
!$omp parallel num_threads (4), private (i, j), reduction (.or. : m)
    i = omp_get_thread_num ()
    j = omp_get_thread_num ()
!$omp single private (k)
    k = 64
!$omp end single nowait
!$omp single private (k) firstprivate (l)
    if (i .ne. omp_get_thread_num () .or. i .ne. j) then
      j = -1
    else
      j = -2
    end if
    if (l .ne. 500) j = -1
    l = 265
!$omp end single copyprivate (j)
    if (i .ne. omp_get_thread_num () .or. j .ne. -2) m = .true.
!$omp endparallel
    if (m) call abort
  end subroutine test_single
end
