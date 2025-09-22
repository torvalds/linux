! { dg-do run }

  integer, dimension (128) :: a, b
  integer :: i, j
  logical :: k
  a = -1
  b = -1
  do i = 1, 128
    if (i .ge. 8 .and. i .le. 15) then
      b(i) = 1 * 256 + i
    else if (i .ge. 19 .and. i .le. 23) then
      b(i) = 2 * 256 + i
    else if (i .ge. 28 .and. i .le. 38) then
      if (iand (i, 1) .eq. 0) b(i) = 3 * 256 + i
    else if (i .ge. 59 .and. i .le. 79) then
      if (iand (i - 59, 3) .eq. 0) b(i) = 4 * 256 + i
    else if (i .ge. 101 .and. i .le. 125) then
      if (mod (i - 101, 12) .eq. 0) b(i) = 5 * 256 + i
    end if
  end do

  k = .false.
  j = 8
!$omp parallel num_threads (4)

!$omp do ordered
  do i = 8, 15
    a(i) = 1 * 256 + i
!$omp ordered
    if (i .ne. j) k = .true.
    j = j + 1
!$omp end ordered
  end do

!$omp single
  j = 23
!$omp end single

!$omp do ordered
  do i = 23, 19, -1
    a(i) = 2 * 256 + i
!$omp ordered
    if (i .ne. j) k = .true.
    j = j - 1
!$omp end ordered
  end do

!$omp single
  j = 28
!$omp end single

!$omp do ordered
  do i = 28, 39, 2
    a(i) = 3 * 256 + i
!$omp ordered
    if (i .ne. j) k = .true.
    j = j + 2
!$omp end ordered
  end do

!$omp single
  j = 79
!$omp end single

!$omp do ordered
  do i = 79, 59, -4
    a(i) = 4 * 256 + i
!$omp ordered
    if (i .ne. j) k = .true.
    j = j - 4
!$omp end ordered
  end do

!$omp single
  j = 125
!$omp end single

!$omp do ordered
  do i = 125, 90, -12
    a(i) = 5 * 256 + i
!$omp ordered
    if (i .ne. j) k = .true.
    j = j - 12
!$omp end ordered
  end do

!$omp end parallel

  if (any (a .ne. b) .or. k) call abort
  a = -1
  k = .false.
  j = 8
!$omp parallel num_threads (4)

!$omp do ordered schedule (static)
  do i = 8, 15
    a(i) = 1 * 256 + i
!$omp ordered
    if (i .ne. j) k = .true.
    j = j + 1
!$omp end ordered
  end do

!$omp single
  j = 23
!$omp end single

!$omp do ordered schedule (static, 1)
  do i = 23, 19, -1
    a(i) = 2 * 256 + i
!$omp ordered
    if (i .ne. j) k = .true.
    j = j - 1
!$omp end ordered
  end do

!$omp single
  j = 28
!$omp end single

!$omp do ordered schedule (static, 3)
  do i = 28, 39, 2
    a(i) = 3 * 256 + i
!$omp ordered
    if (i .ne. j) k = .true.
    j = j + 2
!$omp end ordered
  end do

!$omp single
  j = 79
!$omp end single

!$omp do ordered schedule (static, 6)
  do i = 79, 59, -4
    a(i) = 4 * 256 + i
!$omp ordered
    if (i .ne. j) k = .true.
    j = j - 4
!$omp end ordered
  end do

!$omp single
  j = 125
!$omp end single

!$omp do ordered schedule (static, 2)
  do i = 125, 90, -12
    a(i) = 5 * 256 + i
!$omp ordered
    if (i .ne. j) k = .true.
    j = j - 12
!$omp end ordered
  end do

!$omp end parallel

  if (any (a .ne. b) .or. k) call abort
  a = -1
  k = .false.
  j = 8
!$omp parallel num_threads (4)

!$omp do ordered schedule (dynamic)
  do i = 8, 15
    a(i) = 1 * 256 + i
!$omp ordered
    if (i .ne. j) k = .true.
    j = j + 1
!$omp end ordered
  end do

!$omp single
  j = 23
!$omp end single

