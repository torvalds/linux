! { dg-do run }
! { dg-require-effective-target tls_runtime }

module threadprivate3
  integer, dimension(:,:), pointer :: foo => NULL()
!$omp threadprivate (foo)
end module threadprivate3

  use omp_lib
  use threadprivate3

  integer, dimension(:), pointer :: bar1
  integer, dimension(2), target :: bar2, var
  common /thrc/ bar1, bar2
!$omp threadprivate (/thrc/)

  integer, dimension(:), pointer, save :: bar3 => NULL()
!$omp threadprivate (bar3)

  logical :: l
  type tt
    integer :: a
    integer :: b = 32
  end type tt
  type (tt), save :: baz
!$omp threadprivate (baz)

  l = .false.
  call omp_set_dynamic (.false.)
  call omp_set_num_threads (4)
  var = 6

!$omp parallel num_threads (4) reduction (.or.:l)
  bar2 = omp_get_thread_num ()
  l = associated (bar3)
  bar1 => bar2
  l = l.or..not.associated (bar1)
  l = l.or..not.associated (bar1, bar2)
  l = l.or.any (bar1.ne.omp_get_thread_num ())
  nullify (bar1)
  l = l.or.associated (bar1)
  allocate (bar3 (4))
  l = l.or..not.associated (bar3)
  bar3 = omp_get_thread_num () - 2
  if (omp_get_thread_num () .ne. 0) then
    deallocate (bar3)
    if (associated (bar3)) call abort
  else
    bar1 => var
  end if
  bar2 = omp_get_thread_num () * 6 + 130

  l = l.or.(baz%b.ne.32)
  baz%a = omp_get_thread_num () * 2
  baz%b = omp_get_thread_num () * 2 + 1
!$omp end parallel

  if (l) call abort
  if (.not.associated (bar1)) call abort
  if (any (bar1.ne.6)) call abort
  if (.not.associated (bar3)) call abort
  if (any (bar3 .ne. -2)) call abort
  deallocate (bar3)
  if (associated (bar3)) call abort

  allocate (bar3 (10))
  bar3 = 17

!$omp parallel copyin (bar1, bar2, bar3, baz) num_threads (4) &
!$omp& reduction (.or.:l)
  l = l.or..not.associated (bar1)
  l = l.or.any (bar1.ne.6)
  l = l.or.any (bar2.ne.130)
  l = l.or..not.associated (bar3)
  l = l.or.size (bar3).ne.10
  l = l.or.any (bar3.ne.17)
  allocate (bar1 (4))
  bar1 = omp_get_thread_num ()
  bar2 = omp_get_thread_num () + 8

  l = l.or.(baz%a.ne.0)
  l = l.or.(baz%b.ne.1)
  baz%a = omp_get_thread_num () * 3 + 4
  baz%b = omp_get_thread_num () * 3 + 5

!$omp barrier
  if (omp_get_thread_num () .eq. 0) then
    deallocate (bar3)
  end if
  bar3 => bar2
!$omp barrier

  l = l.or..not.associated (bar1)
  l = l.or..not.associated (bar3)
  l = l.or.any (bar1.ne.omp_get_thread_num ())
  l = l.or.size (bar1).ne.4
  l = l.or.any (bar2.ne.omp_get_thread_num () + 8)
  l = l.or.any (bar3.ne.omp_get_thread_num () + 8)
  l = l.or.size (bar3).ne.2

  l = l.or.(baz%a .ne. omp_get_thread_num () * 3 + 4)
  l = l.or.(baz%b .ne. omp_get_thread_num () * 3 + 5)
!$omp end parallel

  if (l) call abort
end
