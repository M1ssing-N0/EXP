/*****************************************************************************
 *  Description:
 *  -----------
 *
 *  This routine computes the potential, acceleration and density using
 *  the Sturm Liouville direct solution
 *
 *
 *  Call sequence:
 *  -------------
 *
 *  Parameters:
 *  ----------
 *
 *
 *  Returns:
 *  -------
 *
 *  Value
 *
 *  Notes:
 *  -----
 *
 *  By:
 *  --
 *
 *  MDW 11/13/91
 *      06/09/92 updated to use recursion relations rather than tables
 *
 ***************************************************************************/

#include <stdlib.h>
#include <values.h>

#include <thread>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include <SphericalSL.h>

#ifdef RCSID
static char rcsid[] = 
"$Id$";
#endif

void legendre_R(int lmax, double x, Matrix& p);
void dlegendre_R(int lmax, double x, Matrix &p, Matrix &dp);
void sinecosine_R(int mmax, double phi, Vector& c, Vector& s);
double factrl(int n);

double SphericalSL::RMIN     = 0.001;
double SphericalSL::RMAX     = 100.0;
int    SphericalSL::NUMR     = 1000;

int    SphericalSL::selector = 0;
int    SphericalSL::tk_type  = 2;
double SphericalSL::tksmooth = 1.0;
double SphericalSL::tkcum    = 0.95;

SphericalSL::SphericalSL(void)
{
  nthrds  = 1;
  compute = 0;
}

SphericalSL::SphericalSL(int Nth, int lmax, int nmax, int cmap, double rs)
{
  reset(Nth, lmax, nmax, cmap, rs);
  compute = 0;
}

void SphericalSL::reset(int Nth, int lmax, int nmax, int CMAP, double SCALE)
{
  nthrds = Nth;
  NMAX   = nmax;
  LMAX   = lmax<1 ? 1 : lmax;
  
  use.resize(nthrds);
				//  Allocate coefficient matrix
  expcoef  = Matrix(0, LMAX*(LMAX+2), 1, NMAX);
  expcoef1.resize(nthrds);
  for (auto & v : expcoef1) v = Matrix(0, LMAX*(LMAX+2), 1, NMAX);
  
  if (selector) {
#ifdef DEBUG
    cerr << "Process " << myid << ": Selector is 1\n";
#endif

    try {
      cc = new Matrix [LMAX*(LMAX+2) + 1];
      cc1.resize(nthrds);
      for( auto & v : cc1) v = new Matrix [LMAX*(LMAX+2) + 1];
    }
    catch(exception const & msg) {
      cerr << "SphericalSL: " << msg.what() << endl;
      return;
    }
    
    for (int l=0; l<=LMAX*(LMAX+2); l++)
      cc[l] = Matrix(1, NMAX, 1, NMAX);
    
    for (auto & c : cc1) {
      for (int l=0; l<=LMAX*(LMAX+2); l++)
	c[l] = Matrix(1, NMAX, 1, NMAX);
    }
  }
  
  // Allocate and compute normalization matrix
  
  normM = Matrix(0, LMAX, 1, NMAX);
  krnl  = Matrix(0, LMAX, 1, NMAX);
  
				// Potential
  potd.resize(nthrds);
  for (auto & v : potd) v = Matrix(0, LMAX, 1, NMAX);

  dpot.resize(nthrds);
  for (auto & v : dpot) v = Matrix(0, LMAX, 1, NMAX);

				// Density
  dend.resize(nthrds);
  for (auto & v : dend) v = Matrix(0, LMAX, 1, NMAX);

				// Sin, cos, legendre
  
  cosm.resize(nthrds);
  for (auto & v : cosm) v = Vector(0, LMAX);

  sinm.resize(nthrds);
  for (auto & v : sinm) v = Vector(0, LMAX);

  legs.resize(nthrds);
  for (auto & v : legs) v  = Matrix(0, LMAX, 0, LMAX);

  dlegs.resize(nthrds);
  for (auto & v : dlegs) v  = Matrix(0, LMAX, 0, LMAX);
  
  for (int l=0; l<=LMAX; l++) {
    for (int n=1; n<=NMAX; n++) {
      normM[l][n] = 1.0;
      krnl[l][n] = 1.0;
    }
  }
  
				// Factorial matrix
  
  factorial = Matrix(0, LMAX, 0, LMAX);
  
  for (int l=0; l<=LMAX; l++) {
    for (int m=0; m<=l; m++) 
      factorial[l][m] = factrl(l-m)/factrl(l+m);
  }
  
				// Generate Sturm-Liouville grid
  SLGridSph::mpi = 1;		// Turn on MPI
  ortho = new SLGridSph(LMAX, NMAX, NUMR, RMIN, RMAX, true, CMAP, SCALE);

}