!$omp do ordered schedule (dynamic, 4)
  do i = 23, 19, -1
    a(i) = 2 * 256 + i
!$omp ordered
    if (i .ne. j) k = .true.
    j = j - 1
!$omp end ordered
  end do

!$omp single
  j = 28
!$omp end single

!$omp do ordered schedule (dynamic, 1)
  do i = 28, 39, 2
    a(i) = 3 * 256 + i
!$omp ordered
    if (i .ne. j) k = .true.
    j = j + 2
!$omp end ordered
  end do

!$omp single
  j = 79
!$omp end single

!$omp do ordered schedule (dynamic, 2)
  do i = 79, 59, -4
    a(i) = 4 * 256 + i
!$omp ordered
    if (i .ne. j) k = .true.
    j = j - 4
!$omp end ordered
  end do

!$omp single
  j = 125
!$omp end single

!$omp do ordered schedule (dynamic, 3)
  do i = 125, 90, -12
    a(i) = 5 * 256 + i
!$omp ordered
    if (i .ne. j) k = .true.
    j = j - 12
!$omp end ordered
  end do

!$omp end parallel

  if (any (a .ne. b) .or. k) call abort
  a = -1
  k = .false.
  j = 8
!$omp parallel num_threads (4)

!$omp do ordered schedule (guided)
  do i = 8, 15
    a(i) = 1 * 256 + i
!$omp ordered
    if (i .ne. j) k = .true.
    j = j + 1
!$omp end ordered
  end do

!$omp single
  j = 23
!$omp end single

!$omp do ordered schedule (guided, 4)
  do i = 23, 19, -1
    a(i) = 2 * 256 + i
!$omp ordered
    if (i .ne. j) k = .true.
    j = j - 1
!$omp end ordered
  end do

!$omp single
  j = 28
!$omp end single

!$omp do ordered schedule (guided, 1)
  do i = 28, 39, 2
    a(i) = 3 * 256 + i
!$omp ordered
    if (i .ne. j) k = .true.
    j = j + 2
!$omp end ordered
  end do

!$omp single
  j = 79
!$omp end single

!$omp do ordered schedule (guided, 2)
  do i = 79, 59, -4
    a(i) = 4 * 256 + i
!$omp ordered
    if (i .ne. j) k = .true.
    j = j - 4
!$omp end ordered
  end do

!$omp single
  j = 125
!$omp end single

!$omp do ordered schedule (guided, 3)
  do i = 125, 90, -12
    a(i) = 5 * 256 + i
!$omp ordered
    if (i .ne. j) k = .true.
    j = j - 12
!$omp end ordered
  end do

!$omp end parallel

  if (any (a .ne. b) .or. k) call abort
  a = -1
  k = .false.
  j = 8
!$omp parallel num_threads (4)

!$omp do ordered schedule (runtime)
  do i = 8, 15
    a(i) = 1 * 256 + i
!$omp ordered
    if (i .ne. j) k = .true.
    j = j + 1
!$omp end ordered
  end do

!$omp single
  j = 23
!$omp end single

!$omp do ordered schedule (runtime)
  do i = 23, 19, -1
    a(i) = 2 * 256 + i
!$omp ordered
    if (i .ne. j) k = .true.
    j = j - 1
!$omp end ordered
  end do

!$omp single
  j = 28
!$omp end single

!$omp do ordered schedule (runtime)
  do i = 28, 39, 2
    a(i) = 3 * 256 + i
!$omp ordered
    if (i .ne. j) k = .true.
    j = j + 2
!$omp end ordered
  end do

!$omp single
  j = 79
!$omp end single

!$omp do ordered schedule (runtime)
  do i = 79, 59, -4
    a(i) = 4 * 256 + i
!$omp ordered
    if (i .ne. j) k = .true.
    j = j - 4
!$omp end ordered
  end do

!$omp single
  j = 125
!$omp end single

!$omp do ordered schedule (runtime)
  do i = 125, 90, -12
    a(i) = 5 * 256 + i
!$omp ordered
    if (i .ne. j) k = .true.
    j = j - 12
!$omp end ordered
  end do

!$omp end parallel

  if (any (a .ne. b) .or. k) call abort
end
