#ifndef _EmpOrth_h
#define _EmpOrth_h

#include <vector>

#include <gaussQ.h>
#include <math.h>
#include <values.h>

#ifdef USE_DMALLOC
#include <dmalloc.h>
#endif

#include <SLGridMP2.h>

#ifndef STANDALONE
#include <global.H>
#include "expand.h"

#if HAVE_LIBCUDA==1
#include <cudaParticle.cuH>
#include <cudaMappingConstants.cuH>
#endif

#else

#include "Particle.h"
extern int this_step;
extern int Mstep;
extern int mstep;
extern unsigned multistep;
extern vector<int> stepL, stepN;
extern pthread_mutex_t coef_lock;

#endif

//! Encapsulatates a SLGridSph (Sturm-Liouville basis) for use as force method
class EmpCylSL
{
public:

  typedef boost::shared_ptr<SphericalModelTable> SphModTblPtr;
  typedef boost::shared_ptr<Vector> VectorP;
  typedef boost::shared_ptr<Matrix> MatrixP;

private:
  int NMAX;
  int LMAX;
  int MMAX;
  int NORDER;
  int NKEEP;

  int hallfreq, hallcount;
  unsigned nbodstot;
  string hallfile;

  double YMIN, YMAX;
  double dX, dY;
  int M;
  vector<double> cylmass1;
  bool cylmass_made;
  double cylmass;

  vector<double> r, d, m, p;

  double ASCALE;
  double HSCALE;
  double pfac, dfac, ffac;

  Matrix *facC, *facS;
  
  int rank2, rank3;

  double *MPIin, *MPIout;
  double *MPIin_eof, *MPIout_eof;
  double *mpi_double_buf2, *mpi_double_buf3;
  int MPIbufsz, MPItable;
  bool MPIset, MPIset_eof;
  MPI_Status status;

  //@{
  //! All of this should be rewritten more safely, at some point, sorry.
  double**** SC;
  double**** SS;
  //@}

  Matrix *var;

  Vector ev;
  Matrix ef;
  Matrix potd, dpot, dend;
  Vector *cosm, *sinm;
  Matrix *legs, *dlegs;

  SLGridSph *ortho;

  double Rtable, XMIN, XMAX;

  Matrix** potC;
  Matrix** densC;
  Matrix** rforceC;
  Matrix** zforceC;

  Matrix** potS;
  Matrix** densS;
  Matrix** rforceS;
  Matrix** zforceS;

  Matrix* table;

  Matrix* tpot;
  Matrix* tdens;
  Matrix* trforce;
  Matrix* tzforce;

  vector<Vector**> accum_cosL, accum_cosN;
  vector<Vector**> accum_sinL, accum_sinN;
  vector< vector<unsigned> > howmany1;
  vector<unsigned> howmany;
  vector<unsigned> dstepL, dstepN;

  Vector* accum_cos;
  Vector* accum_sin;

  std::vector< std::vector<MatrixP> > accum_cos2;
  std::vector< std::vector<MatrixP> > accum_sin2;
  std::vector< std::vector<double>  > massT1;
  std::vector<double> massT;
  unsigned sampT;


  Matrix *vc, *vs;

  Matrix tabp, tabf, tabd;

  Vector* hold;

  std::vector<short> coefs_made;
  bool eof_made;

  SphModTblPtr make_sl();

  void make_grid();
  void send_eof_grid();
  void receive_eof     (int request_id, int m);
  void compute_eof_grid(int request_id, int m);
  void setup_eof_grid(void);

				// 1=write, 0=read
				// return: 0=failure
  int    cache_grid(int, string file="");		
  double integral(int, int, int, int);
  void   get_pot(Matrix&, Matrix&, double, double);
  void   pca_hall(void);
  double massR(double R);
  double densR(double R);

  //! PCA basis structure for caching and diagnostics
  class PCAbasis
  {
  public:

    //! Data for each harmonic subspace
    class PCAelement
    {
    public:
      //@{
      //! All the public data
      Vector evalJK;
      Vector meanJK;
      Vector coefJK;
      Vector b_Hall;
      Matrix covrJK;
      Matrix evecJK;
      //@}
      