SphericalSL::~SphericalSL(void)
{
  if (selector) {
    delete [] cc;
    for (auto & v : cc1) delete [] v;
  }

  delete ortho;
}


void SphericalSL::compute_coefficients_single(vector<Particle> &part)
{
  double facs1=0.0, facs2=0.0, fac0=4.0*M_PI;

  use[0] = 0;

  for (auto &p : part) {
    
    double xx   = p.pos[0];
    double yy   = p.pos[1];
    double zz   = p.pos[2];
    double mass = p.mass;

    double r2 = (xx*xx + yy*yy + zz*zz);
    double r = sqrt(r2) + MINDOUBLE;

    if (r<=RMAX) {
      use[0]++;
      double costh = zz/r;
      double phi = atan2(yy,xx);
      
      legendre_R(LMAX, costh, legs[0]);
      sinecosine_R(LMAX, phi, cosm[0], sinm[0]);

      ortho->get_pot(potd[0], r);
      
      // l loop
      //
      for (int l=0, loffset=0; l<=LMAX; loffset+=(2*l+1), l++) {

	// m loop
	//
	for (int m=0, moffset=0; m<=l; m++) {
	  if (m==0) {
	    double facs1 = 0.0;
	    if (selector && compute)
	      facs1 = legs[0][l][m]*legs[0][l][m]*mass;
	    for (int n=1; n<=NMAX; n++) {
	      expcoef1[0][loffset+moffset][n] += potd[0][l][n]*legs[0][l][m]*mass*
		fac0/normM[l][n];

	      if (selector && compute) {
		for (int nn=n; nn<=NMAX; nn++)
		  cc1[0][loffset+moffset][n][nn] += potd[0][l][n]*potd[0][l][nn]*
		    facs1/(normM[l][n]*normM[l][nn]);
	      }
	    }

	    moffset++;
	  }
	  else {
	    double fac1 = legs[0][l][m]*cosm[0][m], facs1 = 0.0;
	    double fac2 = legs[0][l][m]*sinm[0][m], facs2 = 0.0;
	    if (selector && compute) {
	      facs1 = fac1*fac1*mass;
	      facs2 = fac2*fac2*mass;
	    }
	    for (int n=1; n<=NMAX; n++) {
	      expcoef1[0][loffset+moffset][n] += potd[0][l][n]*fac1*mass*
		fac0/normM[l][n];

	      expcoef1[0][loffset+moffset+1][n] += potd[0][l][n]*fac2*mass*
		fac0/normM[l][n];

	      if (selector && compute) {
		for (int nn=n; nn<=NMAX; nn++) {
		  cc1[0][loffset+moffset][n][nn] += 
		    potd[0][l][n]*potd[0][l][nn]*facs1/(normM[l][n]*normM[l][nn]);
		  cc1[0][loffset+moffset+1][n][nn] +=
		    potd[0][l][n]*potd[0][l][nn]*facs2/(normM[l][n]*normM[l][nn]);
		}
	      }
		
	    }

	    moffset+=2;
	  }
	}
      }
    }
  }
}

void SphericalSL::compute_coefficients_thread(vector<Particle>& part)
{
  std::thread t[nthrds];
 
  // Launch the threads
  for (int id=0; id<nthrds; ++id) {
    t[id] = std::thread(&SphericalSL::compute_coefficients_thread_call, this, id, &part);
  }
  // Join the threads
  for (int id=0; id<nthrds; ++id) {
    t[id].join();
  }


  // Reduce thread values
  //
  for (int id=1; id<nthrds; id++) {

    use[0] += use[id];

    // l loop
    for (int l=0, loffset=0; l<=LMAX; loffset+=(2*l+1), l++) {

      // m loop
      for (int m=0, moffset=0; m<=l; m++) {

	if (m==0) {
	  for (int n=1; n<=NMAX; n++) {
	    expcoef1[0][loffset+moffset][n] += expcoef1[id][loffset+moffset][n];

	    if (selector && compute) {
	      for (int nn=n; nn<=NMAX; nn++)
		cc1[0][loffset+moffset][n][nn] += cc1[id][loffset+moffset][n][nn];
	    }
	  }

	  moffset++;
	} else {

	  for (int n=1; n<=NMAX; n++) {
	    expcoef1[0][loffset+moffset  ][n] += expcoef1[id][loffset+moffset  ][n];
	    expcoef1[0][loffset+moffset+1][n] += expcoef1[id][loffset+moffset+1][n];
	    
	    if (selector && compute) {
	      for (int nn=n; nn<=NMAX; nn++) {
		cc1[0][loffset+moffset  ][n][nn] += cc1[id][loffset+moffset  ][n][nn];
		cc1[0][loffset+moffset+1][n][nn] += cc1[id][loffset+moffset+1][n][nn];
	      }
	    }
	    
	  }

	  moffset+=2;
	}
      }
    }
  }
}

