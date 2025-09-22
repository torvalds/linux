! { dg-do run }
! { dg-require-effective-target tls_runtime }

module threadprivate1
  double precision :: d
!$omp threadprivate (d)
end module threadprivate1

!$ use omp_lib
  use threadprivate1
  logical :: l
  l = .false.
!$omp parallel num_threads (4) reduction (.or.:l)
  d = omp_get_thread_num () + 6.5
!$omp barrier
  if (d .ne. omp_get_thread_num () + 6.5) l = .true.
!$omp end parallel
  if (l) call abort ()
end
