* { dg-do run }

      program main 
************************************************************
* program to solve a finite difference 
* discretization of Helmholtz equation :  
* (d2/dx2)u + (d2/dy2)u - alpha u = f 
* using Jacobi iterative method. 
*
* Modified: Sanjiv Shah,       Kuck and Associates, Inc. (KAI), 1998
* Author:   Joseph Robicheaux, Kuck and Associates, Inc. (KAI), 1998
* 
* Directives are used in this code to achieve paralleism. 
* All do loops are parallized with default 'static' scheduling.
* 
* Input :  n - grid dimension in x direction 
*          m - grid dimension in y direction
*          alpha - Helmholtz constant (always greater than 0.0)
*          tol   - error tolerance for iterative solver
*          relax - Successice over relaxation parameter
*          mits  - Maximum iterations for iterative solver
*
* On output 
*       : u(n,m) - Dependent variable (solutions)
*       : f(n,m) - Right hand side function 
*************************************************************
      implicit none 

      integer n,m,mits,mtemp
      include "omp_lib.h"
      double precision tol,relax,alpha 

      common /idat/ n,m,mits,mtemp
      common /fdat/tol,alpha,relax
* 
* Read info 
* 
      write(*,*) "Input n,m - grid dimension in x,y direction " 
      n = 64
      m = 64
*     read(5,*) n,m 
      write(*,*) n, m
      write(*,*) "Input alpha - Helmholts constant " 
      alpha = 0.5
*     read(5,*) alpha
      write(*,*) alpha
      write(*,*) "Input relax - Successive over-relaxation parameter"
      relax = 0.9
*     read(5,*) relax 
      write(*,*) relax
      write(*,*) "Input tol - error tolerance for iterative solver" 
      tol = 1.0E-12
*     read(5,*) tol 
      write(*,*) tol
      write(*,*) "Input mits - Maximum iterations for solver" 
      mits = 100
*     read(5,*) mits
      write(*,*) mits

      call omp_set_num_threads (2)

*
* Calls a driver routine 
* 
      call driver () 

      stop
      end 

      subroutine driver ( ) 
*************************************************************
* Subroutine driver () 
* This is where the arrays are allocated and initialzed. 
*
* Working varaibles/arrays 
*     dx  - grid spacing in x direction 
*     dy  - grid spacing in y direction 
*************************************************************
      implicit none 

      integer n,m,mits,mtemp 
      double precision tol,relax,alpha 

      common /idat/ n,m,mits,mtemp
      common /fdat/tol,alpha,relax

      double precision u(n,m),f(n,m),dx,dy

* Initialize data

      call initialize (n,m,alpha,dx,dy,u,f)

* Solve Helmholtz equation

      call jacobi (n,m,dx,dy,alpha,relax,u,f,tol,mits)

* Check error between exact solution

      call  error_check (n,m,alpha,dx,dy,u,f)

      return 
      end 

      subroutine initialize (n,m,alpha,dx,dy,u,f) 
******************************************************
* Initializes data 
* Assumes exact solution is u(x,y) = (1-x^2)*(1-y^2)
*
******************************************************
      implicit none 
     
      integer n,m
      double precision u(n,m),f(n,m),dx,dy,alpha
      
      integer i,j, xx,yy
      double precision PI 
      parameter (PI=3.1415926)

      dx = 2.0 / (n-1)
      dy = 2.0 / (m-1)

* Initilize initial condition and RHS

!$omp parallel do private(xx,yy)
      do j = 1,m
         do i = 1,n
            xx = -1.0 + dx * dble(i-1)        ! -1 < x < 1
            yy = -1.0 + dy * dble(j-1)        ! -1 < y < 1
            u(i,j) = 0.0 
            f(i,j) = -alpha *(1.0-xx*xx)*(1.0-yy*yy) 
     &           - 2.0*(1.0-xx*xx)-2.0*(1.0-yy*yy)
         enddo
      enddo
!$omp end parallel do

      return 
      end 

      subroutine jacobi (n,m,dx,dy,alpha,omega,u,f,tol,maxit)
******************************************************************
* Subroutine HelmholtzJ
* Solves poisson equation on rectangular grid assuming : 
* (1) Uniform discretization in each direction, and 
* (2) Dirichlect boundary conditions 
* 
* Jacobi method is used in this routine 
*
* Input : n,m   Number of grid points in the X/Y directions 
*         dx,dy Grid spacing in the X/Y directions 
*         alpha Helmholtz eqn. coefficient 
*         omega Relaxation factor 
*         f(n,m) Right hand side function 
*         u(n,m) Dependent variable/Solution
*         tol    Tolerance for iterative solver 
*         maxit  Maximum number of iterations 
*
* Output : u(n,m) - Solution 
*****************************************************************
      implicit none 
      integer n,m,maxit
      double precision dx,dy,f(n,m),u(n,m),alpha, tol,omega
*
* Local variables 
* 
      integer i,j,k,k_local 
      double precision error,resid,rsum,ax,ay,b
      double precision error_local, uold(n,m)

      real ta,tb,tc,td,te,ta1,ta2,tb1,tb2,tc1,tc2,td1,td2
      real te1,te2
      real second
      external second
*
* Initialize coefficients 
      ax = 1.0/(dx*dx) ! X-direction coef 
      ay = 1.0/(dy*dy) ! Y-direction coef
      b  = -2.0/(dx*dx)-2.0/(dy*dy) - alpha ! Central coeff  

      error = 10.0 * tol 
      k = 1

      do while (k.le.maxit .and. error.gt. tol) 

         error = 0.0    

* Copy new solution into old
!$omp parallel

!$omp do 
         do j=1,m
            do i=1,n
               uold(i,j) = u(i,j) 
            enddo
         enddo

* Compute stencil, residual, & update

!$omp do private(resid) reduction(+:error)
         do j = 2,m-1
            do i = 2,n-1 
*     Evaluate residual 
               resid = (ax*(uold(i-1,j) + uold(i+1,j)) 
     &                + ay*(uold(i,j-1) + uold(i,j+1))
     &                 + b * uold(i,j) - f(i,j))/b
* Update solution 
               u(i,j) = uold(i,j) - omega * resid
* Accumulate residual error
               error = error + resid*resid 
            end do
         enddo
!$omp enddo nowait

!$omp end parallel

* Error check 

         k = k + 1

         error = sqrt(error)/dble(n*m)
*
      enddo                     ! End iteration loop 
*
      print *, 'Total Number of Iterations ', k 
      print *, 'Residual                   ', error 

      return 
      end 

      subroutine error_check (n,m,alpha,dx,dy,u,f) 
      implicit none 
************************************************************
* Checks error between numerical and exact solution 
*
************************************************************ 
     
      integer n,m
      double precision u(n,m),f(n,m),dx,dy,alpha 
      
      integer i,j
      double precision xx,yy,temp,error 

      dx = 2.0 / (n-1)
      dy = 2.0 / (m-1)
      error = 0.0 

!$omp parallel do private(xx,yy,temp) reduction(+:error)
      do j = 1,m
         do i = 1,n
            xx = -1.0d0 + dx * dble(i-1)
            yy = -1.0d0 + dy * dble(j-1)
            temp  = u(i,j) - (1.0-xx*xx)*(1.0-yy*yy)
            error = error + temp*temp 
         enddo
      enddo
  
      error = sqrt(error)/dble(n*m)

      print *, 'Solution Error : ',error

      return 
      end 
