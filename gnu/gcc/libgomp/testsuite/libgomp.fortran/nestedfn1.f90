! { dg-do run }

  integer :: a, b, c
  a = 1
  b = 2
  c = 3
  call foo
  if (a .ne. 7) call abort
contains
  subroutine foo
    use omp_lib
    logical :: l
    l = .false.
!$omp parallel shared (a) private (b) firstprivate (c) &
!$omp num_threads (2) reduction (.or.:l)
    if (a .ne. 1 .or. c .ne. 3) l = .true.
!$omp barrier
    if (omp_get_thread_num () .eq. 0) then
      a = 4
      b = 5
      c = 6
    end if
!$omp barrier
    if (omp_get_thread_num () .eq. 1) then
      if (a .ne. 4 .or. c .ne. 3) l = .true.
      a = 7
      b = 8
      c = 9
    else if (omp_get_num_threads () .eq. 1) then
      a = 7
    end if
!$omp barrier
    if (omp_get_thread_num () .eq. 0) then
      if (a .ne. 7 .or. b .ne. 5 .or. c .ne. 6) l = .true.
    end if
!$omp barrier
    if (omp_get_thread_num () .eq. 1) then
      if (a .ne. 7 .or. b .ne. 8 .or. c .ne. 9) l = .true.
    end if
!$omp end parallel
    if (l) call abort
  end subroutine foo
end
