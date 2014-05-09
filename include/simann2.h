// This may look like C code, but it is really -*- C++ -*-

// simanneal.hpp	A general purpose Simulated Annealing Class
//	This version allows vector data

// rcsid: @(#)simann.hpp	1.3 15:55:36 3/30/93   EFC

#ifndef SIM_ANNEAL_HPP_
#define SIM_ANNEAL_HPP_ 1.3

#ifdef __GNUG__
#pragma interface
#endif

using namespace std;

#include <string>

#include <ACG.h>
#include <Uniform.h>
#include <Func1d.H>

#ifndef PI
#define PI		3.1415626536
#endif

class SimAnneal
{
 private:

  Func1d *func;			// 
  int dimension;		// 
  int ddwell;			// 
  double rrange;		// 
  double t0;			// 
  double K;			// 
  double rho;			// 
  double dt;			// temperature increment to use when melting
  double tscale	;		// 
  int maxit;			// 
  double c_jump;		// phase transition step size
  int fsave;			// 

  ACG *gen;			// Random number generation from libg++
  Uniform *number_range;	// Define function pointers for ANSI
  Uniform *number_01;
  int err;
  double *x, *xnew, *xbest;
  double y, dy, ybest;

  int equilibrate(const double t, const int n);
  string fname;
  void log_state(int);

 public:

  SimAnneal() :	 func(NULL), dimension(1), ddwell(20), rrange(PI/2.0), 
    t0(0.0), K(1.0), rho(0.5), dt(0.1), tscale(0.1), maxit(400), c_jump(100.0),
    fsave(0) {
    gen = new ACG(10,20);
    number_range = new Uniform(-rrange, rrange, gen);
    number_01 = new Uniform(0.0, 1.0, gen);
  }

  SimAnneal(Func1d* f, const int d = 1);

  ~SimAnneal() 
  { 
    delete [] x; delete [] xnew; delete [] xbest; 
    delete gen; delete number_range; delete number_01; 
  }
  
  int set_up(Func1d* f, const int d = 1, const uint32_t seed=10);
  
  const int operator!() const { return err; }
  
  double melt(const int iters = -1);
  double anneal(const int iters = -1);
  
  int iterations(const int m = -1) { if ( m > 0 ) maxit = m;
				     return maxit; }
  int dwell(const int d = -1)      { if ( d > 0 ) ddwell = d;
				     return ddwell; }
  double Boltzmann(const double k = -1.0)
    { if ( k > 0.0 ) K = k;
      return K; }
  
  double learning_rate(const double r = -1.0)
    { if ( r > 0.0 ) rho = r;
      return rho; }
  double temperature(const double t = -1.0)
    { if ( t > 0.0 ) t0 = t;
      return t0; }
  double jump(const double j = -1.0)
    { if ( j > 0.0 ) c_jump = j;
      return c_jump; }
  double range(const double r = -1.0)
    { 
      if ( r > 0.0 ) 
	{ rrange = r; delete number_range; 
	number_range=new Uniform(-r,r,gen);}
      return rrange; 
    }
  void initial(double* xinit);
  void current(double* xcur);
  void optimum(double* xopt);
  void save_states(const char *name) {
    fsave = 1;
    fname = string(name);
  }
};

#endif
