! { dg-do run }
! { dg-options "-w" }

  character (6) :: c, f2
  character (6) :: d(2)
  c = f1 (6)
  if (c .ne. 'opqrst') call abort
  c = f2 (6)
  if (c .ne. '_/!!/_') call abort
  d = f3 (6)
  if (d(1) .ne. 'opqrst' .or. d(2) .ne. 'a') call abort
  d = f4 (6)
  if (d(1) .ne. 'Opqrst' .or. d(2) .ne. 'A') call abort
contains
  function f1 (n)
    use omp_lib
    character (n) :: f1
    logical :: l
    f1 = 'abcdef'
    l = .false.
!$omp parallel firstprivate (f1) reduction (.or.:l) num_threads (2)
    l = f1 .ne. 'abcdef'
    if (omp_get_thread_num () .eq. 0) f1 = 'ijklmn'
    if (omp_get_thread_num () .eq. 1) f1 = 'IJKLMN'
!$omp barrier
    l = l .or. (omp_get_thread_num () .eq. 0 .and. f1 .ne. 'ijklmn')
    l = l .or. (omp_get_thread_num () .eq. 1 .and. f1 .ne. 'IJKLMN')
!$omp end parallel
    f1 = 'zZzz_z'
!$omp parallel shared (f1) reduction (.or.:l) num_threads (2)
    l = l .or. f1 .ne. 'zZzz_z'
!$omp barrier
!$omp master
    f1 = 'abc'
!$omp end master
!$omp barrier
    l = l .or. f1 .ne. 'abc'
!$omp barrier
    if (omp_get_thread_num () .eq. 1) f1 = 'def'
!$omp barrier
    l = l .or. f1 .ne. 'def'
!$omp end parallel
    if (l) call abort
    f1 = 'opqrst'
  end function f1
  function f3 (n)
    use omp_lib
    character (n), dimension (2) :: f3
    logical :: l
    f3 = 'abcdef'
    l = .false.
!$omp parallel firstprivate (f3) reduction (.or.:l) num_threads (2)
    l = any (f3 .ne. 'abcdef')
    if (omp_get_thread_num () .eq. 0) f3 = 'ijklmn'
    if (omp_get_thread_num () .eq. 1) f3 = 'IJKLMN'
!$omp barrier
    l = l .or. (omp_get_thread_num () .eq. 0 .and. any (f3 .ne. 'ijklmn'))
    l = l .or. (omp_get_thread_num () .eq. 1 .and. any (f3 .ne. 'IJKLMN'))
!$omp end parallel
    f3 = 'zZzz_z'
!$omp parallel shared (f3) reduction (.or.:l) num_threads (2)
    l = l .or. any (f3 .ne. 'zZzz_z')
!$omp barrier
!$omp master
    f3 = 'abc'
!$omp end master
!$omp barrier
    l = l .or. any (f3 .ne. 'abc')
!$omp barrier
    if (omp_get_thread_num () .eq. 1) f3 = 'def'
!$omp barrier
    l = l .or. any (f3 .ne. 'def')
!$omp end parallel
    if (l) call abort
    f3(1) = 'opqrst'
    f3(2) = 'a'
  end function f3
  function f4 (n)
    use omp_lib
    character (n), dimension (n - 4) :: f4
    logical :: l
    f4 = 'abcdef'
    l = .false.
!$omp parallel firstprivate (f4) reduction (.or.:l) num_threads (2)
    l = any (f4 .ne. 'abcdef')
    if (omp_get_thread_num () .eq. 0) f4 = 'ijklmn'
    if (omp_get_thread_num () .eq. 1) f4 = 'IJKLMN'
!$omp barrier
    l = l .or. (omp_get_thread_num () .eq. 0 .and. any (f4 .ne. 'ijklmn'))
    l = l .or. (omp_get_thread_num () .eq. 1 .and. any (f4 .ne. 'IJKLMN'))
    l = l .or. size (f4) .ne. 2
!$omp end parallel
    f4 = 'zZzz_z'
!$omp parallel shared (f4) reduction (.or.:l) num_threads (2)
    l = l .or. any (f4 .ne. 'zZzz_z')
!$omp barrier
!$omp master
    f4 = 'abc'
!$omp end master
!$omp barrier
    l = l .or. any (f4 .ne. 'abc')
!$omp barrier
    if (omp_get_thread_num () .eq. 1) f4 = 'def'
!$omp barrier
    l = l .or. any (f4 .ne. 'def')
    l = l .or. size (f4) .ne. 2
!$omp end parallel
    if (l) call abort
    f4(1) = 'Opqrst'
    f4(2) = 'A'
  end function f4
end
function f2 (n)
  use omp_lib
  character (*) :: f2
  logical :: l
  f2 = 'abcdef'
  l = .false.
!$omp parallel firstprivate (f2) reduction (.or.:l) num_threads (2)
  l = f2 .ne. 'abcdef'
  if (omp_get_thread_num () .eq. 0) f2 = 'ijklmn'
  if (omp_get_thread_num () .eq. 1) f2 = 'IJKLMN'
!$omp barrier
  l = l .or. (omp_get_thread_num () .eq. 0 .and. f2 .ne. 'ijklmn')
  l = l .or. (omp_get_thread_num () .eq. 1 .and. f2 .ne. 'IJKLMN')
!$omp end parallel
  f2 = 'zZzz_z'
!$omp parallel shared (f2) reduction (.or.:l) num_threads (2)
  l = l .or. f2 .ne. 'zZzz_z'
!$omp barrier
!$omp master
  f2 = 'abc'
!$omp end master
!$omp barrier
  l = l .or. f2 .ne. 'abc'
!$omp barrier
  if (omp_get_thread_num () .eq. 1) f2 = 'def'
!$omp barrier
  l = l .or. f2 .ne. 'def'
!$omp end parallel
  if (l) call abort
  f2 = '_/!!/_'
end function f2
