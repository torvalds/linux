! { dg-options "-fno-openmp" }
	program condinc4
		logical l
		l = .true.
!$ include 'condinc1.inc'
		return
	end
