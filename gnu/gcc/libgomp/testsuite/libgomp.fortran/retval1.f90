! { dg-do run }

function f1 ()
  use omp_lib
  real :: f1
  logical :: l
  f1 = 6.5
  l = .false.
!$omp parallel firstprivate (f1) num_threads (2) reduction (.or.:l)
  l = f1 .ne. 6.5
  if (omp_get_thread_num () .eq. 0) f1 = 8.5
  if (omp_get_thread_num () .eq. 1) f1 = 14.5
!$omp barrier
  l = l .or. (omp_get_thread_num () .eq. 0 .and. f1 .ne. 8.5)
  l = l .or. (omp_get_thread_num () .eq. 1 .and. f1 .ne. 14.5)
!$omp end parallel
  if (l) call abort
  f1 = -2.5
end function f1
function f2 ()
  use omp_lib
  real :: f2, e2
  logical :: l
entry e2 ()
  f2 = 6.5
  l = .false.
!$omp parallel firstprivate (e2) num_threads (2) reduction (.or.:l)
  l = e2 .ne. 6.5
  if (omp_get_thread_num () .eq. 0) e2 = 8.5
  if (omp_get_thread_num () .eq. 1) e2 = 14.5
!$omp barrier
  l = l .or. (omp_get_thread_num () .eq. 0 .and. e2 .ne. 8.5)
  l = l .or. (omp_get_thread_num () .eq. 1 .and. e2 .ne. 14.5)
!$omp end parallel
  if (l) call abort
  e2 = 7.5
end function f2
function f3 ()
  use omp_lib
  real :: f3, e3
  logical :: l
entry e3 ()
  f3 = 6.5
  l = .false.
!$omp parallel firstprivate (f3, e3) num_threads (2) reduction (.or.:l)
  l = e3 .ne. 6.5
  l = l .or. f3 .ne. 6.5
  if (omp_get_thread_num () .eq. 0) e3 = 8.5
  if (omp_get_thread_num () .eq. 1) e3 = 14.5
  f3 = e3 - 4.5
!$omp barrier
  l = l .or. (omp_get_thread_num () .eq. 0 .and. e3 .ne. 8.5)
  l = l .or. (omp_get_thread_num () .eq. 1 .and. e3 .ne. 14.5)
  l = l .or. f3 .ne. e3 - 4.5
!$omp end parallel
  if (l) call abort
  e3 = 0.5
end function f3
function f4 () result (r4)
  use omp_lib
  real :: r4, s4
  logical :: l
entry e4 () result (s4)
  r4 = 6.5
  l = .false.
!$omp parallel firstprivate (r4, s4) num_threads (2) reduction (.or.:l)
  l = s4 .ne. 6.5
  l = l .or. r4 .ne. 6.5
  if (omp_get_thread_num () .eq. 0) s4 = 8.5
  if (omp_get_thread_num () .eq. 1) s4 = 14.5
  r4 = s4 - 4.5
!$omp barrier
  l = l .or. (omp_get_thread_num () .eq. 0 .and. s4 .ne. 8.5)
  l = l .or. (omp_get_thread_num () .eq. 1 .and. s4 .ne. 14.5)
  l = l .or. r4 .ne. s4 - 4.5
!$omp end parallel
  if (l) call abort
  s4 = -0.5
end function f4
function f5 (is_f5)
  use omp_lib
  real :: f5
  integer :: e5
  logical :: l, is_f5
entry e5 (is_f5)
  if (is_f5) then
    f5 = 6.5
  else
    e5 = 8
  end if
  l = .false.
!$omp parallel firstprivate (f5, e5) shared (is_f5) num_threads (2) &
!$omp reduction (.or.:l)
  l = .not. is_f5 .and. e5 .ne. 8
  l = l .or. (is_f5 .and. f5 .ne. 6.5)
  if (omp_get_thread_num () .eq. 0) e5 = 8
  if (omp_get_thread_num () .eq. 1) e5 = 14
  f5 = e5 - 4.5
!$omp barrier
  l = l .or. (omp_get_thread_num () .eq. 0 .and. e5 .ne. 8)
  l = l .or. (omp_get_thread_num () .eq. 1 .and. e5 .ne. 14)
  l = l .or. f5 .ne. e5 - 4.5
!$omp end parallel
  if (l) call abort
  if (is_f5) f5 = -2.5
  if (.not. is_f5) e5 = 8
end function f5

  real :: f1, f2, e2, f3, e3, f4, e4, f5
  integer :: e5
  if (f1 () .ne. -2.5) call abort
  if (f2 () .ne. 7.5) call abort
  if (e2 () .ne. 7.5) call abort
  if (f3 () .ne. 0.5) call abort
  if (e3 () .ne. 0.5) call abort
  if (f4 () .ne. -0.5) call abort
  if (e4 () .ne. -0.5) call abort
  if (f5 (.true.) .ne. -2.5) call abort
  if (e5 (.false.) .ne. 8) call abort
end
