! { dg-options "-fno-openmp" }
      program condinc2
      logical l
      l = .true.
C$    include 'condinc1.inc'
      return
      end
