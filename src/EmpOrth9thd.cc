#include <iostream>
#include <iomanip>
#include <sstream>

#include <string>
#include <algorithm>

#include <interp.h>
#include <Timer.h>
#include "exp_thread.h"

#ifndef STANDALONE
#include "expand.h"
#include "global.H"
#else  
				// Constants from expand.h & global.H
extern int nthrds;
extern double tnow;
extern unsigned multistep;
extern int VERBOSE;
#endif

#include <numerical.h>
#include <gaussQ.h>
#include <EmpOrth9thd.h>

extern Vector Symmetric_Eigenvalues_SYEVD(Matrix& a, Matrix& ef, int M);

#undef  TINY
#define TINY 1.0e-16


bool     EmpCylSL::DENS            = false;
bool     EmpCylSL::SELECT          = false;
bool     EmpCylSL::CMAP            = false;
bool     EmpCylSL::logarithmic     = false;
bool     EmpCylSL::enforce_limits  = false;
int      EmpCylSL::NUMX            = 256;
int      EmpCylSL::NUMY            = 128;
int      EmpCylSL::NOUT            = 12;
int      EmpCylSL::NUMR            = 2000;
unsigned EmpCylSL::VFLAG           = 0;
double   EmpCylSL::RMIN            = 0.001;
double   EmpCylSL::RMAX            = 20.0;
double   EmpCylSL::HFAC            = 0.2;
string   EmpCylSL::CACHEFILE       = ".eof.cache.file";

EmpCylSL::EmpModel EmpCylSL::mtype = Exponential;

EmpCylSL::EmpCylSL(void)
{
  NORDER=0;
  MPIset = false;
  MPIset_eof = false;
  coefs_made = vector<short>(multistep+1, false);
  eof_made = false;

  if (DENS)
    MPItable = 4;
  else
    MPItable = 3;

  SC = 0;
  SS = 0;

  ortho = 0;
  model = 0;

  accum_cos = 0;
  accum_sin = 0;

  accum_cos2 = 0;
  accum_sin2 = 0;

  cylmass = 0.0;
  cylmass_made = false;
  cylmass1 = vector<double>(nthrds);

  mpi_double_buf2 = 0;
  mpi_double_buf3 = 0;

  hallfile = "";
  hallcount = 0;
  hallfreq = 50;
}

EmpCylSL::~EmpCylSL(void)
{
  delete ortho;
  delete model;

  if (SC) {

    for (int m=0; m<=MMAX; m++) {
      delete [] potC[m];
      delete [] rforceC[m];
      delete [] zforceC[m];
      if (DENS) delete [] densC[m];
      if (m) {
	delete [] potS[m];
	delete [] rforceS[m];
	delete [] zforceS[m];
	if (DENS) delete [] densS[m];
      }
    }


    delete [] potC;
    delete [] rforceC;
    delete [] zforceC;
    if (DENS) delete [] densC;

    delete [] potS;
    delete [] rforceS;
    delete [] zforceS;
    if (DENS) delete [] densS;

    delete [] tpot;
    delete [] trforce;
    delete [] tzforce;
    if (DENS) delete [] tdens;

    delete [] mpi_double_buf2;
    delete [] mpi_double_buf3;

    for (int nth=0; nth<nthrds; nth++) {

      for (int mm=0; mm<=MMAX; mm++) {
      
	for (int j1=1; j1<=NMAX*(LMAX-mm+1); j1++) {

	  delete [] &SC[nth][mm][j1][1];
	  if (mm) delete [] &SS[nth][mm][j1][1];
	}
	  
	delete [] &SC[nth][mm][1];
	if (mm) delete [] &SS[nth][mm][1];
      }
	
      delete [] SC[nth];
      delete [] SS[nth];
    }
    
    delete [] SC;
    delete [] SS;

    delete [] vc;
    delete [] vs;
    delete [] hold;
    delete [] var;

    delete [] cosm;
    delete [] sinm;
    delete [] legs;
    delete [] dlegs;

    delete [] table;
    delete [] facC;
    delete [] facS;
  }

  if (accum_cos) {

    if (SELECT) {
      for (int nth=0; nth<nthrds; nth++) {
	delete [] accum_cos2[nth];
	delete [] accum_sin2[nth];
      }
    }

    delete [] accum_cos;
    delete [] accum_sin;
    delete [] accum_cos2;
    delete [] accum_sin2;

  }
  
  for (unsigned M=0; M<accum_cosL.size(); M++) {
    
    for (int nth=0; nth<nthrds; nth++) {
      delete [] accum_cosL[M][nth];
      delete [] accum_sinL[M][nth];

      delete [] accum_cosN[M][nth];
      delete [] accum_sinN[M][nth];
    }

    delete [] accum_cosL[M];
    delete [] accum_sinL[M];

    delete [] accum_cosN[M];
    delete [] accum_sinN[M];
  }

  if (MPIset) {
    delete [] MPIin;
    delete [] MPIout;
  }

  if (MPIset_eof) {
    delete [] MPIin_eof;
    delete [] MPIout_eof;
  }

}


EmpCylSL::EmpCylSL(int nmax, int lmax, int mmax, int nord, 
		   double ascale, double hscale)
{
  NMAX = nmax;
  MMAX = mmax;
  LMAX = lmax;
  NORDER = nord;

  ASCALE = ascale;
  HSCALE = hscale;
  pfac = 1.0/sqrt(ascale);
  ffac = pfac/ascale;
  dfac = ffac/ascale;

  SLGridSph::mpi = 1;		// Turn on MPI
  ortho = new SLGridSph(LMAX, NMAX, NUMR, RMIN, RMAX*0.99, make_sl(), 1, 1.0);
  model = 0;

  if (DENS)
    MPItable = 4;
  else
    MPItable = 3;

  SC = 0;
  SS = 0;

  MPIset = false;
  MPIset_eof = false;
  coefs_made = vector<short>(multistep+1, false);
  eof_made = false;

  accum_cos = 0;
  accum_sin = 0;

  accum_cos2 = 0;
  accum_sin2 = 0;

  cylmass = 0.0;
  cylmass1 = vector<double>(nthrds);
  cylmass_made = false;

  mpi_double_buf2 = 0;
  mpi_double_buf3 = 0;

  hallfile = "";
  hallcount = 0;
  hallfreq = 50;
}


void EmpCylSL::reset(int numr, int lmax, int mmax, int nord, 
		     double ascale, double hscale)
{
  NMAX = numr;
  MMAX = mmax;
  LMAX = lmax;
  NORDER = nord;

  ASCALE = ascale;
  HSCALE = hscale;
  pfac = 1.0/sqrt(ascale);
  ffac = pfac/ascale;
  dfac = ffac/ascale;

  SLGridSph::mpi = 1;		// Turn on MPI
  ortho = new SLGridSph(LMAX, NMAX, NUMR, RMIN, RMAX*0.99, make_sl(), 1, 1.0);

  SC = 0;
  SS = 0;

  MPIset = false;
  MPIset_eof = false;
  coefs_made = vector<short>(multistep+1, false);
  eof_made = false;

  if (DENS)
    MPItable = 4;
  else
    MPItable = 3;

  accum_cos = 0;
  accum_sin = 0;

  accum_cos2 = 0;
  accum_sin2 = 0;

  cylmass = 0.0;
  cylmass1 = vector<double>(nthrds);
  cylmass_made = false;

  mpi_double_buf2 = 0;
  mpi_double_buf3 = 0;
}

/*
  Note that the produced by the following three routines
  are in dimensionless units
*/
double EmpCylSL::massR(double R)
{
  double ans=0.0, fac, arg;

  switch (mtype) {
  case Exponential:
    ans = 1.0 - (1.0 + R)*exp(-R); 
    break;
  case Gaussian:
    arg = 0.5*R*R;
    ans = 1.0 - exp(-arg);
    break;
  case Plummer:
    fac = R/(1.0+R);
    ans = pow(fac, 3.0);
    break;
  }

  return ans;
}

double EmpCylSL::densR(double R)
{
  double ans=0.0, fac, arg;

  switch (mtype) {
  case Exponential:
    ans = exp(-R)/(4.0*M_PI*R);
    break;
  case Gaussian:
    arg = 0.5*R*R;
    ans = exp(-arg)/(4.0*M_PI*R);
    break;
  case Plummer:
    fac = 1.0/(1.0+R);
    ans = 3.0*pow(fac, 4.0)/(4.0*M_PI);
    break;
  }

  return ans;
}

SphericalModelTable* EmpCylSL::make_sl()
{
  const int number = 10000;

  r =  vector<double>(number);
  d =  vector<double>(number);
  m =  vector<double>(number);
  p =  vector<double>(number);

  vector<double> mm(number);
  vector<double> pw(number);

				// ------------------------------------------
				// Make radial, density and mass array
				// ------------------------------------------
  double dr;
  if (logarithmic)
    dr = (log(RMAX) - log(RMIN))/(number - 1);
  else
    dr = (RMAX - RMIN)/(number - 1);

  for (int i=0; i<number; i++) {
    if (logarithmic)
      r[i] = RMIN*exp(dr*i);
    else
      r[i] = RMIN + dr*i;

    m[i] = massR(r[i]);
    d[i] = densR(r[i]);
  }

  mm[0] = 0.0;
  pw[0] = 0.0;
  for (int i=1; i<number; i++) {
    mm[i] = mm[i-1] +
      2.0*M_PI*(r[i-1]*r[i-1]*d[i-1] + r[i]*r[i]*d[i])*
      (r[i] - r[i-1]);
    pw[i] = pw[i-1] +
      2.0*M_PI*(r[i-1]*d[i-1] + r[i]*d[i])*(r[i] - r[i-1]);
  }

  for (int i=0; i<number; i++) 
    p[i] = -mm[i]/(r[i]+1.0e-10) - (pw[number-1] - pw[i]);

  if (VFLAG & 1) {
    ostringstream outf;
    outf << "test_adddisk_sl." << myid;
    ofstream out(outf.str().c_str());
    for (int i=0; i<number; i++) {
      out 
	<< setw(15) << r[i] 
	<< setw(15) << d[i] 
	<< setw(15) << m[i] 
	<< setw(15) << p[i] 
	<< setw(15) << mm[i] 
	<< endl;
    }
    out.close();
  }

  model = new SphericalModelTable(number, &r[0]-1, &d[0]-1, &m[0]-1, &p[0]-1); 

  return model;
}

void EmpCylSL::send_eof_grid()
{
  double *MPIbuf  = new double [MPIbufsz];

				// Send to slaves
				// 
  if (myid==0) {

    for (int m=0; m<=MMAX; m++) {
				// Grids in X--Y
				// 
      for (int v=0; v<rank3; v++) {

	for (int ix=0; ix<=NUMX; ix++)
	  for (int iy=0; iy<=NUMY; iy++)
	    MPIbuf[ix*(NUMY+1) + iy] = potC[m][v][ix][iy];

	MPI_Bcast(MPIbuf, MPIbufsz, MPI_DOUBLE, 0, MPI_COMM_WORLD);
	
	for (int ix=0; ix<=NUMX; ix++)
	  for (int iy=0; iy<=NUMY; iy++)
	    MPIbuf[ix*(NUMY+1) + iy] = rforceC[m][v][ix][iy];

	MPI_Bcast(MPIbuf, MPIbufsz, MPI_DOUBLE, 0, MPI_COMM_WORLD);
	
	for (int ix=0; ix<=NUMX; ix++)
	  for (int iy=0; iy<=NUMY; iy++)
	    MPIbuf[ix*(NUMY+1) + iy] = zforceC[m][v][ix][iy];
	
	MPI_Bcast(MPIbuf, MPIbufsz, MPI_DOUBLE, 0, MPI_COMM_WORLD);
	
	if (DENS) {
	  for (int ix=0; ix<=NUMX; ix++)
	    for (int iy=0; iy<=NUMY; iy++)
	      MPIbuf[ix*(NUMY+1) + iy] = densC[m][v][ix][iy];

	  MPI_Bcast(MPIbuf, MPIbufsz, MPI_DOUBLE, 0, MPI_COMM_WORLD);
	}

      }

    }

    for (int m=1; m<=MMAX; m++) {

				// Grids in X--Y

      for (int v=0; v<rank3; v++) {

	for (int ix=0; ix<=NUMX; ix++)
	  for (int iy=0; iy<=NUMY; iy++)
	    MPIbuf[ix*(NUMY+1) + iy] = potS[m][v][ix][iy];

	MPI_Bcast(MPIbuf, MPIbufsz, MPI_DOUBLE, 0, MPI_COMM_WORLD);
	
	for (int ix=0; ix<=NUMX; ix++)
	  for (int iy=0; iy<=NUMY; iy++)
	    MPIbuf[ix*(NUMY+1) + iy] = rforceS[m][v][ix][iy];

	MPI_Bcast(MPIbuf, MPIbufsz, MPI_DOUBLE, 0, MPI_COMM_WORLD);
	
	for (int ix=0; ix<=NUMX; ix++)
	  for (int iy=0; iy<=NUMY; iy++)
	    MPIbuf[ix*(NUMY+1) + iy] = zforceS[m][v][ix][iy];
	
	MPI_Bcast(MPIbuf, MPIbufsz, MPI_DOUBLE, 0, MPI_COMM_WORLD);
	
	if (DENS) {
	  for (int ix=0; ix<=NUMX; ix++)
	    for (int iy=0; iy<=NUMY; iy++)
	      MPIbuf[ix*(NUMY+1) + iy] = densS[m][v][ix][iy];

	  MPI_Bcast(MPIbuf, MPIbufsz, MPI_DOUBLE, 0, MPI_COMM_WORLD);
	}
	
      }

    }

  } else {

				// Get tables from Master
    for (int m=0; m<=MMAX; m++) {

				// Grids in X--Y

      for (int v=0; v<rank3; v++) {

	MPI_Bcast(MPIbuf, MPIbufsz, MPI_DOUBLE, 0, MPI_COMM_WORLD);

	for (int ix=0; ix<=NUMX; ix++)
	  for (int iy=0; iy<=NUMY; iy++)
	    potC[m][v][ix][iy] = MPIbuf[ix*(NUMY+1) + iy];
  
	MPI_Bcast(MPIbuf, MPIbufsz, MPI_DOUBLE, 0, MPI_COMM_WORLD);

	for (int ix=0; ix<=NUMX; ix++)
	  for (int iy=0; iy<=NUMY; iy++)
	    rforceC[m][v][ix][iy] = MPIbuf[ix*(NUMY+1) + iy];
  
	MPI_Bcast(MPIbuf, MPIbufsz, MPI_DOUBLE, 0, MPI_COMM_WORLD);

	for (int ix=0; ix<=NUMX; ix++)
	  for (int iy=0; iy<=NUMY; iy++)
	    zforceC[m][v][ix][iy] = MPIbuf[ix*(NUMY+1) + iy];
  
	if (DENS) {

	  MPI_Bcast(MPIbuf, MPIbufsz, MPI_DOUBLE, 0, MPI_COMM_WORLD);

	  for (int ix=0; ix<=NUMX; ix++)
	    for (int iy=0; iy<=NUMY; iy++)
	      densC[m][v][ix][iy] = MPIbuf[ix*(NUMY+1) + iy];
	}

      }
    }

    for (int m=1; m<=MMAX; m++) {

				// Grids in X--Y

      for (int v=0; v<rank3; v++) {

	MPI_Bcast(MPIbuf, MPIbufsz, MPI_DOUBLE, 0, MPI_COMM_WORLD);

	for (int ix=0; ix<=NUMX; ix++)
	  for (int iy=0; iy<=NUMY; iy++)
	    potS[m][v][ix][iy] = MPIbuf[ix*(NUMY+1) + iy];
  
	MPI_Bcast(MPIbuf, MPIbufsz, MPI_DOUBLE, 0, MPI_COMM_WORLD);

	for (int ix=0; ix<=NUMX; ix++)
	  for (int iy=0; iy<=NUMY; iy++)
	    rforceS[m][v][ix][iy] = MPIbuf[ix*(NUMY+1) + iy];
  
	MPI_Bcast(MPIbuf, MPIbufsz, MPI_DOUBLE, 0, MPI_COMM_WORLD);

	for (int ix=0; ix<=NUMX; ix++)
	  for (int iy=0; iy<=NUMY; iy++)
	    zforceS[m][v][ix][iy] = MPIbuf[ix*(NUMY+1) + iy];
  
	if (DENS) {

	  MPI_Bcast(MPIbuf, MPIbufsz, MPI_DOUBLE, 0, MPI_COMM_WORLD);

	  for (int ix=0; ix<=NUMX; ix++)
	    for (int iy=0; iy<=NUMY; iy++)
	      densS[m][v][ix][iy] = MPIbuf[ix*(NUMY+1) + iy];
  
	}
      }
    }

  }

  delete [] MPIbuf;
  
}


