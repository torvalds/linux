! { dg-do run }

function f1 ()
  real :: f1
  f1 = 6.5
  call sub1
contains
  subroutine sub1
    use omp_lib
    logical :: l
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
  end subroutine sub1
end function f1

  real :: f1
  if (f1 () .ne. -2.5) call abort
end
