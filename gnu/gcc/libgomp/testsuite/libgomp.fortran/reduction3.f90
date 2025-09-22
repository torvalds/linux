! { dg-do run }
!$ use omp_lib

  integer (kind = 4) :: i, ia (6), n, cnt
  real :: r, ra (4)
  double precision :: d, da (5)
  logical :: v

  i = 1
  ia = 2
  r = 3
  ra = 4
  d = 5.5
  da = 6.5
  v = .false.
  cnt = -1

!$omp parallel num_threads (3) private (n) reduction (.or.:v) &
!$omp & reduction (max:i, ia, r, ra, d, da)
!$ if (i .ne. -huge(i)-1 .or. any (ia .ne. -huge(ia)-1)) v = .true.
!$ if (r .ge. -1.0d38 .or. any (ra .ge. -1.0d38)) v = .true.
!$ if (d .ge. -1.0d300 .or. any (da .ge. -1.0d300)) v = .true.
  n = omp_get_thread_num ()
  if (n .eq. 0) then
    cnt = omp_get_num_threads ()
    i = 4
    ia(3:5) = -2
    ia(1) = 7
    r = 5
    ra(1:2) = 6.5
    d = -2.5
    da(2:4) = 8.5
  else if (n .eq. 1) then
    i = 2
    ia(4:6) = 5
    r = 1
    ra(2:4) = -1.5
    d = 8.5
    da(1:3) = 2.5
  else
    i = 1
    ia = 1
    r = -1
    ra = -1
    d = 1
    da = -1
  end if
!$omp end parallel
  if (v) call abort
  if (cnt .eq. 3) then
    if (i .ne. 4 .or. any (ia .ne. (/7, 2, 2, 5, 5, 5/))) call abort
    if (r .ne. 5 .or. any (ra .ne. (/6.5, 6.5, 4., 4./))) call abort
    if (d .ne. 8.5 .or. any (da .ne. (/6.5, 8.5, 8.5, 8.5, 6.5/))) call abort
  end if

  i = 1
  ia = 2
  r = 3
  ra = 4
  d = 5.5
  da = 6.5
  v = .false.
  cnt = -1

!$omp parallel num_threads (3) private (n) reduction (.or.:v) &
!$omp & reduction (min:i, ia, r, ra, d, da)
!$ if (i .ne. 2147483647 .or. any (ia .ne. 2147483647)) v = .true.
!$ if (r .le. 1.0d38 .or. any (ra .le. 1.0d38)) v = .true.
!$ if (d .le. 1.0d300 .or. any (da .le. 1.0d300)) v = .true.
  n = omp_get_thread_num ()
  if (n .eq. 0) then
    cnt = omp_get_num_threads ()
    i = 4
    ia(3:5) = -2
    ia(1) = 7
    r = 5
    ra(1:2) = 6.5
    d = -2.5
    da(2:4) = 8.5
  else if (n .eq. 1) then
    i = 2
    ia(4:6) = 5
    r = 1
    ra(2:4) = -1.5
    d = 8.5
    da(1:3) = 2.5
  else
    i = 1
    ia = 1
    r = -1
    ra = 7
    ra(3) = -8.5
    d = 1
    da(1:4) = 6
  end if
!$omp end parallel
  if (v) call abort
  if (cnt .eq. 3) then
    if (i .ne. 1 .or. any (ia .ne. (/1, 1, -2, -2, -2, 1/))) call abort
    if (r .ne. -1 .or. any (ra .ne. (/4., -1.5, -8.5, -1.5/))) call abort
    if (d .ne. -2.5 .or. any (da .ne. (/2.5, 2.5, 2.5, 6., 6.5/))) call abort
  end if
end
