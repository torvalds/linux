c Test conditional compilation in fixed form if -fno-openmp
! { dg-options "-fno-openmp" }
   10 foo = 2
     &56
      if (foo.ne.256) call abort
      bar = 26
!$2 0 ba
c$   +r = 42
      !$ bar = 62
!$    bar = bar + 1
      if (bar.ne.26) call abort
      baz = bar
*$   0baz = 5
C$   +12! Comment
c$   !4
!$   +!Another comment
*$   &2
!$ X  baz = 0 ! Not valid OpenMP conditional compilation lines
! $   baz = 1
c$ 10&baz = 2
      if (baz.ne.26) call abort
      end
