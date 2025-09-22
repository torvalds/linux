! PR fortran/27916
! Test whether allocatable privatized arrays has "not currently allocated"
! status at the start of OpenMP constructs.
! { dg-do run }

program pr27916
  integer :: n, i
  logical :: r
  integer, dimension(:), allocatable :: a

  r = .false.
!$omp parallel do num_threads (4) default (private) &
!$omp & reduction (.or.: r) schedule (static)
  do n = 1, 16
    r = r .or. allocated (a)
    allocate (a (16))
    r = r .or. .not. allocated (a)
    do i = 1, 16
      a (i) = i
    end do
    deallocate (a)
    r = r .or. allocated (a)
  end do
 !$omp end parallel do
  if (r) call abort
end program pr27916
