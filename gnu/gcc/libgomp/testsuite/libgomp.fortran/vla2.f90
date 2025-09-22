! { dg-do run }

  call test
contains
  subroutine check (x, y, l)
    integer :: x, y
    logical :: l
    l = l .or. x .ne. y
  end subroutine check

  subroutine foo (c, d, e, f, g, h, i, j, k, n)
    use omp_lib
    integer :: n
    character (len = *) :: c
    character (len = n) :: d
    integer, dimension (2, 3:5, n) :: e
    integer, dimension (2, 3:n, n) :: f
    character (len = *), dimension (5, 3:n) :: g
    character (len = n), dimension (5, 3:n) :: h
    real, dimension (:, :, :) :: i
    double precision, dimension (3:, 5:, 7:) :: j
    integer, dimension (:, :, :) :: k
    logical :: l
    integer :: p, q, r
    character (len = n) :: s
    integer, dimension (2, 3:5, n) :: t
    integer, dimension (2, 3:n, n) :: u
    character (len = n), dimension (5, 3:n) :: v
    character (len = 2 * n + 24) :: w
    integer :: x
    character (len = 1) :: y
    l = .false.
!$omp parallel default (none) private (c, d, e, f, g, h, i, j, k) &
!$omp & private (s, t, u, v) reduction (.or.:l) num_threads (6) &
!$omp private (p, q, r, w, x, y)
    x = omp_get_thread_num ()
    w = ''
    if (x .eq. 0) w = 'thread0thr_number_0THREAD0THR_NUMBER_0'
    if (x .eq. 1) w = 'thread1thr_number_1THREAD1THR_NUMBER_1'
    if (x .eq. 2) w = 'thread2thr_number_2THREAD2THR_NUMBER_2'
    if (x .eq. 3) w = 'thread3thr_number_3THREAD3THR_NUMBER_3'
    if (x .eq. 4) w = 'thread4thr_number_4THREAD4THR_NUMBER_4'
    if (x .eq. 5) w = 'thread5thr_number_5THREAD5THR_NUMBER_5'
    c = w(8:19)
    d = w(1:7)
    forall (p = 1:2, q = 3:5, r = 1:7) e(p, q, r) = 5 * x + p + q + 2 * r
    forall (p = 1:2, q = 3:7, r = 1:7) f(p, q, r) = 25 * x + p + q + 2 * r
    forall (p = 1:5, q = 3:7, p + q .le. 8) g(p, q) = w(8:19)
    forall (p = 1:5, q = 3:7, p + q .gt. 8) g(p, q) = w(27:38)
    forall (p = 1:5, q = 3:7, p + q .le. 8) h(p, q) = w(1:7)
    forall (p = 1:5, q = 3:7, p + q .gt. 8) h(p, q) = w(20:26)
    forall (p = 3:5, q = 2:6, r = 1:7) i(p - 2, q - 1, r) = (7.5 + x) * p * q * r
    forall (p = 3:5, q = 2:6, r = 1:7) j(p, q + 3, r + 6) = (9.5 + x) * p * q * r
    forall (p = 1:5, q = 7:7, r = 4:6) k(p, q - 6, r - 3) = 19 + x + p + q + 3 * r
    s = w(20:26)
    forall (p = 1:2, q = 3:5, r = 1:7) t(p, q, r) = -10 + x + p - q + 2 * r
    forall (p = 1:2, q = 3:7, r = 1:7) u(p, q, r) = 30 - x - p + q - 2 * r
    forall (p = 1:5, q = 3:7, p + q .le. 8) v(p, q) = w(1:7)
    forall (p = 1:5, q = 3:7, p + q .gt. 8) v(p, q) = w(20:26)
