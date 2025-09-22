! { dg-do run }
       PROGRAM A26
         INTEGER I, J
         I=1
         J=2
!$OMP PARALLEL PRIVATE(I) FIRSTPRIVATE(J)
           I=3
           J=J+2
!$OMP END PARALLEL
          PRINT *, I, J ! I and J are undefined
      END PROGRAM A26
