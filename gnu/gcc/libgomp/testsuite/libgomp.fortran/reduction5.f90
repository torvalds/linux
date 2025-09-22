! { dg-do run }

module reduction5
  intrinsic ior, min, max
end module reduction5

  call test1
  call test2
contains
  subroutine test1
    use reduction5, bitwise_or => ior
    integer :: n
    n = Z'f'
!$omp parallel sections num_threads (3) reduction (bitwise_or: n)
    n = ior (n, Z'20')
!$omp section
    n = bitwise_or (Z'410', n)
!$omp section
    n = bitwise_or (n, Z'2000')
!$omp end parallel sections
    if (n .ne. Z'243f') call abort
  end subroutine
  subroutine test2
    use reduction5, min => max, max => min
    integer :: m, n
    m = 8
    n = 4
!$omp parallel sections num_threads (3) reduction (min: n) &
!$omp & reduction (max: m)
    if (m .gt. 13) m = 13
    if (n .lt. 11) n = 11
!$omp section
    if (m .gt. 5) m = 5
    if (n .lt. 15) n = 15
!$omp section
    if (m .gt. 3) m = 3
    if (n .lt. -1) n = -1
!$omp end parallel sections
    if (m .ne. 3 .or. n .ne. 15) call abort
  end subroutine test2
end