void SphericalSL::compute_coefficients_thread_call(int id, std::vector<Particle> *p)
{
  double facs1=0.0, facs2=0.0, fac0=4.0*M_PI;

  int nbodies = p->size();
    
  if (nbodies == 0) return;

  int nbeg = nbodies*id/nthrds;
  int nend = nbodies*(id+1)/nthrds;

  use[id] = 0;

  for (int n=nbeg; n<nend; n++) {

    double xx   = (*p)[n].pos[0];
    double yy   = (*p)[n].pos[1];
    double zz   = (*p)[n].pos[2];
    double mass = (*p)[n].mass;

    double r2 = (xx*xx + yy*yy + zz*zz);
    double  r = sqrt(r2) + MINDOUBLE;

    if (r<=RMAX) {

      use[id]++;

      double costh = zz/r;
      double   phi = atan2(yy,xx);
      
      legendre_R(LMAX, costh, legs[id]);
      sinecosine_R(LMAX, phi, cosm[id], sinm[id]);

      ortho->get_pot(potd[id], r);

      // l loop
      for (int l=0, loffset=0; l<=LMAX; loffset+=(2*l+1), l++) {
	// m loop
	for (int m=0, moffset=0; m<=l; m++) {
	  if (m==0) {
	    if (selector && compute)
	      facs1 = legs[id][l][m]*legs[id][l][m]*mass;
	    for (int n=1; n<=NMAX; n++) {
	      expcoef1[id][loffset+moffset][n] += potd[id][l][n]*legs[id][l][m]*mass*
		fac0/normM[l][n];

	      if (selector && compute) {
		for (int nn=n; nn<=NMAX; nn++)
		  cc1[id][loffset+moffset][n][nn] += potd[id][l][n]*potd[id][l][nn]*
		    facs1/(normM[l][n]*normM[l][nn]);
	      }
	    }
	    moffset++;
	  }
	  else {
	    double fac1 = legs[id][l][m]*cosm[id][m];
	    double fac2 = legs[id][l][m]*sinm[id][m];
	    if (selector && compute) {
	      facs1 = fac1*fac1*mass;
	      facs2 = fac2*fac2*mass;
	    }
	    for (int n=1; n<=NMAX; n++) {
	      expcoef1[id][loffset+moffset][n] += potd[id][l][n]*fac1*mass*
		fac0/normM[l][n];

	      expcoef1[id][loffset+moffset+1][n] += potd[id][l][n]*fac2*mass*
		fac0/normM[l][n];

	      if (selector && compute) {
		for (int nn=n; nn<=NMAX; nn++) {
		  cc1[id][loffset+moffset][n][nn] += 
		    potd[id][l][n]*potd[id][l][nn]*facs1/(normM[l][n]*normM[l][nn]);
		  cc1[id][loffset+moffset+1][n][nn] +=
		    potd[id][l][n]*potd[id][l][nn]*facs2/(normM[l][n]*normM[l][nn]);
		}
	      }
		
	    }
	    moffset+=2;
	  }
	}
      }
    }
  }

}


