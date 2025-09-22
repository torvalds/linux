! { dg-do run }
! { dg-options "-fopenmp -fcray-pointer" }

  use omp_lib
  integer :: a, b, c, p
  logical :: l
  pointer (ip, p)
  a = 1
  b = 2
  c = 3
  l = .false.
  ip = loc (a)

!$omp parallel num_threads (2) reduction (.or.:l)
  l = p .ne. 1
!$omp barrier
!$omp master
  ip = loc (b)
!$omp end master
!$omp barrier
  l = l .or. p .ne. 2
!$omp barrier
  if (omp_get_thread_num () .eq. 1 .or. omp_get_num_threads () .lt. 2) &
    ip = loc (c)
!$omp barrier
  l = l .or. p .ne. 3
!$omp end parallel

  if (l) call abort

  l = .false.
!$omp parallel num_threads (2) reduction (.or.:l) default (private)
  ip = loc (a)
  a = 3 * omp_get_thread_num () + 4
  b = a + 1
  c = a + 2
  l = p .ne. 3 * omp_get_thread_num () + 4
  ip = loc (c)
  l = l .or. p .ne. 3 * omp_get_thread_num () + 6
  ip = loc (b)
  l = l .or. p .ne. 3 * omp_get_thread_num () + 5
!$omp end parallel

  if (l) call abort

end
