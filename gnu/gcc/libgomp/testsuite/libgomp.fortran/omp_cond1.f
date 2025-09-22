C Test conditional compilation in fixed form if -fopenmp
! { dg-options "-fopenmp" }
   10 foo = 2
     &56
      if (foo.ne.256) call abort
      bar = 26
!$2 0 ba
c$   +r = 42
      !$ bar = 62
!$    bar = bar + 1
      if (bar.ne.43) call abort
      baz = bar
*$   0baz = 5
C$   +12! Comment
c$   !4
!$   +!Another comment
*$   &2
!$ X  baz = 0 ! Not valid OpenMP conditional compilation lines
! $   baz = 1
c$ 10&baz = 2
      if (baz.ne.51242) call abort
      end
