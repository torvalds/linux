! PR middle-end/28790
! { dg-do run }

program nestomp
  integer :: j
  j = 8
  call bar
  if (j.ne.10) call abort
contains
  subroutine foo (i)
    integer :: i
  !$omp atomic
    j = j + i - 5
  end subroutine
  subroutine bar
  use omp_lib
  integer :: i
  i = 6
  call omp_set_dynamic (.false.)
  !$omp parallel num_threads (2)
    call foo(i)
  !$omp end parallel
  end subroutine
end
