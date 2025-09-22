! { dg-do run }

        PROGRAM A28_3
        EQUIVALENCE (X,Y)
        X = 1.0
!$OMP PARALLEL PRIVATE(X)
          PRINT *,Y         ! Y is undefined
          Y = 10
          PRINT *,X         ! X is undefined
!$OMP END PARALLEL
      END PROGRAM A28_3
