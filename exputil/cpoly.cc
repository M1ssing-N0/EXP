
#include <math.h>
#include <unistd.h>
#include <stdlib.h>
#include <string>

#include <Vector.h>
#include <kevin_complex.h>

#include <cpoly.h>

using namespace std;

/*
	Default constructor; make a null vector.
*/

CPoly::CPoly(void) : CVector()
{
  order = 0;
}


CPoly::CPoly(int n): CVector(0, n)
{
  order = n;
  zero();
}


CPoly::CPoly(int n, double * vec) : CVector(0, n, vec)
{
  order = n;
}



CPoly::CPoly(const CVector& vec) : CVector((CVector &)vec)
{
  if (vec.getlow() != 0) bomb_CPoly("Error constructing CPoly with CVector");
  order = vec.gethigh();
}



/*
	Conversion constructor
*/

CPoly::CPoly(const Poly &p) : CVector((CVector &)p)
{
  order = p.getorder();
}

/*
	Copy constructor; create a new CPoly which is a copy of another.
*/

CPoly::CPoly(const CPoly &p) : CVector((CVector &)p)
{
  order = p.order;
}




/*
	Destructor. Free elements if it exists.
	[Does nothing, yet]
*/

// CPoly::~CPoly() {}


/*
	Function to reduce order if there are leading zero coefficients
*/


void CPoly::reduce_order(void)
{
  while ((*this)[order].real() == 0.0 && (*this)[order].imag() == 0.0 && 
	 order>0) order--;
}


/*
	Assignment operator for CPoly; must be defined as a reference
so that it can be used on lhs. Compatibility checking is performed;
the destination vector is allocated if its elements are undefined.
*/


CPoly &CPoly::operator=(const CPoly &v)
{
  int i;
	
  if (v.getlow()!=0 || v.gethigh()<1) {
    bomb_CPoly_operation("=");
  }

  setsize(0,v.order);
  for (i=0; i<=v.order; i++) (*this)[i] = v[i];
  order = v.order;

  return *this;
}


CPoly CPoly::operator-(void)
{
  int i;
  for (i=0; i<=order; i++) (*this)[i] = -(*this)[i];
  return *this;
}


CPoly &CPoly::operator+=(const CPoly &p2)
{
  int i;
  int n2 = p2.order;
  CVector tmp;

  if (order <= n2) {
    tmp = *this;
    setsize(0,n2);
    zero();
    for (i=0; i<=order; i++) (*this)[i] = tmp[i];
    order = n2;
  }
  for (i=0; i<=n2; i++) (*this)[i] += p2[i];

  reduce_order();
  return *this;
}
	
CPoly &CPoly::operator-=(const CPoly &p2)
{
  int i;
  int n2 = p2.order;
  CVector tmp;

  if (order <= n2) {
    tmp = *this;
    setsize(0,n2);
    zero();
    for (i=0; i<=order; i++) (*this)[i] = tmp[i];
    order = n2;
  }
  for (i=0; i<=n2; i++) (*this)[i] -= p2[i];

  reduce_order();
  return *this;
}


CPoly operator+(const CPoly &p1, const CPoly &p2)
{
  int i;
  CPoly tmp;
  int n1 = p1.order;
  int n2 = p2.order;

  if (n1 <= n2) {
    tmp = p2;
    for (i=0; i<=n1; i++) tmp[i] += p1[i];
  }
  else {
    tmp = p1;
    for (i=0; i<=n2; i++) tmp[i] += p2[i];
  }

  tmp.reduce_order();
  return tmp;
}
	

CPoly operator-(const CPoly &p1, const CPoly &p2)
{
  int i;
  CPoly tmp;
  int n1 = p1.order;
  int n2 = p2.order;

  if (n1 <= n2) {
    tmp = CPoly(n2);
    tmp.zero();
    for (i=0; i<=n1; i++) tmp[i] = p1[i] - p2[i];
    for (i=n1+1; i<=n2; i++) tmp[i] = - p2[i];
  }
  else {
    tmp = p1;
    for (i=0; i<=n2; i++) tmp[i] -= p2[i];
  }

  tmp.reduce_order();
  return tmp;
}
	

				// Cauchy product
CPoly operator&(const CPoly &p1, const CPoly &p2)
{
  int i, j;
  int n1 = p1.order;
  int n2 = p2.order;
  int neworder = n1+n2;
  CPoly tmp(neworder);

  for (i=0; i<=n1; i++) {
    for (j=0; j<=n2; j++) tmp[i+j] += p1[i]*p2[j];
  }

  tmp.reduce_order();
  return tmp;
}
	

CPoly &CPoly::operator&=(const CPoly &p2)
{
  int i, j;
  int n2 = p2.order;
  int neworder = order + n2;
  CPoly tmp = *this;
  setsize(0,neworder);
  zero();

  for (i=0; i<=order; i++) {
    for (j=0; j<=n2; j++) (*this)[i+j] += tmp[i]*p2[j];
  }

  order = neworder;
  reduce_order();
  
  return *this;
}
	

				// Euclidian division for power series
CPoly operator%(const CPoly &p1, const CPoly &p2)
{

  int k,j;
  int n1 = p1.order;
  int n2 = p2.order;

  CPoly quotient(n1);
  CPoly remainder = p1;

  for (k=0; k<=n1; k++) {
    quotient[k] = remainder[k]/p2[0];
    for (j=k+1; j<=n1 && j-k<=n2; j++)
      remainder[j] -= quotient[k]*p2[j-k];
  }
  
  quotient.reduce_order();
  return quotient;
}
	
CPoly &CPoly::operator%=(const CPoly &p2)
{

  int k,j;
  int n1 = order;
  int n2 = p2.order;

  for (k=0; k<=n1; k++) {
    (*this)[k] /= p2[0];
    for (j=k+1; j<=n1 && j-k<=n2; j++)
      (*this)[j] -= (*this)[k]*p2[j-k];
  }
  
  reduce_order();

  return *this;
}

/*
CPoly &CPoly::operator%=(const CPoly &p2)
{

  int k,j;
  int n1 = order;
  int n2 = p2.getorder();

  CPoly quotient = CPoly(n1);
  CPoly remainder = (*this);

  for (k=0; k<=n1; k++) {
    quotient[k] = remainder[k]/p2[0];
    for (j=k+1; j<=n1 && j-k<=n2; j++)
      remainder[j] -= quotient[k]*p2[j-k];
  }
  
  quotient.reduce_order();
  *this = quotient;

  return *this;
}
*/

KComplex CPoly::eval(KComplex z)
{
  int j;
  KComplex p = (*this)[j=order];
  while (j>0) p = p*z + (*this)[--j];

  return p;
}
	
KComplex CPoly::deriv(KComplex z)
{
  int j;
  KComplex p = (*this)[j=order];
  KComplex dp = 0.0;

  while (j>0) {
    dp = dp*z + p;
    p = p*z + (*this)[--j];
  }
  return dp;
}
	
void CPoly::print(ostream& out)
{
  int i;
	
  out << "[" << order << "]: ";
  for (i=0; i<=order; i++)
    out << "(" << (*this)[i].real() << " + " << (*this)[i].imag() << ") ";
  out << endl;

}

void bomb_CPoly(const char *msg)
{
  cerr << "CPoly ERROR: " << msg << '\n';
  exit(0);
}

void bomb_CPoly_operation(const char *op)
{
  string msg("incompatible arguments in operation ");
  msg += op;
  bomb_CPoly(msg.c_str());
}