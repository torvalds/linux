! { dg-do run }
!$ use omp_lib

  character (len = 8) :: h
  character (len = 9) :: i
  h = '01234567'
  i = 'ABCDEFGHI'
  call test (h, i, 9)
contains
  subroutine test (p, q, n)
    character (len = *) :: p
    character (len = n) :: q
    character (len = n) :: r
    character (len = n) :: t
    character (len = n) :: u
    integer, dimension (n + 4) :: s
    logical :: l
    integer :: m
    r = ''
    if (n .gt. 8) r = 'jklmnopqr'
    do m = 1, n + 4
      s(m) = m
    end do
    u = 'abc'
    l = .false.
!$omp parallel firstprivate (p, q, r) private (t, m) reduction (.or.:l) &
!$omp & num_threads (2)
    do m = 1, 13
      if (s(m) .ne. m) l = .true.
    end do
    m = omp_get_thread_num ()
    l = l .or. p .ne. '01234567' .or. q .ne. 'ABCDEFGHI'
    l = l .or. r .ne. 'jklmnopqr' .or. u .ne. 'abc'
!$omp barrier
    if (m .eq. 0) then
      p = 'A'
      q = 'B'
      r = 'C'
      t = '123'
      u = '987654321'
    else if (m .eq. 1) then
      p = 'D'
      q = 'E'
      r = 'F'
      t = '456'
      s = m
    end if
!$omp barrier
    l = l .or. u .ne. '987654321'
    if (any (s .ne. 1)) l = .true.
    if (m .eq. 0) then
      l = l .or. p .ne. 'A' .or. q .ne. 'B' .or. r .ne. 'C'
      l = l .or. t .ne. '123'
    else
      l = l .or. p .ne. 'D' .or. q .ne. 'E' .or. r .ne. 'F'
      l = l .or. t .ne. '456'
    end if
!$omp end parallel
    if (l) call abort
  end subroutine test
end
