! { dg-do run }
!$ use omp_lib
  call test_workshare

contains
  subroutine test_workshare
    integer :: i, j, k, l, m
    double precision, dimension (64) :: d, e
    integer, dimension (10) :: f, g
    integer, dimension (16, 16) :: a, b, c
    integer, dimension (16) :: n
    d(:) = 1
    e = 7
    f = 10
    l = 256
    m = 512
    g(1:3) = -1
    g(4:6) = 0
    g(7:8) = 5
    g(9:10) = 10
    forall (i = 1:16, j = 1:16) a (i, j) = i * 16 + j
    forall (j = 1:16) n (j) = j
!$omp parallel num_threads (4) private (j, k)
!$omp barrier
!$omp workshare
    i = 6
    e(:) = d(:)
    where (g .lt. 0)
      f = 100
    elsewhere (g .eq. 0)
      f = 200 + f
    elsewhere
      where (g .gt. 6) f = f + sum (g)
      f = 300 + f
    end where
    where (f .gt. 210) g = 0
!$omp end workshare nowait
!$omp workshare
    forall (j = 1:16, k = 1:16) b (k, j) = a (j, k)
    forall (k = 1:16) c (k, 1:16) = a (1:16, k)
    forall (j = 2:16, n (17 - j) / 4 * 4 .ne. n (17 - j))
      n (j) = n (j - 1) * n (j)
    end forall
!$omp endworkshare
!$omp workshare
!$omp atomic
    i = i + 8 + 6
!$omp critical
!$omp critical (critical_foox)
    l = 128
!$omp end critical (critical_foox)
!$omp endcritical
!$omp parallel num_threads (2)
!$  if (omp_get_thread_num () .eq. 0) m = omp_get_num_threads ()
!$omp atomic
    l = 1 + l
!$omp end parallel
!$omp end workshare
!$omp end parallel

    if (any (f .ne. (/100, 100, 100, 210, 210, 210, 310, 310, 337, 337/))) &
&     call abort
    if (any (g .ne. (/-1, -1, -1, 0, 0, 0, 0, 0, 0, 0/))) call abort
    if (i .ne. 20) call abort
!$  if (l .ne. 128 + m) call abort
    if (any (d .ne. 1 .or. e .ne. 1)) call abort
    if (any (b .ne. transpose (a))) call abort
    if (any (c .ne. b)) call abort
    if (any (n .ne. (/1, 2, 6, 12, 5, 30, 42, 56, 9, 90, &
&                     110, 132, 13, 182, 210, 240/))) call abort
  end subroutine test_workshare
end