void SphericalSL::accumulate(vector<Particle> &part)
{
  static int firstime=1;
  
  used = 0;

  if (selector) compute = firstime;

  // Clean
  for (int n=1; n<=NMAX; n++) {
    for (int l=0; l<=LMAX*(LMAX+2); l++) {
      expcoef[l][n] = 0.0;
    }
  }

  for (auto & v : expcoef1) {
    for (int l=0; l<=LMAX*(LMAX+2); l++)
      for (int n=1; n<=NMAX; n++) v[l][n] = 0.0;
  }
  
  for (auto & v : use) v = 0;

  if (selector && compute) {
    for (auto & v : cc1) {
      for (int l=0; l<=LMAX*(LMAX+2); l++) {
	for (int n=1; n<=NMAX; n++) {
	  for (int nn=n; nn<=NMAX; nn++) v[l][n][nn] = 0.0;
	}
      }
    }
  }

  if (nthrds>1)
    compute_coefficients_thread(part);
  else
    compute_coefficients_single(part);

  int use0 = 0;

  MPI_Allreduce ( &use[0], &use0,  1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
  if (myid==0) used += use0;

  if (!selector) {

    // l loop
    for (int l=0, loffset=0; l<=LMAX; loffset+=(2*l+1), l++) {

      // m loop
      for (int m=0, moffset=0; m<=l; m++) {
	if (m==0) {
	  
	  MPI_Allreduce ( &expcoef1[0][loffset+moffset][1],
			  &expcoef[loffset+moffset][1],
			  NMAX, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);

	    moffset++;
	  }
	  else {

	    MPI_Allreduce ( &expcoef1[0][loffset+moffset][1],
			    &expcoef[loffset+moffset][1],
			    NMAX, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
	    
	    MPI_Allreduce ( &expcoef1[0][loffset+moffset+1][1],
			    &expcoef[loffset+moffset+1][1],
			    NMAX, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
	    moffset+=2;
	  }
      }
    }
  }
 
  if (selector) {
    
    parallel_gather_coefficients(expcoef, expcoef1[0], cc, cc1[0], LMAX);

    if (myid == 0) pca_hall(compute, expcoef, cc, normM);
    
    parallel_distribute_coefficients(expcoef, LMAX);

    if (firstime) {
      compute = 0;
      firstime = 0;
    }
  }

#ifdef DEBUG
				// Check coefficients
  int iflg = 0;
  
  for (int n=1; n<=NMAX; n++) {
    for (int l=0; l<=LMAX*(LMAX+2); l++) {
      if (std::isnan(expcoef[l][n])) {
	std::cerr << "expcoef[" << l << "][" << n << "] is NaN"
		  << std::endl;
	iflg++;
      }
    }
  }

  if (iflg) {
    std:: cerr << iflg << " NaNs" << std::endl;
    MPI_Finalize();
    exit(-11);
  }
#endif

}


void SphericalSL::determine_fields_at_point
(
 double r, double theta, double phi,
 double *tdens, double *tpotl, double *tpotr, double *tpott, double *tpotp,
 int id
 )
{
  double dfac=0.25/M_PI;

  double costh = cos(theta);

  double fac1 = dfac;

  dlegendre_R(LMAX, costh, legs[id], dlegs[id]);
  sinecosine_R(LMAX, phi, cosm[id], sinm[id]);

				// For exterior solution
  double pfext1 = 1.0;
  double pfext2 = 1.0;
  double r1 = r;

  if (r>RMAX) {
    pfext1 = RMAX/r;
    pfext2 = pfext1;
    r1 = RMAX;
  }

  ortho->get_dens(dend[id], r);
  ortho->get_pot(potd[id], r1);
  ortho->get_force(dpot[id], r1);

  double dens;
  get_dens_coefs(0, expcoef[0], &dens);
  dens *= dfac*dfac;

  double p, dp;
  get_pot_coefs(0, expcoef[0], &p, &dp);

  double potl = fac1*p * pfext2;
  double potr = fac1*dp * pfext2*pfext1;
  double pott = 0.0, potp = 0.0;
      
  // l loop
    
  for (int l=1, loffset=1; l<=LMAX; loffset+=(2*l+1), l++) {
    
    // m loop
    for (int m=0, moffset=0; m<=l; m++) {
      double fac1 = (2.0*l+1.0)/(4.0*M_PI);
      if (m==0) {
	double fac2 = fac1*legs[0][l][m];
	get_dens_coefs(l,expcoef[loffset+moffset],&p);
	dens += dfac*fac2*p;
	get_pot_coefs(l,expcoef[loffset+moffset],&p,&dp);

				// External solution
	p *= pfext2;
	dp *= pfext2*pfext1;

	potl += fac2*p;
	potr += fac2*dp;
	pott += fac1*dlegs[0][l][m]*p;
	moffset++;
      }
      else {
	double fac2 = 2.0 * fac1 * factorial[l][m];
	double fac3 = fac2 * legs[0][l][m];
	double fac4 = fac2 * dlegs[0][l][m];
	double pc, ps, dpc, dps;
	
	get_dens_coefs(l, expcoef[loffset+moffset], &pc);
	get_dens_coefs(l, expcoef[loffset+moffset+1], &ps);
	dens += dfac*fac3*(pc*cosm[id][m] + ps*sinm[id][m]);
	
	get_pot_coefs(l,expcoef[loffset+moffset], &pc, &dpc);
	get_pot_coefs(l,expcoef[loffset+moffset+1], &ps, &dps);

				// External solution
	pc  *= pfext2;
	dpc *= pfext2*pfext1;
	ps  *= pfext2;
	dps *= pfext2*pfext1;

	potl += fac3*( pc*cosm[id][m] +  ps*sinm[id][m]);
	potr += fac3*(dpc*cosm[id][m] + dps*sinm[id][m]);
	pott += fac4*( pc*cosm[id][m] +  ps*sinm[id][m]);
	potp += fac3*(-pc*sinm[id][m] +  ps*cosm[id][m])*m;

	moffset +=2;
      }
    }
  }

  *tdens = dens;
  *tpotl = potl;
  *tpotr = potr;
  *tpott = pott;
  *tpotp = potp;
  
}


void SphericalSL::get_pot_coefs(int l, Vector& coef,
				double *p, double *dp, int id)
{
  double pp=0.0, dpp=0.0;

  for (int i=1; i<=NMAX; i++) {
    pp  += potd[id][l][i] * coef[i];
    dpp += dpot[id][l][i] * coef[i];
  }

  *p = -pp;
  *dp = -dpp;
}

void SphericalSL::get_dens_coefs(int l, Vector& coef, double *p, int id)
{
  double pp = 0.0;

  for (int i=1; i<=NMAX; i++)
    pp  += dend[id][l][i] * coef[i];

  *p = pp;
}
				/* Dump coefficients to a file */

void SphericalSL::dump_coefs(ofstream& out, bool binary)
{
  double tnow = 0.0;

  if (binary) {

    out.write((char *)&tnow, sizeof(double));

    for (int ir=1; ir<=NMAX; ir++) {
      for (int l=0; l<=LMAX*(LMAX+2); l++)
	out.write((char *)&expcoef[l][ir], sizeof(double));
    }
  }
  else {

    // BEGIN: header
    out << "# n |" << std::right;

    for (int l=0, loffset=0; l<=LMAX; loffset+=(2*l+1), l++) {
      for (int m=0, moffset=0; m<=l; m++) {
	if (m==0) {
	  std::ostringstream sout;
	  sout << "(" << l << " " << m << "c ) |";
	  out << std::setw(18) << sout.str();
	  moffset++;
	} else {
	  std::ostringstream soutC, soutS;
	  soutC << "(" << l << " " << m << "c ) |";
	  soutS << "(" << l << " " << m << "s ) |";
	  out << std::setw(18) << soutC.str()
	      << std::setw(18) << soutS.str();
	  moffset += 2;
	}
      }
    }
    out << std::endl;

    out << "#[1]|" << std::right;

    int cnt = 2;
    for (int l=0, loffset=0; l<=LMAX; loffset+=(2*l+1), l++) {
      // m loop
      //
      for (int m=0, moffset=0; m<=l; m++) {
	if (m==0) {
	  std::ostringstream sout;
	  sout << "[" << cnt++ << "] |";
	  out << std::setw(18) << sout.str();
	  moffset++;
	} else {
	  std::ostringstream soutC, soutS;
	  soutC << "[" << cnt++ << "] |";
	  soutS << "[" << cnt++ << "] |";
	  out << std::setw(18) << soutC.str()
	      << std::setw(18) << soutS.str();
	  moffset += 2;
	}
      }
    }
    out << std::endl;

    out << "#---+" << std::right << std::setfill('-');

    for (int l=0, loffset=0; l<=LMAX; loffset+=(2*l+1), l++) {
      for (int m=0, moffset=0; m<=l; m++) {
	if (m==0) {
	  out << std::setw(18) << "+";
	  moffset++;
	} else {
	  out << std::setw(18) << "+"
	      << std::setw(18) << "+";
	  moffset += 2;
	}
      }
    }
    out << std::endl << std::setfill(' ');

    // END: header

    // n loop
    //
    for (int n=1; n<=NMAX; n++) {
      out << std::setw(5) << n;
      // l loop
      //
      for (int l=0, loffset=0; l<=LMAX; loffset+=(2*l+1), l++) {
	// m loop
	//
	for (int m=0, moffset=0; m<=l; m++) {
	  if (m==0) {
	    out << std::setw(18) << expcoef[loffset+moffset][n];
	    moffset++;
	  } else {
	    out << std::setw(18) << expcoef[loffset+moffset+0][n]
		<< std::setw(18) << expcoef[loffset+moffset+1][n];
	    moffset += 2;
	  }
	}
      }
      out << std::endl;
    }
  }
}

void SphericalSL::parallel_gather_coefficients
(
 Matrix& expcoef, Matrix& expcoef1,
 Matrix*& cc, Matrix*& cc1,
 int lmax)
{
  int Ldim = lmax*(lmax + 2) + 1;
  int L0 = 0;
  
  if (myid == 0) {

    for (int l=L0, loffset=0; l<=lmax; loffset+=(2*l+1), l++) {

      for (int m=0, moffset=0; m<=l; m++) {

	if (m==0) {
	  for (int n=1; n<=NMAX; ++n) {
	    expcoef[loffset+moffset][n] = 0.0;

	    for (int nn=n; nn<=NMAX; nn++)
	      cc[loffset+moffset][n][nn] = 0.0;
	  }
	  moffset++;
	}
	else {
	  for (int n=1; n<=NMAX; ++n) {
	    expcoef[loffset+moffset][n] = 0.0;
	    expcoef[loffset+moffset+1][n] = 0.0;

	    for (int nn=n; nn<=NMAX; nn++) {
	      cc[loffset+moffset][n][nn] = 0.0;
	      cc[loffset+moffset+1][n][nn] = 0.0;
	    }
	  }
	  moffset+=2;
	}
      }
    }
  }


  for (int l=L0, loffset=0; l<=lmax; loffset+=(2*l+1), l++) {

    for (int m=0, moffset=0; m<=l; m++) {

      if (m==0) {
	MPI_Reduce(&expcoef1[loffset+moffset][1], 
		   &expcoef[loffset+moffset][1], NMAX, 
		   MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

	for (n=1; n<=NMAX; n++)
	  MPI_Reduce(&cc1[loffset+moffset][n][n],
		     &cc[loffset+moffset][n][n], NMAX-n+1, 
		     MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
	moffset++;
      }
      else {
	MPI_Reduce(&expcoef1[loffset+moffset][1], 
		   &expcoef[loffset+moffset][1], NMAX, 
		   MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

	MPI_Reduce(&expcoef1[loffset+moffset+1][1],
		   &expcoef[loffset+moffset+1][1], NMAX, 
		   MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
	
	if (selector) {
	  for (int n=1; n<=NMAX; n++) {
	    MPI_Reduce(&cc1[loffset+moffset][n][n],
		       &cc[loffset+moffset][n][n], NMAX-n+1, 
		       MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
	    MPI_Reduce(&cc1[loffset+moffset+1][n][n],
		       &cc[loffset+moffset+1][n][n], NMAX-n+1, 
		       MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
	  }
	}

	moffset+=2;
      }
    }
  }

}

void SphericalSL::parallel_distribute_coefficients(Matrix& expcoef, int lmax)
{
  int Ldim = lmax*(lmax + 2) + 1;
  int L0 = 0;

  for (int l=L0, loffset=0; l<=lmax; loffset+=(2*l+1), l++) {

      for (int m=0, moffset=0; m<=l; m++) {

	if (m==0) {
	  MPI_Bcast(&expcoef[loffset+moffset][1], NMAX, MPI_DOUBLE,
		    0, MPI_COMM_WORLD);
	  moffset++;
	}
	else {
	  MPI_Bcast(&expcoef[loffset+moffset][1], NMAX, MPI_DOUBLE,
		     0, MPI_COMM_WORLD);
	  MPI_Bcast(&expcoef[loffset+moffset+1][1], NMAX, MPI_DOUBLE,
		    0, MPI_COMM_WORLD);
	  moffset+=2;
	}
      }
  }

}

void SphericalSL::pca_hall
(int compute, Matrix& expcoef, Matrix*& cc, Matrix& normM)
{
  static Vector smth;
  static Vector *weight;
  static Vector *b_Hall;
  static Vector inv;
  static Vector eval;
  static Vector cuml;
  static Matrix *evec;
  static Matrix Tevec;
  static Matrix sqnorm;

  static Matrix covar;

  static int setup = 0;
  
  if (!setup) {

    int Ldim = LMAX*(LMAX + 2) + 1;
    
    weight = new Vector [Ldim];
    b_Hall = new Vector [Ldim];
    evec = new Matrix [Ldim];
    
    for (int l=0; l<Ldim; l++) {
      weight[l].setsize(1, NMAX);
      b_Hall[l].setsize(1, NMAX);
      evec[l].setsize(1, NMAX, 1, NMAX);
    }

    smth.setsize(1, NMAX);
    inv.setsize(1, NMAX);
    eval.setsize(1, NMAX);
    cuml.setsize(1, NMAX);
    Tevec.setsize(1, NMAX, 1, NMAX);
    covar.setsize(1, NMAX, 1, NMAX);
    sqnorm.setsize(0, LMAX, 1, NMAX);
      
    for (int l=0; l<=LMAX; l++)
      for (int n=1; n<=NMAX; n++) sqnorm[l][n] = sqrt(normM[l][n]);

    setup = 1;
  }


  int L0 = 0;
  double fac02 = 16.0*M_PI*M_PI;

  for (int l=L0, loffset=0; l<=LMAX; loffset+=(2*l+1), l++) {

    for (int m=0, moffset=0; m<=l; m++) {

      int lm = l;
      int indx = loffset+moffset;

      if (m==0) {

	if (compute) {

	  for (int n=1; n<=NMAX; n++) {
	    double b = (cc[indx][n][n]*fac02 - expcoef[indx][n]*expcoef[indx][n]) /
	      (expcoef[indx][n]*expcoef[indx][n]*used);
	    b_Hall[indx][n] = 1.0/(1.0 + b);
	  }
    
	  for (int n=1; n<=NMAX; n++) {
	    for (int nn=n; nn<=NMAX; nn++) {
	      double fac = sqnorm[lm][n]*sqnorm[lm][nn];
	      covar[n][nn] = fac * expcoef[indx][n]*expcoef[indx][nn];
	      if (n!=nn)
		covar[nn][n] = covar[n][nn];
	    }    
	  }

				/* Diagonalize variance */

#ifdef GHQL
	  eval = covar.Symmetric_Eigenvalues_GHQL(evec[indx]);
#else
	  eval = covar.Symmetric_Eigenvalues(evec[indx]);
#endif
	  Tevec = evec[indx].Transpose();

	  if (tk_type == 2) {
	    cuml = eval;
	    for (int n=2; n<=NMAX; n++) cuml[n] += cuml[n-1];
	    double var = cuml[NMAX];
	    for (int n=1; n<=NMAX; n++) cuml[n] /= var;
	  }

	  for (int n=1; n<=NMAX; n++) {

	    double dd = 0.0;
	    for (int nn=1; nn<=NMAX; nn++) 
	      dd += Tevec[n][nn]*expcoef[indx][nn]*sqnorm[lm][nn];

	    double var = eval[n]/used - dd*dd;

	    if (tk_type == 1) {

	      if (tksmooth*var > dd*dd)
		weight[indx][n] = 0.0;
	      else
		weight[indx][n] = 1.0;

	    }
	    else if (tk_type == 2) {
	      
	      if (n==1 || cuml[n] <= tkcum)
		weight[indx][n] = 1.0;
	      else
		weight[indx][n] = 0.0;
		
	    }
	    else if (tk_type == 3) {
	      
	      weight[indx][n] = 1.0/(1.0 + var/(dd*dd + 1.0e-14));
		
	    }
	    else
		weight[indx][n] = 1.0;

	    smth[n] = dd * weight[indx][n];
	  }

	}
	else {
	  Tevec = evec[indx].Transpose();
	  for (n=1; n<=NMAX; n++) {
	    double dd = 0.0;
	    for (int nn=1; nn<=NMAX; nn++) 
	      dd += Tevec[n][nn]*expcoef[indx][nn] * sqnorm[lm][nn];
	    smth[n] = dd * weight[indx][n];
	  }
	}
	    
	inv = evec[indx]*smth;
	for (n=1; n<=NMAX; n++) {
	  expcoef[indx][n] = inv[n]/sqnorm[lm][n];
	  if (tk_type == 0) expcoef[indx][n] *= b_Hall[indx][n];
	}
	
	moffset++;
      }
      else {

	if (compute) {

	  for (int n=1; n<=NMAX; n++) {
	    double b = (cc[indx][n][n]*fac02 - expcoef[indx][n]*expcoef[indx][n]) /
	      (expcoef[indx][n]*expcoef[indx][n]*used);
	    b_Hall[indx][n] = 1.0/(1.0 + b);
	  }
    
	  for (int n=1; n<=NMAX; n++) {
	    for (int nn=n; nn<=NMAX; nn++) {
	      double fac = sqnorm[lm][n] * sqnorm[lm][nn];
	      covar[n][nn] = fac * expcoef[indx][n]*expcoef[indx][nn];
	      if (n!=nn)
		covar[nn][n] = covar[n][nn];
	    }
	  }  

				/* Diagonalize variance */

#ifdef GHQL
	  eval = covar.Symmetric_Eigenvalues_GHQL(evec[indx]);
#else
	  eval = covar.Symmetric_Eigenvalues(evec[indx]);
#endif
	  Tevec = evec[indx].Transpose();

	  if (tk_type == 2) {
	    cuml = eval;
	    for (n=2; n<=NMAX; n++) cuml[n] += cuml[n-1];
	    double var = cuml[NMAX];
	    for (n=1; n<=NMAX; n++) cuml[n] /= var;
	  }

	  for (int n=1; n<=NMAX; n++) {

	    double dd = 0.0;
	    for (int nn=1; nn<=NMAX; nn++) 
	      dd += Tevec[n][nn]*expcoef[indx][nn]*sqnorm[lm][nn];

	    double var = eval[n]/used - dd*dd;

	    if (tk_type == 1) {

	      if (tksmooth*var > dd*dd)
		weight[indx][n] = 0.0;
	      else
		weight[indx][n] = 1.0;

	    }
	    else if (tk_type == 2) {
	      
	      if (n==1 || cuml[n] <= tkcum)
		weight[indx][n] = 1.0;
	      else
		weight[indx][n] = 0.0;
		
	    }
	    else if (tk_type == 3) {
	      
	      weight[indx][n] = 1.0/(1.0 + var/(dd*dd + 1.0e-14));
		
	    }
	    else
		weight[indx][n] = 1.0;

	    smth[n] = dd * weight[indx][n];
	  }
	}
	else {
	  Tevec = evec[indx].Transpose();
	  for (int n=1; n<=NMAX; n++) {
	    double dd = 0.0;
	    for (int nn=1; nn<=NMAX; nn++) 
	      dd += Tevec[n][nn]*expcoef[indx][nn] * sqnorm[lm][nn];
	    smth[n] = dd * weight[indx][n];
	  }
	}
	    
	inv = evec[indx]*smth;
	for (n=1; n<=NMAX; n++) {
	  expcoef[indx][n] = inv[n]/sqnorm[lm][n];
	  if (tk_type == 0) expcoef[indx][n] *= b_Hall[indx][n];
	}
	
	indx++;

	if (compute) {

	  for (int n=1; n<=NMAX; n++) {
	    double b = (cc[indx][n][n]*fac02 - expcoef[indx][n]*expcoef[indx][n]) /
	      (expcoef[indx][n]*expcoef[indx][n]*used);
	    b_Hall[indx][n] = 1.0/(1.0 + b);
	  }
    
	  for (int n=1; n<=NMAX; n++) {
	    for (int nn=n; nn<=NMAX; nn++) {
	      double fac = sqnorm[lm][n] * sqnorm[lm][nn];
	      covar[n][nn] = fac * expcoef[indx][n]*expcoef[indx][nn];
	      if (n!=nn)
		covar[nn][n] = covar[n][nn];
	    }    
	  }

				/* Diagonalize variance */

#ifdef GHQL
	  eval = covar.Symmetric_Eigenvalues_GHQL(evec[indx]);
#else
	  eval = covar.Symmetric_Eigenvalues(evec[indx]);
#endif
	  Tevec = evec[indx].Transpose();

	  if (tk_type == 2) {
	    cuml = eval;
	    for (int n=2; n<=NMAX; n++) cuml[n] += cuml[n-1];
	    double var = cuml[NMAX];
	    for (int n=1; n<=NMAX; n++) cuml[n] /= var;
	  }

	  for (int n=1; n<=NMAX; n++) {

	    double dd = 0.0;
	    for (int nn=1; nn<=NMAX; nn++) 
	      dd += Tevec[n][nn]*expcoef[indx][nn]*sqnorm[lm][nn];

	    double var = eval[n]/used - dd*dd;

	    if (tk_type == 1) {

	      if (tksmooth*var > dd*dd)
		weight[indx][n] = 0.0;
	      else
		weight[indx][n] = 1.0;

	    }
	    else if (tk_type == 2) {
	      
	      if (n==1 || cuml[n] <= tkcum)
		weight[indx][n] = 1.0;
	      else
		weight[indx][n] = 0.0;
		
	    }
	    else if (tk_type == 3) {
	      
	      weight[indx][n] = 1.0/(1.0 + var/(dd*dd + 1.0e-14));
		
	    }
	    else
		weight[indx][n] = 1.0;

	    smth[n] = dd * weight[indx][n];
	  }
	}
	else {
	  Tevec = evec[indx].Transpose();
	  for (int n=1; n<=NMAX; n++) {
	    double dd = 0.0;
	    for (int nn=1; nn<=NMAX; nn++) 
	      dd += Tevec[n][nn]*expcoef[indx][nn] * sqnorm[lm][nn];
	    smth[n] = dd * weight[indx][n];
	  }
	}

	inv = evec[indx]*smth;
	for (int n=1; n<=NMAX; n++) {
	  expcoef[indx][n] = inv[n]/sqnorm[lm][n];
	  if (tk_type == 0) expcoef[indx][n] *= b_Hall[indx][n];
	}
	
	moffset += 2;
      }
    }
  }

}


void SphericalSL::dump_basis(string& dumpname)
{
  static string labels ="pot.";
  
  double rmax = 0.33*RMAX;
  int numr = 400;
  double r, dr = rmax/numr;

  for (int L=0; L<=LMAX; L++) {
    
    ostringstream outs;
    outs << "sphbasis." << L << "." << dumpname.c_str() << '\0';

    ofstream out(outs.str().c_str());
    out.precision(3);
    out.setf(ios::scientific);

    for (int i=0; i<numr; i++) {
      r = dr*(0.5+i);

      out << setw(12) << r;
      for (int n=1; n<=min<int>(NMAX, 3); n++) {
	out
	  << setw(12) << ortho->get_pot(r, L, n, 1)
	  << setw(12) << ortho->get_dens(r, L, n, 1)
	  << setw(12) << ortho->get_force(r, L, n, 1);
      }
      out << endl;
    }
    out.close();
  }

}
    
