  ! { dg-options "-fopenmp" }
program condinc3
  logical l
  l = .false.
    !$ include 'condinc1.inc'
  stop 2
end
