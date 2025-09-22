! { dg-do run }

  integer, dimension (128) :: a, b
  integer :: i
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

!$omp parallel num_threads (4)

!$omp do
  do i = 8, 15
    a(i) = 1 * 256 + i
  end do

!$omp do
  do i = 23, 19, -1
    a(i) = 2 * 256 + i
  end do

!$omp do
  do i = 28, 39, 2
    a(i) = 3 * 256 + i
  end do

!$omp do
  do i = 79, 59, -4
    a(i) = 4 * 256 + i
  end do

!$omp do
  do i = 125, 90, -12
    a(i) = 5 * 256 + i
  end do

!$omp end parallel

  if (any (a .ne. b)) call abort
  a = -1

!$omp parallel num_threads (4)

!$omp do schedule (static)
  do i = 8, 15
    a(i) = 1 * 256 + i
  end do

!$omp do schedule (static, 1)
  do i = 23, 19, -1
    a(i) = 2 * 256 + i
  end do

!$omp do schedule (static, 3)
  do i = 28, 39, 2
    a(i) = 3 * 256 + i
  end do

!$omp do schedule (static, 6)
  do i = 79, 59, -4
    a(i) = 4 * 256 + i
  end do

!$omp do schedule (static, 2)
  do i = 125, 90, -12
    a(i) = 5 * 256 + i
  end do

!$omp end parallel

  if (any (a .ne. b)) call abort
  a = -1

!$omp parallel num_threads (4)

!$omp do schedule (dynamic)
  do i = 8, 15
    a(i) = 1 * 256 + i
  end do

!$omp do schedule (dynamic, 4)
  do i = 23, 19, -1
    a(i) = 2 * 256 + i
  end do

!$omp do schedule (dynamic, 1)
  do i = 28, 39, 2
    a(i) = 3 * 256 + i
  end do

!$omp do schedule (dynamic, 2)
  do i = 79, 59, -4
    a(i) = 4 * 256 + i
  end do

!$omp do schedule (dynamic, 3)
  do i = 125, 90, -12
    a(i) = 5 * 256 + i
  end do

!$omp end parallel

  if (any (a .ne. b)) call abort
  a = -1

!$omp parallel num_threads (4)

!$omp do schedule (guided)
  do i = 8, 15
    a(i) = 1 * 256 + i
  end do

!$omp do schedule (guided, 4)
  do i = 23, 19, -1
    a(i) = 2 * 256 + i
  end do

!$omp do schedule (guided, 1)
  do i = 28, 39, 2
    a(i) = 3 * 256 + i
  end do

!$omp do schedule (guided, 2)
  do i = 79, 59, -4
    a(i) = 4 * 256 + i
  end do

!$omp do schedule (guided, 3)
  do i = 125, 90, -12
    a(i) = 5 * 256 + i
  end do

!$omp end parallel

  if (any (a .ne. b)) call abort
  a = -1

!$omp parallel num_threads (4)

!$omp do schedule (runtime)
  do i = 8, 15
    a(i) = 1 * 256 + i
  end do

!$omp do schedule (runtime)
  do i = 23, 19, -1
    a(i) = 2 * 256 + i
  end do

!$omp do schedule (runtime)
  do i = 28, 39, 2
    a(i) = 3 * 256 + i
  end do

!$omp do schedule (runtime)
  do i = 79, 59, -4
    a(i) = 4 * 256 + i
  end do

!$omp do schedule (runtime)
  do i = 125, 90, -12
    a(i) = 5 * 256 + i
  end do

!$omp end parallel

  if (any (a .ne. b)) call abort
end
