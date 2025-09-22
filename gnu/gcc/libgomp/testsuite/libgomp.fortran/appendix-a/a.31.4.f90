! { dg-do run }
        MODULE M
        INTRINSIC MAX
        END MODULE M
        PROGRAM A31_4
        USE M, REN => MAX
        N=0
!$OMP PARALLEL DO REDUCTION(REN: N) ! still does MAX
        DO I = 1, 100
            N = MAX(N,I)
        END DO
        END PROGRAM A31_4
