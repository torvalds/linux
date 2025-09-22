! { dg-do run }

  use omp_lib
  integer :: i, j, k, m, n
  logical :: l
  equivalence (i, m)
  equivalence (j, n)
  i = 4
  j = 8
  l = .false.
!$omp parallel private (k) firstprivate (i) shared (j) num_threads (2) &
!$omp& reduction (.or.:l)
  l = l .or. i .ne. 4
  l = l .or. j .ne. 8
!$omp barrier
  k = omp_get_thread_num ()
  if (k .eq. 0) then
    i = 14
    j = 15
  end if
!$omp barrier
  if (k .eq. 1) then
    if (i .ne. 4 .or. j .ne. 15) l = .true.
    i = 24
    j = 25
  end if
!$omp barrier
  if (j .ne. 25 .or. i .ne. (k * 10 + 14)) l = .true.
!$omp end parallel
  if (l) call abort
  if (j .ne. 25) call abort
end