int EmpCylSL::read_eof_header(const string& eof_file)
{
  ifstream in(eof_file.c_str());
  if (!in) {
    cerr << "EmpCylSL::cache_grid: error opening file named <" 
	 << eof_file << ">" << endl;
    return 0;
  }

  int tmp;

  in.read((char *)&MMAX,   sizeof(int));
  in.read((char *)&NUMX,   sizeof(int));
  in.read((char *)&NUMY,   sizeof(int));
  in.read((char *)&NMAX,   sizeof(int));
  in.read((char *)&NORDER, sizeof(int));
  in.read((char *)&tmp,    sizeof(int)); 
  if (tmp) DENS = true; else DENS = false;
  in.read((char *)&tmp,    sizeof(int)); 
  if (tmp) CMAP = true; else CMAP = false;
  in.read((char *)&RMIN,   sizeof(double));
  in.read((char *)&RMAX,   sizeof(double));
  in.read((char *)&ASCALE, sizeof(double));
  in.read((char *)&HSCALE, sizeof(double));
  
  if (myid==0) {
    cout << setfill('-') << setw(70) << '-' << endl;
    cout << " Cylindrical parameters read from <" << eof_file << ">" << endl;
    cout << setw(70) << '-' << endl;
    cout << "MMAX="   << MMAX << endl;
    cout << "NUMX="   << NUMX << endl;
    cout << "NUMY="   << NUMY << endl;
    cout << "NMAX="   << NMAX << endl;
    cout << "NORDER=" << NORDER << endl;
    cout << "DENS="   << DENS << endl;
    cout << "CMAP="   << CMAP << endl;
    cout << "RMIN="   << RMIN << endl;
    cout << "RMAX="   << RMAX << endl;
    cout << "ASCALE=" << ASCALE << endl;
    cout << "HSCALE=" << HSCALE << endl;
    cout << setw(70) << '-' << endl << setfill(' ');
  }

  return 1;
}

