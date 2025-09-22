! { dg-do run }
        REAL FUNCTION WORK1(I)
            INTEGER I
            WORK1 = 1.0 * I
            RETURN
        END FUNCTION WORK1

        REAL FUNCTION WORK2(I)
            INTEGER I
            WORK2 = 2.0 * I
            RETURN
        END FUNCTION WORK2

        SUBROUTINE SUBA16(X, Y, INDEX, N)
        REAL X(*), Y(*)
        INTEGER INDEX(*), N
        INTEGER I
!$OMP PARALLEL DO SHARED(X, Y, INDEX, N)
          DO I=1,N
!$OMP ATOMIC
              X(INDEX(I)) = X(INDEX(I)) + WORK1(I)
            Y(I) = Y(I) + WORK2(I)
          ENDDO
      END SUBROUTINE SUBA16

      PROGRAM A16
        REAL X(1000), Y(10000)
        INTEGER INDEX(10000)
        INTEGER I
        DO I=1,10000
          INDEX(I) = MOD(I, 1000) + 1
          Y(I) = 0.0
        ENDDO
        DO I = 1,1000
          X(I) = 0.0
        ENDDO
        CALL SUBA16(X, Y, INDEX, 10000)
        DO I = 1,10
          PRINT *, "X(", I, ") = ", X(I), ", Y(", I, ") = ", Y(I)
        ENDDO
      END PROGRAM A16
