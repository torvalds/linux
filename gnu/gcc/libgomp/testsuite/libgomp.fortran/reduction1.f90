! { dg-do run }
!$ use omp_lib

  integer :: i, ia (6), n, cnt
  real :: r, ra (4)
  double precision :: d, da (5)
  complex :: c, ca (3)
  logical :: v

  i = 1
  ia = 2
  r = 3
  ra = 4
  d = 5.5
  da = 6.5
  c = cmplx (7.5, 1.5)
  ca = cmplx (8.5, -3.0)
  v = .false.
  cnt = -1

!$omp parallel num_threads (3) private (n) reduction (.or.:v) &
!$omp & reduction (+:i, ia, r, ra, d, da, c, ca)
!$ if (i .ne. 0 .or. any (ia .ne. 0)) v = .true.
!$ if (r .ne. 0 .or. any (ra .ne. 0)) v = .true.
!$ if (d .ne. 0 .or. any (da .ne. 0)) v = .true.
!$ if (c .ne. cmplx (0) .or. any (ca .ne. cmplx (0))) v = .true.
  n = omp_get_thread_num ()
  if (n .eq. 0) then
    cnt = omp_get_num_threads ()
    i = 4
    ia(3:5) = -2
    r = 5
    ra(1:2) = 6.5
    d = -2.5
    da(2:4) = 8.5
    c = cmplx (2.5, -3.5)
    ca(1) = cmplx (4.5, 5)
  else if (n .eq. 1) then
    i = 2
    ia(4:6) = 5
    r = 1
    ra(2:4) = -1.5
    d = 8.5
    da(1:3) = 2.5
    c = cmplx (0.5, -3)
    ca(2:3) = cmplx (-1, 6)
  else
    i = 1
    ia = 1
    r = -1
    ra = -1
    d = 1
    da = -1
    c = 1
    ca = cmplx (-1, 0)
  end if
!$omp end parallel
  if (v) call abort
  if (cnt .eq. 3) then
    if (i .ne. 8 .or. any (ia .ne. (/3, 3, 1, 6, 6, 8/))) call abort
    if (r .ne. 8 .or. any (ra .ne. (/9.5, 8.0, 1.5, 1.5/))) call abort
    if (d .ne. 12.5 .or. any (da .ne. (/8.0, 16.5, 16.5, 14.0, 5.5/))) call abort
    if (c .ne. cmplx (11.5, -5)) call abort
    if (ca(1) .ne. cmplx (12, 2)) call abort
    if (ca(2) .ne. cmplx (6.5, 3) .or. ca(2) .ne. ca(3)) call abort
  end if

  i = 1
  ia = 2
  r = 3
  ra = 4
  d = 5.5
  da = 6.5
  c = cmplx (7.5, 1.5)
  ca = cmplx (8.5, -3.0)
  v = .false.
  cnt = -1

!$omp parallel num_threads (3) private (n) reduction (.or.:v) &
!$omp & reduction (-:i, ia, r, ra, d, da, c, ca)
!$ if (i .ne. 0 .or. any (ia .ne. 0)) v = .true.
!$ if (r .ne. 0 .or. any (ra .ne. 0)) v = .true.
!$ if (d .ne. 0 .or. any (da .ne. 0)) v = .true.
!$ if (c .ne. cmplx (0) .or. any (ca .ne. cmplx (0))) v = .true.
  n = omp_get_thread_num ()
  if (n .eq. 0) then
    cnt = omp_get_num_threads ()
    i = 4
    ia(3:5) = -2
    r = 5
    ra(1:2) = 6.5
    d = -2.5
    da(2:4) = 8.5
    c = cmplx (2.5, -3.5)
    ca(1) = cmplx (4.5, 5)
  else if (n .eq. 1) then
    i = 2
    ia(4:6) = 5
    r = 1
    ra(2:4) = -1.5
    d = 8.5
    da(1:3) = 2.5
    c = cmplx (0.5, -3)
    ca(2:3) = cmplx (-1, 6)
  else
    i = 1
    ia = 1
    r = -1
    ra = -1
    d = 1
    da = -1
    c = 1
    ca = cmplx (-1, 0)
  end if
!$omp end parallel
  if (v) call abort
  if (cnt .eq. 3) then
    if (i .ne. 8 .or. any (ia .ne. (/3, 3, 1, 6, 6, 8/))) call abort
    if (r .ne. 8 .or. any (ra .ne. (/9.5, 8.0, 1.5, 1.5/))) call abort
    if (d .ne. 12.5 .or. any (da .ne. (/8.0, 16.5, 16.5, 14.0, 5.5/))) call abort
    if (c .ne. cmplx (11.5, -5)) call abort
    if (ca(1) .ne. cmplx (12, 2)) call abort
    if (ca(2) .ne. cmplx (6.5, 3) .or. ca(2) .ne. ca(3)) call abort
  end if

  i = 1
  ia = 2
  r = 4
  ra = 8
  d = 16
  da = 32
  c = 2
  ca = cmplx (0, 2)
  v = .false.
  cnt = -1

!$omp parallel num_threads (3) private (n) reduction (.or.:v) &
!$omp & reduction (*:i, ia, r, ra, d, da, c, ca)
!$ if (i .ne. 1 .or. any (ia .ne. 1)) v = .true.
!$ if (r .ne. 1 .or. any (ra .ne. 1)) v = .true.
!$ if (d .ne. 1 .or. any (da .ne. 1)) v = .true.
!$ if (c .ne. cmplx (1) .or. any (ca .ne. cmplx (1))) v = .true.
  n = omp_get_thread_num ()
  if (n .eq. 0) then
    cnt = omp_get_num_threads ()
    i = 3
    ia(3:5) = 2
    r = 0.5
    ra(1:2) = 2
    d = -1
    da(2:4) = -2
    c = 2.5
    ca(1) = cmplx (-5, 0)
  else if (n .eq. 1) then
    i = 2
    ia(4:6) = -2
    r = 8
    ra(2:4) = -0.5
    da(1:3) = -1
    c = -3
    ca(2:3) = cmplx (0, -1)
  else
    ia = 2
    r = 0.5
    ra = 0.25
    d = 2.5
    da = -1
    c = cmplx (0, -1)
    ca = cmplx (-1, 0)
  end if
!$omp end parallel
  if (v) call abort
  if (cnt .eq. 3) then
    if (i .ne. 6 .or. any (ia .ne. (/4, 4, 8, -16, -16, -8/))) call abort
    if (r .ne. 8 .or. any (ra .ne. (/4., -2., -1., -1./))) call abort
    if (d .ne. -40 .or. any (da .ne. (/32., -64., -64., 64., -32./))) call abort
    if (c .ne. cmplx (0, 15)) call abort
    if (ca(1) .ne. cmplx (0, 10)) call abort
    if (ca(2) .ne. cmplx (-2, 0) .or. ca(2) .ne. ca(3)) call abort
  end if
end