      //! Constructor
      PCAelement(int n)
      {
	meanJK.setsize(1, n);
	coefJK.setsize(1, n);
	b_Hall.setsize(1, n);
	covrJK.setsize(1, n, 1, n);
	evecJK.setsize(1, n, 1, n);
      }
      
      //! Zero all data
      void reset()
      {
	meanJK.zero();
	coefJK.zero();
	b_Hall.zero();
	covrJK.zero();
	evecJK.zero();
      }
      
    };

    typedef boost::shared_ptr<PCAelement> PCAelemPtr;

    //! The cosine and sine spaces
    std::map<int, PCAelemPtr> C, S;

    //! Mass in the accumulation
    double Tmass;

    //! Constructor
    PCAbasis(int M, int n)
    {
      for (int m=0; m<=M; m++) {
	C[m] = PCAelemPtr(new PCAelement(n));
	if (m) S[m] = PCAelemPtr(new PCAelement(n));
      }
      reset();
    }

    //! Reset all variables to zero for accumulation
    void reset()
    {
      for (auto v : C) v.second->reset();
      for (auto v : S) v.second->reset();
      Tmass = 0.0;
    }

  };

  typedef boost::shared_ptr<PCAbasis> PCAbasisPtr;

  //! Cache PCA information between calls
  PCAbasisPtr pb;

  pthread_mutex_t used_lock;

  //! Thread body for coef accumulation
  void accumulate_thread_call(int id, std::vector<Particle>* p, int mlevel, bool verbose);

  //! Thread body for eof accumulation
  void accumulate_eof_thread_call(int id, std::vector<Particle>* p, bool verbose);


  //! Suppress odd modes
  bool EVEN_M;

public:

  /*! Enum listing the possible selection algorithms for coefficient
    selection */
  enum TKType {
    Hall,             /*!< Tapered signal-to-noise power defined by Hall   */
    Null              /*!< Compute the S/N but do not modify coefficients  */
  };

  //! Type of density model to use
  enum EmpModel {
    Exponential,
    Gaussian, 
    Plummer
  };

  //! TRUE if density is computed
  static bool DENS;

  //! TRUE if signal-to-noise methods are on
  static bool SELECT;

  //! TRUE if VTK diagnostics are on
  static bool PCAVTK;

  //! VTK diagnostic frequency
  static unsigned VTKFRQ;

  //! TRUE if we are using coordinate mapping
  static bool CMAP;

  //! TRUE if mapping is logarithmic
  static bool logarithmic;

  //! No extrapolating beyond grid (default: false)
  static bool enforce_limits;

  //! Density model type
  static EmpModel mtype;
  
  //! Radial basis grid in radial direction
  static int NUMX;
 
  //! Radial basis grid in vertical direction
  static int NUMY;

  //! Number of bases to print (for debug)
  static int NOUT;

  //! Number of entries in radial basis table
  static int NUMR;

  //! Selector output freq (this only affects diagnostic output).
  //! Current default is to perform Hall on every step when selected
  static int HALLFREQ;

  //! Minimum radial value for basis
  static double RMIN;

  //! Maximum radial value for basis
  static double RMAX;

  //! Name of cache file
  static string CACHEFILE;

  //! Fraction of table range for basis images (for debug)
  static double HFAC;

  /** Verbose level flags
      bit   Action
      ---   ------
      0   = quiet
      1   = SL model output
      2   = EOF diagnostics
      4   = Accumulation, PCA Hall, and all other diagnostics
      8   = Debug-level communication details
      16  = Timing info
  */
  static unsigned VFLAG;		// Default=0


  //! Constructor (reset must called later)
  EmpCylSL(void);

  //! Constructor with parameters
  EmpCylSL(int numr, int lmax, int mmax, int nord,
	   double ascale, double hscale);

  //! Destructor
  ~EmpCylSL(void);

  //! Reconstruct basis with new parameters
  void reset(int numr, int lmax, int mmax, int nord,
	     double ascale, double hscale);

