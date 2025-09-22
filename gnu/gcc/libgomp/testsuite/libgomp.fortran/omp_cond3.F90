! Test conditional compilation in free form if -fopenmp
! { dg-options "-fopenmp" }
   10 foo = 2&
  &56
  if (foo.ne.256) call abort
  bar = 26
   !$  20 ba&
!$   &r = 4&
  !$2
      !$bar = 62
   !$ bar = bar + 2
#ifdef _OPENMP
bar = bar - 1
#endif
  if (bar.ne.43) call abort
      baz = bar
!$ 30 baz = 5&     ! Comment
!$12  &  
  !$ + 2
!$X baz = 0 ! Not valid OpenMP conditional compilation lines
! $   baz = 1
baz = baz + 1 !$ baz = 2
      if (baz.ne.515) call abort
      end
