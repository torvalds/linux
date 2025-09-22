! { dg-do run }
!$ use omp_lib

  logical :: l, la (4), m, ma (4), v
  integer :: n, cnt

  l = .true.
  la = (/.true., .false., .true., .true./)
  m = .false.
  ma = (/.false., .false., .false., .true./)
  v = .false.
  cnt = -1

!$omp parallel num_threads (3) private (n) reduction (.or.:v) &
!$omp & reduction (.and.:l, la) reduction (.or.:m, ma)
!$ if (.not. l .or. any (.not. la)) v = .true.
!$ if (m .or. any (ma)) v = .true.
  n = omp_get_thread_num ()
  if (n .eq. 0) then
    cnt = omp_get_num_threads ()
    l = .false.
    la(3) = .false.
    ma(2) = .true.
  else if (n .eq. 1) then
    l = .false.
    la(4) = .false.
    ma(1) = .true.
  else
    la(3) = .false.
    m = .true.
    ma(1) = .true.
  end if
!$omp end parallel
  if (v) call abort
  if (cnt .eq. 3) then
    if (l .or. any (la .neqv. (/.true., .false., .false., .false./))) call abort
    if (.not. m .or. any (ma .neqv. (/.true., .true., .false., .true./))) call abort
  end if

  l = .true.
  la = (/.true., .false., .true., .true./)
  m = .false.
  ma = (/.false., .false., .false., .true./)
  v = .false.
  cnt = -1

!$omp parallel num_threads (3) private (n) reduction (.or.:v) &
!$omp & reduction (.eqv.:l, la) reduction (.neqv.:m, ma)
!$ if (.not. l .or. any (.not. la)) v = .true.
!$ if (m .or. any (ma)) v = .true.
  n = omp_get_thread_num ()
  if (n .eq. 0) then
    cnt = omp_get_num_threads ()
    l = .false.
    la(3) = .false.
    ma(2) = .true.
  else if (n .eq. 1) then
    l = .false.
    la(4) = .false.
    ma(1) = .true.
  else
    la(3) = .false.
    m = .true.
    ma(1) = .true.
  end if
!$omp end parallel
  if (v) call abort
  if (cnt .eq. 3) then
    if (.not. l .or. any (la .neqv. (/.true., .false., .true., .false./))) call abort
    if (.not. m .or. any (ma .neqv. (/.false., .true., .false., .true./))) call abort
  end if

end