  //! Read EOF basis header from saved file
  int read_eof_header(const string& eof_file);

  //! Read EOF basis from saved file
  int read_eof_file(const string& eof_file);

  //! Dump the EOF basis in ascii format
  void dump_eof_file(const string& eof_file, const string& dump_file);

  //! Read basis from cache file
  int read_cache(void);

  //! Parameter access: get norder
  int get_order(void) {return NORDER;}

				// Z coordinate transformation

  //! Compute non-dimensional vertical coordinate from Z
  double z_to_y(double z) { return z/(fabs(z)+DBL_MIN)*asinh(fabs(z/HSCALE)); }

  //! Compute Z from non-dimensional vertical coordinate
  double y_to_z(double y) { return HSCALE*sinh(y); }

  //! For measure transformation
  double d_y_to_z(double y) { return HSCALE*cosh(y); }

  /** Generate EOF by direct integration conditioned on a user
      supplied function
   */
  void generate_eof(int numr, int nump, int numt, 
		    double (*func)(double R, double z, double phi, int M) );

  //! Get basis function value
  void get_all(int m, int n, double r, double z, double phi,
	       double& p, double& d, double& fr, double& fz, double& fp);

  //! Setup for accumulated coefficients
  //@{
  //! All levels
  void setup_accumulation(void);
  //! Single level
  void setup_accumulation(int mlevel);
  //! For EOF
  void setup_eof(void);
  //! Clear mass counter
  void reset_mass(void);
  //@}

  //! Make coefficients from accumulated data
  //@{
  //! All levels
  void make_coefficients(void);
  //! Single level
  void make_coefficients(unsigned mlevel);
  //! Make empirical orthgonal functions
  void make_eof(void);
  //! True if coefficients are made at all levels
  bool coefs_made_all() 
  {
    for (unsigned M=0; M<=multistep; M++) 
      if (!coefs_made[M]) return false;
    return true;
  }
  //@}

  //! Initialize PCA work space
  void init_pca();

  //! Necessary member function currently unused (change design?)
  void determine_coefficients() {};
  //! Necessary member function currently unused (change design?)
  void determine_acceleration_and_potential() {};

  //! Accumulate coefficients from particle distribution
  void accumulate(vector<Particle>& p, int mlev=0, bool verbose=false);

  //! Accumulate coefficients from particle distribution by thread.
  //! Used by external appliations.
  void accumulate_thread(vector<Particle>& p, int mlev=0, bool verbose=false);

  //! Make EOF from particle distribution
  void accumulate_eof(vector<Particle>& p, bool verbose=false);

  //! Make EOF from particle distribution by thread.  Used by external
  //! applications.
  void accumulate_eof_thread(vector<Particle>& p, bool verbose=false);

  //! Add single particle to coefficients
  void accumulate(double r, double z, double phi, double mass,
		  unsigned long seq, int id, int mlev=0);

  //! Add single particle to EOF coefficients
  void accumulate_eof(double r, double z, double phi, double mass, int id, int mlev=0);


  //! Evaluate potential and force field 
  void accumulated_eval(double r, double z, double phi, double& p0,
			double& p, double& fr, double& fz, double& fp);

  //! Evaluate density field
  double accumulated_dens_eval(double r, double z, double phi, double& d0);

  /** Extrapolate and sum coefficents per multistep level to get
      a complete set of coefficients for force evaluation at an
      intermediate time step
  */
  void compute_multistep_coefficients(unsigned mlevel);

  //! For updating levels
  //@{
  vector< vector<Matrix> > differS1, differC1;
  vector<double> workC1, workC, workS1, workS;
  //@}

  /** Update the multi time step coefficient table when moving particle 
      <code>i</code> from level <code>cur</code> to level 
      <code>next</code>
  */
  //@{
  virtual void multistep_update_begin();
  virtual void multistep_update(int from, int to, double r, double z, double phi, double mass, int id);
  virtual void multistep_update_finish();
  virtual void multistep_reset();
  //@}

  //! Print debug info
  void multistep_debug();

  //! Set coefficients from input stream
  void set_coefs(int mm, const Vector& cos1, const Vector& sin1, bool zero);

