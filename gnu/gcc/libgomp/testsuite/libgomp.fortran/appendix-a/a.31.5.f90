! { dg-do run }
            MODULE MOD
            INTRINSIC MAX, MIN
            END MODULE MOD
            PROGRAM A31_5
            USE MOD, MIN=>MAX, MAX=>MIN
            REAL :: R
            R = -HUGE(0.0)
            !$OMP PARALLEL DO REDUCTION(MIN: R) ! still does MAX
            DO I = 1, 1000
                R = MIN(R, SIN(REAL(I)))
            END DO
            PRINT *, R
            END PROGRAM A31_5
