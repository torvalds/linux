! { dg-do run }
!$ use omp_lib

  integer (kind = 4) :: i, ia (6), j, ja (6), k, ka (6), ta (6), n, cnt, x
  logical :: v

  i = Z'ffff0f'
  ia = Z'f0ff0f'
  j = Z'0f0000'
  ja = Z'0f5a00'
  k = Z'055aa0'
  ka = Z'05a5a5'
  v = .false.
  cnt = -1
  x = not(0)

!$omp parallel num_threads (3) private (n) reduction (.or.:v) &
!$omp & reduction (iand:i, ia) reduction (ior:j, ja) reduction (ieor:k, ka)
!$ if (i .ne. x .or. any (ia .ne. x)) v = .true.
!$ if (j .ne. 0 .or. any (ja .ne. 0)) v = .true.
!$ if (k .ne. 0 .or. any (ka .ne. 0)) v = .true.
  n = omp_get_thread_num ()
  if (n .eq. 0) then
    cnt = omp_get_num_threads ()
    i = Z'ff7fff'
    ia(3:5) = Z'fffff1'
    j = Z'078000'
    ja(1:3) = 1
    k = Z'78'
    ka(3:6) = Z'f0f'
  else if (n .eq. 1) then
    i = Z'ffff77'
    ia(2:5) = Z'ffafff'
    j = Z'007800'
    ja(2:5) = 8
    k = Z'57'
    ka(3:4) = Z'f0108'
  else
    i = Z'777fff'
    ia(1:2) = Z'fffff3'
    j = Z'000780'
    ja(5:6) = Z'f00'
    k = Z'1000'
    ka(6:6) = Z'777'
  end if
!$omp end parallel
  if (v) call abort
  if (cnt .eq. 3) then
    ta = (/Z'f0ff03', Z'f0af03', Z'f0af01', Z'f0af01', Z'f0af01', Z'f0ff0f'/)
    if (i .ne. Z'777f07' .or. any (ia .ne. ta)) call abort
    ta = (/Z'f5a01', Z'f5a09', Z'f5a09', Z'f5a08', Z'f5f08', Z'f5f00'/)
    if (j .ne. Z'fff80' .or. any (ja .ne. ta)) call abort
    ta = (/Z'5a5a5', Z'5a5a5', Z'aaba2', Z'aaba2', Z'5aaaa', Z'5addd'/)
    if (k .ne. Z'54a8f' .or. any (ka .ne. ta)) call abort
  end if
end
