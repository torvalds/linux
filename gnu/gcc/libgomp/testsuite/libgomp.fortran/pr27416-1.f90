! PR middle-end/27416
! { dg-do run }

  integer :: j
  j = 6
!$omp parallel num_threads (4)
  call foo (j)
!$omp end parallel
  if (j.ne.6+16) call abort
end

subroutine foo (j)
  integer :: i, j

!$omp do firstprivate (j) lastprivate (j)
  do i = 1, 16
    if (i.eq.16) j = j + i
  end do
end subroutine foo
