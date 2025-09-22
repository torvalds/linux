! { dg-do run }

  integer :: i
  common /c/ i
  i = -1
!$omp parallel shared (i) num_threads (4)
  call test1
!$omp end parallel
end
subroutine test1
  integer :: vari
  call test2
  call test3
contains
  subroutine test2
    use omp_lib
    integer :: i
    common /c/ i
!$omp single
    i = omp_get_thread_num ()
    call test4
!$omp end single copyprivate (vari)
  end subroutine test2
  subroutine test3
    integer :: i
    common /c/ i
    if (i .lt. 0 .or. i .ge. 4) call abort
    if (i + 10 .ne. vari) call abort
  end subroutine test3
  subroutine test4
    use omp_lib
    vari = omp_get_thread_num () + 10
  end subroutine test4
end subroutine test1