!$omp barrier
    y = ''
    if (x .eq. 0) y = '0'
    if (x .eq. 1) y = '1'
    if (x .eq. 2) y = '2'
    if (x .eq. 3) y = '3'
    if (x .eq. 4) y = '4'
    if (x .eq. 5) y = '5'
    l = l .or. w(7:7) .ne. y
    l = l .or. w(19:19) .ne. y
    l = l .or. w(26:26) .ne. y
    l = l .or. w(38:38) .ne. y
    l = l .or. c .ne. w(8:19)
    l = l .or. d .ne. w(1:7)
    l = l .or. s .ne. w(20:26)
    do 103, p = 1, 2
      do 103, q = 3, 7
	do 103, r = 1, 7
	  if (q .lt. 6) l = l .or. e(p, q, r) .ne. 5 * x + p + q + 2 * r
	  l = l .or. f(p, q, r) .ne. 25 * x + p + q + 2 * r
	  if (r .lt. 6 .and. q + r .le. 8) l = l .or. g(r, q) .ne. w(8:19)
	  if (r .lt. 6 .and. q + r .gt. 8) l = l .or. g(r, q) .ne. w(27:38)
	  if (r .lt. 6 .and. q + r .le. 8) l = l .or. h(r, q) .ne. w(1:7)
	  if (r .lt. 6 .and. q + r .gt. 8) l = l .or. h(r, q) .ne. w(20:26)
	  if (q .lt. 6) l = l .or. t(p, q, r) .ne. -10 + x + p - q + 2 * r
	  l = l .or. u(p, q, r) .ne. 30 - x - p + q - 2 * r
	  if (r .lt. 6 .and. q + r .le. 8) l = l .or. v(r, q) .ne. w(1:7)
	  if (r .lt. 6 .and. q + r .gt. 8) l = l .or. v(r, q) .ne. w(20:26)
103 continue
    do 104, p = 3, 5
      do 104, q = 2, 6
	do 104, r = 1, 7
	  l = l .or. i(p - 2, q - 1, r) .ne. (7.5 + x) * p * q * r
	  l = l .or. j(p, q + 3, r + 6) .ne. (9.5 + x) * p * q * r
104 continue
    do 105, p = 1, 5
      do 105, q = 4, 6
	l = l .or. k(p, 1, q - 3) .ne. 19 + x + p + 7 + 3 * q
105 continue
    call check (size (e, 1), 2, l)
    call check (size (e, 2), 3, l)
    call check (size (e, 3), 7, l)
    call check (size (e), 42, l)
    call check (size (f, 1), 2, l)
    call check (size (f, 2), 5, l)
    call check (size (f, 3), 7, l)
    call check (size (f), 70, l)
    call check (size (g, 1), 5, l)
    call check (size (g, 2), 5, l)
    call check (size (g), 25, l)
    call check (size (h, 1), 5, l)
    call check (size (h, 2), 5, l)
    call check (size (h), 25, l)
    call check (size (i, 1), 3, l)
    call check (size (i, 2), 5, l)
    call check (size (i, 3), 7, l)
    call check (size (i), 105, l)
    call check (size (j, 1), 4, l)
    call check (size (j, 2), 5, l)
    call check (size (j, 3), 7, l)
    call check (size (j), 140, l)
    call check (size (k, 1), 5, l)
    call check (size (k, 2), 1, l)
    call check (size (k, 3), 3, l)
    call check (size (k), 15, l)
!$omp end parallel
    if (l) call abort
  end subroutine foo

  subroutine test
    character (len = 12) :: c
    character (len = 7) :: d
    integer, dimension (2, 3:5, 7) :: e
    integer, dimension (2, 3:7, 7) :: f
    character (len = 12), dimension (5, 3:7) :: g
    character (len = 7), dimension (5, 3:7) :: h
    real, dimension (3:5, 2:6, 1:7) :: i
    double precision, dimension (3:6, 2:6, 1:7) :: j
    integer, dimension (1:5, 7:7, 4:6) :: k
    integer :: p, q, r
    call foo (c, d, e, f, g, h, i, j, k, 7)
  end subroutine test
end
