! { dg-do run }
!$ use omp_lib

  character (len = 8) :: h, i
  character (len = 4) :: j, k
  h = '01234567'
  i = 'ABCDEFGH'
  j = 'IJKL'
  k = 'MN'
  call test (h, j)
contains
  subroutine test (p, q)
    character (len = 8) :: p
    character (len = 4) :: q, r
    character (len = 16) :: f
    character (len = 32) :: g
    integer, dimension (18) :: s
    logical :: l
    integer :: m
    f = 'test16'
    g = 'abcdefghijklmnopqrstuvwxyz'
    r = ''
    l = .false.
    s = -6
!$omp parallel firstprivate (f, p, s) private (r, m) reduction (.or.:l) &
!$omp & num_threads (4)
    m = omp_get_thread_num ()
    if (any (s .ne. -6)) l = .true.
    l = l .or. f .ne. 'test16' .or. p .ne. '01234567'
    l = l .or. g .ne. 'abcdefghijklmnopqrstuvwxyz'
    l = l .or. i .ne. 'ABCDEFGH' .or. q .ne. 'IJKL'
    l = l .or. k .ne. 'MN'
!$omp barrier
    if (m .eq. 0) then
      f = 'ffffffff0'
      g = 'xyz'
      i = '123'
      k = '9876'
      p = '_abc'
      q = '_def'
      r = '1_23'
    else if (m .eq. 1) then
      f = '__'
      p = 'xxx'
      r = '7575'
    else if (m .eq. 2) then
      f = 'ZZ'
      p = 'm2'
      r = 'M2'
    else if (m .eq. 3) then
      f = 'YY'
      p = 'm3'
      r = 'M3'
    end if
    s = m
!$omp barrier
    l = l .or. g .ne. 'xyz' .or. i .ne. '123' .or. k .ne. '9876'
    l = l .or. q .ne. '_def'
    if (any (s .ne. m)) l = .true.
    if (m .eq. 0) then
      l = l .or. f .ne. 'ffffffff0' .or. p .ne. '_abc' .or. r .ne. '1_23'
    else if (m .eq. 1) then
      l = l .or. f .ne. '__' .or. p .ne. 'xxx' .or. r .ne. '7575'
    else if (m .eq. 2) then
      l = l .or. f .ne. 'ZZ' .or. p .ne. 'm2' .or. r .ne. 'M2'
    else if (m .eq. 3) then
      l = l .or. f .ne. 'YY' .or. p .ne. 'm3' .or. r .ne. 'M3'
    end if
!$omp end parallel
    if (l) call abort
  end subroutine test
end
