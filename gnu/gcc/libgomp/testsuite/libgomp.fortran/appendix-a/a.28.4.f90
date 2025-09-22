! { dg-do run }

       PROGRAM A28_4
         INTEGER I, J
         INTEGER A(100), B(100)
         EQUIVALENCE (A(51), B(1))
!$OMP PARALLEL DO DEFAULT(PRIVATE) PRIVATE(I,J) LASTPRIVATE(A)
           DO I=1,100
               DO J=1,100
                 B(J) = J - 1
               ENDDO
               DO J=1,100
                 A(J) = J    ! B becomes undefined at this point
               ENDDO
               DO J=1,50
                 B(J) = B(J) + 1 ! B is undefined
                            ! A becomes undefined at this point
               ENDDO
           ENDDO
!$OMP END PARALLEL DO          ! The LASTPRIVATE write for A has
                               ! undefined results
        PRINT *, B  ! B is undefined since the LASTPRIVATE
                    ! write of A was not defined
        END PROGRAM A28_4
