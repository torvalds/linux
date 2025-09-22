! { dg-do compile }

      SUBROUTINE SUB1(X)
        DIMENSION X(10)
        ! This use of X does not conform to the
        ! specification. It would be legal Fortran 90,
        ! but the OpenMP private directive allows the
        ! compiler to break the sequence association that
        ! A had with the rest of the common block.
        FORALL (I = 1:10) X(I) = I
      END SUBROUTINE SUB1
      PROGRAM A28_5
        COMMON /BLOCK5/ A
        DIMENSION B(10)
        EQUIVALENCE (A,B(1))
        ! the common block has to be at least 10 words
        A=0
!$OMP PARALLEL PRIVATE(/BLOCK5/)
          ! Without the private clause,
          ! we would be passing a member of a sequence
          ! that is at least ten elements long.
          ! With the private clause, A may no longer be
          ! sequence-associated.
          CALL SUB1(A)
!$OMP MASTER
            PRINT *, A
!$OMP END MASTER
!$OMP END PARALLEL
      END PROGRAM A28_5