  //! Dump out coefficients to output stream
  void dump_coefs(std::ostream& out);

  //! Dump out coefficients to stream in bianry format
  void dump_coefs_binary(std::ostream& out, double time);

  //! Plot basis
  void dump_basis(const string& name, int step, double Rmax=-1.0);

  //! Plot full fields for debugging
  void dump_images(const string& OUTFILE,
		   double XYOUT, double ZOUT, int OUTR, int OUTZ,
		   bool logscale);

  //! Plot basis images for debugging
  void dump_images_basis(const string& OUTFILE,
			 double XYOUT, double ZOUT, 
			 int OUTR, int OUTZ, bool logscale,
			 int M1, int M2, int N1, int N2);

  //! Plot PCA basis images for debugging
  void dump_images_basis_pca(const string& runtag,
			     double XYOUT, double ZOUT, 
			     int OUTR, int OUTZ, int M, int N, int cnt);

  //! Restrict order
  void restrict_order(int n);

  /** @name Utility functions */
  // @{

  //! Compute Associated Legendre Polynomials, return Matrix type
  void legendre_R(int lmax, double x, Matrix& p);
  /** Compute Associated Legendre Polynomials and derivitives, 
      return Matrix type */
  void dlegendre_R(int lmax, double x, Matrix &p, Matrix &dp);
  //! Compute vectors of sines and cosines by recursion
  void sinecosine_R(int mmax, double phi, Vector& c, Vector& s);

  // @}

  //! Convert from non-dimensional to dimensional radial coordinate
  double xi_to_r(double);

  //! Convert from dimension to non-dimension radial coordinate
  double r_to_xi(double);

  //! Jacobian
  double d_xi_to_r(double);

  //! Return current value of disk scale length
  double get_ascale(void) { return ASCALE; }

  //! Return current value of disk scale height
  double get_hscale(void) { return HSCALE; }

  //! Set even modes only
  void setEven()          { EVEN_M = true; }

  //! Set frequency and file name for selector output
  inline void setHall(string file, unsigned tot, int n=50)
  {
    hallfile = file;
    nbodstot = tot;
    hallfreq = n;
    init_pca();
  }

  //! Set frequency and file name for selector output
  inline void setTotal(unsigned tot) {
    nbodstot = tot;
  }

  void setTK(const std::string& tk)
  {
    if      (tk == "Hall") tk_type = Hall;
    else if (tk == "Null") tk_type = Null;
    else {
      if (myid==0) {
	cout << "EmpCylSL: no such TK type <" << tk << ">"
	     << " using Null type\n";
      }
    }
  }

  vector<double> sanity() { 
    vector<double> ret;
    for (int m=0; m<=MMAX; m++) ret.push_back(accum_cos[0][m]);
    return ret;
  }

#ifndef STANDALONE
#if HAVE_LIBCUDA==1
  cudaMappingConstants getCudaMappingConstants();

  void initialize_cuda(std::vector<cudaArray_t>& cuArray,
		       thrust::host_vector<cudaTextureObject_t>& tex);

  double get_coef(int m, int n, char c)
  {
    if (m >  MMAX)
      throw std::runtime_error("m>mmax");

    if (n >= rank3)
      throw std::runtime_error("n>=norder");

    if (c == 'c')
      return accum_cos[m][n];
    else
      return accum_sin[m][n];
  }

  double& set_coef(int mlevel, int m, int n, char c)
  {
    if (m >  MMAX)
      throw std::runtime_error("m>mmax");

    if (n >= rank3)
      throw std::runtime_error("n>=norder");

    if (c == 'c')
      return accum_cosN[mlevel][0][m][n];
    else
      return accum_sinN[mlevel][0][m][n];
  }

#endif
#endif

private:
  TKType tk_type;

};

extern void legendre_R(int lmax, double x, Matrix& p);
extern void dlegendre_R(int lmax, double x, Matrix &p, Matrix &dp);
extern void sinecosine_R(int mmax, double phi, Vector& c, Vector& s);


#endif

// -*- C++ -*-