int EmpCylSL::read_eof_file(const string& eof_file)
{
  read_eof_header(eof_file);

  pfac = 1.0/sqrt(ASCALE);
  ffac = pfac/ASCALE;
  dfac = ffac/ASCALE;

  SLGridSph::mpi = 1;		// Turn on MPI
  delete ortho;
  ortho = new SLGridSph(LMAX, NMAX, NUMR, RMIN, RMAX*0.99, make_sl(), 1, 1.0);

  setup_eof();
  setup_accumulation();

				// Master tries to read table
  int retcode;
  if (myid==0) retcode = cache_grid(0, eof_file);
  MPI_Bcast(&retcode, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (!retcode) return 0;
				// Send table to slave processes
  send_eof_grid();

  if (myid==0) 
    cerr << "EmpCylSL::read_cache: table forwarded to all processes" << endl;


  eof_made = true;
  coefs_made = vector<short>(multistep+1, false);

  return 1;
}

int EmpCylSL::read_cache(void)
{
  setup_eof();
  setup_accumulation();

				// Master tries to read table
  int retcode;
  if (myid==0) retcode = cache_grid(0);
  MPI_Bcast(&retcode, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (!retcode) return 0;
				// Send table to slave processes
  send_eof_grid();

  if (myid==0) 
    cerr << "EmpCylSL::read_cache: table forwarded to all processes" << endl;


  eof_made = true;
  coefs_made = vector<short>(multistep+1, false);

  return 1;
}


int EmpCylSL::cache_grid(int readwrite, string cachefile)
{

  if (cachefile.size()==0) cachefile = CACHEFILE;

  if (readwrite) {

    ofstream out(cachefile.c_str());
    if (!out) {
      cerr << "EmpCylSL::cache_grid: error writing file" << endl;
      return 0;
    }

    const int one  = 1;
    const int zero = 0;

    out.write((const char *)&MMAX, sizeof(int));
    out.write((const char *)&NUMX, sizeof(int));
    out.write((const char *)&NUMY, sizeof(int));
    out.write((const char *)&NMAX, sizeof(int));
    out.write((const char *)&NORDER, sizeof(int));
    if (DENS) out.write((const char *)&one, sizeof(int));
    else      out.write((const char *)&zero, sizeof(int));
    if (CMAP) out.write((const char *)&one, sizeof(int));
    else      out.write((const char *)&zero, sizeof(int));
    out.write((const char *)&RMIN, sizeof(double));
    out.write((const char *)&RMAX, sizeof(double));
    out.write((const char *)&ASCALE, sizeof(double));
    out.write((const char *)&HSCALE, sizeof(double));
    out.write((const char *)&cylmass, sizeof(double));
    out.write((const char *)&tnow, sizeof(double));

				// Write table

    for (int m=0; m<=MMAX; m++) {

      for (int v=0; v<rank3; v++) {

	for (int ix=0; ix<=NUMX; ix++)
	  for (int iy=0; iy<=NUMY; iy++)
	    out.write((const char *)&potC[m][v][ix][iy], sizeof(double));
	
	for (int ix=0; ix<=NUMX; ix++)
	  for (int iy=0; iy<=NUMY; iy++)
	    out.write((const char *)&rforceC[m][v][ix][iy], sizeof(double));
	  
	for (int ix=0; ix<=NUMX; ix++)
	  for (int iy=0; iy<=NUMY; iy++)
	    out.write((const char *)&zforceC[m][v][ix][iy], sizeof(double));
	  
	if (DENS) {
	  for (int ix=0; ix<=NUMX; ix++)
	    for (int iy=0; iy<=NUMY; iy++)
	      out.write((const char *)&densC[m][v][ix][iy], sizeof(double));

	}
	
      }

    }

    for (int m=1; m<=MMAX; m++) {

      for (int v=0; v<rank3; v++) {

	for (int ix=0; ix<=NUMX; ix++)
	  for (int iy=0; iy<=NUMY; iy++)
	    out.write((const char *)&potS[m][v][ix][iy], sizeof(double));
	
	for (int ix=0; ix<=NUMX; ix++)
	  for (int iy=0; iy<=NUMY; iy++)
	    out.write((const char *)&rforceS[m][v][ix][iy], sizeof(double));
	  
	for (int ix=0; ix<=NUMX; ix++)
	  for (int iy=0; iy<=NUMY; iy++)
	    out.write((const char *)&zforceS[m][v][ix][iy], sizeof(double));
	
	if (DENS) {
	  for (int ix=0; ix<=NUMX; ix++)
	    for (int iy=0; iy<=NUMY; iy++)
	      out.write((const char *)&densS[m][v][ix][iy], sizeof(double));
	}
	
      }

    }

  }
  else {

    ifstream in(cachefile.c_str());
    if (!in) {
      cerr << "EmpCylSL::cache_grid: error opening file" << endl;
      return 0;
    }

    int mmax, numx, numy, nmax, norder, tmp;
    bool cmap=false, dens=false;
    double rmin, rmax, ascl, hscl;

    in.read((char *)&mmax,   sizeof(int));
    in.read((char *)&numx,   sizeof(int));
    in.read((char *)&numy,   sizeof(int));
    in.read((char *)&nmax,   sizeof(int));
    in.read((char *)&norder, sizeof(int));
    in.read((char *)&tmp,    sizeof(int));    if (tmp) dens = true;
    in.read((char *)&tmp,    sizeof(int));    if (tmp) cmap = true;
    in.read((char *)&rmin,   sizeof(double));
    in.read((char *)&rmax,   sizeof(double));
    in.read((char *)&ascl,   sizeof(double));
    in.read((char *)&hscl,   sizeof(double));

				// Spot check compatibility
    if ( (MMAX    != mmax   ) |
	 (NUMX    != numx   ) |
	 (NUMY    != numy   ) |
	 (NMAX    != nmax   ) |
	 (NORDER  != norder ) |
	 (DENS    != dens   ) |
	 (CMAP    != cmap   ) |
	 (fabs(rmin-RMIN)>1.0e-12 ) |
	 (fabs(rmax-RMAX)>1.0e-12 ) |
	 (fabs(ascl-ASCALE)>1.0e-12 ) |
	 (fabs(hscl-HSCALE)>1.0e-12 )
	 ) 
      {
	cout << "MMAX="   << MMAX   << " mmax=" << mmax << endl;
	cout << "NUMX="   << NUMX   << " numx=" << numx << endl;
	cout << "NUMY="   << NUMY   << " numy=" << numy << endl;
	cout << "NMAX="   << NMAX   << " nmax=" << nmax << endl;
	cout << "NORDER=" << NORDER << " norder=" << norder << endl;
	cout << "DENS="   << DENS   << " dens=" << dens << endl;
	cout << "CMAP="   << CMAP   << " cmap=" << cmap << endl;
	cout << "RMIN="   << RMIN   << " rmin=" << rmin << endl;
	cout << "RMAX="   << RMAX   << " rmax=" << rmax << endl;
	cout << "ASCALE=" << ASCALE << " ascale=" << ascl << endl;
	cout << "HSCALE=" << HSCALE << " hscale=" << hscl << endl;
	return 0;
      }
    
    double time;
    in.read((char *)&cylmass, sizeof(double));
    in.read((char *)&time,    sizeof(double));

				// Read table

    for (int m=0; m<=MMAX; m++) {

      for (int v=0; v<rank3; v++) {

	for (int ix=0; ix<=NUMX; ix++)
	  for (int iy=0; iy<=NUMY; iy++)
	    in.read((char *)&potC[m][v][ix][iy], sizeof(double));
	
	for (int ix=0; ix<=NUMX; ix++)
	  for (int iy=0; iy<=NUMY; iy++)
	    in.read((char *)&rforceC[m][v][ix][iy], sizeof(double));
	  
	for (int ix=0; ix<=NUMX; ix++)
	  for (int iy=0; iy<=NUMY; iy++)
	    in.read((char *)&zforceC[m][v][ix][iy], sizeof(double));
	  
	if (DENS) {
	  for (int ix=0; ix<=NUMX; ix++)
	    for (int iy=0; iy<=NUMY; iy++)
	      in.read((char *)&densC[m][v][ix][iy], sizeof(double));

	}
	
      }

    }

    for (int m=1; m<=MMAX; m++) {

      for (int v=0; v<rank3; v++) {

	for (int ix=0; ix<=NUMX; ix++)
	  for (int iy=0; iy<=NUMY; iy++)
	    in.read((char *)&potS[m][v][ix][iy], sizeof(double));
	
	for (int ix=0; ix<=NUMX; ix++)
	  for (int iy=0; iy<=NUMY; iy++)
	    in.read((char *)&rforceS[m][v][ix][iy], sizeof(double));
	  
	for (int ix=0; ix<=NUMX; ix++)
	  for (int iy=0; iy<=NUMY; iy++)
	    in.read((char *)&zforceS[m][v][ix][iy], sizeof(double));
	
	if (DENS) {
	  for (int ix=0; ix<=NUMX; ix++)
	    for (int iy=0; iy<=NUMY; iy++)
	      in.read((char *)&densS[m][v][ix][iy], sizeof(double));
	}
	
      }

    }
    
    Rtable = M_SQRT1_2 * RMAX;
    XMIN = r_to_xi(RMIN*ASCALE);
    XMAX = r_to_xi(Rtable*ASCALE);
    dX = (XMAX - XMIN)/NUMX;
    
    YMIN = z_to_y(-Rtable*ASCALE);
    YMAX = z_to_y( Rtable*ASCALE);
    dY = (YMAX - YMIN)/NUMY;
    
    cerr << "EmpCylSL::cache_grid: file read successfully" << endl;
  }

  return 1;
}

void EmpCylSL::receive_eof(int request_id, int MM)
{
  int type, icnt, off;
  int mm;

  MPI_Recv(&type, 1, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, 
	   MPI_COMM_WORLD, &status);

  int current_source = status.MPI_SOURCE;

  if (VFLAG & 8)
    cerr << "Master beginning to receive from " << current_source 
	 << " . . . " << endl;

  MPI_Recv(&mm, 1, MPI_INT, current_source, MPI_ANY_TAG, 
	   MPI_COMM_WORLD, &status);

  if (VFLAG & 8)
    cerr << "Master receiving from " << current_source << ": type=" << type 
	 << "   M=" << mm << endl;

				// Receive rest of data

  for (int n=0; n<NORDER; n++) {
    MPI_Recv(&mpi_double_buf2[MPIbufsz*(MPItable*n+0)], 
	     MPIbufsz, MPI_DOUBLE, current_source, 13 + MPItable*n+1, 
	     MPI_COMM_WORLD, &status);

    MPI_Recv(&mpi_double_buf2[MPIbufsz*(MPItable*n+1)], 
	     MPIbufsz, MPI_DOUBLE, current_source, 13 + MPItable*n+2, 
	     MPI_COMM_WORLD, &status);

    MPI_Recv(&mpi_double_buf2[MPIbufsz*(MPItable*n+2)], 
	     MPIbufsz, MPI_DOUBLE, current_source, 13 + MPItable*n+3, 
	     MPI_COMM_WORLD, &status);
    if (DENS)
      MPI_Recv(&mpi_double_buf2[MPIbufsz*(MPItable*n+3)], 
	       MPIbufsz, MPI_DOUBLE, current_source, 13 + MPItable*n+4, 
	       MPI_COMM_WORLD, &status);
  }
  

				// Send slave new orders
  if (request_id >=0) {
    MPI_Send(&request_id, 1, MPI_INT, current_source, 1, MPI_COMM_WORLD);
    MPI_Send(&MM, 1, MPI_INT, current_source, 2, MPI_COMM_WORLD);
  }
  else {
    MPI_Send(&request_id, 1, MPI_INT, current_source, 1, MPI_COMM_WORLD);
  }
  
				// Read from buffers

  for (int n=0; n<NORDER; n++) {
  
    off = MPIbufsz*(MPItable*n+0);
    icnt = 0;
    for (int ix=0; ix<=NUMX; ix++)
      for (int iy=0; iy<=NUMY; iy++)
      if (type)
	potC[mm][n][ix][iy]  = mpi_double_buf2[off+icnt++];
      else
	potS[mm][n][ix][iy]  = mpi_double_buf2[off+icnt++];
	


    off = MPIbufsz*(MPItable*n+1);
    icnt = 0;
    for (int ix=0; ix<=NUMX; ix++)
      for (int iy=0; iy<=NUMY; iy++)
      if (type)
	rforceC[mm][n][ix][iy]  = mpi_double_buf2[off+icnt++];
      else
	rforceS[mm][n][ix][iy]  = mpi_double_buf2[off+icnt++];
	

    off = MPIbufsz*(MPItable*n+2);
    icnt = 0;
    for (int ix=0; ix<=NUMX; ix++)
      for (int iy=0; iy<=NUMY; iy++)
      if (type)
	zforceC[mm][n][ix][iy]  = mpi_double_buf2[off+icnt++];
      else
	zforceS[mm][n][ix][iy]  = mpi_double_buf2[off+icnt++];
    

    if (DENS) {
      off = MPIbufsz*(MPItable*n+3);
      icnt = 0;
      for (int ix=0; ix<=NUMX; ix++)
	for (int iy=0; iy<=NUMY; iy++)
	  if (type)
	    densC[mm][n][ix][iy]  = mpi_double_buf2[off+icnt++];
	  else
	    densS[mm][n][ix][iy]  = mpi_double_buf2[off+icnt++];
    }
  }
  
  if (VFLAG & 8)
    cerr << "Master finished receiving: type=" << type << "   M=" 
	 << mm << endl;

  return;

}

void EmpCylSL::compute_eof_grid(int request_id, int m)
{
  //  Read in coefficient matrix or
  //  make grid if needed

				// Sin/cos normalization
  double x, y, r, z;
  double costh, fac1, fac2, dens, potl, potr, pott, fac3, fac4;
  
  int icnt, off;
  
  for (int v=0; v<NORDER; v++) {
    tpot[v].zero();
    trforce[v].zero();
    tzforce[v].zero();
    if (DENS) tdens[v].zero();
  }

  for (int ix=0; ix<=NUMX; ix++) {

    x = XMIN + dX*ix;
    r = xi_to_r(x);

    for (int iy=0; iy<=NUMY; iy++) {

      y = YMIN + dY*iy;
      z = y_to_z(y);

      double rr = sqrt(r*r + z*z) + 1.0e-18;

      ortho->get_pot(potd, rr/ASCALE);
      ortho->get_force(dpot, rr/ASCALE);
      if (DENS) ortho->get_dens(dend, rr/ASCALE);

      costh = z/rr;
      dlegendre_R(LMAX, costh, legs[0], dlegs[0]);
      
      for (int v=0; v<NORDER; v++) {

	for (int ir=1; ir<=NMAX; ir++) {

	  for (int l=m; l<=LMAX; l++) {

	    fac1 = sqrt((2.0*l+1.0)/(4.0*M_PI));

	    if (m==0) {
	      fac2 = fac1*legs[0][l][m];

	      dens = fac2*dend[l][ir] * dfac;
	      potl = fac2*potd[l][ir] * pfac;
	      potr = fac2*dpot[l][ir] * ffac;
	      pott = fac1*dlegs[0][l][m]*potd[l][ir] * pfac;

	    } else {

	      fac2 = M_SQRT2 * fac1 * exp(0.5*(lgamma(l-m+1) - lgamma(l+m+1)));
	      fac3 = fac2 * legs[0][l][m];
	      fac4 = fac2 * dlegs[0][l][m];
	      
	      dens = fac3*dend[l][ir] * dfac;
	      potl = fac3*potd[l][ir] * pfac;
	      potr = fac3*dpot[l][ir] * ffac;
	      pott = fac4*potd[l][ir] * pfac;
	    }
	    
	    int nn = ir + NMAX*(l-m);

	    tpot[v][ix][iy] +=  ef[v+1][nn] * potl;

	    trforce[v][ix][iy] += 
	      -ef[v+1][nn] * (potr*r/rr - pott*z*r/(rr*rr*rr));

	    tzforce[v][ix][iy] += 
	      -ef[v+1][nn] * (potr*z/rr + pott*r*r/(rr*rr*rr));

	    if (DENS) 
	      tdens[v][ix][iy] +=  ef[v+1][nn] * dens;
	  }
	}
      }
    }
  }

				// Send stuff back to master
      
  MPI_Send(&request_id, 1, MPI_INT, 0, 12, MPI_COMM_WORLD);
  MPI_Send(&m, 1, MPI_INT, 0, 12, MPI_COMM_WORLD);
      
    
  for (int n=0; n<NORDER; n++) {

				// normalization factors
    if (DENS)
      tdens[n] *= 0.25/M_PI;

				// Potential

    off = MPIbufsz*(MPItable*n+0);
    icnt = 0;
    for (int ix=0; ix<=NUMX; ix++)
      for (int iy=0; iy<=NUMY; iy++)
	mpi_double_buf2[off + icnt++] = tpot[n][ix][iy];
    
    if (VFLAG & 8)
      cerr << "Slave " << setw(4) << myid << ": with request_id=" << request_id
	   << ", M=" << m << " send Potential" << endl;

    MPI_Send(&mpi_double_buf2[off], MPIbufsz, MPI_DOUBLE, 0, 
	     13 + MPItable*n+1, MPI_COMM_WORLD);

				// R force

    off = MPIbufsz*(MPItable*n+1);
    icnt = 0;
    for (int ix=0; ix<=NUMX; ix++)
      for (int iy=0; iy<=NUMY; iy++)
	mpi_double_buf2[off + icnt++] = trforce[n][ix][iy];
    
    if (VFLAG & 8)
      cerr << "Slave " << setw(4) << myid << ": with request_id=" << request_id
	   << ", M=" << m << " sending R force" << endl;

    MPI_Send(&mpi_double_buf2[off], MPIbufsz, MPI_DOUBLE, 0, 
	     13 + MPItable*n+2, MPI_COMM_WORLD);

				// Z force

    off = MPIbufsz*(MPItable*n+2);
    icnt = 0;
    for (int ix=0; ix<=NUMX; ix++)
      for (int iy=0; iy<=NUMY; iy++)
	mpi_double_buf2[off + icnt++] = tzforce[n][ix][iy];
    
    if (VFLAG & 8)
      cerr << "Slave " << setw(4) << myid << ": with request_id=" << request_id
	   << ", M=" << m << " sending Z force" << endl;


    MPI_Send(&mpi_double_buf2[off], MPIbufsz, MPI_DOUBLE, 0, 
	     13 + MPItable*n+3, MPI_COMM_WORLD);

				// Density

    if (DENS) {
      off = MPIbufsz*(MPItable*n+3);
      icnt = 0;
      for (int ix=0; ix<=NUMX; ix++)
	for (int iy=0; iy<=NUMY; iy++)
	  mpi_double_buf2[off + icnt++] = tdens[n][ix][iy];
    
      if (VFLAG & 8)
	cerr << "Slave " << setw(4) << myid 
	     << ": with request_id=" << request_id
	     << ", M=" << m << " sending Density" << endl;

      MPI_Send(&mpi_double_buf2[off], MPIbufsz, MPI_DOUBLE, 0, 
	       13 + MPItable*n+4, MPI_COMM_WORLD);

    }

  }

}


void EmpCylSL::setup_accumulation(void)
{
  if (!accum_cos) {

    accum_cos = new Vector [MMAX+1];
    accum_sin = new Vector [MMAX+1];

    if (SELECT) {
      accum_cos2 = new Vector* [nthrds];
      accum_sin2 = new Vector* [nthrds];

      for (int nth=0; nth<nthrds;nth++) {
	accum_cos2[nth] = new Vector [MMAX+1];
	accum_sin2[nth] = new Vector [MMAX+1];
      }
    }    

  }

  if (accum_cosL.size() == 0) {

    for (unsigned M=0; M<=multistep; M++) {
      accum_cosL.push_back(new Vector* [nthrds]);
      accum_cosN.push_back(new Vector* [nthrds]);
      accum_sinL.push_back(new Vector* [nthrds]);
      accum_sinN.push_back(new Vector* [nthrds]);
      
      for (int nth=0; nth<nthrds; nth++) {
	accum_cosL[M][nth] = new Vector [MMAX+1];
	accum_cosN[M][nth] = new Vector [MMAX+1];
	accum_sinL[M][nth] = new Vector [MMAX+1];
	accum_sinN[M][nth] = new Vector [MMAX+1];
      }

      howmany1.push_back(vector<unsigned>(nthrds, 0));
      howmany.push_back(0);
    }
    if (VFLAG & 8)
      cerr << "Slave " << setw(4) << myid 
	   << ": tables allocated, MMAX=" << MMAX << endl;
  }

  differC1 = vector< vector<Matrix> >(nthrds);
  differS1 = vector< vector<Matrix> >(nthrds);
  for (int nth=0; nth<nthrds; nth++) {
    differC1[nth] = vector<Matrix>(multistep+1);
    differS1[nth] = vector<Matrix>(multistep+1); 
  }

  unsigned sz = (multistep+1)*(MMAX+1)*NORDER;
  workC1 = vector<double>(sz);
  workC  = vector<double>(sz);
  workS1 = vector<double>(sz);
  workS  = vector<double>(sz);

  dstepL  = vector<unsigned>(multistep+1, 0); 
  dstepN  = vector<unsigned>(multistep+1, 0); 

  cylused = cylused1 = 0;
  cylmass = 0.0;
  cylmass_made = false;

  for (unsigned M=0; M<=multistep; M++) {

    for (int nth=0; nth<nthrds; nth++) {

      for (int m=0; m<=MMAX; m++) {
	
	accum_cosN[M][nth][m].setsize(0, NORDER-1);
	accum_cosN[M][nth][m].zero();

	if (accum_cosL[M][nth][m].getlow()  !=0 ||
	    accum_cosL[M][nth][m].gethigh() != NORDER-1)
	  {
	    accum_cosL[M][nth][m].setsize(0, NORDER-1);
	    accum_cosL[M][nth][m].zero();
	  }

	if (m>0) {
	  accum_sinN[M][nth][m].setsize(0, NORDER-1);
	  accum_sinN[M][nth][m].zero();

	  if (accum_sinL[M][nth][m].getlow()  !=0 ||
	      accum_sinL[M][nth][m].gethigh() != NORDER-1)
	    {
	      accum_sinL[M][nth][m].setsize(0, NORDER-1);
	      accum_sinL[M][nth][m].zero();
	    }
	}
      }
    }
  }

  for (int m=0; m<=MMAX; m++) {
    accum_cos[m].setsize(0, NORDER-1);
    accum_cos[m].zero();
    if (m>0) {
      accum_sin[m].setsize(0, NORDER-1);
      accum_sin[m].zero();
    }
  }
  
  if (SELECT) {
    for (int nth=0; nth<nthrds; nth++) {
      for (int m=0; m<=MMAX; m++) {
	accum_cos2[nth][m].setsize(0, NORDER-1);
	accum_cos2[nth][m].zero();
	if (m>0) {
	  accum_sin2[nth][m].setsize(0, NORDER-1);
	  accum_sin2[nth][m].zero();
	}
      }
    }
  }
  
  coefs_made = vector<short>(multistep+1, false);
}

void EmpCylSL::setup_accumulation(int M)
{
#ifndef STANDALONE
  howmany[M] = 0;

  //
  // Swap buffers
  //
  Vector **p;

  p = accum_cosL[M];
  accum_cosL[M] = accum_cosN[M];
  accum_cosN[M] = p;

  p = accum_sinL[M];
  accum_sinL[M] = accum_sinN[M];
  accum_sinN[M] = p;

  dstepL[M]  = dstepN[M];
  dstepN[M] += mintvl[M];

  //
  // Clean current coefficient files
  //
  for (int nth=0; nth<nthrds; nth++) {

    howmany1[M][nth] = 0;

    for (int m=0; m<=MMAX; m++) {
      
      accum_cosN[M][nth][m].setsize(0, NORDER-1);
      accum_cosN[M][nth][m].zero();

      if (m>0) {
	accum_sinN[M][nth][m].setsize(0, NORDER-1);
	accum_sinN[M][nth][m].zero();
	
      }
    }
  }
  
  coefs_made[M] = false;
#endif
}

void EmpCylSL::setup_eof()
{
  if (!SC) {

    rank2   = NMAX*(LMAX+1);
    rank3   = NORDER;
    
    Rtable  = M_SQRT1_2 * RMAX;
    XMIN    = r_to_xi(RMIN*ASCALE);
    XMAX    = r_to_xi(Rtable*ASCALE);
    dX      = (XMAX - XMIN)/NUMX;

    YMIN    = z_to_y(-Rtable*ASCALE);
    YMAX    = z_to_y( Rtable*ASCALE);
    dY      = (YMAX - YMIN)/NUMY;

    potC    = new Matrix* [MMAX+1];
    rforceC = new Matrix* [MMAX+1];
    zforceC = new Matrix* [MMAX+1];
    if (DENS) densC = new Matrix* [MMAX+1];

    potS    = new Matrix* [MMAX+1];
    rforceS = new Matrix* [MMAX+1];
    zforceS = new Matrix* [MMAX+1];
    if (DENS) densS = new Matrix* [MMAX+1];

    for (int m=0; m<=MMAX; m++) {

      potC[m]    = new Matrix [rank3];
      rforceC[m] = new Matrix [rank3];
      zforceC[m] = new Matrix [rank3];
      if (DENS) densC[m] = new Matrix [rank3];

      for (int v=0; v<rank3; v++) {
	potC   [m][v].setsize(0, NUMX, 0, NUMY);
	rforceC[m][v].setsize(0, NUMX, 0, NUMY);
	zforceC[m][v].setsize(0, NUMX, 0, NUMY);
	if (DENS) densC[m][v].setsize(0, NUMX, 0, NUMY);
      }

    }


    for (int m=1; m<=MMAX; m++) {

      potS[m]    = new Matrix [rank3];
      rforceS[m] = new Matrix [rank3];
      zforceS[m] = new Matrix [rank3];
      if (DENS) densS[m] = new Matrix [rank3];

      for (int v=0; v<rank3; v++) {
	potS   [m][v].setsize(0, NUMX, 0, NUMY);
	rforceS[m][v].setsize(0, NUMX, 0, NUMY);
	zforceS[m][v].setsize(0, NUMX, 0, NUMY);
	if (DENS) densS[m][v].setsize(0, NUMX, 0, NUMY);
      }

    }

    tpot = new Matrix [NORDER];
    trforce = new Matrix [NORDER];
    tzforce = new Matrix [NORDER];
    if (DENS) tdens = new Matrix [NORDER];

    for (int n=0; n<NORDER; n++) {
      tpot[n].setsize(0, NUMX, 0, NUMY);
      trforce[n].setsize(0, NUMX, 0, NUMY);
      tzforce[n].setsize(0, NUMX, 0, NUMY);
      if (DENS) tdens[n].setsize(0, NUMX, 0, NUMY);
    }

    SC = new double*** [nthrds];
    SS = new double*** [nthrds];

    for (int nth=0; nth<nthrds; nth++) {
      SC[nth] = new double** [MMAX+1];
      SS[nth] = new double** [MMAX+1];
    }

    vc = new Matrix [nthrds];
    vs = new Matrix [nthrds];
    hold = new Vector[nthrds];
    for (int i=0; i<nthrds; i++) {
      vc[i].setsize(0, max<int>(1,MMAX), 0, rank3-1);
      vs[i].setsize(0, max<int>(1,MMAX), 0, rank3-1);
      hold[i].setsize(0, rank3-1);
    }

    var = new Matrix[MMAX+1];
    for (int m=0; m<=MMAX; m++)
      var[m].setsize(1, NMAX*(LMAX-m+1), 1, NMAX*(LMAX-m+1));
      
    potd.setsize(0, LMAX, 1, NMAX);
    dpot.setsize(0, LMAX, 1, NMAX);
    dend.setsize(0, LMAX, 1, NMAX);

    cosm = new Vector [nthrds];
    sinm = new Vector [nthrds];
    legs = new Matrix [nthrds];
    dlegs = new Matrix [nthrds];
    for (int i=0; i<nthrds; i++) {
      cosm[i].setsize(0, LMAX);
      sinm[i].setsize(0, LMAX);
      legs[i].setsize(0, LMAX, 0, LMAX);
      dlegs[i].setsize(0, LMAX, 0, LMAX);
    }


    for (int nth=0; nth<nthrds; nth++) {

      for (int m=0; m<=MMAX; m++) {

	SC[nth][m] = new double* [NMAX*(LMAX-m+1)] - 1;
	if (m) SS[nth][m] = new double* [NMAX*(LMAX-m+1)] - 1;

	for (int i=1; i<=NMAX*(LMAX-m+1); i++) {

	  SC[nth][m][i] = new double [NMAX*(LMAX-m+1)] - 1;
	  if (m) SS[nth][m][i] = new double [NMAX*(LMAX-m+1)] - 1;

	}

      }
    
    }

    table = new Matrix [nthrds];
    facC = new Matrix [nthrds];
    facS = new Matrix [nthrds];
    for (int i=0; i<nthrds; i++) {
      table[i].setsize(0, LMAX, 1, NMAX);
      facC[i].setsize(1, NMAX, 0, LMAX);
      facS[i].setsize(1, NMAX, 0, LMAX);
    }

    MPIbufsz = (NUMX+1)*(NUMY+1);

    mpi_double_buf2 = new double [MPIbufsz*NORDER*MPItable];
    mpi_double_buf3 = new double [rank3];
  }

  for (int nth=0; nth<nthrds; nth++) {
    for (int m=0; m<=MMAX; m++)  {
      for (int i=1; i<=NMAX*(LMAX-m+1); i++)  {
	for (int j=1; j<=NMAX*(LMAX-m+1); j++)  {
	  SC[nth][m][i][j] = 0.0;
	  if (m>0) SS[nth][m][i][j] = 0.0;
	}
      }
    }
  }

  eof_made = false;
}


void EmpCylSL::generate_eof(int numr, int nump, int numt, 
			    double (*func)
			    (double R, double z, double phi, int M) )
{
  Timer timer;
  if (VFLAG & 16) timer.start();

  setup_eof();

  LegeQuad lr(numr);
  LegeQuad lt(numt);
  double dphi = 2.0*M_PI/nump;

  double xi, rr, costh, phi, R, z, dens, ylm, jfac;

  int id = 0;			// Not multithreaded
  int nn1, nn2;

  if (VFLAG & 16 && myid==0)
    cout << left
	 << setw(4) << " r"
	 << setw(4) << " t"
	 << setw(4) << " p" << "  Elapsed" << endl
	 << setw(4) << "---"
	 << setw(4) << "---"
	 << setw(4) << "---" << " ---------" << endl;

  int cntr = 0;

  // *** Radial quadrature loop
  for (int qr=1; qr<=numr; qr++) { 
    
    xi = XMIN + (XMAX - XMIN) * lr.knot(qr);
    rr  = xi_to_r(xi);
    ortho->get_pot(table[id], rr/ASCALE);

    // *** cos(theta) quadrature loop
    for (int qt=1; qt<=numt; qt++) {

      if (cntr++ % numprocs != myid) continue;

      costh = -1.0 + 2.0*lt.knot(qt);
      R = rr * sqrt(1.0 - costh*costh);
      z = rr * costh;

      legendre_R(LMAX, costh, legs[id]);

      jfac = dphi*2.0*lt.weight(qt)*(XMAX - XMIN)*lr.weight(qr) 
	* rr*rr / d_xi_to_r(xi);
      
      // *** Phi quadrature loop
      for (int qp=0; qp<nump; qp++) {

	phi = dphi*qp;
	sinecosine_R(LMAX, phi, cosm[id], sinm[id]);

	// *** m loop
	for (int m=0; m<=MMAX; m++) {

	  dens = (*func)(R, z, phi, m) * jfac;

	  // *** ir loop
	  for (int ir=1; ir<=NMAX; ir++) {

	    // *** l loop
	    for (int l=m; l<=LMAX; l++) {

	      ylm = sqrt((2.0*l+1.0)/(4.0*M_PI)) * pfac *
		exp(0.5*(lgamma(l-m+1) - lgamma(l+m+1))) * legs[0][l][m];

	      if (m==0) {

		facC[id][ir][l-m] = ylm*table[id][l][ir];

	      }
	      else {
	  
		facC[id][ir][l-m] = ylm*table[id][l][ir]*cosm[id][m];
		facS[id][ir][l-m] = ylm*table[id][l][ir]*sinm[id][m];

	      }

	    } // *** l loop

	  } // *** ir loop

	  for (int ir1=1; ir1<=NMAX; ir1++) {

	    for (int l1=m; l1<=LMAX; l1++) {

	      nn1 = ir1 + NMAX*(l1-m);

	      if (m==0) {
		
		for (int ir2=1; ir2<=NMAX; ir2++) {
		  for (int l2=m; l2<=LMAX; l2++) {
		    nn2 = ir2 + NMAX*(l2-m);

		    SC[id][m][nn1][nn2] += 
		      facC[id][ir1][l1-m]*facC[id][ir2][l2-m] * dens;
		  }
		}
		
	      } else {
		
		for (int ir2=1; ir2<=NMAX; ir2++) {

		  for (int l2=m; l2<=LMAX; l2++) {
		    
		    nn2 = ir2 + NMAX*(l2-m);

		    SC[id][m][nn1][nn2] += 
		      facC[id][ir1][l1-m]*facC[id][ir2][l2-m] * dens;
		    
		    SS[id][m][nn1][nn2] += 
		      facS[id][ir1][l1-m]*facS[id][ir2][l2-m] * dens;
		  }
		}
	      }
	      
	    } // *** l loop
	    
	  } // *** ir loop
	  
	} // *** m loop

      if (VFLAG & 16 && myid==0)
	cout << left << '\r'
	     << setw(4) << qr
	     << setw(4) << qt
	     << setw(4) << qp << " Secs="
	     << timer.getTime().getTotalTime() << flush;

      } // *** phi quadrature loop

    } // *** cos(theta) quadrature loop

  } // *** r quadrature loop
  
  if (VFLAG & 16) {
    long t = timer.stop().getTotalTime();
    if (myid==0) cout << endl;
    MPI_Barrier(MPI_COMM_WORLD);
    cout << "Process " << setw(4) << myid << ": completed quadrature in " 
	 << t << " seconds" << endl;
    timer.reset();
    timer.start();
  }

  //
  // Now, we are ready to make the EOF basis
  //

  make_eof();

  if (VFLAG & 16) {
    cout << "Process " << setw(4) << myid << ": completed basis in " 
	 << timer.stop().getTotalTime() << " seconds"
	 << endl;
  }

  //
  // We still need to make the coefficients
  //

  coefs_made = vector<short>(multistep+1, false);

}


void EmpCylSL::accumulate_eof(double r, double z, double phi, double mass, 
			      int id, int mlevel)
{
  if (eof_made) {
    if (VFLAG & 2)
      cerr << "accumulate_eof: Process " << setw(4) << myid << ", Thread " 
	   << id << " calling setup_eof()" << endl;
    setup_eof();
  }

  double rr = sqrt(r*r + z*z);

  if (rr/ASCALE>Rtable) return;

  double fac0 = 4.0*M_PI, ylm;

  ortho->get_pot(table[id], rr/ASCALE);
  double costh = z/(rr+1.0e-18);
  legendre_R(LMAX, costh, legs[id]);
  sinecosine_R(LMAX, phi, cosm[id], sinm[id]);

  int nn1, nn2;

  // *** m loop
  for (int m=0; m<=MMAX; m++) {

    // *** ir loop
    for (int ir=1; ir<=NMAX; ir++) {

      // *** l loop
      for (int l=m; l<=LMAX; l++) {

	ylm = sqrt((2.0*l+1.0)/(4.0*M_PI)) * pfac *
	  exp(0.5*(lgamma(l-m+1) - lgamma(l+m+1))) * legs[0][l][m];

	if (m==0) {

	  facC[id][ir][l-m] = ylm*table[id][l][ir];

	}
	else {

	  facC[id][ir][l-m] = ylm*table[id][l][ir]*cosm[id][m];
	  facS[id][ir][l-m] = ylm*table[id][l][ir]*sinm[id][m];

	}

      } // *** l loop

    } // *** ir loop

    for (int ir1=1; ir1<=NMAX; ir1++) {
      for (int l1=m; l1<=LMAX; l1++) {
	nn1 = ir1 + NMAX*(l1-m);

	if (m==0) {
	  
	  for (int ir2=1; ir2<=NMAX; ir2++) {
	    for (int l2=m; l2<=LMAX; l2++) {
	      nn2 = ir2 + NMAX*(l2-m);

	      SC[id][m][nn1][nn2] += 
		facC[id][ir1][l1-m]*facC[id][ir2][l2-m] * mass;
	    }
	  }

	} else {

	  for (int ir2=1; ir2<=NMAX; ir2++) {
	    for (int l2=m; l2<=LMAX; l2++) {
	      nn2 = ir2 + NMAX*(l2-m);
	      
	      SC[id][m][nn1][nn2] += 
		facC[id][ir1][l1-m]*facC[id][ir2][l2-m] * mass;

	      SS[id][m][nn1][nn2] += 
		facS[id][ir1][l1-m]*facS[id][ir2][l2-m] * mass;
	    }
	  }
	}

      } // *** l loop

    } // *** ir loop

  } // *** m loop
  
}

void EmpCylSL::make_eof(void)
{
  Timer timer;
  int icnt;
  double tmp;

  if (!MPIset_eof) {
    MPIin_eof  = new double [rank2*(rank2+1)/2];
    MPIout_eof = new double [rank2*(rank2+1)/2];
    MPIset_eof = true;
  }
  
  //
  //  Sum up over threads
  //

  for (int nth=1; nth<nthrds; nth++) {

    for (int mm=0; mm<=MMAX; mm++) {

      for (int i=1; i<=NMAX*(LMAX-mm+1); i++)
	for (int j=i; j<=NMAX*(LMAX-mm+1); j++)
	  SC[0][mm][i][j] += SC[nth][mm][i][j];
  
    }

    for (int mm=1; mm<=MMAX; mm++) {

      for (int i=1; i<=NMAX*(LMAX-mm+1); i++)
	for (int j=i; j<=NMAX*(LMAX-mm+1); j++)
	  SS[0][mm][i][j] += SS[nth][mm][i][j];
  
    }

  }

  if (VFLAG & 8) {

    for (int mm=0; mm<=MMAX; mm++) {
      bool bad = false;
      for (int i=1; i<=NMAX*(LMAX-mm+1); i++)
	for (int j=i; j<=NMAX*(LMAX-mm+1); j++)
	  if (isnan(SC[0][mm][i][j])) bad = true;
      
      if (bad) {
	cerr << "Process " << myid << ": EmpCylSL has nan in C[" << mm << "]"
	     << endl;
      }
    }
    
    for (int mm=1; mm<=MMAX; mm++) {
      bool bad = false;
      for (int i=1; i<=NMAX*(LMAX-mm+1); i++)
	for (int j=i; j<=NMAX*(LMAX-mm+1); j++)
	  if (isnan(SS[0][mm][i][j])) bad = true;
      
      if (bad) {
	cerr << "Process " << myid << ": EmpCylSL has nan in S[" << mm << "]"
	     << endl;
      }
    }

  }

  //
  //  Distribute covariance to all processes
  //
  for (int mm=0; mm<=MMAX; mm++) {

    icnt=0;
    for (int i=1; i<=NMAX*(LMAX-mm+1); i++)
      for (int j=i; j<=NMAX*(LMAX-mm+1); j++)
	MPIin_eof[icnt++] = SC[0][mm][i][j];
    
    MPI_Allreduce ( MPIin_eof, MPIout_eof, 
		    NMAX*(LMAX-mm+1)*(NMAX*(LMAX-mm+1)+1)/2,
		    MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    icnt=0;
    for (int i=1; i<=NMAX*(LMAX-mm+1); i++)
      for (int j=i; j<=NMAX*(LMAX-mm+1); j++)
	SC[0][mm][i][j] = MPIout_eof[icnt++];
    
  }
  
  for (int mm=1; mm<=MMAX; mm++) {

    icnt=0;
    for (int i=1; i<=NMAX*(LMAX-mm+1); i++)
      for (int j=i; j<=NMAX*(LMAX-mm+1); j++)
	MPIin_eof[icnt++] = SS[0][mm][i][j];
  
    MPI_Allreduce ( MPIin_eof, MPIout_eof, 
		    NMAX*(LMAX-mm+1)*(NMAX*(LMAX-mm+1)+1)/2,
		    MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    icnt=0;
    for (int i=1; i<=NMAX*(LMAX-mm+1); i++)
      for (int j=i; j<=NMAX*(LMAX-mm+1); j++)
	SS[0][mm][i][j] = MPIout_eof[icnt++];
  }


  //
  // DEBUG: check for nan
  //

  if (VFLAG & 8) {

    for (int n=0; n<numprocs; n++) {
      if (myid==n) {
	for (int mm=0; mm<=MMAX; mm++) {
	  bool bad = false;
	  for (int i=1; i<=NMAX*(LMAX-mm+1); i++)
	    for (int j=i; j<=NMAX*(LMAX-mm+1); j++)
	      if (isnan(SC[0][mm][i][j])) bad = true;
	
	  if (bad) {
	    cerr << "Process " << myid << ": EmpCylSL has nan in C[" << mm << "]"
		 << endl;
	  }
	}
	
	for (int mm=1; mm<=MMAX; mm++) {
	  bool bad = false;
	  for (int i=1; i<=NMAX*(LMAX-mm+1); i++)
	    for (int j=i; j<=NMAX*(LMAX-mm+1); j++)
	      if (isnan(SS[0][mm][i][j])) bad = true;
	  
	  if (bad) {
	    cerr << "Process " << myid << ": EmpCylSL has nan in S[" << mm << "]"
		 << endl;
	  }
	}
      }
      MPI_Barrier(MPI_COMM_WORLD);
    }

  }

  // END DEBUG


  if (myid==0) {

    int slave = 0;
    int request_id = 1;		// Begin with cosine case
    int M;

    M = 0;			// Initial counters
    while (M<=MMAX) {
	
      // Send request to slave
      if (slave<numprocs-1) {
	  
	slave++;
	  
	MPI_Send(&request_id, 1, MPI_INT, slave, 1, MPI_COMM_WORLD);
	MPI_Send(&M,  1, MPI_INT, slave, 2, MPI_COMM_WORLD);
	  
	// Increment counters
	request_id++;
	if (request_id>1) {
	  M++;
	  request_id = 0;
	}
	
	if (VFLAG & 8)
	  cerr << "master in make_eof: done waiting on Slave " << slave 
	       << ", next M=" << M << endl;
      }
	
				// If M>MMAX before processor queue exhausted,
				// exit loop and reap the slave data
      if (M>MMAX) break;

      if (slave == numprocs-1) {
	  
	//
	// <Wait and receive and send new request>
	//
	receive_eof(request_id, M);
	  
	// Increment counters

	request_id++;
	if (request_id>1) {
	  M++;
	  request_id = 0;
	}
      }
    }
    
    //
    // <Wait for all slaves to return and flag to continue>
    //
    if (VFLAG & 8)
      cerr << "master in make_eof: now waiting for all slaves to finish" 
	   << endl;
      
				// Dispatch resting slaves
    if (slave < numprocs-1) {
      request_id = -1;		// request_id < 0 means continue
      for (int s=numprocs-1; s>slave; s--) {
	MPI_Send(&request_id, 1, MPI_INT, s, 1, MPI_COMM_WORLD);
      }
    }

				// Get data from working slaves
    while (slave) {
      receive_eof(-1,0);
      slave--;
    }
      
  } else {

    int M, request_id;

    while (1) {
				// Wait for request . . .
      MPI_Recv(&request_id, 1, 
	       MPI_INT, 0, 1, MPI_COMM_WORLD, &status);
      
				// Done!
      if (request_id<0) {
	if (VFLAG & 8)
	  cerr << "Slave " << setw(4) << myid 
	       << ": received DONE signal" << endl;
	break;
      }

      MPI_Recv(&M, 1, 
	       MPI_INT, 0, 2, MPI_COMM_WORLD, &status);
	
      if (VFLAG & 8)
	cerr << "Slave " << setw(4) << myid << ": received orders type="
	     << request_id << "  M=" << M << endl;

      if (request_id) {
				// Complete symmetric part

	for (int i=1; i<NMAX*(LMAX-M+1); i++) {
	  for (int j=i; j<=NMAX*(LMAX-M+1); j++)
	    var[M][i][j] = SC[0][M][i][j];
	}

	for (int i=1; i<NMAX*(LMAX-M+1); i++) {
	  for (int j=i+1; j<=NMAX*(LMAX-M+1); j++) {
	    var[M][j][i] = SC[0][M][i][j];
	  }
	}
    
	double maxV = 0.0;
	for (int i=1; i<=NMAX*(LMAX-M+1); i++) {
	  for (int j=i; j<=NMAX*(LMAX-M+1); j++) {
	    tmp = fabs(var[M][i][j]);
	    if (tmp > maxV) maxV = tmp;
	  }
	}

	var[M] /= maxV;
    
	/*==========================*/
	/* Solve eigenvalue problem */
	/*==========================*/
    
	if (VFLAG & 16) {
	  cout << "Process " << setw(4) << myid 
	       << ": in eigenvalue problem with "
	       << "rank=[" << var[M].getncols() << ", " 
	       << var[M].getnrows() << "]" << endl;
	  timer.reset();
	  timer.start();
	}
	ev = Symmetric_Eigenvalues_SYEVD(var[M], ef, NORDER);
	if (VFLAG & 16) {
	  cout << "Process " << setw(4) << myid 
	       << ": completed eigenproblem in " 
	       << timer.stop().getTotalTime() << " seconds"
	       << endl;
	}

      } else {

				// Complete symmetric part
    

	for (int i=1; i<NMAX*(LMAX-M+1); i++) {
	  for (int j=i; j<=NMAX*(LMAX-M+1); j++)
	    var[M][i][j] = SS[0][M][i][j];
	}

	for (int i=1; i<NMAX*(LMAX-M+1); i++) {
	  for (int j=i+1; j<=NMAX*(LMAX-M+1); j++) {
	    var[M][j][i] = SS[0][M][i][j];
	  }
	}
    
	double maxV = 0.0;
	for (int i=1; i<=NMAX*(LMAX-M+1); i++) {
	  for (int j=i; j<=NMAX*(LMAX-M+1); j++) {
	    tmp = fabs(var[M][i][j]);
	    if (tmp > maxV) maxV = tmp;
	  }
	}
	
	if (maxV>1.0e-5)
	  var[M] /= maxV;
    
    /*==========================*/
    /* Solve eigenvalue problem */
    /*==========================*/
    
	if (VFLAG & 16) {
	  cout << "Process " << setw(4) << myid 
	       << ": in eigenvalue problem with "
	       << "rank=[" << var[M].getncols() << ", " 
	       << var[M].getnrows() << "]" << endl;
	  timer.reset();
	  timer.start();
	}
	ev = Symmetric_Eigenvalues_SYEVD(var[M], ef, NORDER);
	if (VFLAG & 16) {
	  cout << "Process " << setw(4) << myid 
	       << ": completed eigenproblem in " 
	       << timer.stop().getTotalTime() << " seconds"
	       << endl;
	}
      }

      if (VFLAG & 2)
	cerr << "Slave " << setw(4) << myid 
	     << ": with request_id=" << request_id
	     << ", M=" << M << " calling compute_eof_grid" << endl;

      if (VFLAG & 16) {
	timer.reset();
	timer.start();
      }

      compute_eof_grid(request_id, M);

      if (VFLAG & 16) {
	cout << "Process " << setw(4) << myid << ": completed EOF grid for id="
	     << request_id << " and M=" << M << " in " 
	     << timer.stop().getTotalTime() << " seconds"
	     << endl;
      }
      else if (VFLAG & 2)
	cerr << "Slave " << setw(4) << myid 
	     << ": with request_id=" << request_id
	     << ", M=" << M << " COMPLETED compute_eof_grid" << endl;
    }

  }
				// Send grid to all processes
  if (VFLAG & 2) {
    MPI_Barrier(MPI_COMM_WORLD);
    cerr << "Process " << setw(4) << myid 
	 << ": about to enter send_eof_grid" << endl;
  }

  if (VFLAG & 16) {
    timer.reset();
    timer.start();
  }

  send_eof_grid();

  if (VFLAG & 16) {
    cout << "Process " << setw(4) << myid << ": grid reduced in " 
	 << timer.stop().getTotalTime() << " seconds"
	 << endl;
  } 
  else if (VFLAG & 2)
    cerr << "Process " << setw(4) << myid << ": grid reduce completed" << endl;

				// Cache table for restarts
				// (it would be nice to multithread or fork
				//  this call . . . )
  if (myid==0) cache_grid(1);
  
  eof_made      = true;
  coefs_made    = vector<short>(multistep+1, false);

  if (VFLAG & 2) {
    MPI_Barrier(MPI_COMM_WORLD);
    cerr << "Process " << setw(4) << myid 
	 << ": EOF computation completed" << endl;
  }
}


void EmpCylSL::accumulate_eof(vector<Particle>& part, bool verbose)
{

  double r, phi, z, mass;

  int ncnt=0;
  if (myid==0 && verbose) cout << endl;

  setup_eof();

  vector<Particle>::iterator p;
  for (p=part.begin(); p!=part.end(); p++) {

    mass = p->mass;
    r = sqrt(p->pos[0]*p->pos[0] + p->pos[1]*p->pos[1]);
    phi = atan2(p->pos[1], p->pos[0]);
    z = p->pos[2];
    
    accumulate_eof(r, z, phi, mass, 0, p->level);
    if (myid==0 && verbose) {
      if ( (ncnt % 100) == 0) cout << "\r>> " << ncnt << " <<" << flush;
      ncnt++;
    }
  }

}
  

void EmpCylSL::accumulate(vector<Particle>& part, int mlevel, bool verbose)
{
   double r, phi, z, mass;

  int ncnt=0;
  if (myid==0 && verbose) cout << endl;

  setup_accumulation();

  vector<Particle>::iterator p;
  for (p=part.begin(); p!=part.end(); p++) {

    mass = p->mass;
    r = sqrt(p->pos[0]*p->pos[0] + p->pos[1]*p->pos[1]);
    phi = atan2(p->pos[1], p->pos[0]);
    z = p->pos[2];
    
    accumulate(r, z, phi, mass, 0, mlevel);

    if (myid==0 && verbose) {
      if ( (ncnt % 100) == 0) cout << "\r>> " << ncnt << " <<" << flush;
      ncnt++;
    }
  }

}
  

void EmpCylSL::accumulate(double r, double z, double phi, double mass, 
			  int id, int mlevel)
{

  if (coefs_made[mlevel]) {
    ostringstream ostr;
    ostr << "EmpCylSL::accumulate: Process " << myid << ", Thread " << id 
	 << ": calling setup_accumulation from accumulate, aborting" << endl;
    bomb(ostr.str());
  }

  double rr = sqrt(r*r+z*z);
  if (rr/ASCALE>Rtable) return;

  howmany1[mlevel][id]++;

  double msin, mcos;
  int mm;

  double norm = -4.0*M_PI;
  
  if (SELECT) {
    pthread_mutex_lock(&used_lock);
    cylused1++;
    pthread_mutex_unlock(&used_lock);
  }

  get_pot(vc[id], vs[id], r, z);

  for (mm=0; mm<=MMAX; mm++) {

    mcos = cos(phi*mm);
    msin = sin(phi*mm);

    for (int nn=0; nn<rank3; nn++) 
      hold[id][nn] = norm * mass * mcos * vc[id][mm][nn];

    for (int nn=0; nn<rank3; nn++) 
      accum_cosN[mlevel][id][mm][nn] += hold[id][nn];

    if (SELECT) {
      for (int nn=0; nn<rank3; nn++) 
	accum_cos2[id][mm][nn] += hold[id][nn]*hold[id][nn]/mass;
    }
    if (mm>0) {
      for (int nn=0; nn<rank3; nn++) 
	hold[id][nn] = norm * mass * msin * vs[id][mm][nn];
      for (int nn=0; nn<rank3; nn++) 
	accum_sinN[mlevel][id][mm][nn] += hold[id][nn];
      if (SELECT) {
	for (int nn=0; nn<rank3; nn++) 
	  accum_sin2[id][mm][nn] += hold[id][nn]*hold[id][nn]/mass;
      }
    }

    if (multistep==0 || mstep==0)
      cylmass1[id] += mass;
  }

}


void EmpCylSL::make_coefficients(unsigned M0)
{
  int mm, nn;

  if (!MPIset) {
    MPIin  = new double [rank3*(MMAX+1)];
    MPIout = new double [rank3*(MMAX+1)];
    MPIset = true;
  }
  

  for (unsigned M=0; M<=M0; M++) {
    
    if (coefs_made[M]) continue;

				// Sum up over threads
				//
    for (int nth=1; nth<nthrds; nth++) {

      howmany1[M][0] += howmany1[M][nth];

      for (mm=0; mm<=MMAX; mm++)
	for (nn=0; nn<rank3; nn++) {
	  accum_cosN[M][0][mm][nn] += accum_cosN[M][nth][mm][nn];
	}

      
      for (mm=1; mm<=MMAX; mm++)
	for (nn=0; nn<rank3; nn++) {
	  accum_sinN[M][0][mm][nn] += accum_sinN[M][nth][mm][nn];
	}
    }
    
				// Begin distribution loop
				//
    for (mm=0; mm<=MMAX; mm++)
      for (nn=0; nn<rank3; nn++)
	MPIin[mm*rank3 + nn] = accum_cosN[M][0][mm][nn];
    
    MPI_Allreduce ( MPIin, MPIout, rank3*(MMAX+1),
		    MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

    for (mm=0; mm<=MMAX; mm++)
      for (nn=0; nn<rank3; nn++)
	if (multistep)
	  accum_cosN[M][0][mm][nn] = MPIout[mm*rank3 + nn];
	else
	  accum_cos[mm][nn] = MPIout[mm*rank3 + nn];
    
    for (mm=1; mm<=MMAX; mm++)
      for (nn=0; nn<rank3; nn++)
	MPIin[mm*rank3 + nn] = accum_sinN[M][0][mm][nn];
    
    MPI_Allreduce ( MPIin, MPIout, rank3*(MMAX+1),
		    MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  

    for (mm=1; mm<=MMAX; mm++)
      for (nn=0; nn<rank3; nn++)
	if (multistep)
	  accum_sinN[M][0][mm][nn] = MPIout[mm*rank3 + nn];
	else
	  accum_sin[mm][nn] = MPIout[mm*rank3 + nn];
    
    coefs_made[M] = true;
  }
  
}

void EmpCylSL::multistep_reset()
{
  for (unsigned M=0; M<=multistep; M++) dstepN[M] = 0;
}

void EmpCylSL::reset_mass(void)
{ 
  cylmass=0.0; 
  cylmass_made=false; 
  for (int n=0; n<nthrds; n++) cylmass1[n] = 0.0;
}

void EmpCylSL::make_coefficients(void)
{
  if (!cylmass_made) {
    MPI_Allreduce(&cylmass1[0], &cylmass, 1, MPI_DOUBLE, MPI_SUM, 
		  MPI_COMM_WORLD);
    cylmass_made = true;
  }
  
  if (coefs_made_all()) return;

  int mm, nn;

  if (!MPIset) {
    MPIin  = new double [rank3*(MMAX+1)];
    MPIout = new double [rank3*(MMAX+1)];
    MPIset = true;
  }
  

				// Sum up over threads
				// 
  for (unsigned M=0; M<=multistep; M++) {

    if (coefs_made[M]) continue;

    for (int nth=1; nth<nthrds; nth++) {

      howmany1[M][0] += howmany1[M][nth];

      for (mm=0; mm<=MMAX; mm++)
	for (nn=0; nn<rank3; nn++) {
	  accum_cosN[M][0][mm][nn] += accum_cosN[M][nth][mm][nn];
	  if (SELECT && M==0)
	    accum_cos2[0][mm][nn] += accum_cos2[nth][mm][nn];
	}


      for (mm=1; mm<=MMAX; mm++)
	for (nn=0; nn<rank3; nn++) {
	  accum_sinN[M][0][mm][nn] += accum_sinN[M][nth][mm][nn];
	  if (SELECT && M==0)
	    accum_sin2[0][mm][nn] += accum_sin2[nth][mm][nn];
	}
      
    }
  }

				// Begin distribution loop

  for (unsigned M=0; M<=multistep; M++) {

    if (coefs_made[M]) continue;

				// "howmany" is only used for debugging
    MPI_Allreduce ( &howmany1[M][0], &howmany[M], 1, MPI_UNSIGNED,
		    MPI_SUM, MPI_COMM_WORLD);

    for (mm=0; mm<=MMAX; mm++)
      for (nn=0; nn<rank3; nn++)
	MPIin[mm*rank3 + nn] = accum_cosN[M][0][mm][nn];
  
    MPI_Allreduce ( MPIin, MPIout, rank3*(MMAX+1),
		    MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

    for (mm=0; mm<=MMAX; mm++)
      for (nn=0; nn<rank3; nn++)
	if (multistep)
	  accum_cosN[M][0][mm][nn] = MPIout[mm*rank3 + nn];
	else
	  accum_cos[mm][nn] = MPIout[mm*rank3 + nn];
  }
  

  if (SELECT) {
    for (mm=0; mm<=MMAX; mm++)
      for (nn=0; nn<rank3; nn++)
	MPIin[mm*rank3 + nn] = accum_cos2[0][mm][nn];
  
    MPI_Allreduce ( MPIin, MPIout, rank3*(MMAX+1),
		    MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

    for (mm=0; mm<=MMAX; mm++)
      for (nn=0; nn<rank3; nn++)
	accum_cos2[0][mm][nn] = MPIout[mm*rank3 + nn];
  }  


  for (unsigned M=0; M<=multistep; M++) {
    
    if (coefs_made[M]) continue;

    for (mm=1; mm<=MMAX; mm++)
      for (nn=0; nn<rank3; nn++)
	MPIin[mm*rank3 + nn] = accum_sinN[M][0][mm][nn];
  
    MPI_Allreduce ( MPIin, MPIout, rank3*(MMAX+1),
		    MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  

    for (mm=1; mm<=MMAX; mm++)
      for (nn=0; nn<rank3; nn++)
	if (multistep)
	  accum_sinN[M][0][mm][nn] = MPIout[mm*rank3 + nn];
	else
	  accum_sin[mm][nn] = MPIout[mm*rank3 + nn];
  }
  
  if (SELECT) {
    for (mm=1; mm<=MMAX; mm++)
      for (nn=0; nn<rank3; nn++)
	MPIin[mm*rank3 + nn] = accum_sin2[0][mm][nn];
  
    MPI_Allreduce ( MPIin, MPIout, rank3*(MMAX+1),
		    MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  

    for (mm=1; mm<=MMAX; mm++)
      for (nn=0; nn<rank3; nn++)
	accum_sin2[0][mm][nn] = MPIout[mm*rank3 + nn];
  }
  
  if (SELECT) pca_hall();

  coefs_made = vector<short>(multistep+1, true);
}


void EmpCylSL::pca_hall(void)
{
  double sqr, var, fac, tot;
  int mm, nn;

  if (VFLAG & 4)
    cerr << "Process " << setw(4) << myid << ": made it to pca_hall" << endl;

  
				// Need number of particles to compute variance
  MPI_Allreduce ( &cylused1, &cylused, 1,
		  MPI_INT, MPI_SUM, MPI_COMM_WORLD);

  if (VFLAG & 4)
    cerr << "Process " << setw(4) << myid << ": using " 
	 << cylused << " particles" << endl;

  ofstream *hout = NULL;
  if (hallcount++%hallfreq==0 && myid==0 && hallfile.length()>0) {
    hout = new ofstream(hallfile.c_str(), ios::out | ios::app);
    if (!hout) {
      cerr << "Could not open <" << hallfile << "> for appending output" 
	   << endl;
    }
    *hout << "# Time = " << tnow << "  Step=" << hallcount << endl;
    *hout << "#" << endl;
  }

  for (mm=0; mm<=MMAX; mm++)
    for (nn=0; nn<rank3; nn++) {

      tot = 0.0;
      for (unsigned M=0; M<=multistep; M++) tot += accum_cosN[M][0][mm][nn];

      sqr = tot*tot;
      // fac = (accum_cos2[0][mm][nn] - cylmass*sqr + 1.0e-10)/(sqr + 1.0e-18);
      var = accum_cos2[0][mm][nn] - cylmass*sqr;
      fac = sqr/(var/(cylused+1) + sqr + 1.0e-10);
      if (hout) *hout << mm << ", " << nn << ", C:   "
		      << setw(18) << accum_cos2[0][mm][nn] << "  " 
		      << setw(18) << sqr << "  " 
		      << setw(18) << var << "  " 
		      << setw(18) << fac << '\n';

      for (unsigned M=0; M<=multistep; M++) {
	// accum_cosN[M][0][mm][nn] *= 1.0/(1.0 + fac);
	accum_cosN[M][0][mm][nn] *= fac;
      }
    }
  

  for (mm=1; mm<=MMAX; mm++)
    for (nn=0; nn<rank3; nn++) {

      tot = 0.0;
      for (unsigned M=0; M<=multistep; M++) tot += accum_sinN[M][0][mm][nn];

      sqr = tot*tot;
      // fac = (accum_sin2[0][mm][nn] - sqr + 1.0e-10)/(sqr + 1.0e-18);
      var = accum_sin2[0][mm][nn] - cylmass*sqr;
      fac = sqr/(var/(cylused+1) + sqr + 1.0e-10);
      if (hout) *hout << mm << ", " << nn << ", S:   "
		      << setw(18) << accum_sin2[0][mm][nn] << "  " 
		      << setw(18) << sqr << "  " 
		      << setw(18) << var << "  " 
		      << setw(18) << fac << '\n';
      for (unsigned M=0; M<=multistep; M++) {
	// accum_sinN[M][0][mm][nn] *= 1.0/(1.0 + fac);
	accum_sinN[M][0][mm][nn] *= fac;
      }
    }
  
  if (VFLAG & 4)
    cerr << "Process " << setw(4) << myid << ": exiting to pca_hall" << endl;

  if (hout) {
    hout->close();
    delete hout;
  }

}


void EmpCylSL::accumulated_eval(double r, double z, double phi, 
				double &p0, double& p, 
				double& fr, double& fz, double &fp)
{
  if (!coefs_made_all()) {
    if (VFLAG>3)
      cerr << "Process " << myid << ": in EmpCylSL::accumlated_eval, "
	   << "calling make_coefficients()" << endl;
    make_coefficients();
  }

  fr = 0.0;
  fz = 0.0;
  fp = 0.0;
  p = 0.0;

  double rr = sqrt(r*r + z*z);
  if (rr/ASCALE>Rtable) return;

  double X = (r_to_xi(r) - XMIN)/dX;
  double Y = (z_to_y(z)  - YMIN)/dY;

  int ix = (int)X;
  int iy = (int)Y;
  
  if (ix < 0) {
    ix = 0;
    if (enforce_limits) X = 0.0;
  }
  if (iy < 0) {
    iy = 0;
    if (enforce_limits) Y = 0.0;
  }
  
  if (ix >= NUMX) {
    ix = NUMX-1;
    if (enforce_limits) X = NUMX;
  }
  if (iy >= NUMY) {
    iy = NUMY-1;
    if (enforce_limits) Y = NUMY;
  }

  double delx0 = (double)ix + 1.0 - X;
  double dely0 = (double)iy + 1.0 - Y;
  double delx1 = X - (double)ix;
  double dely1 = Y - (double)iy;
  
  double c00 = delx0*dely0;
  double c10 = delx1*dely0;
  double c01 = delx0*dely1;
  double c11 = delx1*dely1;
  
  double ccos, ssin=0.0, fac;
  
  for (int mm=0; mm<=MMAX; mm++) {
    
    ccos = cos(phi*mm);
    ssin = sin(phi*mm);

    for (int n=0; n<rank3; n++) {
      
      fac = accum_cos[mm][n] * ccos;
      
      p += fac *
	(
	 potC[mm][n][ix  ][iy  ] * c00 +
	 potC[mm][n][ix+1][iy  ] * c10 +
	 potC[mm][n][ix  ][iy+1] * c01 +
	 potC[mm][n][ix+1][iy+1] * c11 
	 );
      
      fr += fac *
	(
	 rforceC[mm][n][ix  ][iy  ] * c00 +
	 rforceC[mm][n][ix+1][iy  ] * c10 +
	 rforceC[mm][n][ix  ][iy+1] * c01 +
	 rforceC[mm][n][ix+1][iy+1] * c11
	 );
      
      fz += fac *
	(
	 zforceC[mm][n][ix  ][iy  ] * c00 +
	 zforceC[mm][n][ix+1][iy  ] * c10 +
	 zforceC[mm][n][ix  ][iy+1] * c01 +
	 zforceC[mm][n][ix+1][iy+1] * c11 
	 );
      
      fac = accum_cos[mm][n] * ssin;
      
      fp += fac * mm *
	(
	 potC[mm][n][ix  ][iy  ] * c00 +
	 potC[mm][n][ix+1][iy  ] * c10 +
	 potC[mm][n][ix  ][iy+1] * c01 +
	 potC[mm][n][ix+1][iy+1] * c11 
	 );
      
      
      if (mm) {
	
	fac = accum_sin[mm][n] * ssin;
	
	p += fac *
	  (
	   potS[mm][n][ix  ][iy  ] * c00 +
	   potS[mm][n][ix+1][iy  ] * c10 +
	   potS[mm][n][ix  ][iy+1] * c01 +
	   potS[mm][n][ix+1][iy+1] * c11 
	   );
	
	fr += fac *
	  (
	   rforceS[mm][n][ix  ][iy  ] * c00 +
	   rforceS[mm][n][ix+1][iy  ] * c10 +
	   rforceS[mm][n][ix  ][iy+1] * c01 +
	   rforceS[mm][n][ix+1][iy+1] * c11
	   );
	
	fz += fac *
	  (
	   zforceS[mm][n][ix  ][iy  ] * c00 +
	   zforceS[mm][n][ix+1][iy  ] * c10 +
	   zforceS[mm][n][ix  ][iy+1] * c01 +
	   zforceS[mm][n][ix+1][iy+1] * c11 
	   );
	
	fac = -accum_sin[mm][n] * ccos;
	
	fp += fac * mm *
	  (
	   potS[mm][n][ix  ][iy  ] * c00 +
	   potS[mm][n][ix+1][iy  ] * c10 +
	   potS[mm][n][ix  ][iy+1] * c01 +
	   potS[mm][n][ix+1][iy+1] * c11 
	   );
	
      }
      
    }
    
    if (mm==0) p0 = p;

  }
  
}


double EmpCylSL::accumulated_dens_eval(double r, double z, double phi, 
				       double& d0)
{
  if (!DENS) return 0.0;

  if (!coefs_made_all()) {
    if (VFLAG>3) 
      cerr << "Process " << myid << ": in EmpCylSL::accumlated_dens_eval, "
	   << "calling make_coefficients()" << endl;
    make_coefficients();
  }

  double ans = 0.0;

  double rr = sqrt(r*r + z*z);

  if (rr/ASCALE > Rtable) return ans;

  double X = (r_to_xi(r) - XMIN)/dX;
  double Y = (z_to_y(z)  - YMIN)/dY;

  int ix = (int)X;
  int iy = (int)Y;

  if (ix < 0) {
    ix = 0;
    if (enforce_limits) X = 0.0;
  }
  if (iy < 0) {
    iy = 0;
    if (enforce_limits) Y = 0.0;
  }
  
  if (ix >= NUMX) {
    ix = NUMX-1;
    if (enforce_limits) X = NUMX;
  }
  if (iy >= NUMY) {
    iy = NUMY-1;
    if (enforce_limits) Y = NUMY;
  }

  double delx0 = (double)ix + 1.0 - X;
  double dely0 = (double)iy + 1.0 - Y;
  double delx1 = X - (double)ix;
  double dely1 = Y - (double)iy;

  double c00 = delx0*dely0;
  double c10 = delx1*dely0;
  double c01 = delx0*dely1;
  double c11 = delx1*dely1;

  double ccos, ssin=0.0, fac;
  int n, mm;

  for (mm=0; mm<=MMAX; mm++) {

    ccos = cos(phi*mm);
    ssin = sin(phi*mm);

    for (n=0; n<rank3; n++) {

      fac = accum_cos[mm][n]*ccos;

      ans += fac *
	(
	 densC[mm][n][ix  ][iy  ] * c00 +
	 densC[mm][n][ix+1][iy  ] * c10 +
	 densC[mm][n][ix  ][iy+1] * c01 +
	 densC[mm][n][ix+1][iy+1] * c11 
	 );

      if (mm) {

	fac = accum_sin[mm][n]*ssin;

	ans += fac *
	  (
	   densS[mm][n][ix  ][iy  ] * c00 +
	   densS[mm][n][ix+1][iy  ] * c10 +
	   densS[mm][n][ix  ][iy+1] * c01 +
	   densS[mm][n][ix+1][iy+1] * c11 
	   );
      }

    }

    if (mm==0) d0 = ans;

  }

  return ans;
}


  
void EmpCylSL::get_pot(Matrix& Vc, Matrix& Vs, double r, double z)
{
  Vc.setsize(0, max(1,MMAX), 0, rank3-1);
  Vs.setsize(0, max(1,MMAX), 0, rank3-1);

  if (z/ASCALE > Rtable) z =  Rtable*ASCALE;
  if (z/ASCALE <-Rtable) z = -Rtable*ASCALE;

  double X = (r_to_xi(r) - XMIN)/dX;
  double Y = (z_to_y(z)  - YMIN)/dY;

  int ix = (int)X;
  int iy = (int)Y;

  if (ix < 0) {
    ix = 0;
    if (enforce_limits) X = 0.0;
  }
  if (iy < 0) {
    iy = 0;
    if (enforce_limits) Y = 0.0;
  }
  
  if (ix >= NUMX) {
    ix = NUMX-1;
    if (enforce_limits) X = NUMX;
  }
  if (iy >= NUMY) {
    iy = NUMY-1;
    if (enforce_limits) Y = NUMY;
  }

  double delx0 = (double)ix + 1.0 - X;
  double dely0 = (double)iy + 1.0 - Y;
  double delx1 = X - (double)ix;
  double dely1 = Y - (double)iy;

  double c00 = delx0*dely0;
  double c10 = delx1*dely0;
  double c01 = delx0*dely1;
  double c11 = delx1*dely1;

  double fac = 1.0;

  for (int mm=0; mm<=MMAX; mm++) {
    
    for (int n=0; n<rank3; n++) {

      Vc[mm][n] = fac *
	(
	 potC[mm][n][ix  ][iy  ] * c00 +
	 potC[mm][n][ix+1][iy  ] * c10 +
	 potC[mm][n][ix  ][iy+1] * c01 +
	 potC[mm][n][ix+1][iy+1] * c11 
	 );

      if (mm) {

	Vs[mm][n] = fac *
	  (
	   potS[mm][n][ix  ][iy  ] * c00 +
	   potS[mm][n][ix+1][iy  ] * c10 +
	   potS[mm][n][ix  ][iy+1] * c01 +
	   potS[mm][n][ix+1][iy+1] * c11 
	   );
      }

    }

  }

}
    
  
void EmpCylSL::get_all(int mm, int nn, 
		       double r, double z, double phi,
		       double& p, double& d, 
		       double& fr, double& fz, double &fp)
{
  if (!coefs_made_all()) {
    if (VFLAG & 4) 
      cerr << "Process " << myid << ": in EmpCylSL::gel_all, "
	   << "calling make_coefficients()" << endl;
    make_coefficients();
  }

  fr = 0.0;
  fz = 0.0;
  fp = 0.0;
  p = 0.0;
  d = 0.0;

  double rr = sqrt(r*r + z*z);

  if (rr/ASCALE>Rtable) {
    p = -cylmass/(rr+1.0e-16);
    fr = p*r/(rr+1.0e-16)/(rr+1.0e-16);
    fz = p*z/(rr+1.0e-16)/(rr+1.0e-16);

    return;
  }

  if (z/ASCALE > Rtable) z =  Rtable*ASCALE;
  if (z/ASCALE <-Rtable) z = -Rtable*ASCALE;

  double X = (r_to_xi(r) - XMIN)/dX;
  double Y = (z_to_y(z)  - YMIN)/dY;

  int ix = (int)X;
  int iy = (int)Y;

  if (ix < 0) ix = 0;
  if (iy < 0) iy = 0;
  
  if (ix >= NUMX) ix = NUMX-1;
  if (iy >= NUMY) iy = NUMY-1;

  double delx0 = (double)ix + 1.0 - X;
  double dely0 = (double)iy + 1.0 - Y;
  double delx1 = X - (double)ix;
  double dely1 = Y - (double)iy;

  double c00 = delx0*dely0;
  double c10 = delx1*dely0;
  double c01 = delx0*dely1;
  double c11 = delx1*dely1;

  double ccos, ssin;

  ccos = cos(phi*mm);
  ssin = sin(phi*mm);

  p += ccos *
    (
     potC[mm][nn][ix  ][iy  ] * c00 +
     potC[mm][nn][ix+1][iy  ] * c10 +
     potC[mm][nn][ix  ][iy+1] * c01 +
     potC[mm][nn][ix+1][iy+1] * c11 
	 );

  fr += ccos *
    (
     rforceC[mm][nn][ix  ][iy  ] * c00 +
     rforceC[mm][nn][ix+1][iy  ] * c10 +
     rforceC[mm][nn][ix  ][iy+1] * c01 +
     rforceC[mm][nn][ix+1][iy+1] * c11
	 );
  
  fz += ccos *
    (
     zforceC[mm][nn][ix  ][iy  ] * c00 +
     zforceC[mm][nn][ix+1][iy  ] * c10 +
     zforceC[mm][nn][ix  ][iy+1] * c01 +
     zforceC[mm][nn][ix+1][iy+1] * c11 
     );
  
  fp += ssin * mm *
    (
     potC[mm][nn][ix  ][iy  ] * c00 +
     potC[mm][nn][ix+1][iy  ] * c10 +
     potC[mm][nn][ix  ][iy+1] * c01 +
     potC[mm][nn][ix+1][iy+1] * c11 
     );
  
  if (DENS)
  d += ccos *
    (
     densC[mm][nn][ix  ][iy  ] * c00 +
     densC[mm][nn][ix+1][iy  ] * c10 +
     densC[mm][nn][ix  ][iy+1] * c01 +
     densC[mm][nn][ix+1][iy+1] * c11 
     );


  if (mm) {
    
    p += ssin *
      (
       potS[mm][nn][ix  ][iy  ] * c00 +
       potS[mm][nn][ix+1][iy  ] * c10 +
       potS[mm][nn][ix  ][iy+1] * c01 +
       potS[mm][nn][ix+1][iy+1] * c11 
       );

    fr += ssin *
      (
       rforceS[mm][nn][ix  ][iy  ] * c00 +
       rforceS[mm][nn][ix+1][iy  ] * c10 +
       rforceS[mm][nn][ix  ][iy+1] * c01 +
       rforceS[mm][nn][ix+1][iy+1] * c11
       );

    fz += ssin *
      (
       zforceS[mm][nn][ix  ][iy  ] * c00 +
       zforceS[mm][nn][ix+1][iy  ] * c10 +
       zforceS[mm][nn][ix  ][iy+1] * c01 +
	   zforceS[mm][nn][ix+1][iy+1] * c11 
	   );

    fp += -ccos * mm *
	  (
	   potS[mm][nn][ix  ][iy  ] * c00 +
	   potS[mm][nn][ix+1][iy  ] * c10 +
	   potS[mm][nn][ix  ][iy+1] * c01 +
	   potS[mm][nn][ix+1][iy+1] * c11 
	   );
      
    if (DENS)
    d += ssin *
      (
       densS[mm][nn][ix  ][iy  ] * c00 +
       densS[mm][nn][ix+1][iy  ] * c10 +
       densS[mm][nn][ix  ][iy+1] * c01 +
       densS[mm][nn][ix+1][iy+1] * c11 
       );

  }
  
}


void EmpCylSL::dump_coefs(ostream& out)
{
  out.setf(ios::scientific);

  for (int mm=0; mm<=MMAX; mm++) {

    out << setw(4) << mm;
    for (int j=0; j<rank3; j++)
      out << " " << setw(15) << accum_cos[mm][j];
    out << endl;

    if (mm) {
      out << setw(4) << mm;
      for (int j=0; j<rank3; j++)
	out << " " << setw(15) << accum_sin[mm][j];
      out << endl;
    }

  }
}


#ifdef STANDALONE
#include <coef.H>
static CoefHeader coefheader;
static CoefHeader2 coefheader2;
#endif

void EmpCylSL::dump_coefs_binary(ostream& out, double time)
{
  coefheader2.time = time;
  coefheader2.mmax = MMAX;
  coefheader2.nmax = rank3;

  out.write((const char *)&coefheader2, sizeof(CoefHeader2));

  for (int mm=0; mm<=MMAX; mm++) {

    for (int j=0; j<rank3; j++)
      out.write((const char *)&accum_cos[mm][j], sizeof(double));
    
    if (mm) {

      for (int j=0; j<rank3; j++)
	out.write((const char *)&accum_sin[mm][j], sizeof(double));
      
    }
  }
}


void EmpCylSL::dump_basis(const string& name, int step, double Rmax)
{
  static int numx = 60;
  static int numy = 60;
  
  double rmax;
  if (Rmax>0.0) rmax = Rmax;
  else rmax = 0.33*Rtable*max<double>(ASCALE, HSCALE);
  
  double r, dr = rmax/(numx-1);
  double z, dz = 2.0*rmax/(numy-1);
  double fac=1;
  
  ofstream outC, outS;

  for (int mm=0; mm<=MMAX; mm++) {

    for (int n=0; n<=min<int>(NOUT, rank3-1); n++) {

      ostringstream ins;
      ins << name << ".C." << mm << "." << n << "." << step;
      
      outC.open(ins.str().c_str());
      
      if (mm) {

	ostringstream ins;
	ins << name << ".S." << mm << "." << n << "." << step;
	
	outS.open(ins.str().c_str());
      }
      
				// Ok, write data

      for (int k=0; k<numy; k++) {

	z = -rmax + dz*k;

	for (int j=0; j<numx; j++) {
	  
	  r = dr*j;

	  outC << setw(15) << r << setw(15) << z;

	  double X = (r_to_xi(r) - XMIN)/dX;
	  double Y = ( z_to_y(z) - YMIN)/dY;

	  int ix = (int)X;
	  int iy = (int)Y;

	  if (ix < 0) {
	    ix = 0;
	    if (enforce_limits) X = 0.0;
	  }
	  if (iy < 0) {
	    iy = 0;
	    if (enforce_limits) Y = 0.0;
	  }
	  
	  if (ix >= NUMX) {
	    ix = NUMX-1;
	    if (enforce_limits) X = NUMX;
	  }
	  if (iy >= NUMY) {
	    iy = NUMY-1;
	    if (enforce_limits) Y = NUMY;
	  }
	  
	  double delx0 = (double)ix + 1.0 - X;
	  double dely0 = (double)iy + 1.0 - Y;
	  double delx1 = X - (double)ix;
	  double dely1 = Y - (double)iy;

	  double c00 = delx0*dely0;
	  double c10 = delx1*dely0;
	  double c01 = delx0*dely1;
	  double c11 = delx1*dely1;

	  outC << setw(15) 
	       << fac*(
		       potC[mm][n][ix  ][iy  ] * c00 +
		       potC[mm][n][ix+1][iy  ] * c10 +
		       potC[mm][n][ix  ][iy+1] * c01 +
		       potC[mm][n][ix+1][iy+1] * c11 )
	       << setw(15)
	       << fac*(
		       rforceC[mm][n][ix  ][iy  ] * c00 +
		       rforceC[mm][n][ix+1][iy  ] * c10 +
		       rforceC[mm][n][ix  ][iy+1] * c01 +
		       rforceC[mm][n][ix+1][iy+1] * c11 )
	       << setw(15)
	       << fac*(
		       zforceC[mm][n][ix  ][iy  ] * c00 +
		       zforceC[mm][n][ix+1][iy  ] * c10 +
		       zforceC[mm][n][ix  ][iy+1] * c01 +
		       zforceC[mm][n][ix+1][iy+1] * c11 );
	  
	  if (DENS)
	    outC << setw(15)
		 << fac*(
			 densC[mm][n][ix  ][iy  ] * c00 +
			 densC[mm][n][ix+1][iy  ] * c10 +
			 densC[mm][n][ix  ][iy+1] * c01 +
			 densC[mm][n][ix+1][iy+1] * c11 );

	  outC << endl;

	  if (mm) {
      
	    outS << setw(15) << r << setw(15) << z
		 << setw(15)
		 << fac*(
			 potS[mm][n][ix  ][iy  ] * c00 +
			 potS[mm][n][ix+1][iy  ] * c10 +
			 potS[mm][n][ix  ][iy+1] * c01 +
			 potS[mm][n][ix+1][iy+1] * c11 )
		 << setw(15)
		 << fac*(
			 rforceS[mm][n][ix  ][iy  ] * c00 +
			 rforceS[mm][n][ix+1][iy  ] * c10 +
			 rforceS[mm][n][ix  ][iy+1] * c01 +
			 rforceS[mm][n][ix+1][iy+1] * c11 )
		 << setw(15)
		 << fac*(
			 zforceS[mm][n][ix  ][iy  ] * c00 +
			 zforceS[mm][n][ix+1][iy  ] * c10 +
			 zforceS[mm][n][ix  ][iy+1] * c01 +
			 zforceS[mm][n][ix+1][iy+1] * c11 );
	    
	    if (DENS)
	      outS << setw(15) 
		   << fac*(
			   densS[mm][n][ix  ][iy  ] * c00 +
			   densS[mm][n][ix+1][iy  ] * c10 +
			   densS[mm][n][ix  ][iy+1] * c01 +
			   densS[mm][n][ix+1][iy+1] * c11 );
	    outS << endl;
	  }

	}
	outC << endl;
	if (mm) outS << endl;
      }
      
				// Close output streams
      outC.close();      
      if (mm) outS.close();
    }
  }

}

void EmpCylSL::dump_images(const string& OUTFILE,
			   double XYOUT, double ZOUT, int OUTR, int OUTZ,
			   bool logscale)
{
  if (myid!=0) return;
  if (!coefs_made_all()) return;
  
  double p, d, rf, zf, pf;
  double dr, dz = 2.0*ZOUT/(OUTZ-1);
  double rmin = RMIN*ASCALE;
  
  if (logscale) 
    dr = (log(XYOUT) - log(rmin))/(OUTR-1);
  else
    dr = (XYOUT - rmin)/(OUTR-1);
  
  string Name;
  int Number  = 14;
  int Number2 = 8;
  string Types[] = {".pot", ".dens", ".fr", ".fz", ".fp",
				// Finite difference
		    ".empfr", ".empfz", ".empfp",
				// Compare with recursion
		    ".diffr", ".diffz", ".diffp",
				// Relative difference
		    ".relfr", ".relfz", ".relfp"};
  
  //============
  // Open files
  //============
  ofstream *out = new ofstream [Number];
  for (int j=0; j<Number; j++) {
    Name = OUTFILE + Types[j] + ".eof_recon";
    out[j].open(Name.c_str());
    if (!out[j]) {
      cerr << "Couldn't open <" << Name << ">" << endl;
      break;
    }
  }
  
  double del, tmp, rP, rN, zP, zN, pP, pN, p0, d0;
  double potpr, potnr, potpz, potnz, potpp, potnp;

  
  double hr = HFAC*dr;
  double hz = HFAC*dz;
  double hp = 2.0*M_PI/64.0;
  
  if (logscale) hr = HFAC*0.5*(exp(dr) - exp(-dr));
  
  double r=0.0, z=0.0;
  
  for (int iz=0; iz<OUTZ; iz++) {
    
    z = -ZOUT + dz*iz;

    for (int ir=0; ir<OUTR; ir++) {
      
      if (logscale)
	r = rmin*exp(dr*ir);
      else
	r = rmin + dr*ir;
	  
      accumulated_eval(r, z, 0.0, p0, p, rf, zf, pf);
      d = accumulated_dens_eval(r, z, 0.0, d0);
      
      out[0]  << setw(15) << r << setw(15) << z << setw(15) << p;
      out[1]  << setw(15) << r << setw(15) << z << setw(15) << d;
      out[2]  << setw(15) << r << setw(15) << z << setw(15) << rf;
      out[3]  << setw(15) << r << setw(15) << z << setw(15) << zf;
      out[4]  << setw(15) << r << setw(15) << z << setw(15) << pf;

      
      //===================
      // Finite difference
      //===================
      
      if (logscale) {
	rP = r*(1.0 + 0.5*hr);
	rN = max<double>(1.0e-5, r*(1.0-0.5*hr));
	del = rP - rN;
      }
      else {
	rP = dr*ir+0.5*hr;
	rN = max<double>(1.0e-5, dr*ir-0.5*hr);
	del = hr;
      }
      zP = -ZOUT + dz*iz + 0.5*hz;
      zN = -ZOUT + dz*iz - 0.5*hz;
      
      pP =  0.5*hp;
      pN = -0.5*hp;

      accumulated_eval(rP, z, 0.0, tmp, potpr, tmp, tmp, tmp);
      accumulated_eval(rN, z, 0.0, tmp, potnr, tmp, tmp, tmp);
      accumulated_eval(r, zP, 0.0, tmp, potpz, tmp, tmp, tmp);
      accumulated_eval(r, zN, 0.0, tmp, potnz, tmp, tmp, tmp);
      accumulated_eval(r,  z,  pP, tmp, potpp, tmp, tmp, tmp);
      accumulated_eval(r,  z,  pN, tmp, potnp, tmp, tmp, tmp);
      
      //==================================================

      out[5] << setw(15) << r << setw(15) << z 
	     << setw(15) << -(potpr - potnr)/del;
      
      out[6] << setw(15) << r << setw(15) << z << setw(15)
	     << -(potpz - potnz)/hz;
      
      out[7] << setw(15) << r << setw(15) << z << setw(15)
	     << -(potpp - potnp)/hp;
      
      //==================================================
      

      out[8] << setw(15) << r << setw(15) << z 
	     << setw(15) << (potnr - potpr)/del - rf;
      
      out[9] << setw(15) << r << setw(15) << z << setw(15)
	     << (potnz - potpz)/hz - zf;
      
      out[10] << setw(15) << r << setw(15) << z << setw(15)
	      << (potpp - potnp)/hp - pf;
      
      //==================================================
	  
	  
      out[11] << setw(15) << r << setw(15) << z 
	      << setw(15) << ((potnr - potpr)/del - rf)/(fabs(rf)+1.0e-18);
      
      out[12] << setw(15) << r << setw(15) << z << setw(15)
	      << ((potnz - potpz)/hz - zf)/(fabs(zf)+1.0e-18);
      
      out[13] << setw(15) << r << setw(15) << z << setw(15)
	      << ((potpp - potnp)/hp - pf)/(fabs(pf)+1.0e-18);
      
      //==================================================

      for (int j=0; j<Number; j++) out[j] << endl;

    }
    
    for (int j=0; j<Number; j++) out[j] << endl;
    
  }
  
  //
  // Close current files
  //
  for (int j=0; j<Number; j++) out[j].close();

  //
  // Open files (face on)
  //
  for (int j=0; j<Number2; j++) {
    Name = OUTFILE + Types[j] + ".eof_recon_face";
    out[j].open(Name.c_str());
    if (!out[j]) {
      cerr << "Couldn't open <" << Name << ">" << endl;
      break;
    }
  }
  
  double rr, pp, xx, yy, dxy = 2.0*XYOUT/(OUTR-1);
  
  for (int iy=0; iy<OUTR; iy++) {
    yy = -XYOUT + dxy*iy;
    
    for (int ix=0; ix<OUTR; ix++) {
      xx = -XYOUT + dxy*ix;
      
      rr = sqrt(xx*xx + yy*yy);
      pp = atan2(yy, xx);
      
      accumulated_eval(rr, 0.0, pp, p0, p, rf, zf, pf);
      d = accumulated_dens_eval(rr, 0.0, pp, d0);
      
      out[0]  << setw(15) << xx << setw(15) << yy << setw(15) << p;
      out[1]  << setw(15) << xx << setw(15) << yy << setw(15) << d;
      out[2]  << setw(15) << xx << setw(15) << yy << setw(15) << rf;
      out[3]  << setw(15) << xx << setw(15) << yy << setw(15) << zf;
      out[4]  << setw(15) << xx << setw(15) << yy << setw(15) << pf;

      //===================
      // Finite difference
      //===================
      
      if (logscale) {
	rP = rr*(1.0 + 0.5*hr);
	rN = max<double>(1.0e-5, rr*(1.0-0.5*hr));
	del = rP - rN;
      }
      else {
	rP = rr+0.5*hr;
	rN = max<double>(1.0e-5, rr-0.5*hr);
	del = hr;
      }
      zP =  0.5*hz;
      zN = -0.5*hz;
      
      pP = pp + 0.5*hp;
      pN = pp - 0.5*hp;

      accumulated_eval(rP, 0.0, pp, tmp, potpr, tmp, tmp, tmp);
      accumulated_eval(rN, 0.0, pp, tmp, potnr, tmp, tmp, tmp);
      accumulated_eval(rr,  zP, pp, tmp, potpz, tmp, tmp, tmp);
      accumulated_eval(rr,  zN, pp, tmp, potnz, tmp, tmp, tmp);
      accumulated_eval(rr, 0.0, pP, tmp, potpp, tmp, tmp, tmp);
      accumulated_eval(rr, 0.0, pN, tmp, potnp, tmp, tmp, tmp);
      
      //==================================================

      out[5] << setw(15) << xx << setw(15) << yy 
	     << setw(15) << -(potpr - potnr)/del;
      
      out[6] << setw(15) << xx << setw(15) << yy << setw(15)
	     << -(potpz - potnz)/hz;
      
      out[7] << setw(15) << xx << setw(15) << yy << setw(15)
	     << -(potpp - potnp)/hp;
      
      //==================================================

      for (int j=0; j<Number2; j++) out[j] << endl;

    }
    
    for (int j=0; j<Number2; j++) out[j] << endl;
    
  }
  
  //
  // Close current files and delete
  //
  for (int j=0; j<Number2; j++) out[j].close();
  delete [] out;
}


void EmpCylSL::dump_images_basis(const string& OUTFILE,
				 double XYOUT, double ZOUT, 
				 int OUTR, int OUTZ, bool logscale,
				 int M1, int M2, int N1, int N2)
{
  if (myid!=0) return;
  
  double p, d, rf, zf, pf;
  double dr, dz = 2.0*ZOUT/(OUTZ-1);
  double rmin = RMIN*ASCALE;
  
  if (logscale) 
    dr = (log(XYOUT) - log(rmin))/(OUTR-1);
  else
    dr = (XYOUT - rmin)/(OUTR-1);
  
  int Number  = 10;
  string Types[] = {".pot", ".dens", ".fr", ".fz", ".empfr", ".empfz", 
		    ".diffr", ".diffz", ".relfr", ".relfz"};
  
  double tmp, rP, rN, zP, zN, r, z, del;
  double potpr, potnr, potpz, potnz;
  
  ofstream *out = new ofstream [Number];
  
  double hr = HFAC*dr;
  double hz = HFAC*dz;
  
  if (logscale) hr = HFAC*0.5*(exp(dr) - exp(-dr));
  
  for (int m=M1; m<=M2; m++) {
    
    for (int n=N1; n<=N2; n++) {
      
      
      //============
      // Open files
      //============
      
      for (int j=0; j<Number; j++) {
	ostringstream fname;
	fname << OUTFILE << Types[j] << '.' << m << '.' << n << ".eof_basis";
	out[j].open(fname.str().c_str());
	if (out[j].bad()) {
	  cout << "EmpCylSL::dump_images_basis: failed to open " 
	       << fname.str() << endl;
	  return;
	}
      }
      
      for (int iz=0; iz<OUTZ; iz++) {
	for (int ir=0; ir<OUTR; ir++) {
	  
	  z = -ZOUT + dz*iz;
	  if (logscale)
	    r = rmin*exp(dr*ir);
	  else
	    r = rmin + dr*ir;
	  
	  get_all(m, n, r, z, 0.0, p, d, rf, zf, pf);
	  
	  out[0] << setw(15) << r << setw(15) << z << setw(15) << p;
	  out[1] << setw(15) << r << setw(15) << z << setw(15) << d;
	  out[2] << setw(15) << r << setw(15) << z << setw(15) << rf;
	  out[3] << setw(15) << r << setw(15) << z << setw(15) << zf;
	  
	  //===================
	  // Finite difference
	  //===================
	  
	  if (logscale) {
	    rP = r*(1.0 + 0.5*hr);
	    rN = max<double>(1.0e-5, r*(1.0-0.5*hr));
	    del = rP - rN;
	  }
	  else {
	    rP = dr*ir+0.5*hr;
	    rN = max<double>(1.0e-5, dr*ir-0.5*hr);
	    del = hr;
	  }
	  zP = -ZOUT + dz*iz + 0.5*hz;
	  zN = -ZOUT + dz*iz - 0.5*hz;
	  
	  get_all(m, n, rP, z, 0.0, potpr, tmp, tmp, tmp, tmp);
	  get_all(m, n, rN, z, 0.0, potnr, tmp, tmp, tmp, tmp);
	  get_all(m, n, r, zP, 0.0, potpz, tmp, tmp, tmp, tmp);
	  get_all(m, n, r, zN, 0.0, potnz, tmp, tmp, tmp, tmp);
	  
	  out[4] << setw(15) << r << setw(15) << z 
		 << setw(15) << -(potpr - potnr)/del ;
	  
	  out[5] << setw(15) << r << setw(15) << z << setw(15)
		 << -(potpz - potnz)/hz  << setw(15) << hz;
	  
	  out[6] << setw(15) << r << setw(15) << z << setw(15)
		 << -(potpr - potnr)/del - rf;
	  
	  out[7] << setw(15) << r << setw(15) << z << setw(15)
		 << -(potpz - potnz)/hz  - zf;
	  
	  
	  out[8] << setw(15) << r << setw(15) << z << setw(15)
		 << (-(potpr - potnr)/del - rf)/(fabs(rf)+1.0e-18);
	  
	  out[9] << setw(15) << r << setw(15) << z << setw(15)
		 << (-(potpz - potnz)/hz  - zf)/(fabs(zf)+1.0e-18);
	  
	  for (int j=0; j<Number; j++) out[j] << endl;
	  
	}
	
	for (int j=0; j<Number; j++) out[j] << endl;
	
      }
      
      for (int j=0; j<Number; j++) out[j].close();
    }
    
  }
  
  delete [] out;
}

double EmpCylSL::r_to_xi(double r)
{
  if (CMAP) {
    if (r<0.0) {
      ostringstream msg;
      msg << "radius=" << r << " < 0! [mapped]";
      bomb(msg.str());
    }
    return (r/ASCALE - 1.0)/(r/ASCALE + 1.0);
  } else {
    if (r<0.0)  {
      ostringstream msg;
      msg << "radius=" << r << " < 0!";
      bomb(msg.str());
    }
    return r;
  }
}
    
double EmpCylSL::xi_to_r(double xi)
{
  if (CMAP) {
    if (xi<-1.0) bomb("xi < -1!");
    if (xi>=1.0) bomb("xi >= 1!");

    return (1.0 + xi)/(1.0 - xi) * ASCALE;
  } else {
    return xi;
  }

}

double EmpCylSL::d_xi_to_r(double xi)
{
  if (CMAP) {
    if (xi<-1.0) bomb("xi < -1!");
    if (xi>=1.0) bomb("xi >= 1!");

    return 0.5*(1.0 - xi)*(1.0 - xi) / ASCALE;
  } else {
    return 1.0;
  }
}

void EmpCylSL::bomb(string oops)
{
  cerr << "EmpCylSL: " << oops << endl; 
  MPI_Abort(MPI_COMM_WORLD, -1);
  exit(-1);
}

#define MINEPS 1.0e-10

void EmpCylSL::legendre_R(int lmax, double x, Matrix& p)
{
  double fact, somx2, pll, pl1, pl2;
  int m, l;

  p[0][0] = pll = 1.0;
  if (lmax > 0) {
    somx2 = sqrt( (1.0 - x)*(1.0 + x) );
    fact = 1.0;
    for (m=1; m<=lmax; m++) {
      pll *= -fact*somx2;
      p[m][m] = pll;
      if (isnan(p[m][m]))
	cerr << "legendre_R: p[" << m << "][" << m << "]: pll=" << pll << endl;
      fact += 2.0;
    }
  }

  for (m=0; m<lmax; m++) {
    pl2 = p[m][m];
    p[m+1][m] = pl1 = x*(2*m+1)*pl2;
    for (l=m+2; l<=lmax; l++) {
      p[l][m] = pll = (x*(2*l-1)*pl1-(l+m-1)*pl2)/(l-m);
      if (isnan(p[l][m]))
	cerr << "legendre_R: p[" << l << "][" << m << "]: pll=" << pll << endl;

      pl2 = pl1;
      pl1 = pll;
    }
  }

  if (isnan(x))
    cerr << "legendre_R: x" << endl;
  for(l=0; l<=lmax; l++)
    for (m=0; m<=l; m++)
      if (isnan(p[l][m]))
	cerr << "legendre_R: p[" << l << "][" << m << "] lmax=" 
	     << lmax << endl;

}

void EmpCylSL::dlegendre_R(int lmax, double x, Matrix &p, Matrix &dp)
{
  double fact, somx2, pll, pl1, pl2;
  int m, l;

  p[0][0] = pll = 1.0;
  if (lmax > 0) {
    somx2 = sqrt( (1.0 - x)*(1.0 + x) );
    fact = 1.0;
    for (m=1; m<=lmax; m++) {
      pll *= -fact*somx2;
      p[m][m] = pll;
      fact += 2.0;
    }
  }

  for (m=0; m<lmax; m++) {
    pl2 = p[m][m];
    p[m+1][m] = pl1 = x*(2*m+1)*pl2;
    for (l=m+2; l<=lmax; l++) {
      p[l][m] = pll = (x*(2*l-1)*pl1-(l+m-1)*pl2)/(l-m);
      pl2 = pl1;
      pl1 = pll;
    }
  }

  if (1.0-fabs(x) < MINEPS) {
    if (x>0) x =   1.0 - MINEPS;
    else     x = -(1.0 - MINEPS);
  }

  somx2 = 1.0/(x*x - 1.0);
  dp[0][0] = 0.0;
  for (l=1; l<=lmax; l++) {
    for (m=0; m<l; m++)
      dp[l][m] = somx2*(x*l*p[l][m] - (l+m)*p[l-1][m]);
    dp[l][l] = somx2*x*l*p[l][l];
  }
}

void EmpCylSL::sinecosine_R(int mmax, double phi, Vector& c, Vector& s)
{
  int m;

  c[0] = 1.0;
  s[0] = 0.0;

  c[1] = cos(phi);
  s[1] = sin(phi);

  for (m=2; m<=mmax; m++) {
    c[m] = 2.0*c[1]*c[m-1] - c[m-2];
    s[m] = 2.0*c[1]*s[m-1] - s[m-2];
  }
}


void EmpCylSL::multistep_update_begin()
{
#ifndef STANDALONE
				// Clear the update matricies
  for (int nth=0; nth<nthrds; nth++) {
    for (unsigned M=mfirst[mstep]; M<=multistep; M++) {
      differC1[nth][M].setsize(0, MMAX, 0, rank3-1);
      differS1[nth][M].setsize(0, MMAX, 0, rank3-1);

      for (int mm=0; mm<=MMAX; mm++) {
	for (int nn=0; nn<rank3; nn++) {
	  differC1[nth][M][mm][nn] = differS1[nth][M][mm][nn] = 0.0;
	}
      }
    }
  }

#endif // STANDALONE
}

void EmpCylSL::multistep_update_finish()
{
#ifndef STANDALONE

  unsigned offset0, offset1;
  unsigned sz = (multistep - mfirst[mstep]+1)*(MMAX+1)*rank3;
  for (unsigned j=0; j<sz; j++) 
    workC1[j] = workC[j] = workS1[j] = workS[j] = 0.0;

				// Combine the update matricies
  for (unsigned M=mfirst[mstep]; M<=multistep; M++) {

    offset0 = (M - mfirst[mstep])*(MMAX+1)*rank3;

    for (int mm=0; mm<=MMAX; mm++) {
      
      offset1 = mm*rank3;

      for (int k=0; k<rank3; k++) 
	workC1[offset0+offset1+k] = differC1[0][M][mm][k];
      for (int nth=1; nth<nthrds; nth++)
	for (int k=0; k<rank3; k++) 
	  workC1[offset0+offset1+k] += differC1[nth][M][mm][k];

      if (mm) {
	for (int k=0; k<rank3; k++) 
	  workS1[offset0+offset1+k] = differS1[0][M][mm][k];
	for (int nth=1; nth<nthrds; nth++)
	  for (int k=0; k<rank3; k++) 
	    workS1[offset0+offset1+k] += differS1[nth][M][mm][k];

      }
    }
  }

  MPI_Allreduce (&workC1[0], &workC[0], sz,
		 MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

  MPI_Allreduce (&workS1[0], &workS[0], sz, 
		 MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

  for (unsigned M=mfirst[mstep]; M<=multistep; M++) {

    offset0 = (M - mfirst[mstep])*(MMAX+1)*rank3;

    for (int mm=0; mm<=MMAX; mm++) {
      
      offset1 = mm*rank3;

      for (int nn=0; nn<rank3; nn++) 
	accum_cos[mm][nn] += workC[offset0+offset1+nn];

      if (mm) {
	for (int nn=0; nn<rank3; nn++) 
	  accum_sin[mm][nn] += workS[offset0+offset1+nn];
      }
    }

  }

#endif // STANDALONE
}

void EmpCylSL::multistep_update(int from, int to, double r, double z, double phi, double mass, int id)
{
  double rr = sqrt(r*r+z*z);
  if (rr/ASCALE>Rtable) return;

  double msin, mcos;

  double norm = -4.0*M_PI;
  
  get_pot(vc[id], vs[id], r, z);

  for (int mm=0; mm<=MMAX; mm++) {

    mcos = cos(phi*mm);
    msin = sin(phi*mm);

    for (int nn=0; nn<rank3; nn++) 
      hold[id][nn] = norm * mass * mcos * vc[id][mm][nn];

    for (int nn=0; nn<rank3; nn++) {
      differC1[id][from][mm][nn] -= hold[id][nn];
      differC1[id][to  ][mm][nn] += hold[id][nn];
    }

    if (mm>0) {
      for (int nn=0; nn<rank3; nn++) 
	hold[id][nn] = norm * mass * msin * vs[id][mm][nn];
      for (int nn=0; nn<rank3; nn++) {
	differS1[id][from][mm][nn] -= hold[id][nn];
	differS1[id][to  ][mm][nn] += hold[id][nn];
      }
    }
  }

}



void EmpCylSL::compute_multistep_coefficients(unsigned mlevel)
{
				// Clean coefficient matrix
				// 
  for (int mm=0; mm<=MMAX; mm++) {
    for (int nn=0; nn<rank3; nn++) {
      accum_cos[mm][nn] = 0.0;
      if (mm) accum_sin[mm][nn] = 0.0;
    }
  }
  
				// Interpolate to get coefficients above
  double a, b;			// 
  for (unsigned M=0; M<mlevel; M++) {

				// No interpolation? Should never happen!
    if (dstepN[M] == dstepL[M]) {

      for (int mm=0; mm<=MMAX; mm++) {
	for (int nn=0; nn<rank3; nn++) {
	  accum_cos[mm][nn] += accum_cosN[M][0][mm][nn];
	  if (mm)
	    accum_sin[mm][nn] += accum_sinN[M][0][mm][nn];
	}
      }

    } else {

      b = (double)(mstep - dstepL[M])/(double)(dstepN[M] - dstepL[M]);
      a = 1.0 - b;
      for (int mm=0; mm<=MMAX; mm++) {
	for (int nn=0; nn<rank3; nn++) {
	  accum_cos[mm][nn] += a*accum_cosL[M][0][mm][nn] + b*accum_cosN[M][0][mm][nn];
	  if (mm)
	    accum_sin[mm][nn] += a*accum_sinL[M][0][mm][nn] + b*accum_sinN[M][0][mm][nn];
	}
      }
      // Sanity debug check
      if (a<0.0 && a>1.0) {
	cout << "Process " << myid << ": interpolation error in multistep [a]" << endl;
      }
      if (b<0.0 && b>1.0) {
	cout << "Process " << myid << ": interpolation error in multistep [b]" << endl;
      }
    }
  }
				// Add coefficients at or below this level
				// 
  for (unsigned M=mlevel; M<=multistep; M++) {
    for (int mm=0; mm<=MMAX; mm++) {
      for (int nn=0; nn<rank3; nn++) {
	accum_cos[mm][nn] += accum_cosN[M][0][mm][nn];
	if (mm)
	  accum_sin[mm][nn] += accum_sinN[M][0][mm][nn];
      }
    }
  }

  coefs_made = vector<short>(multistep+1, true);
}


void EmpCylSL::multistep_debug()
{
  for (int n=0; n<numprocs; n++) {
    if (n==myid) {
      cout << "Process " << myid << endl
	   << "   accum_cos[0][0]="
	   << accum_cos[0][0] << endl
	   << "   accum_sin[1][0]="
	   << accum_sin[1][0] << endl;

      cout.setf(ios::scientific);
      int c = cout.precision(2);
      for (unsigned M=0; M<=multistep; M++) {
	cout << "   M=" << M << ": #part=" << howmany[M] << endl;

	cout << "   M=" << M << ", m=0, C_N: ";
	for (int j=0; j<NORDER; j++) 
	  cout << setprecision(2) << setw(10) << accum_cosN[M][0][0][j];
	cout << endl;

	cout << "   M=" << M << ", m=0, C_L: ";
	for (int j=0; j<NORDER; j++) 
	  cout << setprecision(2) << setw(10) << accum_cosL[M][0][0][j];
	cout << endl;

	cout << "   M=" << M << ", m=1, C_N: ";
	for (int j=0; j<NORDER; j++) 
	  cout << setprecision(2) << setw(10) << accum_cosN[M][0][1][j];
	cout << endl;

	cout << "   M=" << M << ", m=1, C_L: ";
	for (int j=0; j<NORDER; j++) 
	  cout << setprecision(2) << setw(10) << accum_cosL[M][0][1][j];
	cout << endl;

	cout << "   M=" << M << ", m=1, S_N: ";
	for (int j=0; j<NORDER; j++) 
	  cout << setprecision(2) << setw(10) << accum_sinN[M][0][1][j];
	cout << endl;

	cout << "   M=" << M << ", m=1, S_L: ";
	for (int j=0; j<NORDER; j++) 
	  cout << setprecision(2) << setw(10) << accum_sinL[M][0][1][j];
	cout << endl;

	cout.precision(c);
      }
    }
    MPI_Barrier(MPI_COMM_WORLD);
  }
}


void EmpCylSL::dump_eof_file(const string& eof_file, const string& output)
{
  ifstream in(eof_file.c_str());
  if (!in) {
    cerr << "EmpCylSL::cache_grid: error opening input file" << endl;
    return;
  }

  ofstream out(output.c_str());
  if (!in) {
    cerr << "EmpCylSL::cache_grid: error opening outputfile" << endl;
    return;
  }

  int mmax, numx, numy, nmax, norder, tmp;
  bool cmap=false, dens=false;
  double rmin, rmax, ascl, hscl;

  in.read((char *)&mmax, sizeof(int));
  in.read((char *)&numx, sizeof(int));
  in.read((char *)&numy, sizeof(int));
  in.read((char *)&nmax, sizeof(int));
  in.read((char *)&norder, sizeof(int));
  in.read((char *)&tmp, sizeof(int)); if (tmp) dens = true;
  in.read((char *)&tmp, sizeof(int)); if (tmp) cmap = true;
  in.read((char *)&rmin, sizeof(double));
  in.read((char *)&rmax, sizeof(double));
  in.read((char *)&ascl, sizeof(double));
  in.read((char *)&hscl, sizeof(double));
  
  out << setw(70) << setfill('-') << '-' << setfill(' ') << endl;
  out << setw(20) << left << "MMAX"   << " : " << MMAX << endl;
  out << setw(20) << left << "NUMX"   << " : " << NUMX << endl;
  out << setw(20) << left << "NUMY"   << " : " << NUMY << endl;
  out << setw(20) << left << "NMAX"   << " : " << NMAX << endl;
  out << setw(20) << left << "NORDER" << " : " << NORDER << endl;
  out << setw(20) << left << "DENS"   << " : " << DENS << endl;
  out << setw(20) << left << "CMAP"   << " : " << CMAP << endl;
  out << setw(20) << left << "RMIN"   << " : " << RMIN << endl;
  out << setw(20) << left << "RMAX"   << " : " << RMAX << endl;
  out << setw(20) << left << "ASCALE" << " : " << ASCALE << endl;
  out << setw(20) << left << "HSCALE" << " : " << HSCALE << endl;
  out << setw(70) << setfill('-') << '-' << setfill(' ') << endl;
    
  double time;
  in.read((char *)&cylmass, sizeof(double));
  in.read((char *)&time,    sizeof(double));
  out << setw(20) << left << "cylmass" << " : " << cylmass << endl;
  out << setw(20) << left << "time"    << " : " << time    << endl;
  out << setw(70) << setfill('-') << '-' << setfill(' ') << endl;

				// Read table

  int nfield = 3;
  if (DENS) nfield += 1;
  
  vector< vector< vector<double> > > mat(nfield);
  for (int n=0; n<nfield; n++) {
    mat[n] = vector< vector<double> >(NUMX);
    for (int j=0; j<NUMX; j++) mat[n][j] = vector<double>(NUMY);
  }

  for (int m=0; m<=MMAX; m++) {
    
    for (int v=0; v<rank3; v++) {

      for (int ix=0; ix<=NUMX; ix++)
	for (int iy=0; iy<=NUMY; iy++) {
	  in.read((char *)&mat[0][ix][iy], sizeof(double));
	}
      
      for (int ix=0; ix<=NUMX; ix++)
	for (int iy=0; iy<=NUMY; iy++)
	  in.read((char *)&mat[1][ix][iy], sizeof(double));
      
      for (int ix=0; ix<=NUMX; ix++)
	for (int iy=0; iy<=NUMY; iy++)
	  in.read((char *)&mat[2][ix][iy], sizeof(double));
      
      if (DENS) {
	for (int ix=0; ix<=NUMX; ix++)
	  for (int iy=0; iy<=NUMY; iy++)
	    in.read((char *)&mat[v][ix][iy], sizeof(double));
	
      }
      
      for (int ix=0; ix<NUMX; ix++) {
	for (int iy=0; iy<NUMY; iy++) {
	  out << left << setw(4) << m << setw(4) << v 
	      << setw(4) << ix << setw(4) << iy;
	  for (int n=0; n<nfield; n++) out << setw(16) << mat[n][ix][iy]; 
	  out << endl;
	}
	
      }
      
    }
    out << setw(70) << setfill('-') << '-' << setfill(' ') << endl;

  }

  for (int m=1; m<=MMAX; m++) {
    
    for (int v=0; v<rank3; v++) {
      
      for (int ix=0; ix<=NUMX; ix++)
	for (int iy=0; iy<=NUMY; iy++)
	  in.read((char *)&mat[0][ix][iy], sizeof(double));
      
      for (int ix=0; ix<=NUMX; ix++)
	for (int iy=0; iy<=NUMY; iy++)
	  in.read((char *)&mat[1][ix][iy], sizeof(double));
      
      for (int ix=0; ix<=NUMX; ix++)
	for (int iy=0; iy<=NUMY; iy++)
	  in.read((char *)&mat[2][ix][iy], sizeof(double));
      
      if (DENS) {
	for (int ix=0; ix<=NUMX; ix++)
	  for (int iy=0; iy<=NUMY; iy++)
	    in.read((char *)&mat[3][ix][iy], sizeof(double));
      }

      for (int ix=0; ix<NUMX; ix++) {
	for (int iy=0; iy<NUMY; iy++) {
	  out << left << setw(4) << m << setw(4) << v 
	      << setw(4) << ix << setw(4) << iy;
	  for (int n=0; n<nfield; n++) out << setw(16) << mat[n][ix][iy]; 
	  out << endl;
	}
      }
    }
      
    out << setw(70) << setfill('-') << '-' << setfill(' ') << endl;

  }
    
}

void EmpCylSL::restrict_order(int n)
{
  for (int m=0; m<=MMAX; m++) {
    for (int k=n; k<NORDER; k++) {
      accum_cos[m][k] = 0.0;
      if (m>0) accum_sin[m][k] = 0.0;
    }
  }
}