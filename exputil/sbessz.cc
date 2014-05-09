/**************************************************************************
*
*		Computes m zeros of spherical Bessel functions (first kind) 
*               j_n(x) by brute force and using Watson's relation for the
*               location of the first zero
*
*
*		MDW: 12/26/1987
*                    port to C++ 02/15/94
**************************************************************************/

#include <math.h>
#include <Vector.h>
#include <numerical.h>

#define STEPS 6
#define TOL 1.0e-7

static int NN;
double jn_sph(int, double);

static double zbess(double z)
{
  return jn_sph(NN,z);
}

Vector sbessjz(int n, int m)
{
  double z,dz,zl,f,fl;
  int i;

  Vector a(1, m);

  NN = n;
  dz = M_PI/STEPS;
  for (i=1, zl=z=0.5+fabs((double)n), fl=jn_sph(n,z); i<=m; i++) {
    z += dz;
    f = jn_sph(n,z);
    while (f*fl>0) {
      zl = z;
      fl = f;
      z += dz;
      f = jn_sph(n,z);
    }
    a[i] = zbrent(zbess,zl,z,TOL);
    zl = z;
    fl = f;
  }

  return a;

}