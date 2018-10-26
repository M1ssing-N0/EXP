#include <algorithm>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <random>
#include <cfloat>
#include <cmath>
#include <ctime>
#include <tuple>
#include <map>

#include <boost/filesystem.hpp>

#include "global.H"
#include "TreeDSMC.H"
#include "CollideIon.H"
#include "localmpi.h"
#include "Species.H"
#include "Configuration.H"
#include "InitContainer.H"

// Version info
//
#define NAME_ID    "CollideIon"
#define VERSION_ID "0.35 [08/01/18 Mean Mass test]"

using namespace std;
using namespace NTC;

double   CollideIon::Nmin       = 1.0e-08;
double   CollideIon::Nmax       = 1.0e+25;
double   CollideIon::Tmin       = 1.0e+03;
double   CollideIon::Tmax       = 1.0e+08;
unsigned CollideIon::Nnum       = 400;
unsigned CollideIon::Tnum       = 200;
string   CollideIon::cache      = ".HeatCool";
bool     CollideIon::equiptn    = false;
bool     CollideIon::scatter    = false;
bool     CollideIon::ExactE     = false;
bool     CollideIon::NoExact    = true;
bool     CollideIon::AlgOrth    = false;
bool     CollideIon::AlgWght    = false;
bool     CollideIon::MeanMass   = false; // Mean-mass algorithm
bool     CollideIon::DebugE     = false;
bool     CollideIon::collLim    = false;
bool     CollideIon::collCor    = false;
unsigned CollideIon::maxSel     = 1000;
bool     CollideIon::E_split    = false; // Deferred energy in Hybrid and Trace
bool     CollideIon::distDiag   = false;
bool     CollideIon::elecDist   = false;
bool     CollideIon::rcmbDist   = false;
bool     CollideIon::rcmbDlog   = true ; // Log scale histogram by default
bool     CollideIon::ntcDist    = false;
bool     CollideIon::enforceMOM = false;
bool     CollideIon::coulScale  = false;
double   CollideIon::coulPow    = 2.0;
unsigned CollideIon::esNum      = 100;
double   CollideIon::esThr      = 0.0;
double   CollideIon::ESthresh   = 1.0e-10;
unsigned CollideIon::NoDelC     = 0;
unsigned CollideIon::maxCoul    = UINT_MAX;
double   CollideIon::logL       = 5.0/(16.0*M_PI); // energy transfer factor
bool     CollideIon::TSESUM     = true;
bool     CollideIon::coulInter  = true;
double   CollideIon::TSCOOL     = 0.05;
double   CollideIon::TSFLOOR    = 0.001;
double   CollideIon::scatFac1   = 1.0; // Hybrid algorithm
double   CollideIon::scatFac2   = 1.0; // Hybrid algorithm
double   CollideIon::tolE       = 1.0e-05;
double   CollideIon::tolCS      = 1.0;

// The recommended value for qCrit is now -1 (that is, turn it off).
// It appears to be unstable based on the TestEquil tests.
double   CollideIon::qCrit      = -1.0;

string   CollideIon::config0    = "CollideIon.config";

bool CollideIon::ElectronEPSM   = false;
CollideIon::ElectronScatter
CollideIon::esType              = CollideIon::always;

CollideIon::esMapType CollideIon::esMap = { {"none",      none},
					    {"always",    always},
					    {"classical", classical},
					    {"limited",   limited},
					    {"fixed",     fixed} };

CollideIon::phMapType CollideIon::phMap = { {"Particle",  perParticle},
					    {"Collision", perCollision} };

// For diagnostic labeling of photoionization algorithm
//
CollideIon::phLabMap CollideIon::phLab = { {perParticle,  "per particle"},
					   {perCollision, "per collision"} };

// For diagnostic labeling of HybridColl enum
//
std::map<unsigned, std::string> CollideIon::HClabel =
  { {0, "None"}, {1, "Ion1"}, {2, "Ion2"}, {4, "Neutral"}, {8, "Scatter"} };


Interact::T CollideIon::elecElec;

// Stash key position in particle attribute array
//
int CollideIon::Pord::key       = -1;

// Renormalized Trace species weights
//
bool
CollideIon::Pord::norm_enforce  = false;

// Add trace energy excess to electron distribution
//
static bool TRACE_ELEC          = false;

// Enable ion-electron secondary scattering where count is the value
//
static unsigned SECONDARY_SCATTER = 0;

// Fraction of excess energy loss to give to the electrons
//
static double TRACE_FRAC        = 1.0;

// Attempt to remove pending excess lost energy for equal and trace
// interactions
//
static bool ALWAYS_APPLY        = false;

// Print collisions by species for debugging
//
static bool COLL_SPECIES        = false;

// Same species tests (for debugging only)
//
static bool SAME_ELEC_SCAT      = false;
static bool DIFF_ELEC_SCAT      = false;
static bool SAME_IONS_SCAT      = false;
static bool SAME_INTERACT       = false;
static bool DIFF_INTERACT       = false;
static bool TRACE_OVERRIDE      = false;

// Test Coulombic cross section computed from mean KE
//
static bool MEAN_KE             = true;

// Suppress distribution of energy to electrons when using NOCOOL
//
static bool NOCOOL_ELEC         = false;

// Suppress distribution of ionization energy between electrons
//
static bool NOSHARE_ELEC        = false;

// Clone temperature of ionizing electron
//
static bool CLONE_ELEC          = false;

// Warn if energy lost is smaller than COM energy available.  For
// debugging.  Set to false for production.
//
static bool frost_warning       = false;

// Very verbose selection debugging. Set to false for production.
//
static bool DEBUG_SL            = false;

// Verbose cross-section debugging. Set to false for production.
//
static bool DEBUG_CR            = false;

// Verbose cross-section debugging for unequal species only. Set to
// false for production.
//
static bool DEBUG_NQ            = false;

// Artifically suppress electron equipartition speed
//
static bool NO_DOF              = true;

// Artifically suppress electron equilibrium velocity
//
static bool NO_VEL              = false;

// Artifically suppress energy loss due to ionization
//
static bool NO_ION_E            = false;

// Do not compute free-free cross section
//
static bool NO_FF               = false;

// Artifically suppress energy loss due to free-free
//
static bool NO_FF_E             = false;

// KE debugging: checks energy bookkeeping. Set to false for
// production
//
static bool KE_DEBUG            = true;

// For debugging only: suppress scattering in Trace method.  Set to
// false for production
//
static bool NO_HSCAT            = false;

// For debugging only: use for scatter only in Trace mehtod to verify
// constancy of masses and weights.  Set to false for production
//
static bool DBG_HSCAT           = false;

// Finalize cell debug diagnostics
//
static bool debugFC             = false;

// Tally ionization potential with energy loss during recombination
//
static bool RECOMB_IP           = false;

// Cross-section debugging; set to false for production
//
static bool CROSS_DBG           = false;

// Excess trace map debugging; set to false for production
//
static bool EXCESS_DBG          = false;

// Minimum energy for Rutherford scattering of ions used to estimate
// the elastic scattering cross section
//
static double FloorEv           = 0.05;

// Minimum relative fraction for allowing a collisional excitation
//
static double minCollFrac       = -1.0;

// Some deeper checks for debugging
//
static bool temp_debug          = false;

static bool scatter_check       = false;

static bool recomb_check        = false;

// Decrease the interacton probability by electron fraction used for
// dominant subspcies for the NTC rate
//
static bool suppress_maxT       = false;

// Use only a fraction of inelastic energy for testing equipartition
//
static double energy_scale      = -1.0;

// Use median energy/velocity value for computing cross section
//
static bool MEDIAN_E            = true;

// Use particle collision counter for debugging
//
static int DEBUG_CNT            = -1;

// Use mass weighting rather than number weigting for AlgWght
//
static bool ALG_WGHT_MASS       = false;

// Use mass weighting rather than number weigting for AlgWght
//
static double Fwght             = 0.5;

// Convert energy in eV to wavelength in angstroms
//
static constexpr double eVtoAng = 12398.41842144513;

// Recombine cross section computing using ion's electron
//
static bool newRecombAlg        = false;

// Use full trace algorithm for interaction fractions below threshold
//
static bool HybridWeightSwitch  = false;

// Debugging newHybrid
//
static bool DBG_NewTest         = false;

// This is for debugging; set to "false" for production
//
bool use_normtest = false;

// Test initial and final energy with NOCOOL set
//
const double testDE_tol         = 1.0e-7;

// Artificially suppress ion-ion scattering in Hybrid method
//
static bool NO_ION_ION          = false;

// Artificially suppress ion-electron scattering in Hybrid method
//
static bool NO_ION_ELECTRON     = false;

// Floor the minimum impact parameter for Coulombic scattering
//
static bool IPS                 = false;

// This flag causes excess to be added to ion
//
bool CollideIon::reverse_apply  = false;

// Apply electron excess to elc_cons
//
bool CollideIon::elec_balance   = true;

// Excess to be added in proportion to active kinetic energy
//
bool CollideIon::ke_weight      = true;


// Per-species cross-section scale factor for testing
//
static std::vector<double> cscl_;
PeriodicTable PT;

static std::string interLabels[] =
  {
    "Any type",			// 0
    "Neutral-neutral",		// 1
    "Neutral-electron",		// 2
    "Neutral-proton",		// 3
    "Ion-electron",		// 4
    "Ion-ion",			// 5
    "Free-free",		// 6
    "Collisional",		// 7
    "Ionization",		// 8
    "Recombination",		// 9
    "Electron-electron"		// 10
  };


// STL container pretty-print for NTC::Interact:T
//
std::ostream& operator<<(std::ostream& o, const NTC::Interact::T& t)
{
  o << "("  << interLabels[std::get<0>(t)]
    << ", " << Interact::label(std::get<1>(t))
    << ", " << Interact::label(std::get<2>(t))
    << ")";
  return o;
}

// Specialization for container initialization (Icont)
//
typedef std::pair<double, unsigned> pairDU;
template<> pairDU Icont<std::map, unsigned, pairDU>::Default()
{
  return pairDU(0, 0);
}

typedef std::pair<double, double> pairDD;
template<> pairDD Icont<std::map, unsigned, pairDD>::Default()
{
  return pairDD(0, 0);
}

typedef std::array<unsigned, 3> arrayU3;
template<> arrayU3 Icont<std::map, unsigned, arrayU3>::Default()
{
  return {0, 0, 0};
}

typedef std::array<double, 2> arrayD2;
template<> arrayD2 Icont<std::map, unsigned short, arrayD2>::Default()
{
  return {0, 1};
}

typedef std::array<double, 4> arrayD4;
template<> arrayD4 Icont<std::map, unsigned, arrayD4>::Default()
{
  return {0, 0, 0, 0};
}


// Define reference add operator for std::pair
//
template<typename T, typename U>
std::pair<T, U> & operator+=(std::pair<T, U> & a, const std::pair<T, U> & b)
{
  a.first  += b.first;
  a.second += b.second;
  return a;
}

// Define add operator for std::pair
//
template<typename T, typename U>
std::pair<T, U> operator+(std::pair<T, U> & a, const std::pair<T, U> & b)
{
  return std::pair<T, U>(a.first + b.first, a.second + b.second);
}

// Define minus operator for std::pair
//
template<typename T, typename U>
std::pair<T, U> operator-(std::pair<T, U> & a, const std::pair<T, U> & b)
{
  return std::pair<T, U>(a.first - b.first, a.second - b.second);
}

// Cross-product operator for std::vector of size 3 only
//
template<typename T>
std::vector<T> operator^(const std::vector<T>& a, const std::vector<T>& b)
{
  assert(a.size()>=3 and b.size()>=3);
  return {a[1]*b[2] - a[2]*b[1], a[2]*b[0] - a[0]*b[2], a[0]*b[1] - a[1]*b[0]};
}

// Scalar-product operator for std::vector
//
template<typename T>
T operator*(const std::vector<T>& a, const std::vector<T>& b)
{
  double ret  = 0.0;
  size_t asiz = a.size(), bsiz = b.size();
  size_t msiz = std::min<size_t>(asiz, bsiz);
  for (size_t i=0; i<msiz; i++) ret += a[i] * b[i];
  return ret;
}

// CONSTRUCTOR
//
CollideIon::CollideIon(ExternalForce *force, Component *comp,
		       double hD, double sD,
		       const std::string& smap, int Nth) :
  Collide(force, comp, hD, sD, NAME_ID, VERSION_ID, Nth)
{
  // Default MFP type
  mfptype = MFP_t::Ncoll;

  // Key position for Hybrid method helper
  //
  CollideIon::Pord::key = use_key;

  // Process the feature config file
  //
  processConfig();

  // Debugging
  //
  itp=0;

  // Photoionizating background?
  //
  use_photoIB = false;
  if (Ion::setIBtype(photoIB)) {
    if (photoIB!="none") {
      std::ostringstream sout;
      sout << "[" << myid 
	   << "] CollideIon found an inconsistent return type for "
	   << "photoionization background <" << photoIB << ">";
      
      throw std::runtime_error(sout.str());
    }
  } else {
    use_photoIB = true;
  }

  // Read species file
  //
  parseSpecies(smap);

  // Fill the Chianti data base
  //
  ch.createIonList(ZList);

  // Cross-section storage
  //
  csections  = std::vector<sKey2Amap> (nthrds);
  csectionsH = std::vector<sKey2Amap> (nthrds);

  // Random variable generators
  //
  gen  = new ACG(11+myid);
  unit = new Uniform(0.0, 1.0, gen);

  // Energy diagnostics
  //
  totalSoFar = 0.0;
  massSoFar  = 0.0;
  lostSoFar  = vector<double>(nthrds, 0.0);

  collD = boost::shared_ptr<collDiag>(new collDiag(this));

  // Banners logging the test algorithms
  //
  if (myid==0 && NOCOOL)
    std::cout << std::endl
	      << "************************************" << std::endl
	      << "*** No cooling is ON for testing ***" << std::endl
	      << "************************************" << std::endl;

  if (myid==0 && scatter)
    std::cout << std::endl
	      << "************************************" << std::endl
	      << "*** Restrict to elastic scatter  ***" << std::endl
	      << "************************************" << std::endl;

  if (myid==0 && equiptn)
    std::cout << std::endl
	      << "************************************" << std::endl
	      << "*** Using electron EQUIPARTITION ***" << std::endl
	      << "************************************" << std::endl;


  // Intialize cross-section scale factor array
  //
  bool csclMode = false;
  double one = 1.0;
  cscl_.resize(101, 0.0);
  for (unsigned z=1; z<=100; z++) {
    cscl_[z] = PT[z]->scale();
    if (cscl_[z] != one) csclMode = true;
  }

  if (myid==0 && csclMode) {
    std::cout << std::endl
	      << "************************************" << std::endl
	      << "*** Cross section scaled for Zs  ***" << std::endl
	      << "************************************" << std::endl
	      << std::setw(6 ) << std::right << "Z"
	      << std::setw(12) << "Element"
	      << std::setw(6 ) << "Abbr"
	      << std::setw(12) << "Factor"
	      << std::endl
	      << std::setw(6)  << "----"
	      << std::setw(12) << "--------"
	      << std::setw(6)  << "----"
	      << std::setw(12) << "--------"
	      << std::endl;
    for (unsigned z=1; z<cscl_.size(); z++) {
      if (cscl_[z] != one) std::cout << std::setw(6 ) << z
				     << std::setw(12) << PT[z]->name()
				     << std::setw(6 ) << PT[z]->abbrev()
				     << std::setw(12) << cscl_[z]
				     << std::endl;
    }
    std::cout << "************************************" << std::endl;
  }

  if (myid==0) {
    std::cout << std::endl
	      << "************************************" << std::endl
	      << "*** Algorithm selection flags ******" << std::endl
	      << "************************************" << std::endl
	      << " " << std::setw(20) << std::left  << "Algorithm type"
	      << AlgorithmLabels[aType]                 << std::endl
	      << " " << std::setw(20) << std::left  << "MEAN_MASS"
	      << (MeanMass ? "on" : "off")              << std::endl
	      << " " << std::setw(20) << std::left  << "ENERGY_ES"
	      << (ExactE ? "on" : "off")                << std::endl
	      <<  " " << std::setw(20) << std::left << "NO_EXACT"
	      << (NoExact ? "on" : "off")                << std::endl
	      <<  " " << std::setw(20) << std::left << "ENERGY_DBG"
	      << (DebugE ? "on" : "off")                << std::endl
	      <<  " " << std::setw(20) << std::left << "ENERGY_ORTHO"
	      << (AlgOrth ? "on" : "off")               << std::endl
	      <<  " " << std::setw(20) << std::left << "ENERGY_WEIGHT"
	      << (AlgWght ? "on" : "off")               << std::endl
	      <<  " " << std::setw(20) << std::left << "SECONDARY_SCATTER"
	      << (SECONDARY_SCATTER ? "on [" : "off [") << SECONDARY_SCATTER
	      << "]" << std::endl
	      <<  " " << std::setw(20) << std::left << "COLL_SPECIES"
	      << (COLL_SPECIES ? "on" : "off")          << std::endl
	      <<  " " << std::setw(20) << std::left << "COLL_LIMIT"
	      << (collLim ? "on" : "off")               << std::endl;
    if (use_photoIB)		// print photoIB parameters
    std::cout <<  " " << std::setw(20) << std::left << "photoIB model"
	      << photoIB                                << std::endl
	      <<  " " << std::setw(20) << std::left << "photoIB method"
	      << phLab[photoIBType]                     << std::endl;
    if (collLim)		// print collLim parameters
    std::cout <<  " " << std::setw(20) << std::left << "maxSel"
	      << maxSel                                 << std::endl
	      <<  " " << std::setw(20) << std::left << "collCor"
	      << (collCor ? "on" : "off")               << std::endl;
    std::cout <<  " " << std::setw(20) << std::left << "scatFac1"
	      << scatFac1                               << std::endl
	      <<  " " << std::setw(20) << std::left << "scatFac2"
	      << scatFac2                               << std::endl
	      <<  " " << std::setw(20) << std::left << "qCrit"
	      << qCrit                                  << std::endl
	      <<  " " << std::setw(20) << std::left << "E_split"
	      << (E_split ? "on" : "off")               << std::endl
	      <<  " " << std::setw(20) << std::left << "TRACE_ELEC"
	      << (TRACE_ELEC ? "on" : "off")            << std::endl
	      <<  " " << std::setw(20) << std::left << "TRACE_FRAC"
	      << TRACE_FRAC                             << std::endl
	      <<  " " << std::setw(20) << std::left << "SAME_ELEC_SCAT"
	      << (SAME_ELEC_SCAT ? "on" : "off")        << std::endl
	      <<  " " << std::setw(20) << std::left << "DIFF_ELEC_SCAT"
	      << (DIFF_ELEC_SCAT ? "on" : "off")        << std::endl
	      <<  " " << std::setw(20) << std::left << "SAME_IONS_SCAT"
	      << (SAME_IONS_SCAT ? "on" : "off")        << std::endl
	      <<  " " << std::setw(20) << std::left << "SAME_INTERACT"
	      << (SAME_INTERACT ? "on" : "off")         << std::endl
	      <<  " " << std::setw(20) << std::left << "DIFF_INTERACT"
	      << (DIFF_INTERACT ? "on" : "off")         << std::endl
	      <<  " " << std::setw(20) << std::left << "INFR_INTERACT"
	      << (TRACE_OVERRIDE ? "on" : "off")        << std::endl
	      <<  " " << std::setw(20) << std::left << "NoDelC"
	      << NoDelC                                 << std::endl
	      <<  " " << std::setw(20) << std::left << "NOCOOL_ELEC"
	      << (NOCOOL_ELEC ? "on" : "off")           << std::endl
	      <<  " " << std::setw(20) << std::left << "NOSHARE_ELEC"
	      << (NOSHARE_ELEC ? "on" : "off")          << std::endl
	      <<  " " << std::setw(20) << std::left << "CLONE_ELEC"
	      << (CLONE_ELEC ? "on" : "off")            << std::endl
	      <<  " " << std::setw(20) << std::left << "RECOMB_KE"
	      << (RECOMB_IP ? "on" : "off")             << std::endl
	      <<  " " << std::setw(20) << std::left << "KE_DEBUG"
	      << (KE_DEBUG ? "on" : "off" )             << std::endl
	      <<  " " << std::setw(20) << std::left << "NO_HSCAT"
	      << (NO_HSCAT ? "on" : "off" )             << std::endl
	      <<  " " << std::setw(20) << std::left << "DBG_HSCAT"
	      << (DBG_HSCAT ? "on" : "off" )            << std::endl
	      <<  " " << std::setw(20) << std::left << "newRecombAlg"
	      << (newRecombAlg ? "on" : "off" )         << std::endl
	      <<  " " << std::setw(20) << std::left << "HybridWeightSwitch"
	      << (HybridWeightSwitch ? "on" : "off" )   << std::endl
	      <<  " " << std::setw(20) << std::left << "DBG_NewTest"
	      << (DBG_NewTest ? "on" : "off" )          << std::endl
	      <<  " " << std::setw(20) << std::left << "NO_ION_ION"
	      << (NO_ION_ION ? "on" : "off" )           << std::endl
	      <<  " " << std::setw(20) << std::left << "NO_ION_ELECTRON"
	      << (NO_ION_ELECTRON ? "on" : "off" )      << std::endl
	      <<  " " << std::setw(20) << std::left << "ION_ELEC_RATE"
	      << (IonElecRate ? "on" : "off" )          << std::endl
	      <<  " " << std::setw(20) << std::left << "ntcDist"
	      << (ntcDist ? "on" : "off" )              << std::endl
	      <<  " " << std::setw(20) << std::left << "elc_cons"
	      << (elc_cons ? "on" : "off" )             << std::endl
	      <<  " " << std::setw(20) << std::left << "enforceMOM"
	      << (enforceMOM ? "on" : "off" )           << std::endl
	      <<  " " << std::setw(20) << std::left << "coulScale"
	      << (coulScale ? "on" : "off" )            << std::endl
	      <<  " " << std::setw(20) << std::left << "coulPow"
	      << coulPow                                << std::endl
	      <<  " " << std::setw(20) << std::left << "use_cons"
	      << use_cons                               << std::endl
	      <<  " " << std::setw(20) << std::left << "MFPtype"
	      << MFP_s.left.at(mfptype)                 << std::endl
	      <<  " " << std::setw(20) << std::left << "spc_pos"
	      << spc_pos                                << std::endl
	      <<  " " << std::setw(20) << std::left << "use_elec"
	      << use_elec                               << std::endl
	      <<  " " << std::setw(20) << std::left << "reverseApply"
	      << (reverse_apply ? "on" : "off" )        << std::endl
	      <<  " " << std::setw(20) << std::left << "elecBalance"
	      << (elec_balance ? "on" : "off" )         << std::endl
	      <<  " " << std::setw(20) << std::left << "KEWeight"
	      << (ke_weight ? "on" : "off" )            << std::endl
	      <<  " " << std::setw(20) << std::left << "maxCoul"
	      << maxCoul                                << std::endl
	      <<  " " << std::setw(20) << std::left << "logL"
	      << logL                                   << std::endl
	      <<  " " << std::setw(20) << std::left << "FreeFree cache"
	      << (Ion::useFreeFreeGrid  ? "on" : "off") << std::endl
	      <<  " " << std::setw(20) << std::left << "RadRecomb cache"
	      << (Ion::useRadRecombGrid ? "on" : "off") << std::endl
	      <<  " " << std::setw(20) << std::left << "Coll excitation cache"
	      << (Ion::useExciteGrid ? "on" : "off") << std::endl
	      <<  " " << std::setw(20) << std::left << "Coll ionization cache"
	      << (Ion::useIonizeGrid ? "on" : "off") << std::endl;
    if (use_ntcdb)
    std::cout <<  " " << std::setw(20) << std::left << "ntcThresh"
	      << ntcThresh                              << std::endl
	      <<  " " << std::setw(20) << std::left << "ntcFactor"
	      << ntcFactor                              << std::endl;
    else
    std::cout <<  " " << std::setw(20) << std::left << "NTC database"
	      << "off"                                  << std::endl;
    std::cout <<  " " << std::setw(20) << std::left << "Spectrum"
	      << (use_spectrum ? (wvlSpect ? "in wavelength" : "in eV")
		  : "off")                              << std::endl
	      <<  " " << std::setw(20) << std::left << "Photoionization"
	      << Ion::getIBtype()                       << std::endl
	      <<  " " << std::setw(20) << std::left << "elec scat type"
	      << getEStype()                            << std::endl
	      << " " << std::setw(20) << std::left << "random seed"
	      << seed                                   << std::endl
	      << "************************************" << std::endl;
  }

  // Per thread workspace initialization
  //
  dCross   .resize(nthrds);
  hCross   .resize(nthrds);
  dCfrac   .resize(nthrds);
  dInter   .resize(nthrds);
  kInter   .resize(nthrds);
  sCross   .resize(nthrds);
  sInter   .resize(nthrds);
  CProb    .resize(nthrds);
  meanF    .resize(nthrds);
  meanE    .resize(nthrds);
  meanR    .resize(nthrds);
  meanM    .resize(nthrds);
  cellM    .resize(nthrds);
  neutF    .resize(nthrds);
  numIf    .resize(nthrds);
  numEf    .resize(nthrds);
  numQ2    .resize(nthrds);
  densE    .resize(nthrds);
  photoW   .resize(nthrds);
  photoN   .resize(nthrds);
  colSc    .resize(nthrds);
  colCf    .resize(nthrds);
  coulCrs  .resize(nthrds);
  CE1      .resize(nthrds);
  CE2      .resize(nthrds);
  FF1      .resize(nthrds);
  FF2      .resize(nthrds);
  CEm      .resize(nthrds);
  FFm      .resize(nthrds);
  kCE      .resize(nthrds);
  kEi      .resize(nthrds);
  kEe1     .resize(nthrds);
  kEe2     .resize(nthrds);
  kEee     .resize(nthrds);
  kE1s     .resize(nthrds);
  kE2s     .resize(nthrds);
  testKE   .resize(nthrds);
  nselRat  .resize(nthrds);
  clrE     .resize(nthrds);
  misE     .resize(nthrds);
  dfrE     .resize(nthrds);
  updE     .resize(nthrds);
  Ncol     .resize(nthrds);
  Nmis     .resize(nthrds);
  Ein1     .resize(nthrds);
  Ein2     .resize(nthrds);
  Ivel2    .resize(nthrds);
  Evel2    .resize(nthrds);
  Evel     .resize(nthrds);
  Eelc     .resize(nthrds);
  Eion     .resize(nthrds);
  molP1    .resize(nthrds);
  molP2    .resize(nthrds);
  etaP1    .resize(nthrds);
  etaP2    .resize(nthrds);
  spTau    .resize(nthrds);
  spCrm    .resize(nthrds);
  spNsel   .resize(nthrds);
  spProb   .resize(nthrds);
  spFtau   .resize(nthrds);
  spEdel   .resize(nthrds);
  spEmax   .resize(nthrds, DBL_MAX);
  spNcol   .resize(nthrds);
  tauIon   .resize(nthrds);
  tauElc   .resize(nthrds);
  colUps   .resize(nthrds);
  velER    .resize(nthrds);
  momD     .resize(nthrds);
  crsD     .resize(nthrds);
  keER     .resize(nthrds);
  keIR     .resize(nthrds);
  elecOvr  .resize(nthrds, 0);
  elecAcc  .resize(nthrds, 0);
  elecTot  .resize(nthrds, 0);
  elecDen  .resize(nthrds, 0);
  elecDn2  .resize(nthrds, 0);
  elecCnt  .resize(nthrds, 0);
  testCnt  .resize(nthrds, 0);
  elecNum  .resize(nthrds, 0);
  elecRat  .resize(nthrds, 0);
  cellEg   .resize(nthrds, 0);
  cellEb   .resize(nthrds, 0);
  dEratg   .resize(nthrds, 0);
  dEratb   .resize(nthrds, 0);
  Nwght    .resize(nthrds, 0);
  Njsel    .resize(nthrds, 0);
  crZero   .resize(nthrds, 0);
  crMiss   .resize(nthrds, 0);
  crTotl   .resize(nthrds, 0);
  rhoSigV  .resize(nthrds, 0);
  rhoSigN  .resize(nthrds, 0);
  Escat    .resize(nthrds);
  Etotl    .resize(nthrds);
  Italy    .resize(nthrds);
  TotlU    .resize(nthrds);
  TotlD    .resize(nthrds);
  Ediag    .resize(nthrds);
  Vdiag    .resize(nthrds);
  cVels    .resize(nthrds);
  cMoms    .resize(nthrds);
  collCount.resize(nthrds);
  ionCHK   .resize(nthrds);
  recombCHK.resize(nthrds);
  clampdat .resize(nthrds);
  epsmES   .resize(nthrds, 0);
  totlES   .resize(nthrds, 0);
  epsmIE   .resize(nthrds, 0);
  totlIE   .resize(nthrds, 0);
  KElost   .resize(nthrds);
  PiProb   .resize(nthrds);
  ABrate   .resize(nthrds);
  energyA  .resize(nthrds);
  recombA  .resize(nthrds);
  photoStat.resize(nthrds);

  for (auto &v : Ediag) {
    for (auto &u : v) u = 0.0;
  }

  for (auto &v : Vdiag) {
    for (auto &u : v) u = 0.0;
  }

  for (auto &v : testKE) {
    for (auto &u : v) u = 0.0;
  }

  for (auto &v : velER) v.set_capacity(bufCap);
  for (auto &v : momD ) v.set_capacity(bufCap);
  for (auto &v : crsD ) v.set_capacity(bufCap);
  for (auto &v : keER ) v.set_capacity(bufCap);
  for (auto &v : keIR ) v.set_capacity(bufCap);

  for (auto &v : clampdat) v = clamp0;
  for (auto &v : spEmax)   v = DBL_MAX;
  for (auto &v : energyA)  v = energyP({DBL_MAX, 0.0, 0.0, 0.0}, 0);

  //
  // Cross-section debugging [INIT]
  //
  if (CROSS_DBG) {
    nextTime_dbg = 0.0;		// Next target time
    nCnt_dbg     = 0;		// Number of cells accumulated so far

    ostringstream ostr;
    ostr << outdir << runtag << ".cross_section." << myid;
    cross_debug = ostr.str();
    std::ifstream in(cross_debug.c_str());
    if (!in) {
      std::ofstream out(cross_debug.c_str());
      out << std::setw( 8) << "Count"
	  << std::setw(18) << "Time"
	  << std::setw(18) << "Initial"
	  << std::setw(18) << "Final"
	  << std::setw(18) << "Ratio"
	  << std::endl
	  << std::setw( 8) << "-------"
	  << std::setw(18) << "-------"
	  << std::setw(18) << "-------"
	  << std::setw(18) << "-------"
	  << std::setw(18) << "-------"
	  << std::endl;
    }
  }

  if (elecDist) {
    elecEV.resize(nthrds);
    for (auto &v : elecEV)    v.set_capacity(bufCap);
    elecEVmin.resize(nthrds);
    for (auto &v : elecEVmin) v.set_capacity(bufCap);
    elecEVavg.resize(nthrds);
    for (auto &v : elecEVavg) v.set_capacity(bufCap);
    elecEVmax.resize(nthrds);
    for (auto &v : elecEVmax) v.set_capacity(bufCap);
    elecEVsub.resize(nthrds);
    for (auto &v : elecEVsub) v.set_capacity(bufCap);
    elecRC.resize(nthrds);
    for (auto &v : elecRC)    v.set_capacity(bufCap);

    setupRcmbTotl();
  }

  // Enum collsion-type label fields
  //
  labels[neut_neut  ] = "geometric ";
  labels[neut_elec  ] = "neutral el";
  labels[neut_prot  ] = "neutral p+";
  labels[ion_elec   ] = "charged el";
  labels[ion_ion    ] = "ion-ion sc";
  labels[free_free  ] = "free-free ";
  labels[colexcite  ] = "col excite";
  labels[ionize     ] = "ionization";
  labels[recomb     ] = "recombine ";

  labels[neut_neut_1] = "geometric  [1]";
  labels[neut_elec_1] = "neutral el [1]";
  labels[neut_prot_1] = "neutral p+ [1]";
  labels[ion_elec_1 ] = "charged el [1]";
  labels[ion_ion_1  ] = "ion-ion sc [1]";
  labels[free_free_1] = "free-free  [1]";
  labels[colexcite_1] = "col excite [1]";
  labels[ionize_1   ] = "ionization [1]";
  labels[recomb_1   ] = "recombine  [1]";

  labels[neut_neut_2] = "geometric  [2]";
  labels[neut_elec_2] = "neutral el [2]";
  labels[neut_prot_2] = "neutral p+ [2]";
  labels[ion_elec_2 ] = "charged el [2]";
  labels[ion_ion_2  ] = "ion-ion sc [2]";
  labels[free_free_2] = "free-free  [2]";
  labels[colexcite_2] = "col excite [2]";
  labels[ionize_2   ] = "ionization [2]";
  labels[recomb_2   ] = "recombine  [2]";

  labels[elec_elec  ] = "el collisions ";

  elecElec = Interact::T(CollideIon::elec_elec,
			      Interact::edef,
			      Interact::edef);

  if (aType == Hybrid) {
    coulombSel = boost::make_shared<coulombSelect>();
  }

  spectrumSetup();
}

CollideIon::~CollideIon()
{
}

void CollideIon::meanFdump(int id)
{
  std::cout << std::left     << std::endl
	    << std::setw(12) << "Species"
	    << std::setw(12) << "weight" << std::endl
	    << std::setw(12) << "------"
	    << std::setw(12) << "------" << std::endl;

  double totalW = 0.0;
  for (auto v : meanF[id]) {
    std::ostringstream istr;
    istr << "(" << v.first.first << ","
	 << v.first.second << ")";
    std::cout << std::setw(12) << istr.str()
	      << std::setw(12) << v.second << std::endl;
    totalW += v.second;
  }
  std::cout << std::setw(12) << "------"
	    << std::setw(12) << "------" << std::endl
	    << std::setw(12) << "Total"
	    << std::setw(12) << totalW << std::endl << std::endl;
}


// Approximate the minimum, mean and maximum velocities in the cell.
// Should be much faster than direct computation
//
bool MaxwellianApprox = false;

// Returns (min, mean, max) velocities in each cell
//
void CollideIon::cellMinMax(pCell* const cell, int id)
{
  if (aType != Hybrid) return;
  
  cVels[id] = { {1.0e20, 0.0, 0.0}, {1.0e20, 0.0, 0.0} };

  std::set<unsigned long> bodies = cell->Bodies();

  if (MaxwellianApprox) {
    const  double xmin=0.001, xmax=5.0, h=0.005;
    static double medianVal;

    typedef boost::shared_ptr<tk::spline> tksplPtr;
    static tksplPtr spl;

    if (spl.get() == 0) {
      std::vector<double> x, y;
      double X = xmin;
      while (X <= xmax) {
	x.push_back(X);
	y.push_back(std::erf(X) - 2.0/sqrt(M_PI)*exp(-X*X)*X);
	X += h;
      }
      spl = tksplPtr(new tk::spline); // The new spline instance
      spl->set_points(y, x);	      // Set the spline data
      medianVal = (*spl)(0.5);	      // Evaluate median
    }

    std::pair<double, double> KEdspE = computeEdsp(cell);
    double KEtot, KEdspC;
    cell->KE(KEtot, KEdspC);
    
    double sigmaI = 2.0*KEdspC, sigmaE = KEdspC;
    if (KEdspE.first>0.0) sigmaE += KEdspE.second/KEdspE.first;
    sigmaI = sqrt(2.0*sigmaI);
    sigmaE = sqrt(2.0*sigmaE);

    double P = 1.0/sqrt(static_cast<double>(bodies.size()));
    double Q = 1.0 - P;
    
    cVels[id]. first[0] = (*spl)(P) * sigmaI;
    cVels[id]. first[1] = medianVal * sigmaI;
    cVels[id]. first[2] = (*spl)(Q) * sigmaI;

    cVels[id].second[0] = (*spl)(P) * sigmaE;
    cVels[id].second[1] = medianVal * sigmaE;
    cVels[id].second[2] = (*spl)(Q) * sigmaE;

    return;
  }

  unsigned count = 0;

  for (auto b1 : bodies) {
    Particle *p1 = tree->Body(b1);

    for (auto b2 : bodies) {
      if (b1 == b2) continue;

      Particle *p2 = tree->Body(b2);

      double velI = 0.0, velE = 0.0, v;

      for (size_t k=0; k<3; k++) {
	v = p1->vel[k] - p2->vel[k];
	velI += v*v;
	v = p1->vel[k] - p2->dattrib[use_elec+k];
	velE += v*v;
      }

      cVels[id].first[1] += velI;
      velI = sqrt(velI);
      cVels[id].first[0] = std::min<double>(cVels[id].first[0], velI);
      cVels[id].first[2] = std::max<double>(cVels[id].first[2], velI);

      cVels[id].second[1] += velE;
      velE = sqrt(velE);
      cVels[id].second[0] = std::min<double>(cVels[id].second[0], velE);
      cVels[id].second[2] = std::max<double>(cVels[id].second[2], velE);

      count++;
    }
  }

  if (count>0) {
    cVels[id]. first[1] = sqrt(cVels[id]. first[1]/count);
    cVels[id].second[1] = sqrt(cVels[id].second[1]/count);
  }

  return;
}

/**
   Precompute all the necessary cross sections
*/
void CollideIon::initialize_cell(pCell* const cell, double rvmax, int id)
{
  // Representative avg cell velocity in cgs
  //
  double vavg = 0.5*rvmax*TreeDSMC::Vunit;

  vavg_dbg = 0.5*rvmax;

  // Representative avg cell energy in ergs
  //
  double Eerg = 0.5*vavg*vavg*amu;

  // Min/Mean/Max electron ion velocity (hybrid only)
  //
  /*
  if (cell->sample) cellMinMax(cell->sample, id);
  else              cellMinMax(cell, id);
  */
  cellMinMax(cell, id);

  std::array<double, 3> iVels = cVels[id].first;
  std::array<double, 3> eVels = cVels[id].second;

  // In eV
  //
  double EeV = Eerg / eV;

  // Clear excess energy diagnostics
  //
  clrE[id] = 0.0;
  misE[id] = 0.0;
  dfrE[id] = 0.0;
  updE[id] = 0.0;
  Ncol[id] = 0;
  Nmis[id] = 0;

  // True particle number in cell
  //
  numIf[id] = 0.0;		// Charged particles
  numEf[id] = 0.0;		// Ions and electrons
  numQ2[id] = 0.0;		// For Coulombic scattering

  // Electron density per species in cell
  //
  densE[id].clear();

  // Total ion density
  //
  double densItot = 0.0;

  // Total charged ion density
  //
  double densQtot = 0.0;

  // Total electron density
  //
  double densEtot = 0.0;

  // Values for estimating effective MFP per cell
  //
  spNcol[id] = 0;

  // NOCOOL KE test
  //
  for (auto &v : testKE[id]) v = 0;
  testCnt[id] = 0;

  // Zero momentum computation
  if (enforceMOM) cMoms[id] = {0, 0, 0, 0};

  // Loop through all bodies in cell
  //
  for (auto b : cell->bods) {

    Particle *p      = tree->Body(b);

    speciesKey K;
    unsigned short Z = 0;

    if (aType == Direct or aType == Weight or aType == Hybrid) {
      K = KeyConvert(p->iattrib[use_key]).getKey();
      Z = K.first;
    }

    if (aType == Direct or aType == Weight) {
      double ee     = K.second - 1;
      double ie     = 0.0;

      if (K.second>1) ie = 1.0;

      numIf[id]    += p->mass * (ie  + ee) / atomic_weights[Z];
      numEf[id]    += p->mass * (1.0 + ee) / atomic_weights[Z];
      densE[id][K] += p->mass * ee / atomic_weights[Z];
      densEtot     += p->mass * ee / atomic_weights[Z];
    }

    if (aType == Hybrid) {

      double ee = 0.0;		// Electron fraction
      double qq = 0.0;		// Charge squared
      double cz = 0.0;		// Charged ion fraction
      for (unsigned short C=0; C<=Z; C++) {
	ee += p->dattrib[spc_pos + C] * C;
	qq += p->dattrib[spc_pos + C] * C * C;
	if (C>0) cz += p->dattrib[spc_pos + C];
      }
      double ie = 1.0 - p->dattrib[spc_pos];

      numIf[id]    += p->mass * (ie  + ee) / atomic_weights[Z];
      numEf[id]    += p->mass * (1.0 + ee) / atomic_weights[Z];
      numQ2[id]    += p->mass * qq / atomic_weights[Z];
      densE[id][K] += p->mass * ee / atomic_weights[Z];
      densEtot     += p->mass * ee / atomic_weights[Z];
      densItot     += p->mass / atomic_weights[Z];
      densQtot     += p->mass * cz / atomic_weights[Z];

      if (enforceMOM) {
	cMoms[id][3] += p->mass;
	for (size_t k=0; k<3; k++)
	  cMoms[id][k] += p->mass*p->vel[k];
	
	if (use_elec >= 0 and ExactE) {
				// Electron fraction per particle
	  double eta = p->mass*atomic_weights[0]/atomic_weights[Z] * ee;
	  cMoms[id][3] += eta;
	  for (size_t k=0; k<3; k++)
	    cMoms[id][k] += eta * p->dattrib[use_elec+k];
	}
      }

      if (KE_DEBUG) {
				// Electron fraction per particle
	double eta = atomic_weights[0]/atomic_weights[Z] * ee;

				// Compute kinetic energy
	double KE  = 0.0;
	for (unsigned j=0; j<3; j++) {
	  KE += p->vel[j] * p->vel[j];
	  if (use_elec >= 0) {
	    KE += eta * p->dattrib[use_elec+j] * p->dattrib[use_elec+j];
	  }
	}

	testKE[id][0] += 0.5 * p->mass * KE;

	if (use_cons>=0)
	  testKE[id][1] += p->dattrib[use_cons];
	if (use_elec>=0 and elc_cons)
	  testKE[id][2] += p->dattrib[use_elec+3];
      }
    } // END: Hybrid

    if (aType == Trace) {
      for (auto s : SpList) {
	speciesKey k = s.first;
				// Number of electrons
	double ee    = k.second - 1;
				// Number fraction of particles
	double ww    = p->dattrib[s.second]/atomic_weights[k.first];

	numQ2[id]    += p->mass * ww * ee*ee;
	numEf[id]    += p->mass * ww * (1.0 + ee);
	densE[id][k] += p->mass * ww * ee;
	densEtot     += p->mass * ww * ee;
	densItot     += p->mass * ww;
	if (k.second>1) densQtot += p->mass * ww;
      }

      numIf[id] = densItot;
      numEf[id] = densEtot;

    } // END: Trace
    
  } // END: body loop

  // Compute mean charge-squared
  if (densQtot>0.0) numQ2[id] /= densQtot;

  // Physical number of particles
  //
  numIf[id] *= TreeDSMC::Munit/amu;
  numEf[id] *= TreeDSMC::Munit/amu;

  // Convert to electron number density in cgs
  //
  double volc = cell->Volume();
  double dfac = TreeDSMC::Munit/amu / (pow(TreeDSMC::Lunit, 3.0)*volc);

  for (auto & v : densE[id]) v.second *= dfac;
  densItot *= dfac;
  densEtot *= dfac;

  elecDen[id] += densEtot;
  elecDn2[id] += densEtot * densEtot;
  elecCnt[id] ++;

  // Mean interparticle spacing in nm
  //
  double   ips = DBL_MAX;
  if (IPS) ips = pow(volc/numEf[id], 0.333333) * TreeDSMC::Lunit * 1.0e7;

  // Convert to cross section in system units
  //
  double crs_units = 1e-14 / (TreeDSMC::Lunit*TreeDSMC::Lunit);


  if (aType == Direct or aType == Weight or aType == Hybrid) {

    unsigned eCnt  = 0;
    double   eVel1 = 0.0, eVel2 = 0.0;
    double   iVel1 = 0.0, iVel2 = 0.0;

    if (minCollFrac > 0.0 or aType == Hybrid) {

      // Mean fraction in trace species
      //
      meanF[id].clear();

      // Sample cell
      //
      pCell *samp = cell->sample;

      if (samp) {

	std::set<unsigned long> bset = samp->Bodies();

	for (auto b : bset) {
				// Particle mass accumulation
	  Particle *p  = c0->Part(b);

				// Mass-weighted trace fraction
	  speciesKey k = KeyConvert(p->iattrib[use_key]).getKey();

				// Add to species bucket
	  if (aType == Hybrid) {
	    for (unsigned short C=0; C<=k.first; C++) {
	      k.second = C + 1;
	      meanF[id][k] += p->mass * p->dattrib[spc_pos+C]
		/ atomic_weights[k.first];
	    }

	  } else {
	    meanF[id][k] += p->mass / atomic_weights[k.first];
	  }
	}

	// Sanity check
	//
	if (1) {
	  double TotalW = 0.0;
	  for (auto v : meanF[id]) TotalW += v.second;

	  if (TotalW == 0.0) {
	    double tmass = 0.0, twght = 0.0;
	    if (aType == Hybrid) {
	      for (auto b : bset) {
		Particle *p = c0->Part(b);
		speciesKey k = KeyConvert(p->iattrib[use_key]).getKey();
		unsigned short Z = k.first;
		// Add to species bucket
		for (unsigned short C=0; C<=Z; C++) {
		  k.second = C + 1;
		  twght += p->mass * p->dattrib[spc_pos+C];
		}
		tmass += p->mass;
	      }
	    }
	    std::cout << "Crazy: #=" << bset.size() << " mass=" << tmass
		      << " weight=" << twght << " cell=" << cell
		      << std::endl;
	    std::cout << std::endl;
	  }
	}

      } // END: sample cell
      else {

	for (auto b : cell->bods) {
				// Particle mass accumulation
	  Particle *p  = cell->Body(b);
				// Mass-weighted trace fraction
	  speciesKey k = KeyConvert(p->iattrib[use_key]).getKey();

				// Add to species bucket
	  if (aType == Hybrid) {
	    for (unsigned short C=0; C<=k.first; C++) {
	      k.second = C + 1;
	      meanF[id][k] += p->mass * p->dattrib[spc_pos+C]
		/ atomic_weights[k.first];
	    }
	  } else {
	    meanF[id][k] += p->mass / atomic_weights[k.first];
	  }
	}
      } // END: interaction cell

      // Normalize mass-weighted fraction
      //
      if (aType == Hybrid) {
	double normT = 0.0;
	for (auto v : meanF[id]) normT += v.second;

	if (normT <= 0.0 or std::isnan(normT) or std::isinf(normT)) {
	  std::cout << "Hybrid norm failure: " << normT << std::endl;
	} else {
	  for (auto &v : meanF[id]) v.second /= normT;
	}

      } else {
	std::map<unsigned short, double> spTotl;
	for (auto v : meanF[id]) {
	  unsigned short Z = v.first.first;
	  if (spTotl.find(Z) == spTotl.end())
	    spTotl[Z]  = v.second;
	  else
	    spTotl[Z] += v.second;
	}

	for (auto &s : meanF[id]) {
	  s.second /= spTotl[s.first.first];
	}
      } // type loop

    } // collMinFrac

    // Sanity check
    //
    if (1) {
      bool   bad  = false;
      double totT = 0.0;
      for (auto v : meanF[id]) {
	if (std::isnan(v.second)) {
	  std::cout << "NaN at (" << v.first.first
		    << "," << v.first.second << ")" << std::endl;
	  bad = true;
	}
	else totT += v.second;
      }
      if (fabs(totT - 1.0) > 1.0e-8) {
	std::cout << "totT = " << totT << std::endl;
      }
      if (bad) {
	std::cout << "nan detected in meanF [before]" << std::endl;
      }
    }

    // Compute mean electron velocity
    //
    if (use_elec>=0) {

      double ewght = 0.0, iwght = 0.0, tmass = 0.0;

      // Sample cell
      //
      pCell *samp = cell->sample;

      if (samp) {
	for (auto c : samp->children) {
	  for (auto i : c.second->bods) {
	    Particle *p = c.second->Body(i);
	    KeyConvert k(p->iattrib[use_key]);

	    if (aType == Hybrid) {
	      double eWght = 0.0;
	      for (unsigned short C=0; C<=k.Z(); C++)
		eWght += p->dattrib[spc_pos + C] * C;

	      eCnt += eWght;

	      double imass = p->mass;
	      double emass = p->mass * eWght;

	      iwght += imass;
	      ewght += emass;
	      tmass += p->mass;

	      for (int l=0; l<3; l++) {
		double ve  = p->dattrib[use_elec+l];
		eVel1 += emass * ve;
		eVel2 += emass * ve*ve;
		double vi  = p->vel[l];
		iVel1 += imass * vi;
		iVel2 += imass * vi*vi;
	      }
	    } // END: "Hybrid"
	    else {
	      if (k.C()>1) {
		for (int l=0; l<3; l++) {
		  double ve  = p->dattrib[use_elec+l];
		  eVel1 += ve;
		  eVel2 += ve*ve;
		  double vi  = p->vel[l];
		  iVel1 += vi;
		  iVel2 += vi*vi;
		}
		eCnt += k.C() - 1;
	      }

	    } // END: "Direct" and "Weight"

	  } // END: body loop

	} // END: sample cell loop

      }  // END: sample cell
      else {

	for (auto i : cell->bods) {

	  Particle *p = cell->Body(i);
	  KeyConvert k(p->iattrib[use_key]);

	  if (aType == Hybrid) {
	    double eWght = 0.0;
	    for (unsigned short C=0; C<=k.Z(); C++)
	      eWght += p->dattrib[spc_pos + C] * C;

	    eCnt += eWght;

	    double imass = p->mass;
	    double emass = p->mass * eWght;

	    iwght += imass;
	    ewght += emass;
	    tmass += p->mass;

	    for (int l=0; l<3; l++) {
	      double ve  = p->dattrib[use_elec+l];
	      eVel1 += emass * ve;
	      eVel2 += emass * ve*ve;
	      double vi  = p->vel[l];
	      iVel1 += imass * vi;
	      iVel2 += imass * vi*vi;
	    }

	  } // END: "Hybrid"

	  else {

	    if (k.C()>1) {
	      for (int l=0; l<3; l++) {
		double ve  = p->dattrib[use_elec+l];
		eVel1 += ve;
		eVel2 += ve*ve;
		double vi  = p->vel[l];
		iVel1 += vi;
		iVel2 += vi*vi;
	      }

	      eCnt += k.C() - 1;
	    }
	  } // END: "Direct" and "Weight"

	} // END: body loop

      } // END: interaction cell

      if (aType == Hybrid) {

	Evel2[id] = Ivel2[id] = 0.0;

	if (ewght>0.0) {
	  Evel2[id] = eVel2/ewght;
	  eVel2 -= eVel1*eVel1/ewght;
	  eVel2 /= ewght;
	}

	if (iwght>0.0) {
	  Ivel2[id] = iVel2/iwght;
	  iVel2 -= iVel1*iVel1/iwght;
	  iVel2 /= iwght;
	}

	Evel[id]  = sqrt( fabs(eVel2 + iVel2) );

	if (tmass>0.0)
	  meanE[id] = ewght/tmass;

      } else {

	if (eCnt>1) {
	  eVel2 -= eVel1*eVel1/eCnt;
	  iVel2 -= iVel1*iVel1/eCnt;
	  Evel[id] = sqrt( fabs(eVel2 + iVel2)/(eCnt-1) );
	} else {
	  Evel[id] = 0.0;
	}
      }

    } // END: use_elec

    // Another sanity check
    //
    if (1) {
      bool   bad  = false;
      double totT = 0.0;
      for (auto v : meanF[id]) {
	if (std::isnan(v.second)) {
	  std::cout << "NaN at (" << v.first.first
		    << "," << v.first.second << ")" << std::endl;
	  bad = true;
	}
	else totT += v.second;
      }
      if (fabs(totT - 1.0) > 1.0e-8) {
	std::cout << "totT = " << totT << std::endl;
      }
      if (bad) {
	std::cout << "nan detected in meanF [after]" << std::endl;
      }
    }

    // it1 and it2 are of type std::map<speciesKey, unsigned>; that
    // is, the number of particles of each speciesKey in the cell
    //
    for (auto it1 : cell->count) {

      speciesKey i1  = it1.first;
      double Radius1 = geometric(i1.first);

      // So, we are computing interactions for all possible
      // interaction pairs
      //
      for (auto it2 : cell->count) {

	speciesKey i2  = it2.first;
	double Radius2 = geometric(i2.first);

	double CrossG  = M_PI*(Radius1 + Radius2)*(Radius1 + Radius2);
	double Cross1  = 0.0;
	double Cross2  = 0.0;

	double m1      = atomic_weights[i1.first];
	double m2      = atomic_weights[i2.first];

	double ne1     = i1.second - 1;
	double ne2     = i2.second - 1;

	double dof1    = 1.0 + ne1;
	double dof2    = 1.0 + ne2;

	if (NO_DOF) dof1 = dof2 = 1.0;

	double eVel1   = sqrt(atomic_weights[i1.first]/atomic_weights[0]/dof1);
	double eVel2   = sqrt(atomic_weights[i2.first]/atomic_weights[0]/dof2);

	if (use_elec) {
	  if (Evel[id]>0.0) eVel1 = eVel2 = Evel[id] / rvmax;
	}

	if (NO_VEL) eVel1 = eVel2 = 1.0;

	if (aType == Hybrid) {

	  unsigned short Z1 = i1.first;
	  unsigned short Z2 = i2.first;

	  double tot = 0.0;	// meanF normalization
	  for (auto v : meanF[id]) tot += v.second;

	  speciesKey k1(Z1, 1), k2(Z2, 1);

	  double neut1 = meanF[id][k1]/tot;
	  double neut2 = meanF[id][k2]/tot;

	  if (tot <= 0.0)
	    {
	      std::cout << "Total <= 0: " << tot << std::endl;
	      neut1 = 0.0;
	      neut2 = 0.0;
	    }

	  double elec1 = 0.0;	// Mean electron number in P1
	  double elec2 = 0.0;	// Mean electron number in P2

	  for (unsigned short C=1; C<=Z1; C++) {
	    k1.second = C + 1;
	    elec1 += meanF[id][k1]*C;
	  }
	  elec1 /= tot;

	  for (unsigned short C=1; C<=Z1; C++) {
	    k2.second = C + 1;
	    elec2 += meanF[id][k2]*C;
	  }
	  elec2 /= tot;

	  if (false) {
	    std::cout << std::string(40, '-') << std::endl << std::right;
	      std::cout << std::setw( 4) << "Z"
			<< std::setw( 4) << "C"
			<< std::setw(14) << "F"
			<< std::endl;
	    for (auto v : meanF[id]) {
	      std::cout << std::setw( 4) << v.first.first
			<< std::setw( 4) << v.first.second
			<< std::setw(14) << v.second/tot
			<< std::endl;
	    }
	    std::cout << std::endl
		      << "Neut1 = " << neut1  << std::endl
		      << "Neut2 = " << neut2  << std::endl
		      << "Elec1 = " << elec1  << std::endl
		      << "Elec2 = " << elec2  << std::endl
		      << std::string(40, '-') << std::endl;
	  }

	  CrossG *= neut1 + neut2;

	  double mu0 = atomic_weights[i1.first]*atomic_weights[i2.first] /
	    (atomic_weights[i1.first] + atomic_weights[i2.first]);

	  double mu1 = atomic_weights[i1.first]*atomic_weights[0] /
	    (atomic_weights[i1.first] + atomic_weights[0]);

	  double mu2 = atomic_weights[i2.first]*atomic_weights[0] /
	    (atomic_weights[i2.first] + atomic_weights[0]);

	  double efac = 0.5 * amu * TreeDSMC::Vunit * TreeDSMC::Vunit;


				// Min/Mean/Max electron energy for P1 ion
	  std::array<double, 3> E1s =
	    {
	      efac*mu1*eVels[0]*eVels[0]/eV,
	      efac*mu1*eVels[1]*eVels[1]/eV,
	      efac*mu1*eVels[2]*eVels[2]/eV
	    };

				// Min/Mean/Max electron energy for P2 ion
	  std::array<double, 3> E2s =
	    {
	      efac*mu2*eVels[0]*eVels[0]/eV,
	      efac*mu2*eVels[1]*eVels[1]/eV,
	      efac*mu2*eVels[2]*eVels[2]/eV
	    };


				// Min/Mean/Max ion energy
	  std::array<double, 3> Eii =
	    {
	      efac*mu0*iVels[0]*iVels[0]/eV,
	      efac*mu0*iVels[1]*iVels[1]/eV,
	      efac*mu0*iVels[2]*iVels[2]/eV
	    };

	  if (elecDist) {
	    elecEVmin[id].push_back(E1s[0]);
	    elecEVavg[id].push_back(E1s[1]);
	    elecEVmax[id].push_back(E1s[2]);
	  }

				// Forbid zero value
	  for (auto & v : E1s) v = std::max<double>(v, FloorEv);
	  for (auto & v : E2s) v = std::max<double>(v, FloorEv);

	  // Neutral-Neutral cross section
	  //
	  {
	    Interact::T t(neut_neut,
			  {Interact::neutral, speciesKey(Z1, 1)},
			  {Interact::neutral, speciesKey(Z2, 1)});

	    csectionsH[id][i1][i2][t] = CrossG *
	      crossfac * crs_units * cscl_[i1.first] * cscl_[i2.first];
	  }

	  if (temp_debug) meanFdump(id);

	  if (MEDIAN_E) {
	    
	    // Neutral-Electron cross section
	    //
	    for (unsigned short C2=1; C2<=Z2; C2++) {

	      Interact::T t(neut_elec,
			    {Interact::neutral,  speciesKey(Z1, 1)},
			    {Interact::electron, speciesKey(Z2, 0)});

	      csectionsH[id][i1][i2][t] =
		elastic(i1.first, E1s[1]) * eVels[1] / rvmax * neut1 * elec2 *
		crossfac * crs_units * cscl_[i1.first] * cscl_[i2.first];
	    }

	    for (unsigned short C1=1; C1<=Z1; C1++) {

	      Interact::T t(neut_elec,
			    {Interact::neutral,  speciesKey(Z2, 1)},
			    {Interact::electron, speciesKey(Z1, 0)});

	      csectionsH[id][i2][i1][t] =
		elastic(i2.first, E2s[1]) * eVels[1] / rvmax * neut2 * elec1 *
		crossfac * crs_units * cscl_[i1.first] * cscl_[i2.first];
	    }
	    
	    // Neutral-proton cross section
	    //
	    if (i2.first==1) {
	      Interact::T t(neut_prot, 
			    {Interact::neutral,  speciesKey(Z2, 1)},
			    {Interact::ion,      speciesKey(1, 2)});

	      csectionsH[id][i1][i2][t] =
		elastic(i1.first, Eii[1], Elastic::proton) *
		iVels[1] / rvmax * neut1 * elec2 *
		crossfac * crs_units * cscl_[i1.first] * cscl_[i2.first];
	    }
	      
	    if (i1.first==1) {
	      Interact::T t(neut_prot, 
			    {Interact::ion,     speciesKey(1, 2)},
			    {Interact::neutral, speciesKey(Z2, 1)});

	      csectionsH[id][i1][i2][t] =
		elastic(i2.first, Eii[1], Elastic::proton) *
		iVels[1] / rvmax * neut2 * elec1 *
		crossfac * crs_units * cscl_[i1.first] * cscl_[i2.first];
	    }
	      
	    // Coulombic (Rutherford) cross section (ion-electron)
	    //
	    for (unsigned short C1=1; C1<=Z1; C1++) {
	      k1.second = C1 + 1;
	    
	      double b = 0.5*esu*esu*C1 /
		std::max<double>(E1s[1]*eV, FloorEv*eV) * 1.0e7; // nm
	      b = std::min<double>(b, ips);

	      if (coulInter) {
		double b_max = sqrt(1.0/(M_PI*pow(numIf[id], 2.0/3.0)));
		std::min<double>(b, b_max);
	      }

	      double cc3 = M_PI*b*b * eVels[1] / rvmax;

	      double mfac = 4.0 * logL;
	      for (unsigned short C2=1; C2<=Z2; C2++) {
		k2.second = C2 + 1;
		
		Interact::T t(ion_elec,
			      {Interact::ion,      speciesKey(Z1, C1+1)},
			      {Interact::electron, speciesKey(Z2, 0   )});

		csectionsH[id][i1][i2][t] =
		  cc3 * C2 * mfac *
		  meanF[id][k1] * meanF[id][k2] / (tot*tot) *
		  crossfac * crs_units * cscl_[i1.first] * cscl_[i2.first];

		// Test
		if (init_dbg and myid==0 and tnow > init_dbg_time and Z1 == init_dbg_Z) {

		  std::ofstream out(runtag + ".heplus_test_cross", ios::out | ios::app);
		  std::ostringstream sout;
		  
		  sout << " #1 [" << Z1 << ", " << Z2 << "] "
		       << "("  << C1 << ", " << C2 << ") = ";
		  
		  out << "Time = " << std::setw(10) << tnow
		      << sout.str() << csectionsH[id][i1][i2][t] << std::endl;
		}
		// End Test

	      }
	    }

	    for (unsigned short C2=1; C2<=Z2; C2++) {
	      k2.second = C2 + 1;

	      double b = 0.5*esu*esu*C2 /
		std::max<double>(E2s[1]*eV, FloorEv*eV) * 1.0e7; // nm

	      b = std::min<double>(b, ips);

	      if (coulInter) {
		double b_max = sqrt(1.0/(M_PI*pow(numIf[id], 2.0/3.0)));
		std::min<double>(b, b_max);
	      }

	      double cc3 = M_PI*b*b * eVels[1] / rvmax;

	      double mfac = 4.0 * logL;
	      for (unsigned short C1=1; C1<=Z1; C1++) {
		k1.second = C1 + 1;

		Interact::T t(ion_elec,
			      {Interact::ion,      speciesKey(Z2, C2+1)},
			      {Interact::electron, speciesKey(Z1, 0   )});

		csectionsH[id][i2][i1][t] =
		  cc3 * C1 * mfac *
		  meanF[id][k1] * meanF[id][k2] / (tot*tot) *
		  crossfac * crs_units * cscl_[i1.first] * cscl_[i2.first];

		// Test
		if (init_dbg and myid==0 and tnow > init_dbg_time and Z1 == init_dbg_Z) {

		  std::ofstream out(runtag + ".heplus_test_cross", ios::out | ios::app);
		  std::ostringstream sout;
		  
		  sout << " #2 [" << Z1 << ", " << Z2 << "] "
		       << "("  << C1 << ", " << C2 << ") = ";
		  
		  out << "Time = " << std::setw(10) << tnow
		      << sout.str() << csectionsH[id][i2][i1][t] << std::endl;
		}
		// End Test
		
	      }
	    }
	    
	    // Coulombic (Rutherford) cross section (ion-ion)
	    //
	    for (unsigned short C1=1; C1<=Z1; C1++) {
	      k1.second = C1 + 1;
	      
	      for (unsigned short C2=1; C2<=Z2; C2++) {
		k2.second = C2 + 1;
		
		double b = 0.5*esu*esu*C1*C2 /
		  std::max<double>(Eii[1]*eV, FloorEv*eV) * 1.0e7; // nm

		b = std::min<double>(b, ips);

		if (coulInter) {
		  double b_max = sqrt(1.0/(M_PI*pow(numIf[id], 2.0/3.0)));
		  std::min<double>(b, b_max);
		}

		double cc3 = M_PI*b*b;
	      
		double mfac = 4.0 * logL;

		Interact::T t(ion_ion,
			      {Interact::ion, speciesKey(Z1, C1+1)},
			      {Interact::ion, speciesKey(Z2, C2+1)});

		csectionsH[id][i1][i2][t] =
		  cc3 * mfac *
		  meanF[id][k1] * meanF[id][k2] / (tot*tot) *
		  crossfac * crs_units * cscl_[i1.first] * cscl_[i2.first];
	      }
	    }
	    
	    // Free-free cross section
	    //
	    for (unsigned short C1=1; C1<=Z1; C1++) {
	      for (unsigned short C2=1; C2<=Z2; C2++) {
		k1.second = C1 + 1;
		k2.second = C2 + 1;
		
		{
		  CFreturn ff3 = ch.IonList[lQ(Z1, C1+1)]->freeFreeCross(E1s[1], id);
		  ff3.first *= eVels[1];
	      
		  Interact::T t(free_free,
				{Interact::ion,      speciesKey(Z1, C1+1)},
				{Interact::electron, speciesKey(Z2, 0   )});

		  csectionsH[id][i1][i2][t] =
		    ff3.first / rvmax * C2 * crs_units *
		    meanF[id][k1] * meanF[id][k2] / (tot*tot);
		}

		{
		  CFreturn ff3 = ch.IonList[lQ(Z2, C2+1)]->freeFreeCross(E2s[1], id);
		  ff3.first *= eVels[1];

		  Interact::T t(free_free,
				{Interact::ion,      speciesKey(Z2, C2+1)},
				{Interact::electron, speciesKey(Z1, 0   )});

		  csectionsH[id][i2][i1][t] =
		    ff3.first / rvmax * C1 * crs_units *
		    meanF[id][k1] * meanF[id][k2] / (tot*tot);
		}
	      }
	    }

	    // Collisional-excitation cross section
	    //
	    for (unsigned short C1=0; C1<Z1; C1++) {
	      for (unsigned short C2=1; C2<=Z2; C2++) {
		k1.second = C1 + 1;
		k2.second = C2 + 1;
		
		double cc3 = 
		  ch.IonList[lQ(Z1, C1+1)]->collExciteCross(E1s[1], id).back().first * eVels[1];

		Interact::T t(colexcite,
			      {Interact::ion,      speciesKey(Z1, C1+1)},
			      {Interact::electron, speciesKey(Z2, 0   )});

		csectionsH[id][i1][i2][t] =
		  cc3 / rvmax * C2 * crs_units *
		  meanF[id][k1] * meanF[id][k2] / (tot*tot);
	      }
	    }
	    
	    for (unsigned short C2=0; C2<Z2; C2++) {
	      for (unsigned short C1=1; C1<=Z1; C1++) {
		k1.second = C1 + 1;
		k2.second = C2 + 1;
		
		double cc3 = ch.IonList[lQ(Z2, C2+1)]->collExciteCross(E2s[1], id).back().first * eVels[1];
	      
		Interact::T t(colexcite,
			      {Interact::ion,      speciesKey(Z2, C2+1)},
			      {Interact::electron, speciesKey(Z1, 0   )});

		csectionsH[id][i2][i1][t] = cc3 / rvmax * C1 * crs_units *
		  meanF[id][k1] * meanF[id][k2] / (tot*tot);
	      }
	    }
	    

	    // Ionization cross section
	    //
	    for (unsigned short C1=0; C1<Z1; C1++) {
	      for (unsigned short C2=1; C2<=Z2; C2++) {
	      k1.second = C1 + 1;
	      k2.second = C2 + 1;

	      double ic3 =
		ch.IonList[lQ(Z1, C1+1)]->directIonCross(E1s[1], id) * eVels[1];

	      Interact::T t(ionize,
			    {Interact::ion,      speciesKey(Z1, C1)},
			    {Interact::electron, speciesKey(Z2, 0 )});

	      csectionsH[id][i1][i2][t] =
		ic3 / rvmax *  C2 * crs_units *
		meanF[id][k1] * meanF[id][k2] / (tot*tot);
	      }
	    }

	    for (unsigned short C2=0; C2<Z2; C2++) {
	      for (unsigned short C1=1; C1<=Z1; C1++) {
		k1.second = C1 + 1;
		k2.second = C2 + 1;
		
		double ic3 =
		  ch.IonList[lQ(Z2, C2+1)]->directIonCross(E2s[1], id) * eVels[1];

		Interact::T t(ionize,
			      {Interact::ion,      speciesKey(Z2, C2+1)},
			      {Interact::electron, speciesKey(Z1, 0   )});

		csectionsH[id][i2][i1][t] = ic3 / rvmax *  C1 * crs_units *
		  meanF[id][k1] * meanF[id][k2] / (tot*tot);
	      }
	    }


	    // Recombination cross section
	    //
	    for (unsigned short C1=1; C1<=Z1; C1++) {
	      for (unsigned short C2=1; C2<=Z2; C2++) {
		k1.second = C1 + 1;
		k2.second = C2 + 1;
		
		double rc3 =
		  ch.IonList[lQ(Z1, C1+1)]->radRecombCross(E1s[1], id).back() * eVels[1];
	      
		Interact::T t(recomb,
			      {Interact::ion,      speciesKey(Z1, C1+1)},
			      {Interact::electron, speciesKey(Z2, 0   )});

		csectionsH[id][i1][i2][t] = rc3 / rvmax * C2  * crs_units *
		  meanF[id][k1] * meanF[id][k2] / (tot*tot);

		rc3 = ch.IonList[lQ(Z2, C2+1)]->radRecombCross(E2s[1], id).back() * eVels[1];
		
		std::get<1>(t) = {Interact::electron, speciesKey(Z1, 0   )};
		std::get<2>(t) = {Interact::ion,      speciesKey(Z2, C2+1)};

		csectionsH[id][i2][i1][t] = rc3 / rvmax * C1  * crs_units *
		  meanF[id][k1] * meanF[id][k2] / (tot*tot);
	      }
	    }
	    
	  } else {
	    

	    // Neutral-Electron cross section
	    //
	    for (unsigned short C2=1; C2<=Z2; C2++) {
	      
	      Interact::T t(neut_elec,
			    {Interact::neutral,  speciesKey(Z1, 1)},
			    {Interact::electron, speciesKey(Z2, 0)});

	      csectionsH[id][i1][i2][t] =
		std::max<double>(
		  { elastic(i1.first, E1s[0]) * eVels[0],
		    elastic(i1.first, E1s[1]) * eVels[1],
		    elastic(i1.first, E1s[2]) * eVels[2] }
		  ) / rvmax * neut1 * elec2 *
		crossfac * crs_units * cscl_[i1.first] * cscl_[i2.first];
	    }

	    for (unsigned short C1=1; C1<=Z1; C1++) {

	      Interact::T t(neut_elec,
			    {Interact::neutral,  speciesKey(Z2, 1)},
			    {Interact::electron, speciesKey(Z1, 0)});

	      csectionsH[id][i2][i1][t] =
		std::max<double>(
		  { elastic(i2.first, E2s[0]) * eVels[0],
		    elastic(i2.first, E2s[1]) * eVels[1],
		    elastic(i2.first, E2s[2]) * eVels[2] }
		  ) / rvmax * neut2 * elec1 *
		crossfac * crs_units * cscl_[i1.first] * cscl_[i2.first];
	    }
	    
	    // Neutral-proton cross section
	    //
	    if (i1.second==1 and i2.first==1) {
	      
	      Interact::T t(neut_prot,
			    {Interact::neutral, speciesKey(Z1, 1)},
			    {Interact::ion,     speciesKey(1, 2)});

	      csectionsH[id][i1][i2][t] =
		std::max<double>(
		  { elastic(i1.first, Eii[0], Elastic::proton) * iVels[0],
		    elastic(i1.first, Eii[1], Elastic::proton) * iVels[1],
		    elastic(i1.first, Eii[2], Elastic::proton) * iVels[2] }
		  ) / rvmax * neut1 * elec2 *
		crossfac * crs_units * cscl_[i1.first] * cscl_[i2.first];
	    }
	      
	    if (i2.second==1 and i1.first==1) {
	      
	      Interact::T t(neut_prot,
			    {Interact::ion,     speciesKey(1, 2)},
			    {Interact::neutral, speciesKey(Z2, 1)});

	      csectionsH[id][i1][i2][t] =
		std::max<double>(
		  { elastic(i2.first, Eii[0], Elastic::proton) * iVels[0],
		    elastic(i2.first, Eii[1], Elastic::proton) * iVels[1],
		    elastic(i2.first, Eii[2], Elastic::proton) * iVels[2] }
		  ) / rvmax * neut2 * elec1 *
		crossfac * crs_units * cscl_[i1.first] * cscl_[i2.first];
	    }
	      
	    
	    // Coulombic (Rutherford) cross section (ion-electron)
	    //
	    for (unsigned short C1=1; C1<=Z1; C1++) {
	      k1.second = C1 + 1;

	      std::vector<double> cc3;
	      for (size_t u=0; u<3; u++) {
		double b = 0.5*esu*esu*C1 /
		  std::max<double>(E1s[u]*eV, FloorEv*eV) * 1.0e7; // nm

		b = std::min<double>(b, ips);

		if (coulInter) {
		  double b_max = sqrt(1.0/(M_PI*pow(numIf[id], 2.0/3.0)));
		  std::min<double>(b, b_max);
		}

		cc3.push_back(M_PI*b*b * eVels[u] / rvmax);
	      }
	      std::sort(cc3.begin(), cc3.end());
	      
	      double mfac = 4.0 * logL;
	      for (unsigned short C2=1; C2<=Z2; C2++) {
		k2.second = C2 + 1;
		
		Interact::T t(ion_elec,
			      {Interact::ion,      speciesKey(Z1, C1+1)},
			      {Interact::electron, speciesKey(Z2, 0   )});

		csectionsH[id][i1][i2][t] = cc3[1] * C2 * mfac *
		  meanF[id][k1] * meanF[id][k2] / (tot*tot) *
		  crossfac * crs_units * cscl_[i1.first] * cscl_[i2.first];
		
		// Test
		if (init_dbg and myid==0 and tnow > init_dbg_time and Z1 == init_dbg_Z) {
		  
		  std::ofstream out(runtag + ".heplus_test_cross", ios::out | ios::app);
		  std::ostringstream sout;
		  
		  sout << " #1 [" << Z1 << ", " << Z2 << "] "
		       << "("  << C1 << ", " << C2 << ") = ";
		  
		  out << "Time = " << std::setw(10) << tnow
		      << sout.str() << csectionsH[id][i1][i2][t] << std::endl;
		}
		// End Test

	      }
	    }

	    for (unsigned short C2=1; C2<=Z2; C2++) {
	      k2.second = C2 + 1;
	      
	      std::vector<double> cc3;
	      for (size_t u=0; u<3; u++) {
		double b = 0.5*esu*esu*C2 /
		  std::max<double>(E2s[u]*eV, FloorEv*eV) * 1.0e7; // nm

		b = std::min<double>(b, ips);

		if (coulInter) {
		  double b_max = sqrt(1.0/(M_PI*pow(numIf[id], 2.0/3.0)));
		  std::min<double>(b, b_max);
		}

		cc3.push_back(M_PI*b*b * eVels[u] / rvmax);
	      }
	      std::sort(cc3.begin(), cc3.end());
	      
	      double mfac = 4.0 * logL;
	      for (unsigned short C1=1; C1<=Z1; C1++) {
		k1.second = C1 + 1;
		
		Interact::T t(ion_elec,
			      {Interact::ion,      speciesKey(Z2, C2+1)},
			      {Interact::electron, speciesKey(Z1, 0   )});

		csectionsH[id][i2][i1][t] = cc3[1] * C1 * mfac *
		  meanF[id][k1] * meanF[id][k2] / (tot*tot) *
		  crossfac * crs_units * cscl_[i1.first] * cscl_[i2.first];

		// Test
		if (init_dbg and myid==0 and tnow > init_dbg_time and Z1 == init_dbg_Z) {
		  
		  std::ofstream out(runtag + ".heplus_test_cross", ios::out | ios::app);
		  std::ostringstream sout;
		  
		  sout << " #2 [" << Z1 << ", " << Z2 << "] "
		       << "("  << C1 << ", " << C2 << ") = ";
		  
		  out << "Time = " << std::setw(10) << tnow
		      << sout.str() << csectionsH[id][i2][i1][t] << std::endl;
		}
		// End Test
		
	      }
	    }
	    
	    // Coulombic (Rutherford) cross section (ion-ion)
	    //
	    for (unsigned short C1=1; C1<=Z1; C1++) {
	      k1.second = C1 + 1;

	      for (unsigned short C2=1; C2<=Z2; C2++) {
		k2.second = C2 + 1;
		
		std::vector<double> cc3;
		for (size_t u=0; u<3; u++) {
		  double b = 0.5*esu*esu*C1*C2 /
		    std::max<double>(Eii[u]*eV, FloorEv*eV) * 1.0e7; // nm

		  b = std::min<double>(b, ips);

		  if (coulInter) {
		    double b_max = sqrt(1.0/(M_PI*pow(numIf[id], 2.0/3.0)));
		    std::min<double>(b, b_max);
		  }

		  cc3.push_back(M_PI*b*b);
		}
		std::sort(cc3.begin(), cc3.end());
		
		double mfac = 4.0 * logL;
		
		Interact::T t(ion_ion,
			      {Interact::ion, speciesKey(Z1, C1+1)},
			      {Interact::ion, speciesKey(Z2, C2+1)});

		csectionsH[id][i1][i2][t] = cc3[1] * mfac *
		  meanF[id][k1] * meanF[id][k2] / (tot*tot) *
		  crossfac * crs_units * cscl_[i1.first] * cscl_[i2.first];
	      }
	    }

	    // Free-free cross section
	    //
	    for (unsigned short C1=1; C1<=Z1; C1++) {
	      for (unsigned short C2=1; C2<=Z2; C2++) {
		k1.second = C1 + 1;
		k2.second = C2 + 1;

		std::vector<CFreturn> ff3;
		
		for (size_t u=0; u<3; u++) {
		  ff3.push_back(ch.IonList[lQ(Z1, C1+1)]->freeFreeCross(E1s[u], id));
		  ff3.back().first *= eVels[u];
		}
		
		std::sort(ff3.begin(), ff3.end(),
			  [](CFreturn d1, CFreturn d2){return d1.first < d2.first;});
		
		Interact::T t(free_free,
			      {Interact::ion,      speciesKey(Z1, C1+1)},
			      {Interact::electron, speciesKey(Z2, 0   )});

		csectionsH[id][i1][i2][t] =
		  ff3[1].first / rvmax * C2 * crs_units *
		  meanF[id][k1] * meanF[id][k2] / (tot*tot);
		
		ff3.clear();
		
		for (size_t u=0; u<3; u++) {
		  ff3.push_back(ch.IonList[lQ(Z2, C2+1)]->freeFreeCross(E2s[u], id));
		  ff3.back().first *= eVels[u];
		}
		
		std::sort(ff3.begin(), ff3.end(),
			  [](CFreturn d1, CFreturn d2){return d1.first < d2.first;});
		
		std::get<1>(t) = {Interact::electron, speciesKey(Z1, 0   )};
		std::get<2>(t) = {Interact::ion,      speciesKey(Z2, C2+1)};

		csectionsH[id][i1][i2][t] = ff3[1].first / rvmax * C1 * crs_units *
		  meanF[id][k1] * meanF[id][k2] / (tot*tot);
	      }
	    }

	    // Collisional-excitation cross section
	    //
	    for (unsigned short C1=0; C1<Z1; C1++) {
	      for (unsigned short C2=1; C2<=Z2; C2++) {
		k1.second = C1 + 1;
		k2.second = C2 + 1;

		std::vector<double> cc3 = {
		  ch.IonList[lQ(Z1, C1+1)]->collExciteCross(E1s[0], id).back().first * eVels[0],
		  ch.IonList[lQ(Z1, C1+1)]->collExciteCross(E1s[1], id).back().first * eVels[1],
		  ch.IonList[lQ(Z1, C1+1)]->collExciteCross(E1s[2], id).back().first * eVels[2]
		};
		std::sort(cc3.begin(), cc3.end());
		
		Interact::T t(colexcite,
			      {Interact::ion,      speciesKey(Z1, C1+1)},
			      {Interact::electron, speciesKey(Z2, 0   )});

		csectionsH[id][i1][i2][t] = cc3[1] / rvmax * C2 * crs_units *
		  meanF[id][k1] * meanF[id][k2] / (tot*tot);
	      }
	    }
	    
	    for (unsigned short C2=0; C2<Z2; C2++) {
	      for (unsigned short C1=1; C1<=Z1; C1++) {
		k1.second = C1 + 1;
		k2.second = C2 + 1;
		
		std::vector<double> cc3 =
		  {
		    ch.IonList[lQ(Z2, C2+1)]->collExciteCross(E2s[0], id).back().first * eVels[0],
		    ch.IonList[lQ(Z2, C2+1)]->collExciteCross(E2s[1], id).back().first * eVels[1],
		    ch.IonList[lQ(Z2, C2+1)]->collExciteCross(E2s[2], id).back().first * eVels[2]
		  };
		std::sort(cc3.begin(), cc3.end());
	      
		Interact::T t(colexcite,
			      {Interact::electron, speciesKey(Z1, 0   )},
			      {Interact::ion,      speciesKey(Z2, C2+1)});

		csectionsH[id][i1][i2][t] = cc3[1] / rvmax * C1 * crs_units *
		  meanF[id][k1] * meanF[id][k2] / (tot*tot);
	      }
	    }
	    
	    // Ionization cross section
	    //
	    for (unsigned short C1=0; C1<Z1; C1++) {
	      for (unsigned short C2=1; C2<=Z2; C2++) {
		k1.second = C1 + 1;
		k2.second = C2 + 1;
		
		std::vector<double> ic3 =
		  {
		    ch.IonList[lQ(Z1, C1+1)]->directIonCross(E1s[0], id) * eVels[0],
		    ch.IonList[lQ(Z1, C1+1)]->directIonCross(E1s[1], id) * eVels[1],
		    ch.IonList[lQ(Z1, C1+1)]->directIonCross(E1s[2], id) * eVels[2],
		  };
		std::sort(ic3.begin(), ic3.end());
		
		Interact::T t(ionize,
			      {Interact::ion,      speciesKey(Z1, C1+1)},
			      {Interact::electron, speciesKey(Z2, 0   )});

		csectionsH[id][i1][i2][t] = ic3[1] / rvmax *  C2 * crs_units *
		  meanF[id][k1] * meanF[id][k2] / (tot*tot);
	      }
	    }

	    for (unsigned short C2=0; C2<Z2; C2++) {
	      for (unsigned short C1=1; C1<=Z1; C1++) {
		k1.second = C1 + 1;
		k2.second = C2 + 1;
		
		std::vector<double> ic3 =
		  {
		    ch.IonList[lQ(Z2, C2+1)]->directIonCross(E2s[0], id) * eVels[0],
		    ch.IonList[lQ(Z2, C2+1)]->directIonCross(E2s[1], id) * eVels[1],
		    ch.IonList[lQ(Z2, C2+1)]->directIonCross(E2s[2], id) * eVels[2],
		  };
		std::sort(ic3.begin(), ic3.end());
		
		Interact::T t(ionize,
			      {Interact::electron, speciesKey(Z1, 0   )},
			      {Interact::ion,      speciesKey(Z2, C2+1)});

		csectionsH[id][i1][i2][t] = ic3[1] / rvmax *  C1 * crs_units *
		  meanF[id][k1] * meanF[id][k2] / (tot*tot);
	      }
	    }


	    // Recombination cross section
	    //
	    for (unsigned short C1=1; C1<=Z1; C1++) {
	      for (unsigned short C2=1; C2<=Z2; C2++) {
		k1.second = C1 + 1;
		k2.second = C2 + 1;
		
		std::vector<double> rc3 =
		  {
		    ch.IonList[lQ(Z1, C1+1)]->radRecombCross(E1s[0], id).back() * eVels[0],
		    ch.IonList[lQ(Z1, C1+1)]->radRecombCross(E1s[1], id).back() * eVels[1],
		    ch.IonList[lQ(Z1, C1+1)]->radRecombCross(E1s[2], id).back() * eVels[2]
		  };
		std::sort(rc3.begin(), rc3.end());
		
		Interact::T t(recomb,
			      {Interact::ion,      speciesKey(Z1, C1+1)},
			      {Interact::electron, speciesKey(Z2, 0   )});

		csectionsH[id][i1][i2][t] = rc3[1] / rvmax * C2  * crs_units *
		  meanF[id][k1] * meanF[id][k2] / (tot*tot);

		rc3 = 
		  {
		    ch.IonList[lQ(Z2, C2+1)]->radRecombCross(E2s[0], id).back() * eVels[0],
		    ch.IonList[lQ(Z2, C2+1)]->radRecombCross(E2s[1], id).back() * eVels[1],
		    ch.IonList[lQ(Z2, C2+1)]->radRecombCross(E2s[2], id).back() * eVels[2]
		  };
		std::sort(rc3.begin(), rc3.end());
		
		std::get<1>(t) = {Interact::electron, speciesKey(Z1, 0   )};
		std::get<2>(t) = {Interact::ion,      speciesKey(Z2, C2+1)};

		csectionsH[id][i1][i2][t] = rc3[1] / rvmax * C1  * crs_units *
		  meanF[id][k1] * meanF[id][k2] / (tot*tot);
	      }
	    }

	  }

	} // END: "Hybrid"
	else {

	  if (i1.second>1 or i2.second>1) CrossG = 0.0;

	  if (i2.second>1) {
	    if (i1.second==1)
	      Cross1 = elastic(i1.first, EeV*m1/dof2) * eVel2 * ne2;
	    else {
	      double b = 0.5*esu*esu*(i1.second - 1) /
		std::max<double>(Eerg*m1/dof2, FloorEv*eV) * 1.0e7; // nm

	      b = std::min<double>(b, ips);

	      if (coulInter) {
		double b_max = sqrt(1.0/(M_PI*pow(numIf[id], 2.0/3.0)));
		std::min<double>(b, b_max);
	      }

	      double mfac = 4.0 * logL;

	      Cross1 = M_PI*b*b * eVel2 * ne2 * mfac;
	    }
	  }

	  if (i1.second>1) {
	    if (i2.second==1)
	      Cross2 = elastic(i2.first, EeV*m2/dof1) * eVel1 * ne1;
	    else {
	      double b = 0.5*esu*esu*(i2.second - 1) /
		std::max<double>(Eerg*m2/dof1, FloorEv*eV) * 1.0e7; // nm

	      b = std::min<double>(b, ips);

	      if (coulInter) {
		double b_max = sqrt(1.0/(M_PI*pow(numIf[id], 2.0/3.0)));
		std::min<double>(b, b_max);
	      }

	      double mfac = 4.0 * logL;

	      Cross2 = M_PI*b*b * eVel1 * ne1 * mfac;
	    }
	  }

	  csections[id][i1][i2]() =  (CrossG + Cross1 + Cross2) *
	    crossfac * 1e-14 / (TreeDSMC::Lunit*TreeDSMC::Lunit) *
	    cscl_[i1.first] * cscl_[i2.first];

	} // END: "Direct" and "Weight"

      } // END: bodies in cell, inner species loop

    } // END: bodies in cell, outer species loop

  } // END: "Direct", "Weight", "Hybrid"

  // Temporary cross section assignment for new Hybrid method
  //
  if (aType == Hybrid) {
    bool debug_dump = false;
    if (debug_dump) {
      std::cout << std::string(40, '-') << std::endl
		<< "Cell XS"            << std::endl
		<< std::string(40, '-') << std::endl
		<< std::setw( 4) << "Z1"
		<< std::setw( 4) << "Z2"
		<< std::setw(20) << "Interaction"
		<< std::setw( 4) << "C1"
		<< std::setw( 4) << "C2"
		<< std::setw(20) << "XS"
		<< std::endl;
    }
    for (auto it1 : cell->count) {
      speciesKey i1  = it1.first;
      for (auto it2 : cell->count) {
	speciesKey i2  = it2.first;
	double total = 0.0;
	for (auto i : csectionsH[id][i1][i2].v) total += i.second;
	csections[id][i1][i2]() = total;
	if (debug_dump) {
	  for (auto i : csectionsH[id][i1][i2].v) {
	    std::cout << std::setw( 4) << i1.first
		      << std::setw( 4) << i2.first
		      << std::setw(40) << i.first
		      << std::setw(20) << i.second
		      << std::endl;
	  }
	}
      }
    }
  } // END: Hybrid

  if (aType == Trace) {

    // In the trace computation, all superparticles are identical!
    // Hence, the use of the defaultKey.  This is slightly inefficient
    // but allows us to reuse the code for both the direct and trace
    // computations.
    //
    csections[id][Particle::defaultKey][Particle::defaultKey]() = 0.0;

    if (use_ntcdb) {

      // Compute mean weights in the cell
      //
      // In auto iterators below:
      //    s is of type std::map<speciesKey, int>
      //    b is of type std::vector<unsigned long>
      //
      // Per cell variables:
      //    meanF[id][sp] is the mean number fraction for species sp
      //    meanE[id]     is the mean number of electrons per particle
      //    meanR[id]     is the mean effective cross-section radius
      //    neutF[id]     is the neutral number fraction
      //    meanM[id]     is the mean molecular weight
      //    numEf[id]     is the effective number of particles
      //    densE[id][sp] is the density of electrons in each cell per species sp
      //    elecDen[id]   is the total density of electrons in each cell
      //    elecDn2[id]   is the mean-squared density of electrons in each cell
      //    elecCnt[id]   is the number of samples in elecDen and elecDn2
      //
      
				// Mean fraction in trace species
				//
      for (auto s : SpList) meanF[id][s.first] = 0.0;
      neutF[id] = meanE[id] = meanR[id] = meanM[id] = cellM[id] = 0.0;

				// Total mass of all particles and
				// relative fraction of trace species
				// and elctron number fraction in this
				// cell
      double massP = 0.0, numbP = 0.0;
      for (auto b : cell->bods) {
				// Particle mass accumulation
	Particle *p = tree->Body(b);
	massP      += p->mass;
	// Mass-weighted trace fraction
	for (auto s : SpList) {
	  speciesKey k = s.first;
	  double ee    = k.second - 1;
	  double ww    = p->dattrib[s.second]/atomic_weights[k.first];
	  
	  // Mean number fraction
	  meanF[id][s.first] += p->mass * ww;
	  // Mean electron number
	  meanE[id]          += p->mass * ww * ee;
	  // Mean number
	  numbP              += p->mass * ww;
	  
	  // For neutrals only
	  if (ee==0) {
	    // Mean particle radius
	    meanR[id]        += p->mass * ww * geometric(k.first);
	    neutF[id]        += p->mass * ww;
	  }
	}

      } // END: bods loop
				// Normalize mass-weighted fraction
				// and electron fraction
				//
      cellM[id] += massP;
      if (massP>0.0) {
	for (auto s : SpList) meanF[id][s.first] /= massP;
	meanE[id] /= massP;
	neutF[id] /= massP;
      }
      if (neutF[id]>0.0) meanR[id] /= neutF[id];
      if (numbP    >0.0) meanM[id]  = massP/numbP;
      
				// Electron velocity factor for this
				// cell
      double eVel = sqrt(amu*meanM[id]/me);

				// Compute neutral and Coulombic cross
				// sections
      for (auto s : SpList) {

	speciesKey k = s.first;
				// Reduced mass for this interation
				//
	double mu = 0.5*meanM[id];

				// Cumulative cross section
				//
	double Cross = 0.0;

				// This species is a neutral
	if (k.second == 1) {
				// Neutral-neutral scattering
				// cross section
	  double Radius = geometric(k.first) + meanR[id];
	  Cross += neutF[id] * M_PI*Radius*Radius * crossfac;


				// Neutral-charged elastic cross section
	  if (meanE[id] > 0.0)	// (recall Eerg and EeV are the mean
				// interparticle KE)
	    Cross += elastic(k.first, EeV * mu) * eVel * meanE[id] * crossfac;

	} else {		// This species is an ion

				// Coulombic elastic scattering
	  double b = 0.5*esu*esu*(k.second - 1) /
	    std::max<double>(Eerg*mu, FloorEv*eV) * 1.0e7; // nm

	  b = std::min<double>(b, ips);

	  if (coulInter) {
	    double b_max = sqrt(1.0/(M_PI*pow(numIf[id]*TreeDSMC::Munit/amu, 2.0/3.0)));
	    std::min<double>(b, b_max);
	  }

	  double mfac = 4.0 * logL;
	  double crs = M_PI*b*b * mfac;
	  
	  Cross += crs * eVel * meanE[id] * crossfac;
	  if (coulScale) {
	    coulCrs[id][k.second - 1][0] = crs;
	    coulCrs[id][k.second - 1][1] = EeV * mu;
	  }
	}
	
	double tCross = Cross * crs_units * cscl_[k.first];

	csections[id][Particle::defaultKey][Particle::defaultKey]() += tCross * meanF[id][k];
      }
    } // NTC per-cell cross section estimation
    else {

      meanM[id] = 0.0;

      size_t nbods = cell->bods.size();
      double massP = 0.0, numbP = 0.0, massE = 0.0;
      double evel2 = 0.0, ivel2 = 0.0;

      meanM[id] = 0.0;

      for (size_t i=0; i<nbods; i++) {
				// The particle
	Particle *p = tree->Body(cell->bods[i]);

				// Mass per cell
	massP += p->mass;
	
				// Mass-weighted trace fraction
	double ee = 0.0;
	for (auto s : SpList) {
	  double ww    = p->dattrib[s.second]/atomic_weights[s.first.first];
				// Mean number
	  numbP += p->mass * ww;
				// Electron fraction
	  ee += p->dattrib[s.second] * (s.first.second - 1);
	}

	double eVel2 = 0.0, iVel2 = 0.0;
	for (int l=0; l<3; l++) {
	  double ve  = p->dattrib[use_elec+l];
	  eVel2 += ve*ve;
	  double vi  = p->vel[l];
	  iVel2 += vi*vi;
	}

	evel2 += p->mass * ee * eVel2;
	ivel2 += p->mass * iVel2;
	massE += p->mass * ee;
      }

      if (numbP>0.0) meanM[id] = massP/numbP;
      if (massP>0.0) Ivel2[id] = ivel2/massP;
      if (massE>0.0) Evel2[id] = evel2/massE;

    } // END: no NTC, estimate plasma cross section only

    Eion[id] = Eelc[id] = 0.0;
    double massP = 0.0, numbP = 0.0, molW = 0.0;
    for (auto b : cell->bods) {
      // Particle mass accumulation
      Particle *p = tree->Body(b);
      massP      += p->mass;
      double wghtE = 0.0;
      // Mass-weighted trace fraction
      for (auto s : SpList) {
	speciesKey k = s.first;
	double ee    = k.second - 1;
	double ww    = p->dattrib[s.second]/atomic_weights[k.first];
	molW        += p->mass * ww;
	wghtE       += p->mass * ww * ee;
      }
	  
      double kEelc = 0.0, kEion = 0.0;
      for (int l=0; l<3; l++) {
	double vi = p->vel[l];
	double ve = p->dattrib[use_elec+l];
	kEion += vi*vi;
	kEelc += ve*ve;
      }
      Eelc[id] += wghtE * kEelc;
      Eion[id] += p->mass * kEion;
      numbP    += wghtE;
    } // END: bods loop

    // Mean KE for Coulombic cross section estimate
    //
    if (massP>0.0) {
      double fac = 0.5 * amu * TreeDSMC::Vunit * TreeDSMC::Vunit / eV;
      Eelc[id] *= fac * atomic_weights[0] / numbP;
      Eion[id] *= fac / molW;
    }

    // Compute per channel Coulombic probabilities
    //
    // Ion probabilities
    //
    double muii = meanM[id]/2.0;
    double muee = atomic_weights[0]/2.0;
    double muie = atomic_weights[0] * meanM[id]/(atomic_weights[0] + meanM[id]);

				// Ion-Ion
    PiProb[id][0] =
      densQtot +
      densEtot * pow(2.0*Ivel2[id], 1.5) * muii*muii /
      (pow(Ivel2[id] + Evel2[id], 1.5) * muie*muie * numQ2[id]);
    //                                               ^
    //                                               |
    // The density is weighted by q^2 for each species

				// Ion-Electron
    PiProb[id][1] =
      densQtot * pow(Ivel2[id] + Evel2[id], 1.5) * muie*muie * numQ2[id] /
      (pow(2.0*Ivel2[id], 1.5) * muii*muii) +
      densEtot ;

				// Electron-Ion
    PiProb[id][2] =
      densQtot +
      densEtot * pow(Ivel2[id] + Evel2[id], 1.5) * muie*muie /
      (pow(2.0*Evel2[id], 1.5) * muee*muee * numQ2[id]);

				// Electron-Electron
    PiProb[id][3] =
      densQtot * pow(2.0*Evel2[id], 1.5) * muee*muee * numQ2[id] /
      (pow(Ivel2[id] + Evel2[id], 1.5) * muie*muie) +
      densEtot;

				// Sanity check
    double test1 = densQtot/PiProb[id][0] + densEtot/PiProb[id][1];
    double test2 = densQtot/PiProb[id][2] + densEtot/PiProb[id][3];

    if (fabs(test1-1.0) > 1.0e-4) { std::cout << "test1 error" << std::endl; }
    if (fabs(test2-1.0) > 1.0e-4) { std::cout << "test2 error" << std::endl; }

				// Rate coefficients
    ABrate[id][0] = 2.0*M_PI * PiProb[id][0] * logL * pow(numQ2[id]*numQ2[id], 2.0);

    ABrate[id][1] = 2.0*M_PI * PiProb[id][1] * logL * pow(numQ2[id], 2.0);

    ABrate[id][2] = 2.0*M_PI * PiProb[id][2] * logL * pow(numQ2[id], 2.0);

    ABrate[id][3] = 2.0*M_PI * PiProb[id][3] * logL ;

    if (not use_ntcdb) {

      size_t nbods = cell->bods.size();
      double Cross = 0.0, Count = 1.0e-18;

      for (size_t i=0; i<nbods; i++) {
	Particle *p1 = tree->Body(cell->bods[i]);

	for (size_t j=i+1; j<nbods; j++) {
	  Particle *p2 = tree->Body(cell->bods[j]);

	  double cr = 0.0;
	  for (size_t k=0; k<3; k++) {
	    double dvel = p1->vel[k] - p2->vel[k];
	    cr += dvel*dvel;
	  }
	
	  // Record the maximum cross section (already in system units)
	  // Cross = std::max<double>(Cross, crossSectionTrace(id, cell, p1, p2, sqrt(cr)));
	  Cross += crossSectionTrace(id, cell, p1, p2, sqrt(cr));
	  Count += 1.0;
	}
      }

      csections[id][Particle::defaultKey][Particle::defaultKey]() = Cross/Count;

    } // END: not use_ntcdb
    
  } // END: Trace
}


Collide::sKey2Amap&
CollideIon::totalScatteringCrossSections(double crm, pCell* const c, int id)
{
  // it1 and it2 are of type std::map<speciesKey, unsigned>

  double vel  = crm * TreeDSMC::Vunit;
  double Eerg = 0.5*vel*vel*amu;
  double EeV  = Eerg / eV;

  // Mean interparticle spacing
  //
  double  volc = c->Volume();
  double   ips = DBL_MAX;
  if (IPS) ips = pow(volc, 0.333333) * TreeDSMC::Lunit * 1.0e7;


  if (aType == Direct or aType == Weight or aType == Hybrid) {

    for (auto it1 : c->count) {

      speciesKey i1 = it1.first;
      double geom1  = geometric(i1.first);

      for (auto it2 : c->count) {

	speciesKey i2 = it2.first;
	double geom2  = geometric(i2.first);

	double m0     = atomic_weights[i1.first] * atomic_weights[i2.first] /
	  (atomic_weights[i1.first] + atomic_weights[i2.first]);
	double m1     = atomic_weights[i1.first];
	double m2     = atomic_weights[i2.first];

	double ne1    = i1.second - 1;
	double ne2    = i2.second - 1;

	double dof1   = 1.0 + ne1;
	double dof2   = 1.0 + ne2;

	if (NO_DOF) dof1 = dof2 = 1.0;

	double eVel1  = sqrt(atomic_weights[i1.first]/atomic_weights[0]/dof1);
	double eVel2  = sqrt(atomic_weights[i2.first]/atomic_weights[0]/dof2);

	if (use_elec) {
	  if (crm>0.0) eVel1 = eVel2 = Evel[id]/crm;
	  else         eVel1 = eVel2 = 0.0;
	}

	if (NO_VEL)   eVel1 = eVel2 = 1.0;

	double Cross1 = 0.0;
	double Cross2 = 0.0;

	if (aType == Hybrid) {

	  unsigned short Z1 = i1.first;
	  unsigned short Z2 = i2.first;

	  speciesKey k1(Z1, 1), k2(Z2, 1);

	  double tot = 0.0;	// meanF normalization
	  for (auto v : meanF[id]) tot += v.second;

	  if (tot <= 0.0) {	// Sanity check
	    std::cout << "*Node#" << std::left << std::setw(3) << myid
		      << " tot="  << std::setw(16) << tot
		      << " mF1="  << std::setw(16) << meanF[id][k1]
		      << " mF2="  << std::setw(16) << meanF[id][k2]
		      << std::endl;
	  }

	  double neut1 = meanF[id][k1]/tot;
	  double neut2 = meanF[id][k2]/tot;

	  double elec1 = 0.0;	// Mean electron number in P1
	  double elec2 = 0.0;	// Mean electron number in P2

	  for (unsigned short C=1; C<=Z1; C++) {
	    k1.second = C + 1;
	    elec1 += meanF[id][k1]*C;
	  }
	  elec1 /= tot;
	  for (unsigned short C=1; C<=Z1; C++) {
	    k2.second = C + 1;
	    elec2 += meanF[id][k2]*C;
	  }
	  elec2 /= tot;


	  double Cross12 = M_PI*(geom1+geom2)*(geom1+geom2) * neut1 * neut2;
	  Cross1 = 0.5*Cross12;
	  Cross2 = 0.5*Cross12;

	  // Electrons in second particle?
	  //
				// Neutral atom-electron scattering
	  for (unsigned short C2=1; C2<=Z2; C2++) {
	    k2.second = C2 + 1;
	    Cross1 += elastic(i1.first, EeV*m1/dof2) * eVel2*C2 *
	      neut1 * meanF[id][k2]/tot;
	  }

				// Rutherford scattering
	  for (unsigned short C1=1; C1<=Z1; C1++) {
	    k1.second = C1 + 1;
	    double b = 0.5*esu*esu*C1 /
	      std::max<double>(Eerg*m1/dof2, FloorEv*eV) * 1.0e7; // nm

	    b = std::min<double>(b, ips);

	    if (coulInter) {
	      double b_max = sqrt(1.0/(M_PI*pow(numIf[id], 2.0/3.0)));
	      std::min<double>(b, b_max);
	    }

	    double mfac = 4.0 * logL;
	    for (unsigned short C2=1; C2<=Z2; C2++) {
	      k2.second = C2 + 1;
	      Cross1 += M_PI*b*b * eVel2 * C2 * mfac *
		meanF[id][k1]/tot * meanF[id][k2]/tot;
	    }
	  }

	  // Electrons in first particle?
	  //
				// Neutral atom-electron scattering
	  for (unsigned short C1=1; C1<=Z1; C1++) {
	    k1.second = C1 + 1;
	    Cross2 += elastic(i2.first, EeV*m2/dof1) * eVel1*C1 *
	      neut2 * meanF[id][k1]/tot;
	  }

				// Rutherford scattering
	  for (unsigned short C2=1; C2<=Z2; C2++) {
	    k2.second = C2 + 1;
	    double b = 0.5*esu*esu*C2 /
	      std::max<double>(Eerg*m2/dof1, FloorEv*eV) * 1.0e7; // nm

	    b = std::min<double>(b, ips);

	    if (coulInter) {
	      double b_max = sqrt(1.0/(M_PI*pow(numIf[id], 2.0/3.0)));
	      std::min<double>(b, b_max);
	    }

	    double mfac = 4.0 * logL;
	    for (unsigned short C1=1; C1<=Z1; C1++) {
	      k1.second = C1 + 1;
	      Cross2 += M_PI*b*b * eVel1 * C1 * mfac *
		meanF[id][k1]/tot * meanF[id][k2]/tot;
	    }
	  }

	  // Ion-ion scattering, Rutherford scattering
	  for (unsigned short C1=1; C1<=Z1; C1++) {
	    k1.second = C1 + 1;
	    for (unsigned short C2=1; C2<=Z2; C2++) {
	      k2.second = C2 + 1;

	      double b = 0.5*esu*esu*C1*C2 /
		std::max<double>(Eerg*m0, FloorEv*eV) * 1.0e7; // nm

	      b = std::min<double>(b, ips);

	      if (coulInter) {
		double b_max = sqrt(1.0/(M_PI*pow(numIf[id], 2.0/3.0)));
		std::min<double>(b, b_max);
	      }

	      double mfac = 4.0 * logL;

	      Cross1 += M_PI*b*b * mfac *
		meanF[id][k1]/tot * meanF[id][k2]/tot;
	    }
	  }

	} // END: type "Hybrid"
	else {

	  // Both particles neutral?
	  //
	  if (i1.second==1 and i2.second==1) {
	    double Cross12 = M_PI*(geom1+geom2)*(geom1+geom2);
	    Cross1 = 0.5*Cross12;
	    Cross2 = 0.5*Cross12;
	  }

	  // Electrons in second particle?
	  //
	  if (ne2) {
	    if (i1.second==1)	// Neutral atom-electron scattering
	      Cross1 = elastic(i1.first, EeV*m1/dof2) * eVel2*ne2;
	    else {		// Rutherford scattering
	      double b = 0.5*esu*esu*(i1.second - 1) /
		std::max<double>(Eerg*m1/dof2, FloorEv*eV) * 1.0e7; // nm

	      b = std::min<double>(b, ips);

	      if (coulInter) {
		double b_max = sqrt(1.0/(M_PI*pow(numIf[id], 2.0/3.0)));
		std::min<double>(b, b_max);
	      }

	      double mfac = 4.0 * logL;
	      Cross1 = M_PI*b*b * eVel2 * ne2 * mfac;
	    }
	  }

	  // Electrons in first particle?
	  //
	  if (ne1) {
	    if (i2.second==1)	// Neutral atom-electron scattering
	      Cross2 = elastic(i2.first, EeV*m2/dof1) * eVel1*ne1;
	    else {		// Rutherford scattering
	      double b = 0.5*esu*esu*(i2.second - 1) /
		std::max<double>(Eerg*m2/dof1, FloorEv*eV) * 1.0e7; // nm

	      b = std::min<double>(b, ips);

	      if (coulInter) {
		double b_max = sqrt(1.0/(M_PI*pow(numIf[id], 2.0/3.0)));
		std::min<double>(b, b_max);
	      }

	      double mfac = 4.0 * logL;
	      Cross2 = M_PI*b*b * eVel1 * ne1 * mfac;
	    }
	  }

	  // Ion-ion scattering
	  //
	  if (ne1>0 and ne2>0) {
	    double b = 0.5*esu*esu*(i1.second - 1)*(i2.second-1) /
	      std::max<double>(Eerg*m0, FloorEv*eV) * 1.0e7; // nm

	    b = std::min<double>(b, ips);

	    if (coulInter) {
	      double b_max = sqrt(1.0/(M_PI*pow(numIf[id], 2.0/3.0)));
	      std::min<double>(b, b_max);
	    }

	    double mfac = 4.0 * logL;
	    Cross1 += M_PI*b*b * mfac;
	  }

	} // END: types "Direct" and "Weight"

	csections[id][i1][i2]() = (Cross1 + Cross2) * crossfac * 1e-14 /
	  (TreeDSMC::Lunit*TreeDSMC::Lunit) *
	  cscl_[i1.first] * cscl_[i2.first];

      }
    }

  }


  if (aType == Trace) {

    csections[id][Particle::defaultKey][Particle::defaultKey]() = 0.0;

    // Compute the mean trace weight in the cell
    //
    // In auto iterators below:
    //    s is of type std::map<speciesKey, int>
    //    b is of type std::vector<unsigned long>
    //
				// Equipartition electron velocity
				//
    double eVel = sqrt(amu*meanM[id]/me);

				// Compute cross sections for all
				// interacting pairs
    for (auto s : SpList) {

      speciesKey k = s.first;
      double Cross = 0.0;

      if (k.second == 1) {
	// Default cross section for neutral cell
	//
	double Radius = geometric(k.first) + meanR[id];
	Cross += neutF[id] * M_PI*Radius*Radius;

	if (meanE[id]>0.0) {

	  // Use neutral-electron
	  //
	  Cross += elastic(k.first, EeV * meanM[id]) * eVel * meanE[id];
	}

      } else {			// This species is an ion

				// Coulumbic elastic scattering
	double b = 0.5*esu*esu*(k.second - 1) /
	  std::max<double>(Eerg*meanM[id], FloorEv*eV) * 1.0e7; // nm

	b = std::min<double>(b, ips);

	if (coulInter) {
	  double b_max = sqrt(1.0/(M_PI*pow(numIf[id]*TreeDSMC::Munit/amu, 2.0/3.0)));
	  std::min<double>(b, b_max);
	}

	double mfac = 4.0 * logL;
	Cross += M_PI*b*b * eVel * meanE[id] * mfac;
      }

      double tCross = Cross * crossfac * 1e-14 /
	(TreeDSMC::Lunit*TreeDSMC::Lunit) * cscl_[k.first];

      csections[id][Particle::defaultKey][Particle::defaultKey]() += tCross * meanF[id][k];
    }
  }

  return csections[id];
}

double CollideIon::crossSectionDirect(int id, pCell* const c,
				      Particle* const p1, Particle* const p2,
				      double cr)
{
  // Mean interparticle spacing
  //
  double  volc = c->Volume();
  double   ips = DBL_MAX;
  if (IPS) ips = pow(volc/numEf[id], 0.333333) * TreeDSMC::Lunit * 1.0e7;

  // Species keys
  //
  KeyConvert k1(p1->iattrib[use_key]);
  KeyConvert k2(p2->iattrib[use_key]);

  unsigned short Z1 = k1.getKey().first, C1 = k1.getKey().second;
  unsigned short Z2 = k2.getKey().first, C2 = k2.getKey().second;

  // Number of atoms in each super particle
  //
  double N1 = (p1->mass*TreeDSMC::Munit)/(atomic_weights[Z1]*amu);
  double N2 = (p2->mass*TreeDSMC::Munit)/(atomic_weights[Z2]*amu);

  // Number of associated electrons for each particle
  //
  double ne1 = C1 - 1;
  double ne2 = C2 - 1;

  // Energy available in the center of mass of the atomic collision
  //
  double m1   = atomic_weights[Z1] * amu;
  double m2   = atomic_weights[Z2] * amu;
  double me   = atomic_weights[0 ] * amu;
  double mu0  = m1 * m2 / (m1 + m2);
  double mu1  = m1;
  double mu2  = m2;
  double vel  = cr * TreeDSMC::Vunit;

  double dof1   = 1.0 + ne1;
  double dof2   = 1.0 + ne2;

  if (NO_DOF) dof1 = dof2 = 1.0;

  // Electron velocity equipartition factors
  //
  double eVel0 = sqrt(mu0/me);
  double eVel1 = sqrt(m1/me/dof1);
  double eVel2 = sqrt(m2/me/dof2);

  if (NO_VEL) {
    eVel0 = eVel1 = eVel2 = 1.0;
  } else if (use_elec) {
    eVel0 = eVel1 = eVel2 = 0.0;
    for (unsigned i=0; i<3; i++) {
      double rvel0 = p1->dattrib[use_elec+i] - p2->dattrib[use_elec+i];
      double rvel1 = p1->dattrib[use_elec+i] - p2->vel[i];
      double rvel2 = p2->dattrib[use_elec+i] - p1->vel[i];
      eVel0 += rvel0*rvel0;
      eVel1 += rvel1*rvel1;
      eVel2 += rvel2*rvel2;
    }
    eVel0 = sqrt(eVel0) * TreeDSMC::Vunit;
    eVel1 = sqrt(eVel1) * TreeDSMC::Vunit;
    eVel2 = sqrt(eVel2) * TreeDSMC::Vunit;
  }

  // Available COM energy
  //
  kEi[id] = 0.5 * mu0 * vel*vel;

  if (use_elec) {
    kEe1[id] = 0.5  * me * eVel2*eVel2;
    kEe2[id] = 0.5  * me * eVel1*eVel1;
    kEee[id] = 0.25 * me * eVel0*eVel0;
  } else {
    kEe1[id] = 0.5 * mu1 * vel*vel/dof2;
    kEe2[id] = 0.5 * mu2 * vel*vel/dof1;
  }

  // These are now ratios
  //
  eVel0 /= vel;
  eVel1 /= vel;
  eVel2 /= vel;

  // Internal energy per particle
  //
  Ein1[id] = Ein2[id] = 0.0;

  if (use_Eint>=0) {
    Ein1[id] = p1->dattrib[use_Eint] * TreeDSMC::Eunit / N1;
    Ein2[id] = p2->dattrib[use_Eint] * TreeDSMC::Eunit / N2;

    // Compute the total available energy and divide among degrees of freedom
    // Convert ergs to eV
    //
    kEe1[id] = (kEe1[id] + Ein1[id]) / eV;
    kEe2[id] = (kEe1[id] + Ein2[id]) / eV;
  } else {
    kEe1[id] /= eV;
    kEe2[id] /= eV;
  }

  kEi[id] /= eV;

  // Save the per-interaction cross sections
  //
  dCross[id].clear();

  // Index the interactions
  //
  dInter[id].clear();

  double sum12 = 0.0;		// Accumulate inelastic total cross
  double sum21 = 0.0;		// sections as we go


  //--------------------------------------------------
  // Total scattering cross section
  //--------------------------------------------------

  double cross00 = 0.0;
  double cross12 = 0.0;
  double cross21 = 0.0;
  double cross1p = 0.0;
  double cross2p = 0.0;

				//-------------------------------
				// Both particles neutral
				//-------------------------------
  if (C1==1 and C2==1) {
				// Geometric cross sections based on
				// atomic radius
    cross12 = geometric(Z1);
    dCross[id].push_back(cross12*crossfac*cscl_[Z1]);
    dInter[id].push_back(neut_neut_1);

    cross21 = geometric(Z2);
    dCross[id].push_back(cross21*crossfac*cscl_[Z2]);
    dInter[id].push_back(neut_neut_2);
  }

				//-------------------------------
				// Electrons in second particle
				//-------------------------------
  if (ne2 > 0) {
    if (C1==1) {		// Neutral atom-electron scattering
      cross12 = elastic(Z1, kEe1[id]) * eVel2 * ne2 * crossfac * cscl_[Z1];
      dCross[id].push_back(cross12);
      dInter[id].push_back(neut_elec_1);
    }  else {			// Rutherford scattering
      double b = 0.5*esu*esu*(C1-1) /
	std::max<double>(kEe1[id]*eV, FloorEv*eV) * 1.0e7; // nm

      b = std::min<double>(b, ips);

      if (coulInter) {
	double b_max = sqrt(1.0/(M_PI*pow(numIf[id], 2.0/3.0)));
	std::min<double>(b, b_max);
      }

      double mfac = 4.0 * logL;
      cross12 = M_PI*b*b * eVel2 * ne2 * crossfac * cscl_[Z1] * mfac;
      dCross[id].push_back(cross12);
      dInter[id].push_back(ion_elec_1);
    }
  }

				//-------------------------------
				// Electrons in first particle
				//-------------------------------
  if (ne1 > 0) {
    if (C2==1) {		// Neutral atom-electron scattering
      cross21 = elastic(Z2, kEe2[id]) * eVel2 * ne2 * crossfac * cscl_[Z2];
      dCross[id].push_back(cross21);
      dInter[id].push_back(neut_elec_2);
    } else {			// Rutherford scattering
      double b = 0.5*esu*esu*(C2-1) /
	std::max<double>(kEe2[id]*eV, FloorEv*eV) * 1.0e7; // nm

      b = std::min<double>(b, ips);

      if (coulInter) {
	double b_max = sqrt(1.0/(M_PI*pow(numIf[id], 2.0/3.0)));
	std::min<double>(b, b_max);
      }

      double mfac = 4.0 * logL;
      cross21 = M_PI*b*b * eVel2 * ne2 * crossfac * cscl_[Z2] * mfac;
      dCross[id].push_back(cross21);
      dInter[id].push_back(ion_elec_2);
    }
  }
				//-------------------------------
				// Atom-ion elastic scattering
				//-------------------------------

  if (Z2==1 and C2==2) {
    if (C1==1) {		// Neutral atom-electron scattering
      cross1p = elastic(Z1, kEi[id]) * vel * crossfac * cscl_[Z1];
      dCross[id].push_back(cross1p);
      dInter[id].push_back(neut_prot_1);
    }
  }

  if (Z1==1 and C2==1) {
    if (C2==1) {		// Neutral atom-electron scattering
      cross2p = elastic(Z2, kEi[id]) * vel * crossfac * cscl_[Z2];
      dCross[id].push_back(cross2p);
      dInter[id].push_back(neut_prot_2);
    }
  }

				//-------------------------------
				// Electrons in first particle
				//-------------------------------
  if (ne1 > 0) {
    if (C2==1) {		// Neutral atom-electron scattering
      cross21 = elastic(Z2, kEe2[id]) * eVel2 * ne2 * crossfac * cscl_[Z2];
      dCross[id].push_back(cross21);
      dInter[id].push_back(neut_elec_2);
    } else {			// Rutherford scattering
      double b = 0.5*esu*esu*(C2-1) /
	std::max<double>(kEe2[id]*eV, FloorEv*eV) * 1.0e7; // nm

      b = std::min<double>(b, ips);

      if (coulInter) {
	double b_max = sqrt(1.0/(M_PI*pow(numIf[id], 2.0/3.0)));
	std::min<double>(b, b_max);
      }

      double mfac = 4.0 * logL;
      cross21 = M_PI*b*b * eVel2 * ne2 * crossfac * cscl_[Z2] * mfac;
      dCross[id].push_back(cross21);
      dInter[id].push_back(ion_elec_2);
    }
  }

				//-------------------------------
				// Ion-ion scattering
				//-------------------------------
  if (ne1 > 0 and ne2 > 0) {
    double b = 0.5*esu*esu*(C1-1)*(C2-1) /
      std::max<double>(kEi[id]*eV, FloorEv*eV) * 1.0e7; // in nm

    b = std::min<double>(b, ips);

    if (coulInter) {
      double b_max = sqrt(1.0/(M_PI*pow(numIf[id], 2.0/3.0)));
      std::min<double>(b, b_max);
    }

    double mfac = 4.0 * logL;
    double crossS = M_PI*b*b * crossfac * cscl_[Z1] * cscl_[Z2] * mfac;
    dCross[id].push_back(crossS);
    dInter[id].push_back(ion_ion_1);
    
    cross12 += crossS;
  }

  //--------------------------------------------------
  // Ion keys
  //--------------------------------------------------

  lQ Q1(Z1, C1), Q2(Z2, C2);

  //===================================================================
  //  ___      _                                      _   _    _
  // | _ \_  _| |_   _ _  _____ __ __  _ __  __ _ _ _| |_(_)__| |___
  // |  _/ || |  _| | ' \/ -_) V  V / | '_ \/ _` | '_|  _| / _| / -_)
  // |_|  \_,_|\__| |_||_\___|\_/\_/  | .__/\__,_|_|  \__|_\__|_\___|
  //                                  |_|
  //  _     _                   _   _               _
  // (_)_ _| |_ ___ _ _ __ _ __| |_(_)___ _ _  ___ | |_  ___ _ _ ___
  // | | ' \  _/ -_) '_/ _` / _|  _| / _ \ ' \(_-< | ' \/ -_) '_/ -_)
  // |_|_||_\__\___|_| \__,_\__|\__|_\___/_||_/__/ |_||_\___|_| \___|
  //
  //===================================================================


  //--------------------------------------------------
  // Particle 1 interacts with Particle 2
  //--------------------------------------------------

				//-------------------------------
				// *** Free-free
				//-------------------------------
  if (C1 > 1 and ne2 > 0) {	// Ion and Ion only

    FF1[id] = ch.IonList[Q1]->freeFreeCross(kEe1[id], id);

    double crs = eVel2 * ne2 * FF1[id].first;

    if (crs>0.0) {
      dCross[id].push_back(crs);
      dInter[id].push_back(free_free_1);
      sum12 += crs;
    }
  }
				//-------------------------------
				// *** Collisional excitation
				//-------------------------------
  if (ne2 > 0 and C1 <= Z1) {	// Particle 1 must be bound

    CE1[id] = ch.IonList[Q1]->collExciteCross(kEe1[id], id);

    double crs = eVel2 * ne2 * CE1[id].back().first;

    if (crs>0.0) {
      dCross[id].push_back(crs);
      dInter[id].push_back(colexcite_1);
      sum12 += crs;
    }
  }
				//-------------------------------
				// *** Ionization cross section
				//-------------------------------
  if (ne2 > 0 and C1 <= Z1) {	// Particle 1 must be bound

    double DI1 = ch.IonList[Q1]->directIonCross(kEe1[id], id);
    double crs = eVel2 * ne2 * DI1;

    if (crs>0.0) {
      dCross[id].push_back(crs);
      dInter[id].push_back(ionize_1);
      sum12 += crs;
    }
  }
				//-------------------------------
				// *** Radiative recombination
				//-------------------------------
  if (C1 > 1 and ne2 > 0) {	// Particle 1 must be an ion

    std::vector<double> RE1 = ch.IonList[Q1]->radRecombCross(kEe1[id], id);
    double crs = eVel2 * ne2 * RE1.back();

    if (crs>0.0) {
      dCross[id].push_back(crs);
      dInter[id].push_back(recomb_1);
      sum12 += crs;
    }
  }


  //--------------------------------------------------
  // Particle 2 interacts with Particle 1
  //--------------------------------------------------

				//-------------------------------
				// *** Free-free
				//-------------------------------
  if (C2 > 1 and ne1 > 0) {
    FF2[id] = ch.IonList[Q2]->freeFreeCross(kEe2[id], id);

    double crs = eVel1 * ne1 * FF2[id].first;

    if (crs>0.0) {
      dCross[id].push_back(crs);
      dInter[id].push_back(free_free_2);
      sum21 += crs;
    }
  }
				//-------------------------------
				// *** Collisional excitation
				//-------------------------------
  if (ne1 > 0 and C2 <= Z2) {

    CE2[id] = ch.IonList[Q2]->collExciteCross(kEe2[id], id);
    double crs = eVel1 * ne1 * CE2[id].back().first;

    if (crs>0.0) {
      dCross[id].push_back(crs);
      dInter[id].push_back(colexcite_2);
      sum21 += crs;
    }
  }
				//-------------------------------
				// *** Ionization cross section
				//-------------------------------
  if (ne1 > 0 and C2 <= Z2) {
    double DI2 = ch.IonList[Q2]->directIonCross(kEe2[id], id);
    double crs = eVel1 * ne1 * DI2;

    if (crs>0.0) {
      dCross[id].push_back(crs);
      dInter[id].push_back(ionize_2);
      sum21 += crs;
    }
  }
				//-------------------------------
				// *** Radiative recombination
				//-------------------------------
  if (C2 > 1 and ne1 > 0) {
    std::vector<double> RE2 = ch.IonList[Q2]->radRecombCross(kEe2[id], id);
    double crs = eVel1 * ne1 * RE2.back();

    if (crs>0.0) {
      dCross[id].push_back(crs);
      dInter[id].push_back(recomb_2);
      sum21 += crs;
    }
  }
				//-------------------------------
				// *** Convert to system units
				//-------------------------------
  return (cross00 + cross12 + cross21 + cross1p + cross2p + sum12 + sum21) *
    1e-14 / (TreeDSMC::Lunit*TreeDSMC::Lunit);
}

double CollideIon::crossSectionWeight
(int id, pCell* const c, Particle* const _p1, Particle* const _p2, double cr)
{
  Particle* p1 = _p1;		// Pointer copies
  Particle* p2 = _p2;

  // Mean interparticle spacing
  //
  double  volc = c->Volume();
  double   ips = DBL_MAX;
  if (IPS) ips = pow(volc/numEf[id], 0.333333) * TreeDSMC::Lunit * 1.0e7;

  // Species keys
  //
  KeyConvert k1(p1->iattrib[use_key]);
  KeyConvert k2(p2->iattrib[use_key]);

  unsigned short Z1 = k1.getKey().first, C1 = k1.getKey().second;
  unsigned short Z2 = k2.getKey().first, C2 = k2.getKey().second;

  if (ZWList[Z1] < ZWList[Z2]) {
    Particle *pT = p1;
    p1 = p2;
    p2 = pT;

    k1 = KeyConvert(p1->iattrib[use_key]);
    k2 = KeyConvert(p2->iattrib[use_key]);

    Z1 = k1.getKey().first;
    C1 = k1.getKey().second;
    Z2 = k2.getKey().first;
    C2 = k2.getKey().second;
  }

  // Number of atoms in each super particle
  //
  double N1   = p1->mass*TreeDSMC::Munit/amu / atomic_weights[Z1];
  double N2   = p2->mass*TreeDSMC::Munit/amu / atomic_weights[Z2];

  // Number of associated electrons for each particle
  //
  double ne1  = C1 - 1;
  double ne2  = C2 - 1;

  double dof1 = 1.0 + ne1;
  double dof2 = 1.0 + ne2;

  if (NO_DOF) dof1 = dof2 = 1.0;

  // Energy available in the center of mass of the atomic collision
  //
  double vel = cr * TreeDSMC::Vunit;
  double m1  = atomic_weights[Z1]*amu;
  double m2  = atomic_weights[Z2]*amu;
  double me  = atomic_weights[ 0]*amu;
  double mu0 = m1 * m2 / (m1 + m2);
  double mu1 = m1 * me / (m1 + me);
  double mu2 = me * m2 / (me + m2);

  // Electron velocity equipartition factors
  //
  double eVel0 = sqrt(mu0/me);
  double eVel1 = sqrt(m1/me/dof1);
  double eVel2 = sqrt(m2/me/dof2);

  if (NO_VEL) {
    eVel0 = eVel1 = eVel2 = 1.0;
  } else if (use_elec) {
    eVel0 = eVel1 = eVel2 = 0.0;
    for (unsigned i=0; i<3; i++) {
      double rvel0 = p1->dattrib[use_elec+i] - p2->dattrib[use_elec+i];
      double rvel1 = p1->dattrib[use_elec+i] - p2->vel[i];
      double rvel2 = p2->dattrib[use_elec+i] - p1->vel[i];
      eVel0 += rvel0*rvel0;
      eVel1 += rvel1*rvel1;
      eVel2 += rvel2*rvel2;
    }
    eVel0 = sqrt(eVel0) * TreeDSMC::Vunit;
    eVel1 = sqrt(eVel1) * TreeDSMC::Vunit;
    eVel2 = sqrt(eVel2) * TreeDSMC::Vunit;

    eVel0   /= vel;		// These are now ratios
    eVel1   /= vel;
    eVel2   /= vel;
  }


  // Available COM energy
  //
  kEi [id] = 0.5 * mu0 * vel*vel;
  kEe1[id] = 0.5 * mu1 * vel*vel * eVel2*eVel2/dof2;
  kEe2[id] = 0.5 * mu2 * vel*vel * eVel1*eVel1/dof1;
  kEee[id] = 0.25 * me * vel*vel * eVel0*eVel0;

  // Internal energy per particle
  //
  Ein1[id] = Ein2[id] = 0.0;

  if (use_Eint>=0) {
    Ein1[id] = p1->dattrib[use_Eint] * TreeDSMC::Eunit / N1;
    Ein2[id] = p2->dattrib[use_Eint] * TreeDSMC::Eunit / N2;

    // Compute the total available energy and divide among degrees of freedom
    // Convert ergs to eV
    //
    kEe1[id] = (kEe1[id] + Ein1[id]) / eV;
    kEe2[id] = (kEe1[id] + Ein2[id]) / eV;
  } else {
    kEe1[id] /= eV;
    kEe2[id] /= eV;
  }

  kEi[id] /= eV;

  // Save the per-interaction cross sections
  //
  dCross[id].clear();

  // Index the interactions
  //
  dInter[id].clear();

  double sum12 = 0.0;		// Accumulate inelastic total cross
  double sum21 = 0.0;		// sections as we go

  //--------------------------------------------------
  // Total scattering cross section
  //--------------------------------------------------

  double cross00 = 0.0;
  double cross12 = 0.0;
  double cross21 = 0.0;
  double cross1p = 0.0;
  double cross2p = 0.0;

				//-------------------------------
				// Both particles neutral
				//-------------------------------
  if (C1==1 and C2==1) {
				// Geometric cross sections based on
				// atomic radius
    cross12 = geometric(Z1);
    dCross[id].push_back(cross12*crossfac*cscl_[Z1]);
    dInter[id].push_back(neut_neut_1);

    cross21 = geometric(Z2);
    dCross[id].push_back(cross21*crossfac*cscl_[Z2]);
    dInter[id].push_back(neut_neut_2);
  }

				//-------------------------------
				// Electrons in second particle
				//-------------------------------
  if (ne2 > 0) {
    if (C1==1) {		// Neutral atom-electron scattering
      cross12 = elastic(Z1, kEe1[id]) * eVel2 * ne2 * crossfac * cscl_[Z1];
      dCross[id].push_back(cross12);
      dInter[id].push_back(neut_elec_1);
    }  else {			// Rutherford scattering
      double b = 0.5*esu*esu*(C1-1) /
	std::max<double>(kEe1[id]*eV, FloorEv*eV) * 1.0e7; // nm

      b = std::min<double>(b, ips);

      if (coulInter) {
	double b_max = sqrt(1.0/(M_PI*pow(numIf[id], 2.0/3.0)));
	std::min<double>(b, b_max);
      }

      double mfac = 4.0 * logL;
      cross12 = M_PI*b*b * eVel2 * ne2 * crossfac * cscl_[Z1] * mfac;
      dCross[id].push_back(cross12);
      dInter[id].push_back(ion_elec_1);
    }
  }

				//-------------------------------
				// Electrons in first particle
				//-------------------------------
  if (ne1 > 0) {
    if (C2==1) {		// Neutral atom-electron scattering
      cross21 = elastic(Z2, kEe2[id]) * eVel1 * ne1 * crossfac * cscl_[Z2];
      dCross[id].push_back(cross21);
      dInter[id].push_back(neut_elec_2);
    } else {			// Rutherford scattering
      double b = 0.5*esu*esu*(C2-1) /
	std::max<double>(kEe2[id]*eV, FloorEv*eV) * 1.0e7; // nm

      b = std::min<double>(b, ips);

      if (coulInter) {
	double b_max = sqrt(1.0/(M_PI*pow(numIf[id], 2.0/3.0)));
	std::min<double>(b, b_max);
      }

      double mfac = 4.0 * logL;
      cross21 = M_PI*b*b * eVel1 * ne1 * crossfac * cscl_[Z2] * mfac;
      dCross[id].push_back(cross21);
      dInter[id].push_back(ion_elec_2);
    }
  }

				//-------------------------------
				// Atom-ion elastic scattering
				//-------------------------------

  if (Z2==1 and C2==2) {
    if (C1==1) {		// Neutral atom-electron scattering
      cross1p = elastic(Z1, kEi[id]) * vel * crossfac * cscl_[Z1];
      dCross[id].push_back(cross1p);
      dInter[id].push_back(neut_prot_1);
    }
  }

  if (Z1==1 and C2==1) {
    if (C2==1) {		// Neutral atom-electron scattering
      cross2p = elastic(Z2, kEi[id]) * vel * crossfac * cscl_[Z2];
      dCross[id].push_back(cross2p);
      dInter[id].push_back(neut_prot_2);
    }
  }

				//-------------------------------
				// Ion-ion scattering
				//-------------------------------
  if (ne1 > 0 and ne2 > 0) {
    double b = 0.5*esu*esu*(C1-1)*(C2-1) /
      std::max<double>(kEi[id]*eV, FloorEv*eV) * 1.0e7; // in nm

    b = std::min<double>(b, ips);

    if (coulInter) {
      double b_max = sqrt(1.0/(M_PI*pow(numIf[id], 2.0/3.0)));
      std::min<double>(b, b_max);
    }

    double mfac = 4.0 * logL;
    double crossS = M_PI*b*b * crossfac * cscl_[Z1] * cscl_[Z2] * mfac;
    dCross[id].push_back(crossS);
    dInter[id].push_back(ion_ion_1);

    cross12 += crossS;
  }


  //--------------------------------------------------
  // Ion keys
  //--------------------------------------------------

  lQ Q1(Z1, C1), Q2(Z2, C2);

  //===================================================================
  //  ___      _                                      _   _    _
  // | _ \_  _| |_   _ _  _____ __ __  _ __  __ _ _ _| |_(_)__| |___
  // |  _/ || |  _| | ' \/ -_) V  V / | '_ \/ _` | '_|  _| / _| / -_)
  // |_|  \_,_|\__| |_||_\___|\_/\_/  | .__/\__,_|_|  \__|_\__|_\___|
  //                                  |_|
  //  _     _                   _   _               _
  // (_)_ _| |_ ___ _ _ __ _ __| |_(_)___ _ _  ___ | |_  ___ _ _ ___
  // | | ' \  _/ -_) '_/ _` / _|  _| / _ \ ' \(_-< | ' \/ -_) '_/ -_)
  // |_|_||_\__\___|_| \__,_\__|\__|_\___/_||_/__/ |_||_\___|_| \___|
  //
  //===================================================================


  //--------------------------------------------------
  // Particle 1 interacts with Particle 2
  //--------------------------------------------------

				//-------------------------------
				// *** Free-free
				//-------------------------------
  if (C1 > 1 and ne2 > 0) {	// Ion and Ion only

    FF1[id] = ch.IonList[Q1]->freeFreeCross(kEe1[id], id);

    double crs = eVel2 * ne2 * FF1[id].first;

    if (crs>0.0) {
      dCross[id].push_back(crs);
      dInter[id].push_back(free_free_1);
      sum12 += crs;
    }
  }
				//-------------------------------
				// *** Collisional excitation
				//-------------------------------
  if (ne2 > 0 and C1 <= Z1) {	// Particle 1 must be bound

      CE1[id] = ch.IonList[Q1]->collExciteCross(kEe1[id], id);

      double crs = eVel2 * ne2 * CE1[id].back().first;

      if (crs>0.0) {
	dCross[id].push_back(crs);
	dInter[id].push_back(colexcite_1);
	sum12 += crs;
      }

  }
				//-------------------------------
				// *** Ionization cross section
				//-------------------------------
  if (ne2 > 0 and C1 <= Z1) {	// Particle 1 must be bound

    double DI1 = ch.IonList[Q1]->directIonCross(kEe1[id], id);
    double crs = eVel2 * ne2 * DI1;

    if (crs>0.0) {
      dCross[id].push_back(crs);
      dInter[id].push_back(ionize_1);
      sum12 += crs;
    }
  }
				//-------------------------------
				// *** Radiative recombination
				//-------------------------------
  if (C1 > 1 and ne2 > 0) {	// Particle 1 must be an ion

    std::vector<double> RE1 = ch.IonList[Q1]->radRecombCross(kEe1[id], id);
    double crs = eVel2 * ne2 * RE1.back();

    if (crs>0.0) {
      dCross[id].push_back(crs);
      dInter[id].push_back(recomb_1);
      sum12 += crs;
    }
  }


  //--------------------------------------------------
  // Particle 2 interacts with Particle 1
  //--------------------------------------------------

				//-------------------------------
				// *** Free-free
				//-------------------------------
  if (C2 > 1 and ne1 > 0) {

    FF2[id] = ch.IonList[Q2]->freeFreeCross(kEe2[id], id);

    double crs = eVel1 * ne1 * FF2[id].first;

    if (crs>0.0) {
      dCross[id].push_back(crs);
      dInter[id].push_back(free_free_2);
      sum21 += crs;
    }
  }
				//-------------------------------
				// *** Collisional excitation
				//-------------------------------
  if (ne1 > 0 and C2 <= Z2) {

    CE2[id] = ch.IonList[Q2]->collExciteCross(kEe2[id], id);
    double crs = eVel1 * ne1 * CE2[id].back().first;

    if (crs>0.0) {
      dCross[id].push_back(crs);
      dInter[id].push_back(colexcite_2);
      sum21 += crs;
    }
  }
				//-------------------------------
				// *** Ionization cross section
				//-------------------------------
  if (ne1 > 0 and C2 <= Z2) {

    double DI2 = ch.IonList[Q2]->directIonCross(kEe2[id], id);
    double crs = eVel1 * ne1 * DI2;

    if (crs>0.0) {
      dCross[id].push_back(crs);
      dInter[id].push_back(ionize_2);
      sum21 += crs;
    }
  }
				//-------------------------------
				// *** Radiative recombination
				//-------------------------------
  if (C2 > 1 and ne1 > 0) {

    std::vector<double> RE2 = ch.IonList[Q2]->radRecombCross(kEe2[id], id);
    double crs = eVel1 * ne1 * RE2.back();

    if (crs>0.0) {
      dCross[id].push_back(crs);
      dInter[id].push_back(recomb_2);
      sum21 += crs;
    }
  }

				//-------------------------------
				// *** Convert to system units
				//-------------------------------
  return (cross00 + cross12 + cross21 + cross1p + cross2p + sum12 + sum21) *
    1e-14 / (TreeDSMC::Lunit*TreeDSMC::Lunit);
}


// For debugging.  Set to false for production
//                      |
//                      v
static bool DEBUG_CRS = false;
void trap_crs(double cross)
{
  if (std::isnan(cross)) {
    std::cout << "Cross section is NaN" << std::endl;
  }
}

double CollideIon::crossSectionHybrid
(int id, pCell* const c, Particle* const _p1, Particle* const _p2,
 double cr, const Interact::T& itype)
{
  // Cumulated value of \( \sigma v/v_c \)
  //
  double totalXS = 0.0;

  // Channel probability tally
  //
  for (auto & v : CProb[id]) v = 0.0;

  // Pointer copies
  //
  Particle* p1 = _p1;
  Particle* p2 = _p2;

  // Convert to cross section in system units
  //
  double crs_units = 1e-14 / (TreeDSMC::Lunit*TreeDSMC::Lunit);

  // Mean interparticle spacing
  //
  double  volc = c->Volume();
  double   ips = DBL_MAX;
  if (IPS) ips = pow(volc/numEf[id], 0.333333) * TreeDSMC::Lunit * 1.0e7;

  // Species keys
  //
  KeyConvert k1(p1->iattrib[use_key]);
  KeyConvert k2(p2->iattrib[use_key]);

  unsigned short Z1 = k1.getKey().first;
  unsigned short Z2 = k2.getKey().first;

  // Number of atoms in each super particle
  //
  double N1    = p1->mass*TreeDSMC::Munit/amu / atomic_weights[Z1];
  double N2    = p2->mass*TreeDSMC::Munit/amu / atomic_weights[Z2];

  // Energy available in the center of mass of the atomic collision
  //
  double vel   = cr * TreeDSMC::Vunit;

  double m1    = atomic_weights[Z1]*amu;
  double m2    = atomic_weights[Z2]*amu;
  double me    = atomic_weights[ 0]*amu;

  double mu0   = m1 * m2 / (m1 + m2);
  double mu1   = m1 * me / (m1 + me);
  double mu2   = me * m2 / (me + m2);

  // Ion-ion, ion-electron, and electron-electron relative velocities
  //
  double eVel0 = 0.0;
  double eVel1 = 0.0;
  double eVel2 = 0.0;
  double eVelI = 0.0;
  double sVel1 = 0.0;
  double sVel2 = 0.0;

  // Ion-ion
  for (unsigned i=0; i<3; i++) {
    double rvel = p1->vel[i] - p2->vel[i];
    eVelI += rvel * rvel;
  }

  if (NO_VEL) {
    eVel0 = eVel1 = eVel2 = 1.0;
  } else if (use_elec) {
    eVel0 = eVel1 = eVel2 = 0.0;
    for (unsigned i=0; i<3; i++) {
      // Electron-electron
      double rvel0 = p1->dattrib[use_elec+i] - p2->dattrib[use_elec+i];
 
      // Electron (p1) and Ion (p2)
      double rvel1 = p1->dattrib[use_elec+i] - p2->vel[i];
 
      // Electron (p2) and Ion (p1)
      double rvel2 = p2->dattrib[use_elec+i] - p1->vel[i];

      eVel0 += rvel0*rvel0;
      eVel1 += rvel1*rvel1;
      eVel2 += rvel2*rvel2;

      // Electron (p1) and Ion (p1)
      rvel1 = p1->dattrib[use_elec+i] - p1->vel[i];

      // Electron (p2) and Ion (p2)
      rvel2 = p2->dattrib[use_elec+i] - p2->vel[i];

      sVel1 += rvel1*rvel1;
      sVel2 += rvel2*rvel2;
    }
    eVel0 = sqrt(eVel0) * TreeDSMC::Vunit;
    eVel1 = sqrt(eVel1) * TreeDSMC::Vunit;
    eVel2 = sqrt(eVel2) * TreeDSMC::Vunit;
    sVel1 = sqrt(sVel1) * TreeDSMC::Vunit;
    sVel2 = sqrt(sVel2) * TreeDSMC::Vunit;

    eVel0   /= vel;		// These are now ratios
    eVel1   /= vel;
    eVel2   /= vel;
    sVel1   /= vel;
    sVel2   /= vel;
  }

  // Available COM energy
  //
  kEi [id] = 0.5  * mu0 * vel*vel;
  kEe1[id] = 0.5  * mu1 * vel*vel * eVel2*eVel2;
  kEe2[id] = 0.5  * mu2 * vel*vel * eVel1*eVel1;
  kEee[id] = 0.25 * me  * vel*vel * eVel0*eVel0;
  kE1s[id] = 0.5  * mu1 * vel*vel * sVel1*sVel1;
  kE2s[id] = 0.5  * mu2 * vel*vel * sVel2*sVel2;

  // Internal energy per particle
  //
  Ein1[id] = Ein2[id] = 0.0;

  if (use_Eint>=0) {

    Ein1[id] = p1->dattrib[use_Eint] * TreeDSMC::Eunit / N1;
    Ein2[id] = p2->dattrib[use_Eint] * TreeDSMC::Eunit / N2;

    // Compute the total available energy and divide among degrees of freedom
    // Convert ergs to eV
    //
    kEe1[id] = (kEe1[id] + Ein1[id]) / eV;
    kEe2[id] = (kEe1[id] + Ein2[id]) / eV;
  } else {
    kEe1[id] /= eV;
    kEe2[id] /= eV;
  }

  kEi[id] /= eV;

  // Energy floor
  //
  kEe1[id] = std::max<double>(kEe1[id], FloorEv);
  kEe2[id] = std::max<double>(kEe2[id], FloorEv);
  kEi [id] = std::max<double>(kEi[id],  FloorEv);

  // For verbose diagnostic output only
  //
  if (elecDist) {
    elecEV[id].push_back(kEe1[id]);
    elecEV[id].push_back(kEe2[id]);
  }

  // Erase the list
  //
  hCross[id].clear();

  // Joint species probability
  // -------------------------
  // NB: The first particle (outer loop) is the neutral or ion.  The
  // second particle (outerloop) is the electron or neutral.
  //
  for (unsigned short C1=0; C1<=Z1; C1++) {

    for (unsigned short C2=0; C2<=Z2; C2++) {

      //--------------------------------------------------
      // Ion keys
      //--------------------------------------------------

      lQ Q1(Z1, C1+1), Q2(Z2, C2+1);

      orderedPair op(Z1, Z2), op1(Z1, 1), op2(Z2, 1);

      //--------------------------------------------------
      // Particle 1 interacts with Particle 2
      //--------------------------------------------------
      
      double fac1 = p1->dattrib[spc_pos+C1];
      double fac2 = p2->dattrib[spc_pos+C2];
      double cfac = fac1 * fac2;

      //===================================================================
      //  ___      _                                      _   _    _
      // | _ \_  _| |_   _ _  _____ __ __  _ __  __ _ _ _| |_(_)__| |___
      // |  _/ || |  _| | ' \/ -_) V  V / | '_ \/ _` | '_|  _| / _| / -_)
      // |_|  \_,_|\__| |_||_\___|\_/\_/  | .__/\__,_|_|  \__|_\__|_\___|
      //                                  |_|
      //  _     _                   _   _               _
      // (_)_ _| |_ ___ _ _ __ _ __| |_(_)___ _ _  ___ | |_  ___ _ _ ___
      // | | ' \  _/ -_) '_/ _` / _|  _| / _ \ ' \(_-< | ' \/ -_) '_/ -_)
      // |_|_||_\__\___|_| \__,_\__|\__|_\___/_||_/__/ |_||_\___|_| \___|
      //
      //===================================================================

      //-------------------------------
      // *** Both particles neutral
      //-------------------------------

      if (C1==0 and C2==0) {

	double cross = 0.0;
				// Geometric cross sections based on
				// atomic radius
	double crs1 = geometric(Z1) * cfac;
	
	if (DEBUG_CRS) trap_crs(crs1*crossfac*cscl_[Z1]);
	
	cross += crs1*crossfac*cscl_[Z1];

	double crs2 = geometric(Z2) * cfac;

	if (DEBUG_CRS) trap_crs(crs2*crossfac*cscl_[Z2]);
	
	cross += crs2*crossfac*cscl_[Z2];
	
	Interact::T t
	{ neut_neut, {Interact::neutral, speciesKey(Z1, 1)}, {Interact::neutral, speciesKey(Z2, 1)} };
	
	hCross[id].push_back(XStup(t));
	hCross[id].back().crs = cross;
	
	CProb[id][0] += cross;
	totalXS      += cross;
      }

      // --------------------------------------
      // *** Neutral atom-electron scattering
      // --------------------------------------
      
      if (C1==0 and C2>0) {

	double crs =
	  elastic(Z1, kEe1[id]) * eVel2 *
	  C2 * crossfac * cscl_[Z1] * cfac;
	
	if (DEBUG_CRS) trap_crs(crs);

	Interact::T t
	{ neut_elec, {Interact::ion, speciesKey(Z1, C1+1)}, {Interact::electron, speciesKey(Z2, 0)} };

	hCross[id].push_back(XStup(t));
	hCross[id].back().crs = crs;

	CProb[id][1] += crs;
	totalXS      += crs;
      }

      if (C2==0 and C1>0) {

	double crs =
	  elastic(Z2, kEe2[id]) * eVel1 *
	  C1 * crossfac * cscl_[Z2] * cfac;
	
	if (DEBUG_CRS) trap_crs(crs);

	Interact::T t
	{ neut_elec, {Interact::electron, speciesKey(Z1, 0)}, {Interact::ion, speciesKey(Z2, C2+1)} };
	
	hCross[id].push_back(XStup(t));
	hCross[id].back().crs = crs;

	CProb[id][2] += crs;
	totalXS      += crs;
      }

      // --------------------------------------
      // *** Ion-electron scattering
      // --------------------------------------
      
      if (C1>0 and C2>0) {

	// p1 is ion, p2 is electron
	{
	  double b = 0.5*esu*esu*C1 /
		std::max<double>(kEe1[id]*eV, FloorEv*eV) * 1.0e7; // nm

	  b = std::min<double>(b, ips);
	
	  if (coulInter) {
	    double b_max = sqrt(1.0/(M_PI*pow(numIf[id], 2.0/3.0)));
	    std::min<double>(b, b_max);
	  }

	  double mfac = 4.0 * logL;
	
	  double crs =
	    M_PI*b*b * eVel2 *
	    C2 * crossfac * cscl_[Z1] * mfac * cfac;
	
	  if (DEBUG_CRS) trap_crs(crs);
	
	  Interact::T t
	  { ion_elec, {Interact::ion, speciesKey(Z1, C1+1)}, {Interact::electron, speciesKey(Z2, 0)} };
	  
	  hCross[id].push_back(XStup(t));
	  hCross[id].back().crs = crs;
	  
	  CProb[id][1] += crs;
	  totalXS      += crs;
	}
	  
	// p1 is electron, p2 is ion
	{
	  double b = 0.5*esu*esu*C2 /
	  std::max<double>(kEe2[id]*eV, FloorEv*eV) * 1.0e7; // nm

	  b = std::min<double>(b, ips);

	  if (coulInter) {
	    double b_max = sqrt(1.0/(M_PI*pow(numIf[id], 2.0/3.0)));
	    std::min<double>(b, b_max);
	  }
	
	  double mfac = 4.0 * logL;
	
	  double crs =
	    M_PI*b*b * eVel2 *
	    C1 * crossfac * cscl_[Z2] * mfac * cfac;
	
	  if (DEBUG_CRS) trap_crs(crs);
	
	  Interact::T t
	  { ion_elec, {Interact::electron, speciesKey(Z1, 0)}, {Interact::ion, speciesKey(Z2, C2+1)} };
	  
	  hCross[id].push_back(XStup(t));
	  hCross[id].back().crs = crs;
	  
	  CProb[id][2] += crs;
	  totalXS      += crs;
	}

      } // end: ion-electron scattering

      // --------------------------------------
      // *** Neutral atom-proton scattering
      // --------------------------------------

      if (C1==0 and Z2==1 and C2==1) {
	double crs1 = elastic(Z1, kEi[id], Elastic::proton) *
	  crossfac * cscl_[Z1] * cfac;
	
	if (DEBUG_CRS) trap_crs(crs1);
	
	Interact::T t
	{ neut_prot, {Interact::neutral, speciesKey(Z1, C1+1)}, {Interact::ion, speciesKey(Z2, C2+1)} };

	hCross[id].push_back(XStup(t));
	hCross[id].back().crs = crs1;
	  
	CProb[id][0] += crs1;
	totalXS      += crs1;
      } // end: neutral-proton scattering

      if (C2==0 and Z1==1 and C1==1) {
	double crs1 = elastic(Z2, kEi[id], Elastic::proton) *
	  crossfac * cscl_[Z2] * cfac;
	
	if (DEBUG_CRS) trap_crs(crs1);
	
	Interact::T t
	{ neut_prot, {Interact::ion, speciesKey(Z1, C1+1)}, {Interact::neutral, speciesKey(Z2, C2+1)} };

	hCross[id].push_back(XStup(t));
	hCross[id].back().crs = crs1;
	
	CProb[id][0] += crs1;
	totalXS      += crs1;
      } // end: neutral-proton scattering

      //-------------------------------
      // *** Free-free
      //-------------------------------
      
      if (C1>0 and C2>0) {
	// p1 ion, p2 electron
	{
	  double ke   = std::max<double>(kEe1[id], FloorEv);
	  CFreturn ff = ch.IonList[Q1]->freeFreeCross(ke, id);
	  double crs  = eVel2 * C2 * ff.first * cfac * nselRat[id];
	
	  if (std::isinf(crs)) crs = 0.0; // Sanity check
	
	  if (crs>0.0) {

	    Interact::T t
	    { free_free, {Interact::ion, speciesKey(Z1, C1+1)}, {Interact::electron, speciesKey(Z2, 0)} };
	    
	    hCross[id].push_back(XStup(t));
	    hCross[id].back().crs = crs;
	    hCross[id].back().CF  = ff;

	    CProb[id][1] += crs;
	    totalXS      += crs;
	  }
	}
	// p2 ion, p1 electron
	{
	  double ke   = std::max<double>(kEe2[id], FloorEv);
	  CFreturn ff = ch.IonList[Q2]->freeFreeCross(ke, id);
	  double crs  = eVel1 * C1 * ff.first * cfac * nselRat[id];
	  
	  if (std::isinf(crs)) crs = 0.0; // Sanity check
	  
	  if (crs>0.0) {
	    
	    Interact::T t
	    { free_free, {Interact::electron, speciesKey(Z1, 0)}, {Interact::ion, speciesKey(Z2, C2+1)} };
	    
	    hCross[id].push_back(XStup(t));
	    hCross[id].back().crs = crs;
	    hCross[id].back().CF  = ff;

	    CProb[id][2] += crs;
	    totalXS      += crs;
	  }
	}
      } // end:ion_elec scattering

      //-------------------------------
      // *** Collisional excitation
      //-------------------------------
      
      // p1 nucleus has bound electron, p2 has a free electron
      if (C1<Z1 and C2>0) {
	double ke   = std::max<double>(kEe1[id], FloorEv);
	CEvector CE = ch.IonList[Q1]->collExciteCross(ke, id);
	double crs  = eVel2 * C2 * CE.back().first * cfac * nselRat[id];
	
	if (DEBUG_CRS) trap_crs(crs);
	
	if (crs > 0.0) {
	  Interact::T t
	  { colexcite, {Interact::ion, speciesKey(Z1, C1+1)}, {Interact::electron, speciesKey(Z2, 0)} };

	  hCross[id].push_back(XStup(t));
	  hCross[id].back().crs = crs;
	  hCross[id].back().CE  = CE;

	  CProb[id][1] += crs;
	  totalXS      += crs;
	}
      } // end: colexcite

      // p2 nucleus has bound electron, p1 has a free electron
      if (C2<Z2 and C1>0) {
	double ke   = std::max<double>(kEe2[id], FloorEv);
	CEvector CE = ch.IonList[Q2]->collExciteCross(ke, id);
	double crs  = eVel1 * C1 * CE.back().first * cfac * nselRat[id];
	
	if (DEBUG_CRS) trap_crs(crs);
	
	if (crs > 0.0) {
	  Interact::T t
	  { colexcite, {Interact::electron, speciesKey(Z1, 0)}, {Interact::ion, speciesKey(Z2, C2+1)} };

	  hCross[id].push_back(XStup(t));
	  hCross[id].back().crs = crs;
	  hCross[id].back().CE  = CE;

	  CProb[id][2] += crs;
	  totalXS      += crs;
	}
      } // end: colexcite

      //-------------------------------
      // *** Ionization cross section
      //-------------------------------
      
      // p1 nucleus has bound electron, p2 has a free electron
      if (C1<Z1 and C2>0) {
	
	double ke  = std::max<double>(kEe1[id], FloorEv);
	double DI  = ch.IonList[Q1]->directIonCross(ke, id);
	double crs = eVel2 * C2 * DI * cfac * nselRat[id];
	
	if (DEBUG_CRS) trap_crs(crs);
	
	if (crs > 0.0) {
	  Interact::T t
	  { ionize, {Interact::ion, speciesKey(Z1, C1+1)}, {Interact::electron, speciesKey(Z2, 0)} };

	  hCross[id].push_back(XStup(t));
	  hCross[id].back().crs = crs;

	  CProb[id][1] += crs;
	  totalXS      += crs;
	}
      }  // end: ionize

      // p2 nucleus has bound electron, p1 has a free electron
      if (C2<Z2 and C1>0) {
	
	double ke  = std::max<double>(kEe2[id], FloorEv);
	double DI  = ch.IonList[Q2]->directIonCross(ke, id);
	double crs = eVel1 * C1 * DI * cfac * nselRat[id];
	
	if (DEBUG_CRS) trap_crs(crs);
	
	if (crs > 0.0) {
	  Interact::T t
	  { ionize, {Interact::electron, speciesKey(Z1, 0)}, {Interact::ion, speciesKey(Z2, C2+1)} };

	  hCross[id].push_back(XStup(t));
	  hCross[id].back().crs = crs;

	  CProb[id][2] += crs;
	  totalXS      += crs;
	}
      }  // end: ionize

      //-------------------------------
      // *** Radiative recombination
      //-------------------------------

      // The "new" algorithm uses the electron energy of the ion's
      // electron rather than the standard particle partner.
      //
      if (newRecombAlg) {

	// p1 ion and p1 electron
	if (C1>0) {
	  double ke              = std::max<double>(kE1s[id], FloorEv);
	  std::vector<double> RE = ch.IonList[Q1]->radRecombCross(ke, id);
	  double crs = sVel1 * C1 * RE.back() * cfac * nselRat[id];
	
	  if (DEBUG_CRS) trap_crs(crs);
	
	  if (crs > 0.0) {
	    Interact::T t
	    { recomb, {Interact::ion, speciesKey(Z1, C1+1)}, {Interact::electron, speciesKey(Z1, 0)} };
	    
	    hCross[id].push_back(XStup(t));
	    hCross[id].back().crs = crs;

	    CProb[id][1] += crs;
	    totalXS      += crs;
	  }
	}
	  
	// p2 ion and p1 electron
	if (C2>0) {
	  double ke              = std::max<double>(kE2s[id], FloorEv);
	  std::vector<double> RE = ch.IonList[Q2]->radRecombCross(ke, id);
	  double crs = sVel2 * C2 * RE.back() * cfac * nselRat[id];
	  
	  if (DEBUG_CRS) trap_crs(crs);
	  
	  if (crs > 0.0) {
	    Interact::T t
	    { recomb, {Interact::electron, speciesKey(Z2, 0)}, {Interact::ion, speciesKey(Z2, C2+1)} };
	    
	    hCross[id].push_back(XStup(t));
	    hCross[id].back().crs = crs;

	    CProb[id][2] += crs;
	    totalXS      += crs;
	  }
	}

      } // end: new recomb algorithm
      else {
	if (C1>0 and C2>0) {
	  // p1 ion and p2 electron
	  {
	    double ke              = std::max<double>(kEe1[id], FloorEv);
	    std::vector<double> RE = ch.IonList[Q1]->radRecombCross(ke, id);

	    double crs = eVel2 * C2 * RE.back() * cfac * nselRat[id];
	    
	    if (DEBUG_CRS) trap_crs(crs);
	
	    if (crs > 0.0) {
	      Interact::T t
	      { recomb, {Interact::ion, speciesKey(Z1, C1+1)}, {Interact::electron, speciesKey(Z2, 0)} };
	  
	      hCross[id].push_back(XStup(t));
	      hCross[id].back().crs = crs;

	      CProb[id][1] += crs;
	      totalXS      += crs;
	    }
	  }

	  // p2 ion and p1 electron
	  {
	    double ke              = std::max<double>(kEe2[id], FloorEv);
	    std::vector<double> RE = ch.IonList[Q2]->radRecombCross(ke, id);

	    double crs = eVel1 * C1 * RE.back() * cfac * nselRat[id];
	    
	    if (DEBUG_CRS) trap_crs(crs);
	    
	    if (crs > 0.0) {
	      Interact::T t
	      { recomb, {Interact::electron, speciesKey(Z1, 0)}, {Interact::ion, speciesKey(Z2, C2+1)} };
	      
	      hCross[id].push_back(XStup(t));
	      hCross[id].back().crs = crs;

	      CProb[id][2] += crs;
	      totalXS      += crs;
	    }
	  }

	} // end: original recomb algorithm

      } // end: recombination

    } // end: inner ionization state loop
 
  } // end: outer ionization state loop

  checkProb(id, false, totalXS);

  if (totalXS>0.0) {
    for (auto & v : CProb[id]) v /= totalXS;
  }

  return totalXS * crs_units;
}


double CollideIon::crossSectionTrace(int id, pCell* const c,
				     Particle* const _p1, Particle* const _p2,
				     double cr)
{
  // Channel probability tally
  //
  for (auto & v : CProb[id]) v = 0.0;

  // Pointer copies
  //
  Particle* p1 = _p1;
  Particle* p2 = _p2;

  // Convert to cross section in system units
  //
  double crs_units = 1e-14 / (TreeDSMC::Lunit*TreeDSMC::Lunit);

  // Mean interparticle spacing
  //
  /*
  // Currently unused
  //
  // double  volc  = c->Volume();
  double   ips  = DBL_MAX;
  if (IPS) ips  = pow(volc/numEf[id], 0.333333) * TreeDSMC::Lunit * 1.0e7;
  */

  // Electron fraction and mean molecular weight for each particle
  //
  double Eta1=0.0, Eta2=0.0, Mu1=0.0, Mu2=0.0, Sum1=0.0, Sum2=0.0;

  for (auto s : SpList) {
				// Number fraction of ions
    double one = p1->dattrib[s.second] / atomic_weights[s.first.first];
    double two = p2->dattrib[s.second] / atomic_weights[s.first.first];

				// Electron number fraction
    Eta1 += one * (s.first.second - 1);
    Eta2 += two * (s.first.second - 1);

    Sum1 += one;
    Sum2 += two;
  }
				// The number of electrons per particle
  Eta1 /= Sum1;
  Eta2 /= Sum2;
				// The molecular weight
  Mu1 = 1.0/Sum1;
  Mu2 = 1.0/Sum2;
				// Cache these for later use
  molP1[id] = Mu1;
  molP2[id] = Mu2;

  etaP1[id] = Eta1;
  etaP2[id] = Eta2;

  // Number of atoms in each super particle
  //
  double N1 = p1->mass*TreeDSMC::Munit/(Mu1*amu);
  double N2 = p2->mass*TreeDSMC::Munit/(Mu2*amu);


  // Energy available in the center of mass of the atomic collision
  //
  double vel = cr * TreeDSMC::Vunit;


  // Ion-ion, ion-electron, and electron-electron relative velocities
  //

  double eVel0 = 0.0;		// Electron relative velocities
  double eVel1 = 0.0;
  double eVel2 = 0.0;
  double gVel0 = 0.0;		// Scaled velocities for mean-mass algorithm
  double gVel1 = 0.0;
  double gVel2 = 0.0;
  double eVelI = 0.0;		// Ion relative velocity
  double sVel1 = 0.0;
  double sVel2 = 0.0;
  
  // Ion-ion
  for (unsigned i=0; i<3; i++) {
    double rvel = p1->vel[i] - p2->vel[i];
    eVelI += rvel * rvel;
  }
    
  if (NO_VEL) {
    eVel0 = eVel1 = eVel2 = 1.0;
    gVel0 = gVel1 = gVel2 = 1.0;
  } else if (use_elec) {
    eVel0 = eVel1 = eVel2 = 0.0;
    gVel0 = gVel1 = gVel2 = 0.0;
    for (unsigned i=0; i<3; i++) {
      // Electron-electron
      double rvel0 = p1->dattrib[use_elec+i] - p2->dattrib[use_elec+i];

      // Electron (p1) and Ion (p2)
      double rvel1 = p1->dattrib[use_elec+i] - p2->vel[i];

      // Electron (p2) and Ion (p1)
      double rvel2 = p2->dattrib[use_elec+i] - p1->vel[i];
      
      eVel0 += rvel0*rvel0;
      eVel1 += rvel1*rvel1;
      eVel2 += rvel2*rvel2;
      
      // Scaled electron relative velocity
      if (MeanMass) {
	rvel0 = p1->dattrib[use_elec+i]*sqrt(Eta1/Mu1) - p2->dattrib[use_elec+i]*sqrt(Eta2/Mu2);
	rvel1 = p1->dattrib[use_elec+i]*sqrt(Eta1/Mu1) - p2->vel[i];
	rvel2 = p2->dattrib[use_elec+i]*sqrt(Eta2/Mu2) - p1->vel[i];

	gVel0 += rvel0*rvel0;
	gVel1 += rvel1*rvel1;
	gVel2 += rvel2*rvel2;
      }


      // Electron (p1) and Ion (p1)
      rvel1 = p1->dattrib[use_elec+i] - p1->vel[i];

      // Electron (p2) and Ion (p2)
      rvel2 = p2->dattrib[use_elec+i] - p2->vel[i];
      
      sVel1 += rvel1*rvel1;
      sVel2 += rvel2*rvel2;
    }
    eVel0 = sqrt(eVel0) * TreeDSMC::Vunit;
    eVel1 = sqrt(eVel1) * TreeDSMC::Vunit;
    eVel2 = sqrt(eVel2) * TreeDSMC::Vunit;
    sVel1 = sqrt(sVel1) * TreeDSMC::Vunit;
    sVel2 = sqrt(sVel2) * TreeDSMC::Vunit;
    
    eVel0   /= vel;		// These are now ratios
    eVel1   /= vel;
    eVel2   /= vel;
    sVel1   /= vel;
    sVel2   /= vel;

    // Pick scaled relative velocities for mean-mass algorithm
    if (MeanMass) {
      gVel0 = sqrt(gVel0) * TreeDSMC::Vunit / vel;
      gVel1 = sqrt(gVel1) * TreeDSMC::Vunit / vel;
      gVel2 = sqrt(gVel2) * TreeDSMC::Vunit / vel;
    }
    // Pick true relative velocity for all other algorithms
    else {
      gVel0 = eVel0;
      gVel1 = eVel1;
      gVel2 = eVel2;
    }
  }

  // Erase the cross-section list
  //
  hCross[id].clear();

  // For convenience: proton key
  //
  speciesKey proton(1, 2);

  double m1  = molP1[id]*amu;
  double m2  = molP2[id]*amu;
  double me  = atomic_weights[ 0]*amu;
  
  double mu0 = m1 * m2 / (m1 + m2);
  double mu1 = m1 * me / (m1 + me);
  double mu2 = me * m2 / (me + m2);

  // Available COM energy
  //
				// p1 ion : p2 ion
  kEi [id] = 0.5  * mu0 * vel*vel;
				// p1 ion : p2 electron
  kEe1[id] = 0.5  * mu1 * vel*vel * eVel2*eVel2;
				// p2 ion : p1 electron
  kEe2[id] = 0.5  * mu2 * vel*vel * eVel1*eVel1;
				// p1 electron : p2 electron
  kEee[id] = 0.25 * me  * vel*vel * eVel0*eVel0;
				// p1 ion : p1 electron
  kE1s[id] = 0.5  * mu1 * vel*vel * sVel1*sVel1;
				// p2 ion : p2 electron
  kE2s[id] = 0.5  * mu2 * vel*vel * sVel2*sVel2;

  
  // Internal energy per particle
  //
  Ein1[id] = Ein2[id] = 0.0;

  if (use_Eint>=0) {

    Ein1[id] = p1->dattrib[use_Eint] * TreeDSMC::Eunit / N1;
    Ein2[id] = p2->dattrib[use_Eint] * TreeDSMC::Eunit / N2;

    // Compute the total available energy and divide among degrees of freedom
    // Convert ergs to eV
    //
    kEe1[id] = (kEe1[id] + Ein1[id]) / eV;
    kEe2[id] = (kEe1[id] + Ein2[id]) / eV;
  } else {
    kEe1[id] /= eV;
    kEe2[id] /= eV;
  }
    
  kEi[id] /= eV;

  // Energy floor
  //
  kEe1[id] = std::max<double>(kEe1[id], FloorEv);
  kEe2[id] = std::max<double>(kEe2[id], FloorEv);
  kEi [id] = std::max<double>(kEi [id], FloorEv);
  
  // For verbose diagnostic output only
  //
  if (elecDist) {
    elecEV[id].push_back(kEe1[id]);
    elecEV[id].push_back(kEe2[id]);
  }


  //===================================================================
  //  ___      _                                      _   _    _
  // | _ \_  _| |_   _ _  _____ __ __  _ __  __ _ _ _| |_(_)__| |___
  // |  _/ || |  _| | ' \/ -_) V  V / | '_ \/ _` | '_|  _| / _| / -_)
  // |_|  \_,_|\__| |_||_\___|\_/\_/  | .__/\__,_|_|  \__|_\__|_\___|
  //                                  |_|
  //  _     _                   _   _               _
  // (_)_ _| |_ ___ _ _ __ _ __| |_(_)___ _ _  ___ | |_  ___ _ _ ___
  // | | ' \  _/ -_) '_/ _` / _|  _| / _ \ ' \(_-< | ' \/ -_) '_/ -_)
  // |_|_||_\__\___|_| \__,_\__|\__|_\___/_||_/__/ |_||_\___|_| \___|
  //
  //===================================================================
      
  // Loop through p1 baryon interactions with neutral atom and
  // electrons of p2
  //
  for (auto s : SpList) {

    // Species keys
    //
    speciesKey k = s.first;

    unsigned short Z = k.first;
    unsigned short C = k.second;
    unsigned short P = C - 1;

    //--------------------------------------------------
    // Ion key
    //--------------------------------------------------

    lQ Q(Z, C);

    //--------------------------------------------------
    // Ion interaction for species k
    //--------------------------------------------------
    
    Interact::pElem Ion {Interact::ion, k};

    //--------------------------------------------------
    // Fraction in this state
    //--------------------------------------------------

    double fac1 = p1->dattrib[s.second] / atomic_weights[Z] / Sum1;
    double fac2 = p2->dattrib[s.second] / atomic_weights[Z] / Sum2;

    for (auto ss : SpList) {

      speciesKey kk = ss.first;

      unsigned short ZZ = kk.first;
      unsigned short CC = kk.second;
      unsigned short PP = CC - 1;

      //--------------------------------------------------
      // Ion keys
      //--------------------------------------------------

      lQ QQ(ZZ, CC);

      //--------------------------------------------------
      // Particle 1 interacts with Particle 2
      //--------------------------------------------------
    
      double cfac = p1->dattrib[s.second] * p2->dattrib[ss.second];
      orderedPair op(Z, ZZ), op1(Z, 1);

      //-------------------------------
      // *** Both particles neutral
      //-------------------------------
    
      if (P==0 and PP==0) {
	
	double cross = 0.0;
				// Geometric cross sections based on
				// atomic radius
	double crs = (geometric(Z)*cscl_[Z] + geometric(ZZ)*cscl_[ZZ]) * cfac;
	
				// Double counting
	if (Z == ZZ) crs *= 0.5;

	if (DEBUG_CRS) trap_crs(crs*crossfac);

	cross += crs*crossfac;

	Interact::T t
	{ neut_neut, {Interact::neutral, k}, {Interact::neutral, kk} };
	
	hCross[id].push_back(XStup(t));
	hCross[id].back().crs = cross;
	
	CProb[id][0] += cross;
      }

      // --------------------------------------
      // *** Neutral atom-proton scattering
      // --------------------------------------

      if (P==0 and kk==proton) {
	double crs1 = elastic(Z, kEi[id], Elastic::proton) *
	  crossfac * cscl_[Z] * cfac;
	
	if (DEBUG_CRS) trap_crs(crs1);
	
	Interact::T t
	{ neut_prot, {Interact::neutral, k}, {Interact::ion, kk} };

	hCross[id].push_back(XStup(t));
	hCross[id].back().crs = crs1;
	
	CProb[id][0] += crs1;
      } // end: neutral-proton scattering

      if (PP==0 and k==proton) {
	double crs1 = elastic(ZZ, kEi[id], Elastic::proton) *
	  crossfac * cscl_[ZZ] * cfac;
	
	if (DEBUG_CRS) trap_crs(crs1);
	
	Interact::T t
	{ neut_prot, Ion, {Interact::neutral, kk} };

	hCross[id].push_back(XStup(t));
	hCross[id].back().crs = crs1;
	
	CProb[id][0] += crs1;
      } // end: neutral-proton scattering


      // --------------------------------------
      // *** Ion-ion scattering
      // --------------------------------------
      //
      if (P>0 and PP>0) {
	double kEc  = MEAN_KE ? Eion[id]  : kEi[id];
	double afac = esu*esu/std::max<double>(2.0*kEc*eV, FloorEv*eV) * 1.0e7;
	double crs  = 2.0 * ABrate[id][0] * afac*afac / PiProb[id][0];

	Interact::T t
	{ ion_ion, Ion, {Interact::ion, kk} };

	hCross[id].push_back(XStup(t));
	hCross[id].back().crs = crs;
	
	CProb[id][0] += crs;
      }
      // End: ion-ion scattering


    } // End of inner species loop

    // --------------------------------------
    // *** Neutral atom-electron scattering
    // --------------------------------------
    
    // Particle 1 ION, Particle 2 ELECTRON
    //
    if (P==0 and Eta2>0.0) {
      double crs = elastic(Z, kEe1[id]) * gVel2 * Eta2 *
	crossfac * cscl_[Z] * fac1;

      if (DEBUG_CRS) trap_crs(crs);

      Interact::T t { neut_elec, Ion, Interact::edef };

      hCross[id].push_back(XStup(t));
      hCross[id].back().crs = crs;
      
      CProb[id][1] += crs;
    }

    // Particle 2 ION, Particle 1 ELECTRON
    //
    if (P==0 and Eta1>0.0) {

      double crs = elastic(Z, kEe2[id]) * gVel1 * Eta1 *
	crossfac * cscl_[Z] * fac2;
	
      if (DEBUG_CRS) trap_crs(crs);

      Interact::T t { neut_elec, Interact::edef, Ion };
      
      hCross[id].push_back(XStup(t));
      hCross[id].back().crs = crs;
      
      CProb[id][2] += crs;
    }

    // --------------------------------------
    // *** Ion-electron scattering
    // --------------------------------------
    //
    if (P>0 and Eta2>0) {

      // Particle 1 ION, Particle 2 ELECTRON
      {
	double crs   = 0.0;

	if (coulScale) {

	  crs = coulCrs[id][P][0] * pow(kEe1[id]/coulCrs[id][P][1], coulPow) *
	    gVel2 * Eta2 * crossfac * cscl_[Z] * fac1;
	  
	} else {
	  double kEc  = MEAN_KE ? Eelc[id]  : kEe1[id];
	  double afac = esu*esu/std::max<double>(2.0*kEc*eV, FloorEv*eV) * 1.0e7;

	  crs = 2.0 * ABrate[id][1] * afac*afac * gVel2 / PiProb[id][1];
	}
	
	if (DEBUG_CRS) trap_crs(crs);
	
	Interact::T t { ion_elec, Ion, Interact::edef };
	
	hCross[id].push_back(XStup(t));
	hCross[id].back().crs = crs;

	CProb[id][1] += crs;
      }
	  
      // Particle 2 ION, Particle 1 ELECTRON
      {
	double crs   = 0.0;

	if (coulScale) {
	  crs = coulCrs[id][P][0] * pow(kEe2[id]/coulCrs[id][P][1], coulPow) *
	    gVel1 * Eta1 * crossfac * cscl_[Z] * fac2;
	} else {
	  double kEc  = MEAN_KE ? Eelc[id]  : kEe2[id];
	  double afac = esu*esu/std::max<double>(2.0*kEc*eV, FloorEv*eV) * 1.0e7;

	  crs = 2.0 * ABrate[id][2] * afac*afac * gVel1 / PiProb[id][2];
	}
	
	if (DEBUG_CRS) trap_crs(crs);
	
	Interact::T t { ion_elec, Interact::edef, Ion };
	  
	hCross[id].push_back(XStup(t));
	hCross[id].back().crs = crs;
	  
	CProb[id][2] += crs;
      }

    }
    // end: ion-electron scattering


    //-------------------------------
    // *** Free-free
    //-------------------------------
      
    if (!NO_FF and Eta1>0.0 and Eta2>0.0) {

      // Particle 1 ION, Particle 2 ELECTRON
      {
	double   ke  = std::max<double>(kEe1[id], FloorEv);
	CFreturn ff  = ch.IonList[Q]->freeFreeCross(ke, id);

	double crs  = gVel2 * Eta2 * ff.first * fac1;
	
	if (std::isinf(crs)) crs = 0.0; // Sanity check
	
	if (crs>0.0) {

	  Interact::T t { free_free, Ion, Interact::edef };
	    
	  hCross[id].push_back(XStup(t));
	  hCross[id].back().crs = crs;
	  hCross[id].back().CF  = ff;

	  CProb[id][1] += crs;
	}
      }

      // Particle 2 ION, Particle 1 ELECTRON
      {
	double    ke = std::max<double>(kEe2[id], FloorEv);
	CFreturn  ff = ch.IonList[Q]->freeFreeCross(ke, id);

	double crs  = gVel1 * Eta1 * ff.first * fac2;
	
	if (std::isinf(crs)) crs = 0.0; // Sanity check
	  
	if (crs>0.0) {
	  
	  Interact::T t { free_free, Interact::edef, Ion };
	    
	  hCross[id].push_back(XStup(t));
	  hCross[id].back().crs = crs;
	  hCross[id].back().CF  = ff;

	  CProb[id][2] += crs;
	}
      }
    }
    // end: free-free 

    //-------------------------------
    // *** Collisional excitation
    //-------------------------------
    
    // Particle 1 nucleus has BOUND ELECTRON, Particle 2 has FREE ELECTRON
    //
    //  +--- Charge of the current subspecies
    //  |
    //  |       +--- Electron fraction of partner
    //  |       |
    //  V       V
    if (P<Z and Eta2>0.0) {
      double    ke = std::max<double>(kEe1[id], FloorEv);
      CEvector  CE = ch.IonList[Q]->collExciteCross(ke, id);

      double   crs = gVel2 * Eta2 * CE.back().first * fac1;
      
      if (DEBUG_CRS) trap_crs(crs);
      
      if (crs > 0.0) {
	Interact::T t { colexcite, Ion, Interact::edef };
	
	hCross[id].push_back(XStup(t));
	hCross[id].back().crs = crs;
	hCross[id].back().CE  = CE;

	CProb[id][1] += crs;
      }
    }
    // end: colexcite
    
    // Particle 2 nucleus has BOUND ELECTRON, Particle 1 has FREE ELECTRON
    //
    //  +--- Charge of the current subspecies
    //  |
    //  |       +--- Electron fraction of partner
    //  |       |
    //  V       V
    if (P<Z and Eta1>0) {
      double    ke = std::max<double>(kEe2[id], FloorEv);
      CEvector  CE = ch.IonList[Q]->collExciteCross(ke, id);

      double   crs = gVel1 * Eta1 * CE.back().first * fac2;
      
      if (DEBUG_CRS) trap_crs(crs);
      
      if (crs > 0.0) {
	Interact::T t { colexcite, Interact::edef, Ion };
	
	hCross[id].push_back(XStup(t));
	hCross[id].back().crs = crs;
	hCross[id].back().CE  = CE;
	
	CProb[id][2] += crs;
      }
    }
    // end: colexcite

    //-------------------------------
    // *** Ionization cross section
    //-------------------------------
      
    // Particle 1 nucleus has BOUND ELECTRON, Particle 2 has FREE ELECTRON
    //
    //  +--- Charge of the current subspecies
    //  |
    //  |       +--- Electron fraction of partner
    //  |       |
    //  V       V
    if (P<Z and Eta2>0) {
      
      double ke    = std::max<double>(kEe1[id], FloorEv);
      double DI    = ch.IonList[Q]->directIonCross(ke, id);

      double crs   = gVel2 * Eta2 * DI * fac1;
      
      if (DEBUG_CRS) trap_crs(crs);
      
      if (crs > 0.0) {
	Interact::T t { ionize, Ion, Interact::edef };
	
	hCross[id].push_back(XStup(t));
	hCross[id].back().crs = crs;
	
	CProb[id][1] += crs;
      }
    }
    // end: ionize
    
    // Particle 2 nucleus has BOUND ELECTRON, Particle 1 has FREE ELECTRON
    //
    //  +--- Charge of the current subspecies
    //  |
    //  |       +--- Electron fraction of partner
    //  |       |
    //  V       V
    if (P<Z and Eta1) {
      
      double ke    = std::max<double>(kEe2[id], FloorEv);
      double DI    = ch.IonList[Q]->directIonCross(ke, id);

      double crs   = gVel1 * Eta1 * DI * fac2;
      
      if (DEBUG_CRS) trap_crs(crs);
      
      if (crs > 0.0) {
	Interact::T t { ionize, Interact::edef, Ion };
	
	hCross[id].push_back(XStup(t));
	hCross[id].back().crs = crs;
	
	CProb[id][2] += crs;
      }
    }
    // end: ionize

    //-------------------------------
    // *** Radiative recombination
    //-------------------------------

    // The "new" algorithm uses the electron energy of the ion's
    // electron rather than the standard particle partner.
    //

    if (newRecombAlg) {

      // Particle 1 is ION, Particle 2 has ELECTRON
      //
      //  +--- Ion charge
      //  |
      //  v
      if (P>0) {
	double ke              = std::max<double>(kE1s[id], FloorEv);
	std::vector<double> RE = ch.IonList[Q]->radRecombCross(ke, id);

	double crs = Eta1 * RE.back() * fac1;

	if (MeanMass) crs *= gVel1;
	else          crs *= sVel1;
	
	if (scatter_check and recomb_check) {
	  if (MeanMass) {
	    double val = gVel1 * vel * 1.0e-14 * RE.back();
	    recombA[id].add(k, Eta1, val);
	  } else {
	    double val = sVel1 * vel * 1.0e-14 * RE.back();
	    recombA[id].add(k, Eta1, val);
	  }
	}

	if (DEBUG_CRS) trap_crs(crs);
	
	if (crs > 0.0) {
	  Interact::T t { recomb, Ion, Interact::edef };
	  
	  hCross[id].push_back(XStup(t));
	  hCross[id].back().crs = crs;
	  
	  CProb[id][1] += crs;
	}
      }
      
      // Particle 2 is ION, Particle 1 has ELECTRON
      //
      //  +--- Ion charge
      //  |
      //  v
      if (P>0) {
	double ke              = std::max<double>(kE2s[id], FloorEv);
	std::vector<double> RE = ch.IonList[Q]->radRecombCross(ke, id);

	double crs = Eta2 * RE.back() * fac2;
	
	if (MeanMass) crs *= gVel2;
	else          crs *= sVel2;

	if (scatter_check and recomb_check) {
	  if (MeanMass) {
	    double val = gVel2 * vel * 1.0e-14 * RE.back();
	    recombA[id].add(k, Eta2, val);
	  } else {
	    double val = sVel2 * vel * 1.0e-14 * RE.back();
	    recombA[id].add(k, Eta2, val);
	  }
	}

	if (DEBUG_CRS) trap_crs(crs);
	  
	if (crs > 0.0) {
	  Interact::T t { recomb, Interact::edef, Ion };
	  
	  hCross[id].push_back(XStup(t));
	  hCross[id].back().crs = crs;
	  
	  CProb[id][2] += crs;
	}
      }
      
    } // end: new recomb algorithm
    else {
      // Particle 1 is ION, Particle 2 has ELECTRON
      //
      //  +--- Charge of the current subspecies
      //  |
      //  |       +--- Electron fraction of partner
      //  |       |
      //  V       V
      if (P>0 and Eta2>0.0) {
	  double ke              = std::max<double>(kEe1[id], FloorEv);
	  std::vector<double> RE = ch.IonList[Q]->radRecombCross(ke, id);

	  double crs = Eta2 * RE.back() * fac1;
	  
	  if (MeanMass) crs *= gVel2;
	  else          crs *= sVel2;

	  if (scatter_check and recomb_check) {
	    if (MeanMass) {
	      double val = gVel2 * vel * 1.0e-14 * RE.back();
	      recombA[id].add(k, Eta2, val);
	    } else {
	      double val = sVel2 * vel * 1.0e-14 * RE.back();
	      recombA[id].add(k, Eta2, val);
	    }
	  }

	  if (DEBUG_CRS) trap_crs(crs);
	  
	  if (crs > 0.0) {
	    Interact::T t { recomb, Ion, Interact::edef };
	    
	    hCross[id].push_back(XStup(t));
	    hCross[id].back().crs = crs;
	    
	    CProb[id][1] += crs;
	  }
      }

      // Particle 2 is ION, Particle 1 has ELECTRON
      //
      //  +--- Charge of the current subspecies
      //  |
      //  |       +--- Electron fraction of partner
      //  |       |
      //  V       V
      if (P>0 and Eta1>0.0) {
	double ke = std::max<double>(kEe2[id], FloorEv);
	  std::vector<double> RE = ch.IonList[Q]->radRecombCross(ke, id);
	  
	  double crs = Eta1 * RE.back() * fac2;
	  
	  if (MeanMass) crs *= gVel1;
	  else          crs *= sVel1;

	  if (scatter_check and recomb_check) {
	    if (MeanMass) {
	      double val = gVel1 * vel * 1.0e-14 * RE.back();
	      recombA[id].add(k, Eta1, val);
	    } else {
	      double val = sVel1 * vel * 1.0e-14 * RE.back();
	      recombA[id].add(k, Eta1, val);
	    }
	  }
	  
	  if (DEBUG_CRS) trap_crs(crs);
	  
	  if (crs > 0.0) {
	    Interact::T t { recomb, Interact::edef, Ion };
	    
	    hCross[id].push_back(XStup(t));
	    hCross[id].back().crs = crs;
	    
	    CProb[id][2] += crs;
	  }
	  
      } // end: original recomb algorithm
      
    } // end: recombination

  } // end: outer ionization state loop

  double totalXS = 0.0;
  for (auto & v : CProb[id]) {
    v *= crs_units;
    totalXS += v;
  }

  return totalXS;

} // end: crossSectionTrace


int CollideIon::inelasticDirect(int id, pCell* const c,
				Particle* const p1, Particle* const p2,
				double *cr)
{
  int ret = 0;			// No error (flag)
  int interFlag = -1;		// Invalid value by default

  // Species keys
  //
  KeyConvert k1(p1->iattrib[use_key]);
  KeyConvert k2(p2->iattrib[use_key]);

  collTDPtr ctd1 = (*collD)[k1.getKey()];
  collTDPtr ctd2 = (*collD)[k2.getKey()];

  unsigned short Z1 = k1.getKey().first, C1 = k1.getKey().second;
  unsigned short Z2 = k2.getKey().first, C2 = k2.getKey().second;

  if (SAME_INTERACT and Z1 != Z2) return 0;
  if (DIFF_INTERACT and Z1 == Z2) return 0;

  // Number of atoms in each super particle
  //
  double N1 = (p1->mass*TreeDSMC::Munit)/(atomic_weights[Z1]*amu);
  double N2 = (p2->mass*TreeDSMC::Munit)/(atomic_weights[Z2]*amu);

  double NN = std::min<double>(N1, N2);	// Currently, N1 should equal N2

  // Number of associated electrons for each particle
  //
  double ne1 = C1 - 1;
  double ne2 = C2 - 1;

  // The total mass in system units
  //
  double Mt = p1->mass + p2->mass;
  if (Mt<=0.0) return ret;

  // Reduced mass in ballistic collision (system units)
  //
  double Mu = p1->mass * p2->mass / Mt;

  double kE  = 0.5*Mu*(*cr)*(*cr);

  // For tracking energy conservation (system units)
  //
  double delE  = 0.0;

  // Now that the interactions have been calculated, create the
  // normalized cross section list to pick the interaction
  //
  std::vector<double> TotalCross;
  double tCross = 0.0;
  for (size_t i = 0; i < dCross[id].size(); i++) {
    // Sanity check (mostly for debugging, NaN should never occur)
    if (std::isnan(dCross[id][i])) {
      std::ostringstream sout;
      sout << "dCross[" << id << "][" << i << "] is NaN!";
      std::cout << std::setw(22) << sout.str()
		<< std::setw(14) << dInter[id][i]
		<< std::setw(18) << labels[dInter[id][i]]
		<< std::endl;
    } else if (std::isinf(dCross[id][i])) {
      std::ostringstream sout;
      sout << "dCross[" << id << "][" << i << "] is ";
      std::cout << std::setw(20) << sout.str()
		<< std::setw(14) << dCross[id][i]
		<< std::setw(14) << dInter[id][i]
		<< std::setw(18) << labels[dInter[id][i]]
		<< std::endl;
    } else {
      tCross += dCross[id][i];
      TotalCross.push_back(tCross);
    }
  }

  //----------------------------
  // Which particle interacted?
  //----------------------------
  //
  // Will be 1 or 2, dependending on which ion or neutral is
  // selected for inelastic interaction.  Will be 0 if no inealistic
  // interaction is selected.
  //
  int partflag = 0;


  // Sanity check: total cross section should be positive!
  //
  if (tCross != 0) {
    // Cumulative cross-section distribution for interaction selection
    //
    std::vector<double> CDF;
    for (size_t i = 0; i < TotalCross.size(); i++) {
      // Sanity check (mostly for debugging, NaN should never occur)
      if (std::isnan(TotalCross[i])) {
	std::cout << "TotalCross[i][" << id << "][" << i << "] is NaN"
		  << std::endl;
      } else {
	CDF.push_back(TotalCross[i]/tCross);
      }
    }

    // Use a random variate to select the interaction from the
    // discrete cumulatative probability distribution (CDF)
    //
    double ran = (*unit)();
    int index  = -1;
    for (size_t i = 0; i < CDF.size(); i++) {
      if (ran < CDF[i]) {
	index = static_cast<int>(i);
	break;
      }
    }

    // Sanity check: did not assign index??
    //
    if (index<0) {
      std::cout << "CDF location falure, myid=" << myid
		<< ", ran=" << ran
		<< ", siz=" << CDF.size()
		<< ", beg=" << CDF.front()
		<< ", end=" << CDF.back()
		<< ", tot=" << tCross
		<< std::endl;
      index = 0;
    }

    // Finally, set the interaction type based on the selected index
    //
    interFlag = dInter[id][index];

    //-------------------------
    // VERBOSE DEBUG TEST
    //-------------------------
    //
    if (DEBUG_CR and (!DEBUG_NQ or Z1 != Z2) ) {
      //
      // Output on collisions for now . . .
      //
      if (interFlag % 100 == colexcite) {
	std::cout << std::setw( 8) << "index"
		  << std::setw( 8) << "flag"
		  << std::setw(14) << "cross"
		  << std::setw(14) << "prob"
		  << std::setw(14) << "cumul"
		  << std::setw(18) << "type label"
		  << std::endl
		  << std::setw( 8) << "-----"
		  << std::setw( 8) << "-----"
		  << std::setw(14) << "---------"
		  << std::setw(14) << "---------"
		  << std::setw(14) << "---------"
		  << std::setw(18) << "---------------"
		  << std::endl;
	for (size_t i = 0; i < dCross[id].size(); i++) {
	  std::cout << std::setw( 8) << i
		    << std::setw( 8) << dInter[id][i]
		    << std::setw(14) << dCross[id][i]
		    << std::setw(14) << dCross[id][i]/tCross
		    << std::setw(14) << CDF[i]
		    << std::setw(18) << labels[dInter[id][i]]
		    << std::endl;
	}
	std::cout << std::endl;
      }
    }

    //--------------------------------------------------
    // Ion keys
    //--------------------------------------------------

    lQ Q1(Z1, C1), Q2(Z2, C2);

    //-------------------------
    // Particle 1 interactions
    //-------------------------

    if (interFlag == neut_neut_1) {
      ctd1->nn[id][0] += 1;
      ctd1->nn[id][1] += NN;
    }

    if (interFlag == neut_elec_1) {
      ctd1->ne[id][0] += 1;
      ctd1->ne[id][1] += NN;
    }

    if (interFlag == neut_prot_1) {
      ctd1->np[id][0] += 1;
      ctd1->np[id][1] += NN;
    }

    if (interFlag == ion_elec_1) {
      ctd1->ie[id][0] += 1;
      ctd1->ie[id][1] += NN;
    }

    if (interFlag == free_free_1) {
      delE          = IS.selectFFInteract(FF1[id]);
      partflag      = 1;
      ctd1->ff[id][0] += 1;
      ctd1->ff[id][1] += NN;
      ctd1->ff[id][2] += delE * NN;
    }

    if (interFlag == colexcite_1) {
      delE = IS.selectCEInteract(ch.IonList[Q1], CE1[id]);
      partflag      = 1;
      ctd1->CE[id][0] += 1;
      ctd1->CE[id][1] += NN;
      ctd1->CE[id][2] += delE * NN;
    }

    if (interFlag == ionize_1) {
      delE          = IS.DIInterLoss(ch.IonList[Q1]);
      p1->iattrib[use_key] = k1.updateC(++C1);
      partflag      = 1;
      ctd1->CI[id][0] += 1;
      ctd1->CI[id][1] += NN;
      ctd1->CI[id][2] += delE * NN;
    }

    if (interFlag == recomb_1) {

      // if (use_elec<0) delE = kEe1[id];
      delE = kEe1[id];
      if (RECOMB_IP) delE += ch.IonList[lQ(Z2, C2-1)]->ip;

      p1->iattrib[use_key] = k1.updateC(--C1);
      partflag      = 1;
      ctd1->RR[id][0] += 1;
      ctd1->RR[id][1] += NN;
      ctd1->RR[id][2] += delE * NN;
    }

    //-------------------------
    // Particle 2 interactions
    //-------------------------

    if (interFlag == neut_neut_2) {
      ctd2->nn[id][0] += 1;
      ctd2->nn[id][1] += NN;
    }

    if (interFlag == neut_elec_2) {
      ctd2->ne[id][0] += 1;
      ctd2->ne[id][1] += NN;
    }

    if (interFlag == neut_prot_2) {
      ctd2->np[id][0] += 1;
      ctd2->np[id][1] += NN;
    }

    if (interFlag == ion_elec_2) {
      ctd2->ie[id][0] += 1;
      ctd2->ie[id][1] += NN;
    }

    if (interFlag == free_free_2) {
      delE          = IS.selectFFInteract(FF2[id]);
      partflag      = 2;
      ctd2->ff[id][0] += 1;
      ctd2->ff[id][1] += NN;
      ctd2->ff[id][2] += delE * NN;
    }

    if (interFlag == colexcite_2) {
      delE         = IS.selectCEInteract(ch.IonList[Q2], CE2[id]);
      partflag     = 2;
      ctd2->CE[id][0] += 1;
      ctd2->CE[id][1] += NN;
      ctd2->CE[id][2] += delE * NN;
    }

    if (interFlag == ionize_2) {
      delE = IS.DIInterLoss(ch.IonList[Q2]);
      p2->iattrib[use_key] = k2.updateC(++C2);
      ctd2->CI[id][0] += 1;
      ctd2->CI[id][1] += NN;
      ctd2->CI[id][2] += delE * NN;
      partflag     = 2;
    }

    if (interFlag == recomb_2) {

      // if (use_elec<0) delE = kEe2[id];
      delE = kEe2[id];
      if (RECOMB_IP) delE += ch.IonList[lQ(Z2, C2-1)]->ip;

      p2->iattrib[use_key] = k2.updateC(--C2);
      partflag     = 2;
      ctd2->RR[id][0] += 1;
      ctd2->RR[id][1] += NN;
      ctd2->RR[id][2] += delE * NN;
    }

    // Convert to super particle
    //
    if (partflag) delE *= NN;

    // Convert back to cgs
    //
    delE = delE * eV;
  }

  // For elastic interactions, delE == 0
  //
  assert(delE >= 0.0);

  // Artifically prevent cooling by setting the energy removed from
  // the COM frame to zero
  //
  if (NOCOOL) delE = 0.0;

  // Elastic event
  //
  if (delE<=0.0) return ret;

  // Convert energy loss to system units
  //
  delE = delE/TreeDSMC::Eunit;

  // Assign interaction energy variables
  //
  double totE=0.0, kEe=0.0;

  // -----------------
  // ENERGY DIAGNOSTIC
  // -----------------
  // Electrons from Particle 2 have interacted with atom/ion in Particle 1
  //
  if (partflag==1) {
    totE = kE;			// KE + internal
    if (use_Eint>=0) totE += p2->dattrib[use_Eint];

    kEe  = kEe1[id];		// Electron energy

				// Energy diagnostics
    bool prior = std::isnan(ctd1->eV_av[id]);
    ctd1->eV_av[id] += kEe1[id];
    if (std::isnan(ctd1->eV_av[id])) {
      std::cout << "NAN eV_N[1]=" << ctd1->eV_N[id]
		<< ", prior=" << std::boolalpha << prior << std::endl;
    }
    ctd1->eV_N[id]++;
    ctd1->eV_min[id] = std::min(ctd1->eV_min[id], kEe1[id]);
    ctd1->eV_max[id] = std::max(ctd2->eV_max[id], kEe1[id]);

    if (kEe1[id] > 10.2) { ctd1->eV_10[id]++;}
  }

  // Electrons from Particle 1 interacted with atom/ion in Particle 2
  //
  if (partflag==2) {
    totE = kE;			// KE + internal
    if (use_Eint>=0) totE += p1->dattrib[use_Eint];

    kEe  = kEe2[id];		// Electron energy

				// Energy diagnostics
    bool prior = std::isnan(ctd2->eV_av[id]);
    ctd2->eV_av[id] += kEe2[id];
    if (std::isnan(ctd2->eV_av[id])) {
      std::cout << "NAN eV_N[2]=" << ctd2->eV_N[id]
		<< ", prior=" << std::boolalpha << prior << std::endl;
    }
    ctd2->eV_N[id]++;
    ctd2->eV_min[id] = std::min(ctd2->eV_min[id], kEe2[id]);
    ctd2->eV_max[id] = std::max(ctd2->eV_max[id], kEe2[id]);

    if (kEe2[id] > 10.2) { ctd2->eV_10[id]++; }
  }

  // Mass per particle in amu for this interaction
  //
  double m1 = atomic_weights[Z1];
  double m2 = atomic_weights[Z2];

  // Assign electron mass to doner ion particle and compute relative
  // velocity
  //
  std::vector<double> vrel(3), vcom(3), v1(3), v2(3), vcomE(3);
  double vi2 = 0.0, vf2 = 0.0;

  if (use_elec and interFlag > 100 and interFlag < 200) {

    m2 = atomic_weights[0];	// Particle 2 is the electron

    for (int k=0; k<3; k++) {
      v1[k] = p1->vel[k];	// Particle 1 is the ion
      v2[k] = p2->dattrib[use_elec+k];
      vi2  += v2[k] * v2[k];
    }

    // Secondary electron-ion scattering
    //
    for (unsigned n=0; n<SECONDARY_SCATTER; n++) {
      double M1 = atomic_weights[Z2];
      double M2 = atomic_weights[ 0];
      double Mt = M1 + M2;

      for (int k=0; k<3; k++)
	vcomE[k] = (M1*p2->vel[k] + M2*p2->dattrib[use_elec+k])/Mt;
    }

  } else if (use_elec and interFlag > 200 and interFlag < 300) {

    m1 = atomic_weights[0];	// Particle 1 is the electron

    for (int k=0; k<3; k++) {
      v1[k] = p1->dattrib[use_elec+k];
      v2[k] = p2->vel[k];	// Particle 2 is the ion
      vi2  += v1[k] * v1[k];
    }

    // Secondary electron-ion scattering
    //
    for (unsigned n=0; n<SECONDARY_SCATTER; n++) {
      double M1 = atomic_weights[Z1];
      double M2 = atomic_weights[ 0];
      double Mt = M1 + M2;

      for (int k=0; k<3; k++)
	vcomE[k] = (M1*p1->vel[k] + M2*p1->dattrib[use_elec+k])/Mt;
    }

  } else {
				// Neutrals or ions and electrons
    for (int k=0; k<3; k++) {
      v1[k] = p1->vel[k];
      v2[k] = p2->vel[k];
    }
  }

  // Available center of mass energy in the ballistic collision
  // (system units)
  //
  kE = 0.0;
  for (unsigned k=0; k<3; k++) {
    vcom[k] = (m1*v1[k] + m2*v2[k]) / Mt;
    kE += (v1[k] - v2[k])*(v1[k] - v2[k]);
  }

  // Relative velocity, system units
  //
  double vi = sqrt(kE);

  // Available KE in COM frame, system units
  //
  kE *= 0.5*NN*Mu;

  // Warn if energy lost is greater than total energy available to
  // lose
  //
  if (frost_warning && delE > totE)
      std::cout << "delE > KE!! (" << delE << " > " << totE
		<< "), Interaction type = " << interFlag
		<< " kEe  = "  << kEe
		<< std::endl;


  // Cooling rate diagnostic histogram
  //
  if (TSDIAG && delE>0.0) {
				// Histogram index
    int indx = (int)floor(log(totE/delE)/(log(2.0)*TSPOW) + 5);
				// Floor and ceiling
    if (indx<0 ) indx = 0;
    if (indx>10) indx = 10;
				// Add entry
    EoverT[id][indx] += Mt;
  }

  // Accumulate energy for time step cooling computation
  //
  if (use_delt>=0 && delE>0.0) {
    spEdel[id] += delE;		// DIRECT
    if (delE>0.0) spEmax[id]  = std::min<double>(spEmax[id], totE/delE);
  }

  if (use_exes>=0) {
    // (-/+) value means under/overcooled: positive/negative increment
    // to delE NB: delE may be < 0 if too much energy was radiated
    // previously . . .
    //
    delE -= p1->dattrib[use_exes] + p2->dattrib[use_exes];
  }

  // Sufficient energy available for selected loss
  //
  if (totE > delE) {

    lostSoFar[id] += delE;
    decelT[id]    += delE;

    totE          -= delE;	// Remove the energy from the total
				// available

				// Energy per particle
    double kEm = totE;

				// Get new relative velocity
    (*cr)          = sqrt( 2.0*kEm/Mu );

    ret            = 0;		// No error

    if (partflag==1) {
      ctd1->dv[id][0] += 1;
      ctd1->dv[id][1] += N1;
      ctd1->dv[id][2] +=
	0.5*Mu*(vi - (*cr))*(vi - (*cr)) * TreeDSMC::Eunit / eV;
    }

    if (partflag==2) {
      ctd2->dv[id][0] += 1;
      ctd2->dv[id][1] += N2;
      ctd2->dv[id][2] +=
	0.5*Mu*(vi - (*cr))*(vi - (*cr)) * TreeDSMC::Eunit / eV;
    }

				// Distribute electron energy to particles
    if (use_Eint>=0) {
      if (partflag==1) p2->dattrib[use_Eint] = ne2 * kEm;
      if (partflag==2) p1->dattrib[use_Eint] = ne1 * kEm;
    }
				// Zero out internal energy excess
    if (use_exes>=0)		// since excess is now used up
      p1->dattrib[use_exes] = p2->dattrib[use_exes] = 0.0;

  } else {
				// All available energy will be lost
    lostSoFar[id] += totE;
    decolT[id]    += totE - delE;

    (*cr)          = 0.0;
    ret            = 1;		// Set error flag

    if (partflag==1) {
      ctd1->dv[id][0] += 1;
      ctd1->dv[id][1] += N1;
      ctd1->dv[id][2] +=
	0.5*Mu*(vi - (*cr))*(vi - (*cr)) * TreeDSMC::Eunit / eV;
    }

    if (partflag==2) {
      ctd2->dv[id][0] += 1;
      ctd2->dv[id][1] += N2;
      ctd2->dv[id][2] +=
	0.5*Mu*(vi - (*cr))*(vi - (*cr)) * TreeDSMC::Eunit / eV;
    }

				// Remaining energy split set to zero
    if (use_Eint>=0)
      p1->dattrib[use_Eint] = p2->dattrib[use_Eint] = 0.0;

				// Reset internal energy excess
    if (use_exes>=0) {
      p1->dattrib[use_exes] = p1->mass*(totE - delE)/Mt;
      p2->dattrib[use_exes] = p2->mass*(totE - delE)/Mt;
    }
  }

  // Compute post-collision relative velocity for an elastic interaction
  //
  vrel = unit_vector();
  for (auto & v : vrel) v *= vi;
  //                         ^
  //                         |
  // Velocity in center of mass, computed from v1, v2
  //

  // Compute the change of energy in the collision frame by computing
  // the velocity reduction factor
  //
  double vfac = 1.0;
  if (kE>0.0) vfac = totE>0.0 ? sqrt(totE/kE) : 0.0;


  // Update post-collision velocities.  In the electron version, the
  // momentum is assumed to be coupled to the ions, so the ion
  // momentum must be conserved.
  //
  for (size_t k=0; k<3; k++) {
    v1[k] = vcom[k] + m2/Mt*vrel[k]*vfac;
    v2[k] = vcom[k] - m1/Mt*vrel[k]*vfac;
  }

  // Update electron velocties.  Electron velocity is computed so that
  // momentum is conserved ignoring the doner ion.  Use of reduction
  // factor keeps electrons and ions in equipartition.
  //
  if (use_elec and interFlag > 100 and interFlag < 200) {

    if (equiptn) {
      for (size_t k=0; k<3; k++) {
	vcom[k] = (m1*p1->vel[k] + m2*p2->dattrib[use_elec+k])/Mt;
	vrel[k] = (vcom[k] - v2[k])*Mt/m1;
      }

      for (size_t k=0; k<3; k++) {
	p1->vel[k] = vcom[k] + m2/Mt * vrel[k];
      }
    }

    // Electron from particle #2
    //
    for (size_t k=0; k<3; k++) {
      p1->vel[k] = v1[k];
      p2->dattrib[use_elec+k] = v2[k];
      vf2 += v2[k] * v2[k];
    }

    // Debug electron energy loss/gain
    //
    velER[id].push_back(vf2/vi2);


    // Secondary electron-ion scattering
    //
    for (unsigned n=0; n<SECONDARY_SCATTER; n++) {
      double M1 = atomic_weights[Z2];
      double M2 = atomic_weights[ 0];

      for (int k=0; k<3; k++)
	  p2->vel[k] = vcomE[k] + M2/M1*(vcomE[k] - v2[k]);
    }

  } else if (use_elec and interFlag > 200 and interFlag < 300) {

    if (equiptn) {
      for (size_t k=0; k<3; k++) {
	vcom[k] = (m1*p1->dattrib[use_elec+k] + m2*p2->vel[k])/Mt;
	vrel[k] = (vcom[k] - v1[k])*Mt/m2;
      }

      for (size_t k=0; k<3; k++) {
	p2->vel[k] = vcom[k] + m1/Mt * vrel[k];
      }
    }

    // Electron from particle #1
    //
    for (size_t k=0; k<3; k++) {
      p1->dattrib[use_elec+k] = v1[k];
      p2->vel[k] = v2[k];
      vf2 += v1[k] * v1[k];
    }

    // Debug electron energy loss/gain
    //
    velER[id].push_back(vf2/vi2);


    // Secondary electron-ion scattering
    //
    for (unsigned n=0; n<SECONDARY_SCATTER; n++) {
      double M1 = atomic_weights[Z1];
      double M2 = atomic_weights[ 0];

      for (int k=0; k<3; k++)
	p1->vel[k] = vcomE[k] + M2/M1*(vcomE[k] - v1[k]);
    }

  } else {
    for (size_t k=0; k<3; k++) {
      p1->vel[k] = v1[k];
      p2->vel[k] = v2[k];
    }
  }

  *cr = 0.0;
  for (size_t k=0; k<3; k++) {
    double v1 = p1->vel[k];
    double v2 = p2->vel[k];
    *cr += (v1 - v2)*(v1 - v2);
  }
  *cr = sqrt(*cr);

  if (equiptn and use_elec) {

    if (interFlag > 100 and interFlag < 200) {

      m1 = atomic_weights[Z2];
      m2 = atomic_weights[0 ];
      Mt = m1 + m2;
      Mu = m1 * m2 / Mt;

      double KE1i = 0.0, KE2i = 0.0;
      double KE1f = 0.0, KE2f = 0.0;
      double cost = 0.0, VC2  = 0.0, VR2 = 0.0;

      for (size_t k=0; k<3; k++) {
	KE1i += p2->vel[k] * p2->vel[k];
	KE2i += p2->dattrib[use_elec+k] * p2->dattrib[use_elec+k];
	cost += p2->vel[k] * p2->dattrib[use_elec+k];

	vcom[k] = (m1*p2->vel[k] + m2*p2->dattrib[use_elec+k])/Mt;
	vrel[k] = p2->vel[k] - p2->dattrib[use_elec+k];

	VC2    += vcom[k] * vcom[k];
	VR2    += vrel[k] * vrel[k];
      }

      if (KE1i > 0.0 and KE2i > 0.0) cost /= sqrt(KE1i * KE2i);

      double dmr   = cost / (m1 - m2);
      double gamma = 1.0 + 4.0*Mt*Mu*dmr*dmr;
      double E0    = 0.5*Mt*VC2 + 0.5*Mu*VR2;

      double gamP  = 1.0 + sqrt(1.0 - 1.0/gamma);
      double gamN  = 1.0 - sqrt(1.0 - 1.0/gamma);

      double virP  =
	(VC2 - E0/Mt*gamN)*(VC2 - E0/Mt*gamN) +
	(VR2 - E0/Mu*gamP)*(VR2 - E0/Mu*gamP) ;

      double virN  =
	(VC2 - E0/Mt*gamP)*(VC2 - E0/Mt*gamP) +
	(VR2 - E0/Mu*gamN)*(VR2 - E0/Mu*gamN) ;

      double vcfac = 0.0, vrfac = 0.0;

      if (virP > virN) {
	vcfac = sqrt(E0/Mt*gamN);
	vrfac = sqrt(E0/Mu*gamP);
      } else {
	vcfac = sqrt(E0/Mt*gamP);
	vrfac = sqrt(E0/Mu*gamN);
      }

      if (VC2>0.0) {
	for (size_t k=0; k<3; k++) vcom[k] /= sqrt(VC2);
      } else {
	vcom = unit_vector();
      }

      if (VR2>0.0) {
	for (size_t k=0; k<3; k++) vrel[k] /= sqrt(VR2);
      } else {
	vrel = unit_vector();
      }

      for (size_t k=0; k<3; k++) {
	p2->vel[k]              = vcom[k]*vcfac + m2/Mt * vrel[k]*vrfac;
	p2->dattrib[use_elec+k] = vcom[k]*vcfac - m1/Mt * vrel[k]*vrfac;

	KE1f += p2->vel[k] * p2->vel[k];
	KE2f += p2->dattrib[use_elec+k] * p2->dattrib[use_elec+k];
      }

      KE1i *= 0.5*m1;
      KE1f *= 0.5*m1;

      KE2i *= 0.5*m2;
      KE2f *= 0.5*m2;

      double KEi = KE1i + KE2i;
      double KEf = KE1f + KE2f;

      if ( fabs(KEi - KEf) > 1.0e-14*KEi ) {
	std::cout << "Test(1): keI=["
		  << std::setw(16) << KE1i << ", "
		  << std::setw(16) << KE2i << "] keF=["
		  << std::setw(16) << KE1f << ", "
		  << std::setw(16) << KE2f << "] vir=["
		  << std::setw(16) << virP << ", "
		  << std::setw(16) << virN << "] "
		  << std::endl;
      }
    }

    if (interFlag > 200 and interFlag < 300) {

      m1 = atomic_weights[Z1];
      m2 = atomic_weights[0 ];
      Mt = m1 + m2;
      Mu = m1 * m2 / Mt;

      double KE1i = 0.0, KE2i = 0.0;
      double KE1f = 0.0, KE2f = 0.0;
      double cost = 0.0, VC2 = 0.0, VR2 = 0.0;

      for (size_t k=0; k<3; k++) {
	KE1i += p1->vel[k] * p1->vel[k];
	KE2i += p1->dattrib[use_elec+k] * p1->dattrib[use_elec+k];
	cost += p1->vel[k] * p1->dattrib[use_elec+k];

	vcom[k] = (m1*p1->vel[k] + m2*p1->dattrib[use_elec+k])/Mt;
	vrel[k] = p1->vel[k] - p1->dattrib[use_elec+k];

	VC2    += vcom[k] * vcom[k];
	VR2    += vrel[k] * vrel[k];
      }

      if (KE1i > 0.0 and KE2i > 0.0) cost /= sqrt(KE1i * KE2i);

      double dmr   = cost / (m1 - m2);
      double gamma = 1.0 + 4.0*Mt*Mu*dmr*dmr;
      double E0    = 0.5*Mt*VC2 + 0.5*Mu*VR2;

      double gamP  = 1.0 + sqrt(1.0 - 1.0/gamma);
      double gamN  = 1.0 - sqrt(1.0 - 1.0/gamma);

      double virP  =
	(VC2 - E0/Mt*gamN)*(VC2 - E0/Mt*gamN) +
	(VR2 - E0/Mu*gamP)*(VR2 - E0/Mu*gamP) ;

      double virN  =
	(VC2 - E0/Mt*gamP)*(VC2 - E0/Mt*gamP) +
	(VR2 - E0/Mu*gamN)*(VR2 - E0/Mu*gamN) ;

      double vcfac = 0.0, vrfac = 0.0;

      if (virP > virN) {
	vcfac = sqrt(E0/Mt*gamN);
	vrfac = sqrt(E0/Mu*gamP);
      } else {
	vcfac = sqrt(E0/Mt*gamP);
	vrfac = sqrt(E0/Mu*gamN);
      }

      if (VC2>0.0) {
	for (size_t k=0; k<3; k++) vcom[k] /= sqrt(VC2);
      } else {
	vcom = unit_vector();
      }

      if (VR2>0.0) {
	for (size_t k=0; k<3; k++) vrel[k] /= sqrt(VR2);
      } else {
	vrel = unit_vector();
      }

      for (size_t k=0; k<3; k++) {
	p1->vel[k]              = vcom[k]*vcfac + m2/Mt * vrel[k]*vrfac;
	p1->dattrib[use_elec+k] = vcom[k]*vcfac - m1/Mt * vrel[k]*vrfac;

	KE1f += p1->vel[k] * p1->vel[k];
	KE2f += p1->dattrib[use_elec+k] * p1->dattrib[use_elec+k];
      }

      KE1i *= 0.5*m1;
      KE1f *= 0.5*m1;

      KE2i *= 0.5*m2;
      KE2f *= 0.5*m2;

      double KEi = KE1i + KE2i;
      double KEf = KE1f + KE2f;

      if ( fabs(KEi - KEf) > 1.0e-14*KEi ) {
	std::cout << "Test(1): keI=["
		  << std::setw(16) << KE1i << ", "
		  << std::setw(16) << KE2i << "] keF=["
		  << std::setw(16) << KE1f << ", "
		  << std::setw(16) << KE2f << "] vir=["
		  << std::setw(16) << virP << ", "
		  << std::setw(16) << virN << "] "
		  << std::endl;
      }
    }
  }

  // Scatter electrons
  //
  if (esType == always and C1>1 and C2>1) {
    double vi = 0.0;
    for (int k=0; k<3; k++) {
      double d1 = p1->dattrib[use_elec+k];
      double d2 = p2->dattrib[use_elec+k];
      vcom[k] = 0.5*(d1 + d2);
      vi     += (d1 - d2) * (d1 - d2);
    }
    vi = sqrt(vi);

    vrel = unit_vector();
    for (auto & v : vrel) v *= vi;

    for (int k=0; k<3; k++) {
      p1->dattrib[use_elec+k] = vcom[k] + 0.5*vrel[k];
      p2->dattrib[use_elec+k] = vcom[k] - 0.5*vrel[k];
    }

    return 0;
  }

  return ret;
}

const std::tuple<int, int, int> zorder(const std::vector<double> & p)
{
  typedef std::pair<double, int> dk;
  std::vector<dk> z(3);
  for (int k=0; k<3; k++) z[k] = dk(fabs(p[k]), k);
  if (z[0].first>z[2].first) zswap(z[0], z[2]);
  if (z[1].first>z[2].first) zswap(z[1], z[2]);
  
  std::tuple<int, int, int> ret {z[0].second, z[1].second, z[2].second};
  return ret;
}


int CollideIon::inelasticWeight(int id, pCell* const c,
				Particle* const _p1, Particle* const _p2,
				double *cr)
{
  int ret       =  0;		// No error (flag)
  int interFlag = -1;		// Invalid value by default

  Particle* p1  = _p1;		// Copy pointers for swapping, if
  Particle* p2  = _p2;		// necessary


  // Species keys for pointers before swapping
  //
  KeyConvert k1(p1->iattrib[use_key]);
  KeyConvert k2(p2->iattrib[use_key]);

  collTDPtr ctd1 = (*collD)[k1.getKey()];
  collTDPtr ctd2 = (*collD)[k2.getKey()];

  unsigned short Z1 = k1.getKey().first, C1 = k1.getKey().second;
  unsigned short Z2 = k2.getKey().first, C2 = k2.getKey().second;

  if (SAME_INTERACT and Z1 != Z2) return 0;
  if (DIFF_INTERACT and Z1 == Z2) return 0;

  // Particle 1 is assumed to be the "dominant" species and Particle 2
  // is assumed to be the "trace" species (or another "dominant").
  // Swap particle pointers if necessary.
  //
  if (p1->mass/atomic_weights[Z1] < p2->mass/atomic_weights[Z2]) {

    // Swap the particle pointers
    //
    zswap(p1, p2);

    // Swap the collision diag pointers
    //
    zswap(ctd1, ctd2);
    
    // Swap the keys and species indices
    //
    zswap(k1, k2);
    zswap(Z1, Z2);
    zswap(C1, C2);
  }

  // Find the trace ratio
  //
  double Wa = p1->mass / atomic_weights[Z1];
  double Wb = p2->mass / atomic_weights[Z2];
  double  q = Wb / Wa;

  // Number interacting atoms
  //
  double NN = Wb * TreeDSMC::Munit / amu;

  // For tracking energy conservation (system units)
  //
  double delE  = 0.0;

  // Ion KE
  //
  double iI1 = 0.0, iI2 = 0.0;
  for (auto v : p1->vel) iI1 *= v*v;
  for (auto v : p2->vel) iI2 *= v*v;
  iI1 *= 0.5*p1->mass;
  iI2 *= 0.5*p2->mass;

  // Electron KE
  //
  double iE1 = 0.0, iE2 = 0.0;
  if (use_elec) {
    for (size_t k=0; k<3; k++) {
      iE1 += p1->dattrib[use_elec+k] * p1->dattrib[use_elec+k];
      iE2 += p2->dattrib[use_elec+k] * p2->dattrib[use_elec+k];
    }
    iE1 *= 0.5*p1->mass * atomic_weights[0]/atomic_weights[Z1];
    iE2 *= 0.5*p2->mass * atomic_weights[0]/atomic_weights[Z2];
  }

  // Now that the interactions have been calculated, create the
  // normalized cross section list to pick the interaction
  //
  std::vector<double> TotalCross;
  double tCross = 0.0;
  for (size_t i = 0; i < dCross[id].size(); i++) {

    // Sanity check (mostly for debugging, NaN should never occur)
    //
    if (std::isnan(dCross[id][i])) {
      std::ostringstream sout;
      sout << "dCross[" << id << "][" << i << "] is NaN!";
      std::cout << std::setw(22) << sout.str()
		<< std::setw(14) << dInter[id][i]
		<< std::setw(18) << labels[dInter[id][i]]
		<< std::endl;
    } else if (std::isinf(dCross[id][i])) {
      std::ostringstream sout;
      sout << "dCross[" << id << "][" << i << "] is";
      std::cout << std::setw(20) << sout.str()
		<< std::setw(14) << dCross[id][i]
		<< std::setw(14) << dInter[id][i]
		<< std::setw(18) << labels[dInter[id][i]]
		<< std::endl;
    } else {

      bool ok = false;		// Reject all interactions by default

      // Accumulate the list here
      //
      if (NoDelC)  {
	ok = true;
				// Pass events that are NOT ionization
				// or recombination, or both
	if (NoDelC & 0x1 and dInter[id][i] % 100 == recomb) ok = false;
	if (NoDelC & 0x2 and dInter[id][i] % 100 == ionize) ok = false;

      } else if (scatter) {
				// Only pass elastic scattering events
	if (dInter[id][i] % 100 < 4) ok = true;

				// Otherwise, test all events . . .
      } else {
				// Test for Particle #1 collisional excitation
	if (dInter[id][i] == 105) {
	  double frac = meanF[id][k1.getKey()];
	  if (frac > minCollFrac) {
	    ok = true;
	  }
	}
				// Test for Particle #2 collisional excitation
	else if (dInter[id][i] == 205) {
	  double frac = meanF[id][k2.getKey()];
	  if (frac > minCollFrac) {
	    ok = true;
	  }
	}
	else {			// Pass all other interactions . . .
	  ok = true;
	}
      }

      if (ok) tCross += dCross[id][i];

      TotalCross.push_back(tCross);
    }
  }

  //
  // Cross section scale factor
  //
  double scaleCrossSection = tCross/csections[id][k1.getKey()][k2.getKey()]() *
    1e-14 / (TreeDSMC::Lunit*TreeDSMC::Lunit);

  NN *= scaleCrossSection;

  //----------------------------
  // Which particle interacted?
  //----------------------------
  //
  // Will be 1 or 2, dependending on which ion or neutral is
  // selected for inelastic interaction.  Will be 0 if no inealistic
  // interaction is selected.
  //
  int partflag = 0;

  // NOCOOL debugging
  //
  double NCXTRA = 0.0;

  // Sanity check: total cross section should be positive!
  //
  if (tCross > 0.0) {

    // Cumulative cross-section distribution for interaction selection
    //
    std::vector<double> CDF;
    for (size_t i = 0; i < TotalCross.size(); i++) {
      // Sanity check (mostly for debugging, NaN should never occur)
      if (std::isnan(TotalCross[i])) {
	std::cout << "TotalCross[i][" << id << "][" << i << "] is NaN"
		  << std::endl;
      } else {
	CDF.push_back(TotalCross[i]/tCross);
      }
    }

    // Use a random variate to select the interaction from the
    // discrete cumulatative probability distribution (CDF)
    //
    double ran = (*unit)();
    int index  = -1;
    for (size_t i = 0; i < CDF.size(); i++) {
      if (ran < CDF[i]) {
	index = static_cast<int>(i);
	break;
      }
    }

    // Sanity check: did not assign index??
    //
    if (index<0) {
      std::cout << "CDF location falure, myid=" << myid
		<< ", ran=" << ran
		<< ", siz=" << CDF.size()
		<< ", beg=" << CDF.front()
		<< ", end=" << CDF.back()
		<< ", tot=" << tCross
		<< std::endl;
      index = 0;
    }

    // Finally, set the interaction type based on the selected index
    //
    interFlag = dInter[id][index];

    //-------------------------
    // VERBOSE DEBUG TEST
    //-------------------------
    //
    if (DEBUG_CR and (!DEBUG_NQ or Z1 != Z2) ) {
      speciesKey i1 = k1.getKey();
      speciesKey i2 = k2.getKey();
      double cfac = 1e-14 / (TreeDSMC::Lunit*TreeDSMC::Lunit);
      //
      // Output on collisions for now . . .
      //
      std::cout << std::setw( 8) << "index"
		<< std::setw( 8) << "flag"
		<< std::setw(14) << "cross"
		<< std::setw(14) << "prob"
		<< std::setw(14) << "cumul"
		<< std::setw(14) << "tCross/max"
		<< std::setw(18) << "type label"
		<< std::endl
		<< std::setw( 8) << "-----"
		<< std::setw( 8) << "-----"
		<< std::setw(14) << "---------"
		<< std::setw(14) << "---------"
		<< std::setw(14) << "---------"
		<< std::setw(14) << "---------"
		<< std::setw(18) << "---------------"
		<< std::endl;
      for (size_t i = 0; i < dCross[id].size(); i++) {
	std::cout << std::setw( 8) << i
		  << std::setw( 8) << dInter[id][i]
		  << std::setw(14) << dCross[id][i]
		  << std::setw(14) << dCross[id][i]/tCross
		  << std::setw(14) << CDF[i]
		  << std::setw(14) << dCross[id][i]/csections[id][i1][i2]() * cfac
		  << std::setw(18) << labels[dInter[id][i]]
		  << std::endl;
      }
      std::cout << std::endl;

    }

    //--------------------------------------------------
    // Ion keys
    //--------------------------------------------------

    lQ Q1(Z1, C1), Q2(Z2, C2);

    //-------------------------
    // Particle 1 interactions
    //-------------------------

    if (interFlag == neut_neut_1) {
      ctd1->nn[id][0] += 1;
      ctd1->nn[id][1] += NN;
    }

    if (interFlag == neut_elec_1) {
      ctd1->ne[id][0] += 1;
      ctd1->ne[id][1] += NN;
    }

    if (interFlag == neut_prot_1) {
      ctd2->np[id][0] += 1;
      ctd2->np[id][1] += NN;
    }

    if (interFlag == ion_elec_1) {
      ctd1->ie[id][0] += 1;
      ctd1->ie[id][1] += NN;
    }

    if (interFlag == free_free_1) {
      delE          = IS.selectFFInteract(FF1[id]);
      partflag      = 1;
      if (NO_FF_E) delE = 0.0;
      ctd1->ff[id][0] += 1;
      ctd1->ff[id][1] += Wb;
      ctd1->ff[id][2] += delE * NN;
    }

    if (interFlag == colexcite_1) {
      delE = IS.selectCEInteract(ch.IonList[Q1], CE1[id]);
      partflag      = 1;
      ctd1->CE[id][0] += 1;
      ctd1->CE[id][1] += Wb;
      ctd1->CE[id][2] += delE * NN;
    }

    if (interFlag == ionize_1) {
      delE          = IS.DIInterLoss(ch.IonList[Q1]);
      p1->iattrib[use_key] = k1.updateC(++C1);
      partflag      = 1;
      if (NO_ION_E) delE = 0.0;
      ctd1->CI[id][0] += 1;
      ctd1->CI[id][1] += Wb;
      ctd1->CI[id][2] += delE * NN;
    }

    if (interFlag == recomb_1) {

      p1->iattrib[use_key] = k1.updateC(--C1);
      partflag      = 1;

      // if (use_elec<0) delE = kEe1[id];
      delE = kEe1[id];
      if (RECOMB_IP) delE += ch.IonList[lQ(Z1, C1)]->ip;

      ctd1->RR[id][0] += 1;
      ctd1->RR[id][1] += Wb;
      ctd1->RR[id][2] += delE * NN;

      // Add the KE from the recombined electron back to the free pool
      //
      if (NOCOOL and !NOCOOL_ELEC and C1==1 and use_cons>=0) {
	double lKE = 0.0, fE = 0.5*Wa*atomic_weights[0];
	for (size_t k=0; k<3; k++) {
	  double t = p1->dattrib[use_elec+k];
	  lKE += fE*t*t;
	}

	NCXTRA += lKE;

	if (q<1)
	  p1->dattrib[use_cons] += lKE;
	else {
	  p1->dattrib[use_cons] += lKE * 0.5;
	  p2->dattrib[use_cons] += lKE * 0.5;
	}
      }
    }

    //-------------------------
    // Particle 2 interactions
    //-------------------------

    if (interFlag == neut_neut_2) {
      ctd2->nn[id][0] += 1;
      ctd2->nn[id][1] += NN;
    }

    if (interFlag == neut_elec_2) {
      ctd2->ne[id][0] += 1;
      ctd2->ne[id][1] += NN;
    }

    if (interFlag == neut_prot_2) {
      ctd2->np[id][0] += 1;
      ctd2->np[id][1] += NN;
    }

    if (interFlag == ion_elec_2) {
      ctd2->ie[id][0] += 1;
      ctd2->ie[id][1] += NN;
    }

    if (interFlag == free_free_2) {
      delE          = IS.selectFFInteract(FF2[id]);
      partflag      = 2;
      if (NO_FF_E) delE = 0.0;
      ctd2->ff[id][0] += 1;
      ctd2->ff[id][1] += Wb;
      ctd2->ff[id][2] += delE * NN;
    }

    if (interFlag == colexcite_2) {
      delE         = IS.selectCEInteract(ch.IonList[Q2], CE2[id]);
      partflag     = 2;
      ctd2->CE[id][0] += 1;
      ctd2->CE[id][1] += Wb;
      ctd2->CE[id][2] += delE * NN;
    }

    if (interFlag == ionize_2) {
      delE = IS.DIInterLoss(ch.IonList[Q2]);
      p2->iattrib[use_key] = k2.updateC(++C2);
      partflag     = 2;
      if (NO_ION_E) delE = 0.0;
      ctd2->CI[id][0] += 1;
      ctd2->CI[id][1] += Wb;
      ctd2->CI[id][2] += delE * NN;
    }

    if (interFlag == recomb_2) {

      p2->iattrib[use_key] = k2.updateC(--C2);
      partflag     = 2;

      // if (use_elec<0) delE = kEe2[id];
      delE = kEe2[id];
      if (RECOMB_IP) delE += ch.IonList[lQ(Z2, C2)]->ip;

      ctd2->RR[id][0] += 1;
      ctd2->RR[id][1] += Wb;
      ctd2->RR[id][2] += delE * NN;

      // Add the KE from the recombined electron back to the free pool
      //
      if (NOCOOL and !NOCOOL_ELEC and C2==1 and use_cons>=0) {
	double lKE = 0.0, fE = 0.5*Wb*atomic_weights[0];
	for (size_t k=0; k<3; k++) {
	  double t = p2->dattrib[use_elec+k];
	  lKE += fE*t*t;
	}

	NCXTRA += lKE;

	if (q<1)
	  p1->dattrib[use_cons] += lKE;
	else {
	  p1->dattrib[use_cons] += lKE * 0.5;
	  p2->dattrib[use_cons] += lKE * 0.5;
	}
      }
    }

    // Convert to super particle
    //
    delE *= NN;

    // Convert back to cgs
    //
    delE = delE * eV;
  }

  // Collision counts
  //
  if (COLL_SPECIES) {
    dKey dk(k1.getKey(), k2.getKey());
    if (collCount[id].find(dk) == collCount[id].end()) collCount[id][dk] = ccZ;
    if (interFlag % 100 <= 2) collCount[id][dk][0]++;
    else                      collCount[id][dk][1]++;
  }

  // Debugging test
  //
  if (SAME_IONS_SCAT and interFlag % 100 <= 2) {
    if (Z1 != Z2) return 0;
  }

  // Work vectors
  //
  std::vector<double> vrel(3), vcom(3), v1(3), v2(3);

  // For elastic interactions, delE == 0
  //
  assert(delE >= 0.0);

  // Artifically prevent cooling by setting the energy removed from
  // the COM frame to zero
  //
  if (NOCOOL) delE = 0.0;

  // Convert energy loss to system units
  //
  delE = delE/TreeDSMC::Eunit;

  // -----------------
  // ENERGY DIAGNOSTIC
  // -----------------
  // Electrons from Particle 2 have interacted with atom/ion in Particle 1
  //
  if (partflag==1) {

    bool prior = std::isnan(ctd2->eV_av[id]);
    ctd1->eV_av[id] += kEe1[id];
    if (std::isnan(ctd2->eV_av[id])) {
      std::cout << "NAN eV_N=" << ctd1->eV_N[id]
		<< ", prior=" << std::boolalpha << prior << std::endl;
    }
    ctd1->eV_N[id]++;
    ctd1->eV_min[id] = std::min(ctd1->eV_min[id], kEe1[id]);
    ctd1->eV_max[id] = std::max(ctd2->eV_max[id], kEe1[id]);

    if (kEe1[id] > 10.2) { ctd1->eV_10[id]++;}
  }

  // -----------------
  // ENERGY DIAGNOSTIC
  // -----------------
  // Electrons from Particle 1 interacted with atom/ion in Particle 2
  //
  if (partflag==2) {

    bool prior = std::isnan(ctd2->eV_av[id]);
    ctd2->eV_av[id] += kEe2[id];
    if (std::isnan(ctd2->eV_av[id])) {
      std::cout << "NAN eV_N=" << ctd2->eV_N[id]
		<< ", prior=" << std::boolalpha << prior << std::endl;
    }
    ctd2->eV_N[id]++;
    ctd2->eV_min[id] = std::min(ctd2->eV_min[id], kEe2[id]);
    ctd2->eV_max[id] = std::max(ctd2->eV_max[id], kEe2[id]);

    if (kEe2[id] > 10.2) { ctd2->eV_10[id]++; }
  }

  //
  // Perform energy adjustment in ion, system COM frame with system
  // mass units
  //

  // Mass per particle in amu for this interaction
  //
  double m1 = atomic_weights[Z1];
  double m2 = atomic_weights[Z2];

  // Assign electron mass to doner ion particle and compute relative
  // velocity
  //
  double vi2 = 0.0, vf2 = 0.0;

  if (use_elec and interFlag > 100 and interFlag < 200) {

    m2 = atomic_weights[0];	// Particle 2 is the electron

    for (int k=0; k<3; k++) {
      v1[k] = p1->vel[k];	// Particle 1 is the ion
      v2[k] = p2->dattrib[use_elec+k];
      vi2  += v2[k] * v2[k];
    }

  } else if (use_elec and interFlag > 200 and interFlag < 300) {

    m1 = atomic_weights[0];	// Particle 1 is the electron

    for (int k=0; k<3; k++) {
      v1[k] = p1->dattrib[use_elec+k];
      v2[k] = p2->vel[k];	// Particle 2 is the ion
      vi2  += v1[k] * v1[k];
    }

  } else {
				// Neutrals or ions and electrons
    for (int k=0; k<3; k++) {
      v1[k] = p1->vel[k];
      v2[k] = p2->vel[k];
    }
  }

  // For debugging kinetic energy bookkeeping
  //
  double KE1i = 0.0, KE2i = 0.0;
  double KE1f = 0.0, KE2f = 0.0;

  if (KE_DEBUG) {
    for (auto v : v1) KE1i += v*v;
    for (auto v : v2) KE2i += v*v;
  }

  // Total effective mass in the collision (atomic mass units)
  //
  double Mt = m1 + m2;

  // Reduced mass (atomic mass units)
  //
  double Mu = m1 * m2 / Mt;


  // Available center of mass energy in the ballistic collision
  // (system units)
  //
  double kE = 0.0;
  for (unsigned k=0; k<3; k++) {
    vcom[k] = (m1*v1[k] + m2*v2[k]) / Mt;
    kE += (v1[k] - v2[k])*(v1[k] - v2[k]);
  }

  // Relative velocity, system units
  //
  double vi = sqrt(kE);

  // Available KE in COM frame, system units
  //
  kE *= 0.5*Wa*q*Mu;

  // Total energy available in COM after removing radiative and
  // collisional loss.  A negative value for totE will be handled
  // below . . .
  //
  double totE  = kE - delE;

  // Cooling rate diagnostic histogram
  //
  if (TSDIAG && delE>0.0) {
				// Histogram index
    int indx = (int)floor(log(kE/delE)/(log(2.0)*TSPOW) + 5);
				// Floor and ceiling
    if (indx<0 ) indx = 0;
    if (indx>10) indx = 10;
				// Add entry
    EoverT[id][indx] += p1->mass + p2->mass;
  }

  // Accumulate energy for time step cooling computation
  //
  if (use_delt>=0 && delE>0.0) {
    spEdel[id] += delE;		// WEIGHT
    if (delE>0.0) spEmax[id]  = std::min<double>(spEmax[id], totE/delE);
  }

  if (use_exes>=0 && delE>0.0) {
    // (-/+) value means under/overcooled: positive/negative increment
    // to delE NB: delE may be < 0 if too much energy was radiated
    // previously . . .
    //
    delE -= p1->dattrib[use_exes] + p2->dattrib[use_exes];
    p1->dattrib[use_exes] = p2->dattrib[use_exes] = 0.0;
  }

  lostSoFar[id] += delE;
  decelT[id]    += delE;

  ret            = 0;		// No error

  if (partflag==1) {
    ctd1->dv[id][0] += 1;
    ctd1->dv[id][1] += Wb;
    ctd1->dv[id][2] += delE;
  }

  if (partflag==2) {
    ctd2->dv[id][0] += 1;
    ctd2->dv[id][1] += Wb;
    ctd2->dv[id][2] += delE;
  }

  // Assign interaction energy variables
  //

  double Exs = 0.0;		// Exs variable for KE debugging only

  bool in_exactE = false;

  if (use_cons >= 0) {

    if (TRACE_OVERRIDE) {
      //
      // Override special trace species treatment
      //
      double del = 0.0;

      if (use_elec>=0) {

				// Particle 1: ion
				// Particle 2: electron
	if (interFlag > 100 and interFlag < 200) {

	  del += p1->dattrib[use_cons];
	  p1->dattrib[use_cons] = 0.0;

	  if (elc_cons) {
	    del += p2->dattrib[use_elec+3];
	    p2->dattrib[use_elec+3] = 0.0;
	  } else {
	    del += p2->dattrib[use_cons];
	    p2->dattrib[use_cons]   = 0.0;
	  }	   

				// Particle 1: electron
				// Particle 2: ion
	} else if (interFlag > 200 and interFlag < 300) {

	  if (elc_cons) {
	    del += p1->dattrib[use_elec+3];
	    p1->dattrib[use_elec+3] = 0.0;
	  } else {
	    del += p1->dattrib[use_cons];
	    p1->dattrib[use_cons] = 0.0;
	  }

	  del += p2->dattrib[use_cons];
	  p2->dattrib[use_cons] = 0.0;

				// Particle 1: ion
				// Particle 2: ion
	} else {

	  del += p1->dattrib[use_cons];
	  p1->dattrib[use_cons] = 0.0;

	  del += p2->dattrib[use_cons];
	  p2->dattrib[use_cons] = 0.0;

	}

      } else {
	del = p1->dattrib[use_cons] + p2->dattrib[use_cons];
	p1->dattrib[use_cons] = p2->dattrib[use_cons] = 0.0;
      }

      Exs  += del;
      totE += del;

    } else {

      //
      // Not a trace interaction
      //
      if (Z1 == Z2) {

	double del = 0.0;

	if (use_elec>=0) {
				// Particle 1: ion
				// Particle 2: electron
	  if (interFlag > 100 and interFlag < 200) {

	    del += p1->dattrib[use_cons];
	    p1->dattrib[use_cons] = 0.0;

	    if (elc_cons) {
	      del += p2->dattrib[use_elec+3];
	      p2->dattrib[use_elec+3] = 0.0;
	    } else {
	      del += p2->dattrib[use_cons];
	      p2->dattrib[use_cons]   = 0.0;
	    }

				// Particle 1: electron
				// Particle 2: ion
	  } else if (interFlag > 200 and interFlag < 300) {

	    if (elc_cons) {
	      del += p1->dattrib[use_elec+3];
	      p1->dattrib[use_elec+3] = 0.0;
	    } else {
	      del += p1->dattrib[use_cons];
	      p1->dattrib[use_cons] = 0.0;
	    }

	    del += p2->dattrib[use_cons];
	    p2->dattrib[use_cons] = 0.0;

				// Particle 1: ion
				// Particle 2: ion
	  } else {

	    del += p1->dattrib[use_cons];
	    p1->dattrib[use_cons] = 0.0;

	    del += p2->dattrib[use_cons];
	    p2->dattrib[use_cons] = 0.0;

	  }

	} else {
	  del = p1->dattrib[use_cons] + p2->dattrib[use_cons];
	  p1->dattrib[use_cons] = p2->dattrib[use_cons] = 0.0;
	}


	Exs  += del;
	totE += del;

      } else if (ExactE) {

	// Particle 1: ion
	// Particle 2: electron
	if (interFlag > 100 and interFlag < 200) {
	  p1->dattrib[use_cons]     += -0.5*delE;
	  if (elc_cons)
	    p2->dattrib[use_elec+3] += -0.5*delE;
	  else
	    p2->dattrib[use_cons]   += -0.5*delE;
	}
	// Particle 1: electron
	// Particle 2: ion
	else if (interFlag > 200 and interFlag < 300) {
	  if (elc_cons)
	    p1->dattrib[use_elec+3] += -0.5*delE;
	  else
	    p1->dattrib[use_cons]   += -0.5*delE;
	  p2->dattrib[use_cons]     += -0.5*delE;
	}
	// Neutral interaction
	else {
	  p1->dattrib[use_cons]     += -0.5*delE;
	  p2->dattrib[use_cons]     += -0.5*delE;
	}

	// Reset total energy to initial energy, deferring an changes to
	// non-trace interactions
	//
	totE = kE;

	in_exactE = true;
      }

    } // end : SAME_TRAC_SUPP if/then

  } // end: trace-particle energy loss assignment


  vrel = unit_vector();
  for (auto & v : vrel) v *= vi;
  //                         ^
  //                         |
  // Velocity in center of mass, computed from v1, v2
  //

  // Attempt to defer negative energy adjustment
  //
  double missE = std::min<double>(0.0, totE);


  // Compute the change of energy in the collision frame by computing
  // the velocity reduction factor
  //
  double vfac = 1.0;
  if (kE>0.0) vfac = totE>0.0 ? sqrt(totE/kE) : 0.0;


  // Use explicit energy conservation algorithm
  //
  double vrat = 1.0;
  double gamm = 0.0;
  bool  algok = false;

  std::vector<double> w1(3, 0.0);

  if (ExactE and q < 1.0) {

    double v1i2 = 0.0, b1f2 = 0.0, v2i2 = 0.0, b2f2 = 0.0, vcm2 = 0.0, udif = 0.0;
    std::vector<double> uu(3), vv(3);
    for (size_t k=0; k<3; k++) {
      uu[k] = vcom[k] + m2/Mt*vrel[k];
      vv[k] = vcom[k] - m1/Mt*vrel[k];
      vcm2 += vcom[k] * vcom[k];
      v1i2 += v1[k] * v1[k];
      v2i2 += v2[k] * v2[k];
      b1f2 += uu[k] * uu[k];
      b2f2 += vv[k] * vv[k];
      udif += (v1[k] - uu[k])*(v1[k] - uu[k]);
    }

    if (AlgOrth) {

      // Cross product to determine orthgonal direction
      //
      w1 = uu ^ v1;

      // Normalize
      //
      double wnrm = 0.0;
      for (auto   v : w1) wnrm += v*v;

      const double tol = 1.0e-12;
      // Generate random vector if |u|~0 or |v1|~0
      if (v1i2 < tol*b1f2 or b1f2 < tol*v1i2) {
	for (auto & v : w1) v = (*norm)();
      }
      // Choose random orthogonal vector if uu || v1
      else if (wnrm < tol*v1i2) {
	auto t3 = zorder(v1);
	int i0 = std::get<0>(t3), i1 = std::get<1>(t3), i2 = std::get<2>(t3);
	w1[i0] = (*norm)();
	w1[i1] = (*norm)();
	w1[i2] = -(w1[i0]*v1[i0] + w1[i1]*v1[i1])/v1[i2];
	wnrm = 0.0; for (auto v : w1) wnrm += v*v;
      }
      // Sanity check on norm |w|
      if (wnrm > tol*sqrt(vcm2)) {
	for (auto & v : w1) v *= 1.0/sqrt(wnrm);
	gamm = sqrt(q*(1.0 - q)*udif);
	algok = true;
      }
    }

    if (!AlgOrth or !algok) {

      double qT = 0.0;
      for (size_t k=0; k<3; k++) qT += v1[k]*uu[k];

      if (v1i2 > 0.0 and b1f2 > 0.0) qT *= q/v1i2;

      vrat =
	( -qT + std::copysign(1.0, qT)*sqrt(qT*qT + (1.0 - q)*(q*b1f2/v1i2 + 1.0)) )/(1.0 - q);
    }

    // Test
    double v1f2 = 0.0;
    for (size_t k=0; k<3; k++) {
      double vv = (1.0 - q)*v1[k]*vrat + q*uu[k] + w1[k]*gamm;
      v1f2 += vv*vv;
    }

    double KE_1i = 0.5*m1*v1i2;
    double KE_2i = 0.5*q*m2*v2i2;
    double KE_1f = 0.5*m1*v1f2;
    double KE_2f = 0.5*q*m2*b2f2;

    double KEi   = KE_1i + KE_2i;
    double KEf   = KE_1f + KE_2f;
    double difE  = KEi - KEf;

    if (fabs(difE)/(KEi + KEf) > 1.0e-10) {
      std::cout << "Ooops, delE = " << difE
		<< ", totE = " << KEi + KEf << std::endl;
    }
  }

  // Update post-collision velocities.  In the electron version, the
  // momentum is assumed to be coupled to the ions, so the ion
  // momentum must be conserved.  Particle 2 is trace by construction.
  //
  double deltaKE = 0.0, qKEfac = 0.5*Wa*m1*q*(1.0 - q);
  for (size_t k=0; k<3; k++) {
    double v0 = vcom[k] + m2/Mt*vrel[k]*vfac;

    if (!ExactE)
      deltaKE += (v0 - v1[k])*(v0 - v1[k]) * qKEfac;

    v1[k] = (1.0 - q)*v1[k]*vrat + q*v0 + w1[k]*gamm;
    v2[k] = vcom[k] - m1/Mt*vrel[k]*vfac;
  }

  // Save energy adjustments for next interation.  Split between like
  // species ONLY.
  //
  if (use_cons >= 0) {

    double del = -missE;

    // Energy is added to electron KE for use_elec >= 0
    if (use_elec < 0)
      del -= deltaKE;

    else if (C1==1 and C2==1)
      del -= deltaKE;

    if (TRACE_OVERRIDE) {
      //
      // Override default trace species treatment; split energy
      // adjustment between interaction particles
      //
      if (C1 == 1 or use_elec<0)
	p1->dattrib[use_cons  ]   += 0.5*del;
      else {
	if (elc_cons)
	  p1->dattrib[use_elec+3] += 0.5*del;
	else
	  p1->dattrib[use_cons]   += 0.5*del;
      }
      
      if (C1 == 2 or use_elec<0)
	p2->dattrib[use_cons]     += 0.5*del;
      else {
	if (elc_cons)
	  p2->dattrib[use_elec+3] += 0.5*del;
	else
	  p2->dattrib[use_cons]   += 0.5*del;
      }

    } else {
      //
      // Split energy adjustment between like species ONLY.
      // Otherwise, assign to non-trace particle.
      //
      if (Z1 == Z2) {
	p1->dattrib[use_cons] += 0.5*del;
	p2->dattrib[use_cons] += 0.5*del;
      } else {
	if (C1 == 1 or use_elec<0)
	  p1->dattrib[use_cons]     += del;
	else {
	  if (elc_cons) 
	    p1->dattrib[use_elec+3] += del;
	  else
	    p1->dattrib[use_cons  ] += del;
	}	    
      }
    }
  }

  // Update particle velocties
  // -------------------------
  //
  // Electron velocity is computed so that momentum is conserved
  // ignoring the donor ion
  //
  bool electronic = false;

  if (use_elec and interFlag > 100 and interFlag < 200) {

    electronic = true;

    if (equiptn) {
      for (size_t k=0; k<3; k++) {
	vcom[k] = (m1*p1->vel[k] + m2*p2->dattrib[use_elec+k])/Mt;
	vrel[k] = (vcom[k] - v2[k])*Mt/m1;
      }

      for (size_t k=0; k<3; k++) {
	p1->vel[k] = vcom[k] + m2/Mt * vrel[k];
      }
    }

    // Upscale electron velocity to conserve energy
    //
    double vfac = 1.0;
    if (Z1 != Z2) {
      if (TRACE_ELEC) {
	p1->dattrib[use_cons]     -= deltaKE * (1.0 - TRACE_FRAC);
	if (elc_cons)
	  p2->dattrib[use_elec+3] -= deltaKE * TRACE_FRAC;
	else
	  p2->dattrib[use_cons  ] -= deltaKE * TRACE_FRAC;
      } else {
	double ke2 = 0.0;
	for (auto v : v2) ke2 += v*v;
	ke2 *= 0.5*Wb*m2;
	vfac = sqrt(1.0 + deltaKE/ke2);
      }
    }

    // Electron from particle #2
    //
    double elecE0 = 0.0;
    for (size_t k=0; k<3; k++) {
				// Compute energy
      v2[k] *= vfac;
      vf2 += v2[k] * v2[k];
				// Assign to particles
      p1->vel[k] = v1[k];
      elecE0 += v2[k] * v2[k];
      p2->dattrib[use_elec+k] = v2[k];
				// Zero velocity for recombined
				// electron
      if (interFlag == recomb_1 and C1==1)
	p1->dattrib[use_elec+k] = 0.0;
    }

				// Duplicate electron energy if previously
				// neutral
				//
    if (CLONE_ELEC and interFlag == ionize_1 and C1==2) {

      double EE1 = 0.0, EE2 = 0.0;
	for (size_t k=0; k<3; k++) {
	  EE1 += v1[k]*v1[k];
	  EE2 += v2[k]*v2[k];
      }

      for (size_t k=0; k<3; k++)
	p2->dattrib[use_elec+k] = v2[k] * EE1/EE2;

				// Share electron energy if previously
				// neutral
				//
    } else if (!NOSHARE_ELEC and interFlag == ionize_1 and C1==2) {
				// KE prefactor for Particle #1
      double fE1 = 0.5*Wa*atomic_weights[0];
				// KE prefactor for Particle #2
      double fE2 = 0.5*Wb*atomic_weights[0];
				// For energy conservation
      double elecE1 = 0.0, elecE2 = 0.0;

      //
      // Split the energy between the outgoing electrons
      //

      //
      // Initial electron energy of Particle #1 is 0 (i.e. neutral)
      // Total electron energy is that of Particle #2:
      // E0= 1/2*m_e*Wb*v0^2
      //
      // Split between two electrons:
      // E1 = (1 - u)*E0 = (1 - u)*1/2*m_e*Wb*v0^2 = 1/2*m_e*Wa*v1^2
      // ---> v1^2 = (1-u)*q*v0^2
      //
      // E2 = u*E0 = u*1/2*m_e*Wb*v0^2 = 1/2*m_e*Wb*v2^2
      // ---> v2^2 = u*v0^2
      //
      double u  = (*unit)();
      double vs1 = sqrt(q*(1.0 - u));
      double vs2 = sqrt(u);

      for (size_t k=0; k<3; k++) {
	double t1 = vs1*v2[k];
	double t2 = vs2*v2[k];
	elecE1 += fE1*t1*t1;
	elecE2 += fE2*t2*t2;
	p1->dattrib[use_elec+k] = t1;
	p2->dattrib[use_elec+k] = t2;
      }

      elecE0 *= fE2;

      double deltaE_e = elecE0 - elecE1 - elecE2;

      NCXTRA += deltaE_e;

      if (use_cons>=0) {
	if (q<1.0) {
	  if (elc_cons)
	    p1->dattrib[use_elec+3] += deltaE_e;
	  else
	    p1->dattrib[use_cons]   += deltaE_e;
	} else {
	  if (elc_cons) {
	    p1->dattrib[use_elec+3] += 0.5*deltaE_e;
	    p2->dattrib[use_elec+3] += 0.5*deltaE_e;
	  } else {
	    p1->dattrib[use_cons]   += 0.5*deltaE_e;
	    p2->dattrib[use_cons]   += 0.5*deltaE_e;
	  }
	}
      }
    }

    // For diagnostic electron energy loss/gain distribution
    //
    velER[id].push_back(vf2/vi2);

  } else if (use_elec and interFlag > 200 and interFlag < 300) {

    electronic = true;

    if (equiptn) {
      for (size_t k=0; k<3; k++) {
	vcom[k] = (m1*p1->dattrib[use_elec+k] + m2*p2->vel[k])/Mt;
	vrel[k] = (vcom[k] - v1[k])*Mt/m2;
      }

      for (size_t k=0; k<3; k++) {
	p2->vel[k] = vcom[k] + m1/Mt * vrel[k];
      }
    }

    // Upscale electron velocity to conserve energy
    //
    double vfac = 1.0;
    if (Z1 != Z2) {
      if (TRACE_ELEC) {
	if (elc_cons)
	  p1->dattrib[use_elec+3] -= deltaKE * TRACE_FRAC;
	else
	  p1->dattrib[use_cons]   -= deltaKE * TRACE_FRAC;
	p2->dattrib[use_cons]     -= deltaKE * (1.0 - TRACE_FRAC);
      } else {
	double ke1 = 0.0;
	for (auto v : v1) ke1 += v*v;
	ke1 *= 0.5*Wa*m1;
	vfac = sqrt(1.0 + deltaKE/ke1);
      }
    }

    // Electron from particle #1
    //
    double elecE0 = 0.0;
    for (size_t k=0; k<3; k++) {
				// Compute energy
      v1[k] *= vfac;
      vf2 += v1[k] * v1[k];
				// Assign to particles
      elecE0 += v1[k] * v1[k];
      p1->dattrib[use_elec+k] = v1[k];
      p2->vel[k] = v2[k];
				// Zero velocity for recombined
				// electron
      if (interFlag == recomb_2 and C2==1)
	p2->dattrib[use_elec+k] = 0.0;
    }

				// Duplicate electron energy if previously
				// neutral
				//
    if (CLONE_ELEC and interFlag == ionize_2 and C2==2) {

      double EE1 = 0.0, EE2 = 0.0;
	for (size_t k=0; k<3; k++) {
	  EE1 += v1[k]*v1[k];
	  EE2 += v2[k]*v2[k];
      }

      for (size_t k=0; k<3; k++)
	p1->dattrib[use_elec+k] = v2[k] * EE2/EE1;

				// Share electron energy if previously
				// neutral
				//
    } else if (!NOSHARE_ELEC and interFlag == ionize_2 and C2==2) {
				// KE prefactor for Particle #1
      double fE1 = 0.5*Wa*atomic_weights[0];
				// KE prefactor for Particle #2
      double fE2 = 0.5*Wb*atomic_weights[0];
				// For energy conservation
      double elecE1 = 0.0, elecE2 = 0.0;

      //
      // Split fraction q of the energy between the outgoing electrons
      //

      //
      // Initial electron energy of Particle #2 is 0 (neutral)
      // Total electron energy is that of Particle #1:
      // E0= 1/2*m_e*Wa*v0^2
      //
      // Split initial particle by q, then between two electrons:
      // E1 = (1 - q)*E0 + q*(1 - u)*E0
      //    = [(1 - q) + q*(1 - u)]*1/2*m_e*Wa*v0^2 = 1/2*m_e*Wa*v1^2
      // ---> v1^2 = [1-q + q*(1-u)]*v0^2
      //
      // E2 = u*q*E0 = u*1/2*m_e*Wb*v0^2 = 1/2*m_e*Wb*v2^2
      // ---> v2^2 = u*v0^2
      //

      double u  = (*unit)();
      double vs1 = sqrt((1.0 - q) + q*(1.0 - u));
      double vs2 = sqrt(u);

      for (size_t k=0; k<3; k++) {
	double t1 = vs1*v1[k];
	double t2 = vs2*v1[k];
	elecE1 += fE1*t1*t1;
	elecE2 += fE2*t2*t2;
	p1->dattrib[use_elec+k] = t1;
	p2->dattrib[use_elec+k] = t2;
      }

      elecE0 *= fE1;

      double deltaE_e = elecE0 - elecE1 - elecE2;

      NCXTRA += deltaE_e;

      if (use_cons>=0) {
	if (q<1.0) {
	  if (elc_cons)
	    p1->dattrib[use_elec+3] += deltaE_e;
	  else
	    p1->dattrib[use_cons]   += deltaE_e;
	} else {
	  if (elc_cons) {
	    p1->dattrib[use_elec+3] += 0.5*deltaE_e;
	    p2->dattrib[use_elec+3] += 0.5*deltaE_e;
	  } else {
	    p1->dattrib[use_cons]   += 0.5*deltaE_e;
	    p2->dattrib[use_cons]   += 0.5*deltaE_e;
	  }
	}
      }
    }

    // For diagnostic electron energy loss/gain distribution
    //
    velER[id].push_back(vf2/vi2);

  } else {
    for (size_t k=0; k<3; k++) {
      p1->vel[k] = v1[k];
      p2->vel[k] = v2[k];
    }
  }

  // KE debugging
  //
  if (KE_DEBUG) {

    for (auto v : v1) KE1f += v*v;
    for (auto v : v2) KE2f += v*v;

				// Pre collision KE
    KE1i *= 0.5*Wa*m1;
    KE2i *= 0.5*Wb*m2;
				// Post collision KE
    KE1f *= 0.5*Wa*m1;
    KE2f *= 0.5*Wb*m2;

    double tKEi = KE1i + KE2i;	// Total pre collision KE
    double tKEf = KE1f + KE2f;	// Total post collision KE
    double dKE  = tKEi - tKEf;	// Energy balance

    if (m1<1.0) {
      if (KE1i > 0) keER[id].push_back((KE1i - KE1f)/KE1i);
      if (KE2i > 0) keIR[id].push_back((KE2i - KE2f)/KE2i);
    }

    if (m2<1.0) {
      if (KE1i > 0) keIR[id].push_back((KE1i - KE1f)/KE1i);
      if (KE2i > 0) keER[id].push_back((KE2i - KE2f)/KE2i);
    }
				// Check energy balance including excess
    double testE = dKE;

    if (Z1 == Z2 or !ExactE) testE -= delE + missE;

				// Add in energy loss/gain
    if (Z1==Z2 or TRACE_OVERRIDE)
      testE += Exs;
				// Correct for trace-algorithm excess
    else if ( (C1==1 and C2==1) or electronic  )
      testE -= deltaKE;

    if (fabs(testE) > tolE*(tKEi+tKEf) )
      std::cout << "Total ("<< m1 << "," << m2 << ") = "
		<< std::setw(14) << testE
		<< ", dKE=" << std::setw(14) << dKE
		<< ", KE0=" << std::setw(14) << kE
		<< ", tot=" << std::setw(14) << totE
		<< ", com=" << std::setw(14) << deltaKE
		<< ", exs=" << std::setw(14) << Exs
		<< ", del=" << std::setw(14) << delE
		<< ", mis=" << std::setw(14) << missE
		<< ", fac=" << std::setw(14) << vfac
		<< (in_exactE ? ", in ExactE" : "")
		<< std::endl;

  } // Energy conservation debugging diagnostic (KE_DEBUG)



  // Enforce electron equipartition
  //
  if (equiptn and use_elec) {

    // Electron from Particle 2
    //
    if (interFlag > 100 and interFlag < 200) {

      m1 = atomic_weights[Z2];
      m2 = atomic_weights[0 ];
      Mt = m1 + m2;
      Mu = m1 * m2 / Mt;

      KE1i = KE2i = 0.0;
      KE1f = KE2f = 0.0;

      double cost = 0.0, VC2 = 0.0, VR2 = 0.0;

      for (size_t k=0; k<3; k++) {
	KE1i += p2->vel[k] * p2->vel[k];
	KE2i += p2->dattrib[use_elec+k] * p2->dattrib[use_elec+k];
	cost += p2->vel[k] * p2->dattrib[use_elec+k];

	vcom[k] = (m1*p2->vel[k] + m2*p2->dattrib[use_elec+k])/Mt;
	vrel[k] = p2->vel[k] - p2->dattrib[use_elec+k];

	VC2    += vcom[k] * vcom[k];
	VR2    += vrel[k] * vrel[k];
      }

      if (KE1i > 0.0 and KE2i > 0.0) cost /= sqrt(KE1i * KE2i);

      double dmr   = cost / (m1 - m2);
      double gamma = 1.0 + 4.0*Mt*Mu*dmr*dmr;
      double E0    = 0.5*Mt*VC2 + 0.5*Mu*VR2;

      double gamP  = 1.0 + sqrt(1.0 - 1.0/gamma);
      double gamN  = 1.0 - sqrt(1.0 - 1.0/gamma);

      double virP  =
	(VC2 - E0/Mt*gamN)*(VC2 - E0/Mt*gamN) +
	(VR2 - E0/Mu*gamP)*(VR2 - E0/Mu*gamP) ;

      double virN  =
	(VC2 - E0/Mt*gamP)*(VC2 - E0/Mt*gamP) +
	(VR2 - E0/Mu*gamN)*(VR2 - E0/Mu*gamN) ;

      double vcfac = 0.0, vrfac = 0.0;

      if (virP > virN) {
	vcfac = sqrt(E0/Mt*gamN);
	vrfac = sqrt(E0/Mu*gamP);
      } else {
	vcfac = sqrt(E0/Mt*gamP);
	vrfac = sqrt(E0/Mu*gamN);
      }

      if (VC2>0.0) {
	for (size_t k=0; k<3; k++) vcom[k] /= sqrt(VC2);
      } else {
	vcom = unit_vector();
      }

      if (VR2>0.0) {
	for (size_t k=0; k<3; k++) vrel[k] /= sqrt(VR2);
      } else {
	vrel = unit_vector();
      }

      for (size_t k=0; k<3; k++) {
	p2->vel[k]              = vcom[k]*vcfac + m2/Mt * vrel[k]*vrfac;
	p2->dattrib[use_elec+k] = vcom[k]*vcfac - m1/Mt * vrel[k]*vrfac;

	KE1f += p2->vel[k] * p2->vel[k];
	KE2f += p2->dattrib[use_elec+k] * p2->dattrib[use_elec+k];
      }

      KE1i *= 0.5*m1;
      KE1f *= 0.5*m1;

      KE2i *= 0.5*m2;
      KE2f *= 0.5*m2;

      double KEi = KE1i + KE2i;
      double KEf = KE1f + KE2f;

      if ( fabs(KEi - KEf) > 1.0e-14*KEi ) {
	std::cout << "Test(1): keI=["
		  << std::setw(16) << KE1i << ", "
		  << std::setw(16) << KE2i << "] keF=["
		  << std::setw(16) << KE1f << ", "
		  << std::setw(16) << KE2f << "] vir=["
		  << std::setw(16) << virP << ", "
		  << std::setw(16) << virN << "] "
		  << std::endl;
      }

    } // end: electron from Particle 2

    // Electron from Particle 1
    //
    if (interFlag > 200 and interFlag < 300) {

      m1 = atomic_weights[Z1];
      m2 = atomic_weights[0 ];
      Mt = m1 + m2;
      Mu = m1 * m2 / Mt;

      KE1i = KE2i = 0.0;
      KE1f = KE2f = 0.0;

      double cost = 0.0, VC2 = 0.0, VR2 = 0.0;

      for (size_t k=0; k<3; k++) {
	KE1i += p1->vel[k] * p1->vel[k];
	KE2i += p1->dattrib[use_elec+k] * p1->dattrib[use_elec+k];
	cost += p1->vel[k] * p1->dattrib[use_elec+k];

	vcom[k] = (m1*p1->vel[k] + m2*p1->dattrib[use_elec+k])/Mt;
	vrel[k] = p1->vel[k] - p1->dattrib[use_elec+k];

	VC2    += vcom[k] * vcom[k];
	VR2    += vrel[k] * vrel[k];
      }

      if (KE1i > 0.0 and KE2i > 0.0) cost /= sqrt(KE1i * KE2i);

      double dmr   = cost / (m1 - m2);
      double gamma = 1.0 + 4.0*Mt*Mu*dmr*dmr;
      double E0    = 0.5*Mt*VC2 + 0.5*Mu*VR2;

      double gamP  = 1.0 + sqrt(1.0 - 1.0/gamma);
      double gamN  = 1.0 - sqrt(1.0 - 1.0/gamma);

      double virP  =
	(VC2 - E0/Mt*gamN)*(VC2 - E0/Mt*gamN) +
	(VR2 - E0/Mu*gamP)*(VR2 - E0/Mu*gamP) ;

      double virN  =
	(VC2 - E0/Mt*gamP)*(VC2 - E0/Mt*gamP) +
	(VR2 - E0/Mu*gamN)*(VR2 - E0/Mu*gamN) ;

      double vcfac = 0.0, vrfac = 0.0;

      if (virP > virN) {
	vcfac = sqrt(E0/Mt*gamN);
	vrfac = sqrt(E0/Mu*gamP);
      } else {
	vcfac = sqrt(E0/Mt*gamP);
	vrfac = sqrt(E0/Mu*gamN);
      }

      if (VC2>0.0) {
	for (size_t k=0; k<3; k++) vcom[k] /= sqrt(VC2);
      } else {
	vcom = unit_vector();
      }

      if (VR2>0.0) {
	for (size_t k=0; k<3; k++) vrel[k] /= sqrt(VR2);
      } else {
	vrel = unit_vector();
      }

      for (size_t k=0; k<3; k++) {
	p1->vel[k]              = vcom[k]*vcfac + m2/Mt * vrel[k]*vrfac;
	p1->dattrib[use_elec+k] = vcom[k]*vcfac - m1/Mt * vrel[k]*vrfac;

	KE1f += p1->vel[k] * p1->vel[k];
	KE2f += p1->dattrib[use_elec+k] * p1->dattrib[use_elec+k];
      }

      KE1i *= 0.5*m1;
      KE1f *= 0.5*m1;

      KE2i *= 0.5*m2;
      KE2f *= 0.5*m2;

      double KEi = KE1i + KE2i;
      double KEf = KE1f + KE2f;

      if ( fabs(KEi - KEf) > 1.0e-14*KEi ) {
	std::cout << "Test(1): keI=["
		  << std::setw(16) << KE1i << ", "
		  << std::setw(16) << KE2i << "] keF=["
		  << std::setw(16) << KE1f << ", "
		  << std::setw(16) << KE2f << "] vir=["
		  << std::setw(16) << virP << ", "
		  << std::setw(16) << virN << "] "
		  << std::endl;
      }

    }  // end: electron from Particle 2

  } // Equipartition stanza for electrons

  // Scatter electrons
  //
  if (esType == always and C1>1 and C2>1) {
    double vi = 0.0;
    for (int k=0; k<3; k++) {
      double d1 = p1->dattrib[use_elec+k];
      double d2 = p2->dattrib[use_elec+k];
      vcom[k] = 0.5*(d1 + d2);
      vi     += (d1 - d2) * (d1 - d2);
    }
    vi = sqrt(vi);

    vrel = unit_vector();

    for (int k=0; k<3; k++) {
      p1->dattrib[use_elec+k] = vcom[k] + 0.5*vrel[k];
      p2->dattrib[use_elec+k] = vcom[k] - 0.5*vrel[k];
    }

    return 0;
  }

  // NOCOOL debugging
  //
  double fI1 = 0.0, fE1 = 0.0;
  double fI2 = 0.0, fE2 = 0.0;
  if (KE_DEBUG) {
    for (size_t k=0; k<3; k++) {
      fI1 += p1->vel[k]*p1->vel[k];
      fI2 += p2->vel[k]*p2->vel[k];
      if (use_elec>=0) {
	if (C1>1) fE1 += p1->dattrib[use_elec+k]*p1->dattrib[use_elec+k];
	if (C2>1) fE2 += p2->dattrib[use_elec+k]*p2->dattrib[use_elec+k];
      }
    }
    fI1 *= 0.5*p1->mass;
    fI2 *= 0.5*p2->mass;
    fE1 *= 0.5*p1->mass * atomic_weights[0]/atomic_weights[Z1];
    fE2 *= 0.5*p2->mass * atomic_weights[0]/atomic_weights[Z2];

    double Einit = iI1 + iI2 + iE1 + iE2;
    double Efinl = fI1 + fI2 + fE1 + fE2;

    double testE = Einit - Efinl - NCXTRA;

    if (Z1==Z2)			// Add in energy loss/gain
      testE += Exs - delE - missE;
				// Correct for trace-algorithm excess
    else if ((C1==1 and C2==1) or electronic)
      testE -= deltaKE;

    if (fabs(testE) > tolE*Einit )
      std::cout << "NC total ("<< m1 << "," << m2 << ") = "
		<< std::setw(14) << testE
		<< ", flg=" << std::setw(6)  << interFlag
		<< ", dKE=" << std::setw(14) << Einit - Efinl
		<< ", com=" << std::setw(14) << deltaKE
		<< ", exs=" << std::setw(14) << Exs
		<< ", del=" << std::setw(14) << delE
		<< ", mis=" << std::setw(14) << missE
		<< ", NCX=" << std::setw(14) << NCXTRA
		<< std::endl;
  }

  return ret;
}

void CollideIon::normTest(Particle* const p, const std::string& lab)
{
  static unsigned long serialno = 0;

  serialno++;

  double tot = 0.0;
  bool posdef = true;
  unsigned short Z = 0;

  if (aType == Trace ) {
    for (auto s : SpList) {
      tot += p->dattrib[s.second];
      if (p->dattrib[s.second] < 0.0) posdef = false;
    }
  } else {
    Z = KeyConvert(p->iattrib[use_key]).Z();
    
    for (unsigned short C=0; C<=Z; C++) {
      tot += p->dattrib[spc_pos+C];
      if (p->dattrib[spc_pos+C] < 0.0) posdef = false;
    }
  }

  if (!posdef) {
    std::cout << "[" << myid << "] Values not posdef, norm" << tot << " for " << lab
	      << ", T=" << tnow  << ", index=" << p->indx
	      << ", Z=" << Z << ", #=" << serialno;
    std::cout << ", ";
    if (aType == Trace) {
      for (auto s : SpList)
	std::cout << std::setw(18) << p->dattrib[s.second];
    } else {
      for (size_t C=0; C<=Z; C++)
	std::cout << std::setw(18) << p->dattrib[spc_pos+C];
    }
    std::cout << std::endl;
  }

  if (tot > 0.0) {
    if (fabs(tot-1.0) > 1.0e-6) {
      std::cout << "[" << myid << "] Unexpected norm=" << tot << " for " << lab
		<< ", T=" << tnow  << ", index=" << p->indx
		<< ", Z=" << Z << ", #=" << serialno;
      if (DEBUG_CNT>=0) std::cout << ", Count=" << p->iattrib[DEBUG_CNT];
      std::cout << ", ";
      if (aType == Trace) {
	for (auto s : SpList) {
	  std::cout << std::setw(18) << p->dattrib[s.second];
	  p->dattrib[s.second] /= tot;
	}
      } else {
	for (size_t C=0; C<=Z; C++) {
	  std::cout << std::setw(18) << p->dattrib[spc_pos+C];
	  p->dattrib[spc_pos+C] /= tot;
	}
      }
      std::cout << std::endl;
    }
  } else {
    std::cout << "[" << myid << "] Invalid zero norm for " << lab << ", T=" << tnow
	      << ", index=" << p->indx << ", Z=" << Z << ", #=" << serialno;
    if (DEBUG_CNT>=0) std::cout << ", Count=" << p->iattrib[DEBUG_CNT];
    std::cout << std::endl;
  }
}

void CollideIon::secondaryScatter(Particle *p)
{
  if (use_elec<0) return;

  double KEi = 0.0;
  if (DBG_NewTest) KEi = energyInPart(p);

  unsigned short Z = KeyConvert(p->iattrib[use_key]).getKey().first;

  double W1 = 0.0;
  double W2 = 0.0;

  double M1 = atomic_weights[Z];
  double M2 = atomic_weights[0];

  if (aType == Trace) {
    for (auto s : SpList) {
      speciesKey k = s.first;
      double     P = k.second - 1;
      W1 += p->dattrib[s.second]/atomic_weights[k.first];
      W2 += p->dattrib[s.second]/atomic_weights[k.first] * P;
    }
    M1 = 1.0/W1;
  } else {
    W1 = 0.0;
    for (unsigned short C=0; C<=Z; C++) W2 += p->dattrib[spc_pos + C] * C;
  }

  double MT = M1 + M2;

  std::vector<double> v1(3), v2(3);

  if (W1 >= W2) {
    for (size_t k=0; k<3; k++) {
      v1[k] = p->vel[k];
      v2[k] = p->dattrib[use_elec+k];
    }
  } else {
    zswap(W1, W2);
    zswap(M1, M2);

    for (size_t k=0; k<3; k++) {
      v1[k] = p->dattrib[use_elec+k];
      v2[k] = p->vel[k];
    }
  }

  double q = W2/W1;

  std::vector<double> vcom(3), uu(3), vv(3);
  double vi = 0.0;
  for (int k=0; k<3; k++) {
    vcom[k] = (M1*v1[k] + M2*v2[k])/MT;
    double v = v1[k] - v2[k];
    vi += v * v;
  }
  vi = sqrt(vi);

  std::vector<double> vrel = unit_vector();
  for (auto & v : vrel) v *= vi;

  // Use explicit energy conservation algorithm
  //
  double v1i2 = 0.0, b1f2 = 0.0, v2i2 = 0.0, vrat = 1.0;
  for (size_t k=0; k<3; k++) {
    uu[k] = vcom[k] + M2/MT*vrel[k];
    vv[k] = vcom[k] - M1/MT*vrel[k];
    v1i2 += v1[k] * v1[k];
    v2i2 += v2[k] * v2[k];
    b1f2 += uu[k] * uu[k];
  }

  if (q < 1.0) {
    double qT = 0.0;
    for (size_t k=0; k<3; k++) qT += v1[k]*uu[k];

    if (v1i2 > 0.0 and b1f2 > 0.0) qT *= q/v1i2;

    vrat = -qT +
      std::copysign(1.0, qT)*sqrt(qT*qT + (1.0 - q)*(q*b1f2/v1i2 + 1.0));
  }

  for (size_t k=0; k<3; k++) {
    double v0 = vcom[k] + M2/MT*vrel[k];
    v1[k] = v1[k]*vrat + q*v0;
    v2[k] = vcom[k] - M1/MT*vrel[k];
  }
  
  if (DBG_NewTest) {
    double KEf = energyInPart(p);
    double dE = KEi - KEf;
    if (fabs(dE) > tolE*KEi) {
      std::cout << "**ERROR secondary scatter dE=" << dE
		<< " rel=" << dE/KEi << " q=" << q << std::endl;
    } else {
      std::cout << "**GOOD secondary scatter dE=" << dE
		<< " rel=" << dE/KEi << " q=" << q << std::endl;
    }
  }
}

double CollideIon::energyInPart(Particle *p)
{
  double ee = 0.0;

  if (aType == Trace) {
    for (auto s : SpList) {
      speciesKey k = s.first;
      unsigned short P = k.second - 1;
      ee += p->dattrib[s.second]*P/atomic_weights[k.first];
    }

    ee *= atomic_weights[0];
  } else {
    unsigned short Z = KeyConvert(p->iattrib[use_key]).getKey().first;

    for (unsigned short C=1; C<=Z; C++) ee += p->dattrib[spc_pos+C]*C;

    ee *= atomic_weights[0]/atomic_weights[Z];
  }

  if (false and DBG_NewTest)
    std::cout << "energyInPart: eta="
	      << std::setprecision(14) << std::setw(22) << ee << std::endl;


  double KEi = 0.0, KEe = 0.0;
  for (size_t k =0; k<3; k++) {
    KEi += p->vel[k] * p->vel[k];
    if (use_elec>=0) {
      KEe += p->dattrib[use_elec+k] * p->dattrib[use_elec+k];
    }
  }

  if (false and DBG_NewTest)
    std::cout << "energyInPart: vi2="
	      << std::setprecision(14) << std::setw(22) << KEi
	      << " ve2=" << std::setw(22) << KEe << std::endl;

  KEi *= 0.5*p->mass;
  KEe *= 0.5*p->mass*ee;

  if (false and DBG_NewTest)
    std::cout << "energyInPart: KEi="
	      << std::setprecision(14) << std::setw(22) << KEi
	      << " KEe=" << std::setw(22) << KEe
	      << " KEt=" << std::setw(22) << KEi + KEe << std::endl;

  return KEi + KEe;
}


double CollideIon::energyInPair(Particle *p1, Particle *p2)
{
  return energyInPart(p1) + energyInPart(p2);
}

std::pair<double, double>
CollideIon::energyInPairPartial(Particle *p1, Particle *p2, HybridColl iType,
				const std::string& msg)
{
  double n1 = 0.0, n2 = 0.0;

  if (aType == Trace) {
    for (auto s : SpList) {
      speciesKey k = s.first;
      unsigned short Z = k.first;      // Atomic number
      unsigned short P = k.second - 1; // Charge
      // Electron fraction
      n1 += p1->dattrib[s.second]*P/atomic_weights[Z];
      n2 += p2->dattrib[s.second]*P/atomic_weights[Z];
    }
    n1 *= atomic_weights[0];
    n2 *= atomic_weights[0];
  } else {
    unsigned short Z1 = KeyConvert(p1->iattrib[use_key]).getKey().first;
    unsigned short Z2 = KeyConvert(p2->iattrib[use_key]).getKey().first;

    for (unsigned short C=1; C<=Z1; C++) n1 += p1->dattrib[spc_pos+C]*C;
    for (unsigned short C=1; C<=Z2; C++) n2 += p2->dattrib[spc_pos+C]*C;

    n1 *= atomic_weights[0]/atomic_weights[Z1];
    n2 *= atomic_weights[0]/atomic_weights[Z2];
  }

  double KEi1 = 0.0, KEi2 = 0.0, KEe1 = 0.0, KEe2 = 0.0;
  for (size_t k =0; k<3; k++) {
    KEi1 += p1->vel[k] * p1->vel[k];
    KEi2 += p2->vel[k] * p2->vel[k];
    if (use_elec>=0) {
      KEe1 += p1->dattrib[use_elec+k] * p1->dattrib[use_elec+k];
      KEe2 += p2->dattrib[use_elec+k] * p2->dattrib[use_elec+k];
    }
  }

  if (false and DBG_NewTest and msg.size()) {
    std::cout << printDivider << std::endl;
    std::cout << msg << std::endl << printDivider << std::endl
	      << "Neutral" << std::endl
	      << std::setprecision(14) << std::scientific
	      << " e1=" << std::setw(22) << n1
	      << " e2=" << std::setw(22) << n2
	      << " V1=" << std::setw(22) << KEi1
	      << " V2=" << std::setw(22) << KEi2
	      << std::endl << "Ion1" << std::endl
	      << " e1=" << std::setw(22) << n1
	      << " e2=" << std::setw(22) << n2
	      << " V1=" << std::setw(22) << KEi1
	      << " V2=" << std::setw(22) << KEe2
	      << std::endl << "Ion2" << std::endl
	      << " e1=" << std::setw(22) << n1
	      << " e2=" << std::setw(22) << n2
	      << " V1=" << std::setw(22) << KEe1
	      << " V2=" << std::setw(22) << KEi2
	      << std::endl << printDivider << std::endl
	      << std::setprecision(5);
  }

  KEi1 *= 0.5*p1->mass;
  KEi2 *= 0.5*p2->mass;
  KEe1 *= 0.5*p1->mass*n1;
  KEe2 *= 0.5*p2->mass*n2;

  std::pair<double, double> ret;
  if (iType == Ion1) {
    ret.first  = KEi1 + KEe2;
    ret.second = KEi2 + KEe1;
  } else if (iType == Ion2) {
    ret.first  = KEe1 + KEi2;
    ret.second = KEi1 + KEe2;
  } else {
    ret.first  = KEi1 + KEi2;
    ret.second = KEe1 + KEe2;
  }

  return ret;
}

void CollideIon::checkProb(int id, bool norm, double tot)
{
  std::array<double, 3> DProb;

  double totalXS = 0.0;
  for (auto I : hCross[id]) {
    Interact::T O = I.t;
    Interact::pElem I1 = std::get<1>(O);
    Interact::pElem I2 = std::get<2>(O);
    double XS = I.crs;
    if (I2.first == Interact::electron)
      DProb[1] += XS;
    else if (I1.first == Interact::electron)
      DProb[2] += XS;
    else
      DProb[0] += XS;

    totalXS += XS;
  }

  if (norm) {
    double sum = 0.0;
    for (auto   v : DProb) sum += v;
    for (auto & v : DProb) v /= sum;
  }

  bool okay = true;
  for (size_t k=0; k<3; k++) {
    if (fabs(CProb[id][k] - DProb[k]) > 1.0e-10*DProb[k]+1.0e-16) okay = false;
  }

  if (not okay) {
    std::cout << std::setw(18) << "CProb"  << std::setw(18) << "DProb"  << std::endl
	      << std::setw(18) << "------" << std::setw(18) << "------" << std::endl;
    for (size_t k=0; k<3; k++) {
      std::cout << std::setw(18) << CProb[id][k] << std::setw(18) << DProb[k] << std::endl;
    }

    std::cout << std::endl;
    for (auto I : hCross[id]) {
      Interact::T O = I.t;
      Interact::pElem I1 = std::get<1>(O);
      Interact::pElem I2 = std::get<2>(O);
      double XS = I.crs;
      
      std::string S1, S2;
      if      (I1.first == Interact::neutral)   S1 = "neutral";
      else if (I1.first == Interact::ion)       S1 = "ion";
      else if (I1.first == Interact::electron)  S1 = "electron";

      if      (I2.first == Interact::neutral)   S2 = "neutral";
      else if (I2.first == Interact::ion)       S2 = "ion";
      else if (I2.first == Interact::electron)  S2 = "electron";
						  
      std::cout << std::setw(20) << interLabels[std::get<0>(O)]
		<< std::setw(10) << S1
		<< std::setw(10) << S2
		<< std::setw(18) << XS
		<< std::setw(18) << XS/totalXS
		<< std::endl;
    }

    if (norm) {
      std::cout << std::setw(20) << "TOTALS"
		<< std::setw(10) << ""
		<< std::setw(10) << ""
		<< std::setw(18) << tot
		<< std::setw(18) << 1.0
		<< std::endl;
      std::cout << std::endl << "CProb size=" << CProb[id].size() << std::endl;
    }

    std::cout << "**ERROR cprob does not match" << std::endl;

  } // end: not okay
}


int CollideIon::inelasticHybrid(int id, pCell* const c,
				Particle* const _p1, Particle* const _p2,
				double *cr, const Interact::T& itype, double prob)
{
  int ret         =  0;		// No error (flag)

  Particle* p1    = _p1;	// Copy pointers for swapping, if
  Particle* p2    = _p2;	// necessary

  typedef std::array<double, 3> Telem;
  std::array<Telem, 3> PE;
  for (auto & v : PE) v = {};	// Zero initialization


  // Species keys for pointers before swapping
  //
  speciesKey     k1 = KeyConvert(p1->iattrib[use_key]).getKey();
  speciesKey     k2 = KeyConvert(p2->iattrib[use_key]).getKey();

  unsigned short Z1 = k1.first;
  unsigned short Z2 = k2.first;

  if (SAME_INTERACT and Z1 != Z2) return ret;
  if (DIFF_INTERACT and Z1 == Z2) return ret;

  // These are the number of electrons in each particle to be scaled
  // by number of atoms/ions in each superparticle.  Compute electron
  // KE at the same time.
  //
  double iE1 = 0.0, iE2 = 0.0;
  if (use_elec) {
    for (size_t k=0; k<3; k++) {
      iE1 += p1->dattrib[use_elec+k] * p1->dattrib[use_elec+k];
      iE2 += p2->dattrib[use_elec+k] * p2->dattrib[use_elec+k];
    }
    iE1 *= 0.5*p1->mass * atomic_weights[0]/atomic_weights[Z1];
    iE2 *= 0.5*p2->mass * atomic_weights[0]/atomic_weights[Z2];
  }

  // Ion KE
  //
  double iI1 = 0.0, iI2 = 0.0;
  for (auto v : p1->vel) iI1 *= v*v;
  for (auto v : p2->vel) iI2 *= v*v;
  iI1 *= 0.5*p1->mass;
  iI2 *= 0.5*p2->mass;

  // Debug energy conservation
  //
  double KE_initl_check = 0.0;
  std::array<double, 2> KE_initl_econs = {0.0, 0.0};
  double deltaSum = 0.0, delEsum = 0.0, delEmis = 0.0, delEdfr = 0.0;
  double delEloss = 0.0, delEfnl = 0.0;
  
  if (KE_DEBUG) {
    KE_initl_check = energyInPair(p1, p2);
    if (use_cons>=0)
      KE_initl_econs[0] += p1->dattrib[use_cons  ] + p2->dattrib[use_cons  ];
    if (use_elec>=0 and elc_cons)
      KE_initl_econs[1] += p1->dattrib[use_elec+3] + p2->dattrib[use_elec+3];
  }

  // Proportional to number of true particles in each superparticle
  //
  double W1 = p1->mass/atomic_weights[Z1];
  double W2 = p2->mass/atomic_weights[Z2];

  std::array<PordPtr, 3> PP =
    { PordPtr(new Pord(this, p1, p2, W1, W2, Pord::ion_ion,      qCrit) ),
      PordPtr(new Pord(this, p1, p2, W1, W2, Pord::ion_electron, qCrit) ),
      PordPtr(new Pord(this, p1, p2, W1, W2, Pord::electron_ion, qCrit) ) };

  bool HWswitch = false;
  if (HybridWeightSwitch) {
    HWswitch = PP[0]->wght and PP[1]->wght and PP[2]->wght;
    Nwght[id]++;
  } else {
    Njsel[id]++;
  }

  // Sanity check
  //
  if (use_normtest) {
    normTest(p1, "p1 [Before]");
    normTest(p2, "p2 [Before]");
  }

  // Collision count debugging
  //
  if (DEBUG_CNT >= 0 and prob < 0.0) {
    p1->iattrib[DEBUG_CNT] += 1;
    p2->iattrib[DEBUG_CNT] += 1;
  }

  // NOCOOL debugging
  //
  double NCXTRA = 0.0;
  bool ok = false;		// Reject all interactions by default

  int maxInterFlag = -1;
  double maxP      = 0.0;
  std::array<double, 2> ionExtra {0, 0};
  std::array<double, 2> rcbExtra {0, 0};
				// Record maximum prob interaction
  Interact::pElem maxI1, maxI2;
      
  
  // Run through all interactions in the cross-section map to include
  // ionization-state weightings.  Recall, the map contains values of
  // vrel*sigma*weight/cr.
  //
  double totalXS = 0.0;
  std::vector<size_t> order;
  size_t cnt = 0;
  for (auto I : hCross[id]) {
    order.push_back(cnt++);
    totalXS += I.crs;
  }

  // Randomize interaction order
  //
  std::random_shuffle(order.begin(), order.end());

  // Now, determine energy contribution for each interaction process.
  //
  for (auto O : order) {
    
    auto I             = hCross[id][O];
    int interFlag      = std::get<0>(I.t);
    double XS          = I.crs;
    double Prob        = XS/totalXS;

    if (Prob < 1.0e-14) continue;

    // Compute class id
    //
    size_t cid = 0;
    if (std::get<2>(I.t).first == NTC::Interact::electron and
	std::get<1>(I.t).first != NTC::Interact::electron) cid = 1;

    if (std::get<1>(I.t).first == NTC::Interact::electron and
	std::get<2>(I.t).first != NTC::Interact::electron) cid = 2;

    if (std::get<2>(I.t).first != NTC::Interact::electron and
	std::get<1>(I.t).first != NTC::Interact::electron) cid = 0;

    if (std::get<2>(I.t).first == NTC::Interact::electron and
	std::get<1>(I.t).first == NTC::Interact::electron)
      {
	std::cout << "CRAZY pair: two electrons" << std::endl;
	cid = 0;
      }

    // Logic for selecting allowed interaction types
    //
    if (NoDelC)  {
      ok = true;
				// Pass events that are NOT ionization
				// or recombination, or both
      if (NoDelC & 0x1 and interFlag == recomb) ok = false;
      if (NoDelC & 0x2 and interFlag == ionize) ok = false;

    } else if (scatter) {
				// Only pass elastic scattering events
      if (interFlag < 6) ok = true;

				// Otherwise, pass all events . . .
    } else {
      ok = true;
    }
    
    // Following the selection logic above, do this interaction!
    //
    if (ok) {

      // Energy loss
      //
      double dE = 0.0;

      // Zx is the atomic number (charge), Px is the offset in
      // ionization state from 0, Cx is the _usual_ ionization level
      // (e.g. C=1 is neutral).
      //
      unsigned short Z1 = PP[cid]->getZ(1);
      unsigned short Z2 = PP[cid]->getZ(2);

      speciesKey k1 = PP[cid]->K(1);
      speciesKey k2 = PP[cid]->K(2);

      Interact::pElem I1 = std::get<1>(I.t);
      Interact::pElem I2 = std::get<2>(I.t);
      
      // Index of ionization level (which has offset of 1)
      unsigned short  C1 = I1.second.second; 
      unsigned short  C2 = I2.second.second;

      // Traditional ionization state (e.g. C1=1 is neutral)
      unsigned short  P1 = C1 - 1; 
      unsigned short  P2 = C2 - 1; 
      
      // Temporary debugging
      //
      if (scatter_check) {
	unsigned short ZZ = (I1.first==Interact::electron ? Z2 : Z1);
	if (interFlag == ion_elec) Escat[id][ZZ] += Prob;
	Etotl[id][ZZ] += Prob;
      }
    
      // For hybrid method, the speciesKey level is set to zero.
      // Replace with the correct subspecies value.
      //
      PP[cid]->K(1).second = C1;
      PP[cid]->K(2).second = C2;

      lQ Q1(Z1, C1), Q2(Z2, C2);

      // Retrieve the diagnostic stanza for this species (correctly
      // including the ionization level)
      //
      collTDPtr ctd1 = (*collD)[PP[cid]->K(1)];
      collTDPtr ctd2 = (*collD)[PP[cid]->K(2)];
      
      // Select the maximum probability channel
      //
      if (Prob > maxP) {
	maxInterFlag = interFlag;
	maxP = Prob;
	maxI1 = I1;
	maxI2 = I2;
      }

      // Number interacting atoms
      //
      double N0 = PP[cid]->W2 * TreeDSMC::Munit / amu;

      // Number of real atoms in this interaction
      //
      double NN = N0 * Prob;

      //* BEGIN DEEP DEBUG *//
      if (init_dbg and myid==0 and tnow > init_dbg_time and Z1 == init_dbg_Z and prob < 0.0) {

	std::ofstream out(runtag + ".heplus_test_cross", ios::out | ios::app);

	std::ostringstream sout;
	
	sout << ", [" << Z1 << ", " << Z2 << "] " << O;
	
	out << "Time = " << std::setw(10) << tnow
	    << sout.str()
	    << ", prob = " << std::setw(10) << Prob
	    << ", NN = " << NN
	    << std::endl;
      }

      //-----------------------------
      // Parse each interaction type
      //-----------------------------

      if (interFlag == neut_neut) {
	ctd1->nn[id][0] += Prob;
	ctd1->nn[id][1] += NN;
	
	ctd2->nn[id][0] += Prob;
	ctd2->nn[id][1] += NN;
	
	PE[0][0] += Prob;
      }

      if (interFlag == neut_elec) {

	if (I1.first == Interact::electron) {
	  ctd2->ne[id][0] += Prob;
	  ctd2->ne[id][1] += NN;
	  PE[2][0] += Prob;
	} else {
	  ctd1->ne[id][0] += Prob;
	  ctd1->ne[id][1] += NN;
	  PE[1][0] += Prob;
	}

      }

      if (interFlag == neut_prot) {
	
	if (I1.first == Interact::neutral) {
	  ctd2->np[id][0] += Prob;
	  ctd2->np[id][1] += NN;
	} else {
	  ctd1->np[id][0] += Prob;
	  ctd1->np[id][1] += NN;
	}
	PE[0][0] += Prob;
      }

      if (interFlag == ion_elec) {
	if (I1.first == Interact::electron) {
	  ctd2->ie[id][0] += Prob;
	  ctd2->ie[id][1] += NN;
	  PE[2][0] += Prob;
	} else {
	  ctd1->ie[id][0] += Prob;
	  ctd1->ie[id][1] += NN;
	  PE[1][0] += Prob;
	}

	//* BEGIN DEEP DEBUG *//
	if (init_dbg and myid==0 and tnow > init_dbg_time and Z1 == init_dbg_Z and prob < 0.0) {
	  std::ofstream out(runtag + ".heplus_test_cross", ios::out | ios::app);
	  
	  std::ostringstream sout;
	  
	  sout << ", [" << Z1 << ", " << Z2 << "] " << O;
	  
	  out << "Time = " << std::setw(10) << tnow
	      << sout.str()
	      << ", Prob = " << std::setw(10) << Prob
	      << ", NN = " << NN
	      << ", #1 = " << ctd1->ie[id][0]
	      << ", #2 = " << ctd2->ie[id][0]
	      << std::endl;
	}
      }
      
      if (interFlag == free_free) {

	if (I1.first == Interact::electron) {
	  //
	  // Ion is p2
	  //
	  double tmpE = IS.selectFFInteract(I.CF);

	  if (HWswitch) dE = tmpE * Prob;
	  else          dE = tmpE * Prob/CProb[id][2];

	  if (energy_scale > 0.0) dE *= energy_scale;
	  if (NO_FF_E) dE = 0.0;
	  
	  ctd2->ff[id][0] += Prob;
	  ctd2->ff[id][1] += NN;
	  if (not NOCOOL) ctd2->ff[id][2] += N0 * dE;
	  if (use_spectrum) spectrumAdd(id, interFlag, tmpE, NN);

	  PE[2] += {Prob, dE};

	} else {
	  //
	  // Ion is p1
	  //
	  double tmpE = IS.selectFFInteract(I.CF);

	  if (HWswitch) dE = tmpE * Prob;
	  else          dE = tmpE * Prob/CProb[id][1];

	  if (energy_scale > 0.0) dE *= energy_scale;
	  if (NO_FF_E) dE = 0.0;
	  
	  ctd1->ff[id][0] += Prob;
	  ctd1->ff[id][1] += NN;
	  if (not NOCOOL) ctd1->ff[id][2] += N0 * dE;
	  if (use_spectrum) spectrumAdd(id, interFlag, tmpE, NN);

	  PE[1] += {Prob, dE};
	}

      }

      if (interFlag == colexcite) {

	if (I1.first == Interact::electron) {
	  //
	  // Ion is p2
	  //
	  double tmpE = IS.selectCEInteract(ch.IonList[Q2], I.CE);

	  if (HWswitch) dE = tmpE * Prob;
	  else          dE = tmpE * Prob/CProb[id][2];

	  if (Prob/CProb[id][2] > 1.0 or Prob/CProb[id][2] < 0.0) {
	    checkProb(id);
	    std::cout << "**ERROR: crazy prob <colexcite 2>=" << Prob
		      << ", Pr=" << CProb[id][2] << std::endl;
	  }

	  if (energy_scale > 0.0) dE *= energy_scale;
	    
	  ctd2->CE[id][0] += Prob;
	  ctd2->CE[id][1] += NN;
	  if (not NOCOOL) ctd2->CE[id][2] += N0 * dE;
	  if (use_spectrum) spectrumAdd(id, interFlag, tmpE, NN);

	  PE[2] += {Prob, dE};
	} else {
	  //
	  // Ion is p1
	  //
	  double tmpE = IS.selectCEInteract(ch.IonList[Q1], I.CE);

	  if (HWswitch) dE = tmpE * Prob;
	  else          dE = tmpE * Prob/CProb[id][1];

	  if (Prob/CProb[id][1] > 1.0  or Prob/CProb[id][1] < 0.0) {
	    checkProb(id);
	    std::cout << "**ERROR: crazy prob <colexcite 1>=" << Prob
		      << ", Pr=" << CProb[id][1] << std::endl;
	  }

	  if (energy_scale > 0.0) dE *= energy_scale;
	  
	  ctd1->CE[id][0] += Prob;
	  ctd1->CE[id][1] += NN;
	  if (not NOCOOL) ctd1->CE[id][2] += N0 * dE;
	  if (use_spectrum) spectrumAdd(id, interFlag, tmpE, NN);

	  PE[1] += {Prob, dE};
	}
      }

      if (interFlag == ionize) {

	if (I1.first == Interact::electron) {
	  //
	  // Ion is p2
	  //
	  if (use_normtest) {
	    std::ostringstream sout;
	    sout << "[Before ionize]: C2=" << P2
		 << ", Prob=" << Prob;
	    PP[2]->normTest(1, sout.str());
	    PP[2]->normTest(2, sout.str());
	    if (C2<1 or C2>Z2) {
	      std::cout << "[ionize] bad C2=" << C2
			<< " or C1=" << C1 << std::endl;
	    }
	  }

	  // The scaled-up interaction fraction
	  double Pr = HWswitch ? Prob : Prob/CProb[id][2];

	  if (Pr > 1.0 or Pr < 0.0) {
	    checkProb(id);
	    std::cout << "**ERROR: crazy prob <ionize 2>=" << Prob
		      << ", Pr=" << CProb[id][2] << std::endl;
	  }

	  if (Pr < PP[2]->F(2, P2)) {
	    PP[2]->F(2, P2  ) -= Pr;
	    PP[2]->F(2, P2+1) += Pr;
	  } else {
	    Pr = PP[2]->F(2, P2);
	    PP[2]->F(2, P2  )  = 0.0;
	    PP[2]->F(2, P2+1) += Pr;
	  }

	  if (HWswitch) Prob = Pr;
	  else          Prob = Pr * CProb[id][2];
	  
	  if (use_normtest) {
	    std::ostringstream sout;
	    sout << "[After ionize]: C2=" << C2-1
		 << ", Prob=" << Prob;
	    PP[2]->normTest(1, sout.str());
	    PP[2]->normTest(2, sout.str());
	  }
	  
	  double tmpE = IS.DIInterLoss(ch.IonList[Q2]);
	  dE = tmpE * Pr;

	  // Queue the added electron KE for removal
	  ionExtra[1] += iE2 * Pr;

	  // Energy for ionized electron comes from COM
	  dE += iE2 * Pr * TreeDSMC::Eunit / (N0*eV);

	  if (std::isinf(iE2 * Pr)) {
	    std::cout << "**ERROR: crazy ion energy [2]=" << iE2
		      << ", Pr=" << Pr << std::endl;
	  }

	  if (energy_scale > 0.0) dE *= energy_scale;
	  if (NO_ION_E) dE = 0.0;
	    
	  ctd2->CI[id][0] += Prob;
	  ctd2->CI[id][1] += NN;
	  if (not NOCOOL) ctd2->CI[id][2] += N0 * dE;
	  if (use_spectrum) spectrumAdd(id, interFlag, tmpE, NN);
	    
	  PE[2] += {Prob, dE};
	    
	  if (IonRecombChk) {
	    if (ionCHK[id].find(k2) == ionCHK[id].end()) ionCHK[id][k2] = 0.0;
	    ionCHK[id][k2] += XS * (*cr);
	  }
	  
	} // END: swapped
	else {
	  //
	  // Ion is p1
	  //
	  if (use_normtest) {
	    std::ostringstream sout;
	    sout << "[Before ionize]: C1=" << P1
		 << ", Prob=" << Prob;
	    PP[1]->normTest(1, sout.str());
	    PP[1]->normTest(2, sout.str());
	    if (C1<1 or C1>Z1) {
	      std::cout << "[ionize] bad C1=" << C1
			<< " or C2=" << C2 << std::endl;
	    }
	  }

	  // The scaled-up interaction fraction
	  double Pr = HWswitch ? Prob : Prob/CProb[id][1];

	  if (Pr > 1.0 or Pr < 0.0) {
	    checkProb(id);
	    std::cout << "**ERROR: crazy prob <ionize 1>=" << Prob
		      << ", Pr=" << CProb[id][1] << std::endl;
	  }

	  if (Pr < PP[1]->F(1, P1)) {
	    PP[1]->F(1, P1  ) -= Pr;
	    PP[1]->F(1, P1+1) += Pr;
	  } else {
	    Pr = PP[1]->F(1, P1);
	    PP[1]->F(1, P1  )  = 0.0;
	    PP[1]->F(1, P1+1) += Pr;
	  }

	  if (HWswitch) Prob = Pr;
	  else          Prob = Pr * CProb[id][1];

	  if (use_normtest) {
	    std::ostringstream sout;
	    sout << "[After ionize]: C1=" << C1-1
		 << ", Prob=" << Prob;
	    PP[1]->normTest(1, sout.str());
	    PP[1]->normTest(2, sout.str());
	  }
	    
	  double tmpE = IS.DIInterLoss(ch.IonList[Q1]);
	  dE = tmpE * Pr;
	  
	  // Queue the added electron KE for removal
	  ionExtra[0] += iE1 * Pr;

	  // Energy for ionized electron comes from COM
	  dE += iE1 * Pr * TreeDSMC::Eunit / (N0*eV);

	  if (std::isinf(iE1 * Pr)) {
	    std::cout << "**ERROR: crazy ion energy [1]=" << iE1
		      << ", Pr=" << Pr << std::endl;
	  }

	  if (energy_scale > 0.0) dE *= energy_scale;
	  if (NO_ION_E) dE = 0.0;
	    
	  ctd1->CI[id][0] += Prob;
	  ctd1->CI[id][1] += NN;
	  if (not NOCOOL) ctd1->CI[id][2] += N0 * dE;
	  if (use_spectrum) spectrumAdd(id, interFlag, tmpE, NN);
	  
	  PE[1] += {Prob, dE};
	  
	  if (IonRecombChk) {
	    if (ionCHK[id].find(k1) == ionCHK[id].end()) ionCHK[id][k1] = 0.0;
	    ionCHK[id][k1] += XS * (*cr);
	  }
	}
      }
	
      if (interFlag == recomb) {

	if (I1.first == Interact::electron) {
	  //
	  // Ion is p2
	  //
	  if (use_normtest) {
	    std::ostringstream sout;
	    sout << "[Before recomb]: C2=" << C2
		 << ", Prob=" << Prob << ", w=" << PP[2]->F(2, C2);
	    PP[2]->normTest(1, sout.str());
	    PP[2]->normTest(2, sout.str());
				// Sanity check
	    if (C2<2 or C2>Z2+1) {
	      std::cout << "[recomb] bad C2=" << C2 << std::endl;
	    }
	  }
	  
	  // The scaled-up interaction fraction
	  double Pr = HWswitch ? Prob : Prob/CProb[id][2];

	  if (Pr > 1.0 or Pr < 0.0) {
	    checkProb(id);
	    std::cout << "**ERROR: crazy prob <recomb 2>=" << Prob
		      << ", Pr=" << CProb[id][2] << std::endl;
	  }

	  if (Pr < PP[2]->F(2, P2)) {
	    PP[2]->F(2, P2  ) -= Pr;
	    PP[2]->F(2, P2-1) += Pr;
	  } else {
	    Pr = PP[2]->F(2, P2);
	    PP[2]->F(2, P2  )  = 0.0;
	    PP[2]->F(2, P2-1) += Pr;
	  }
	  
	  if (HWswitch) Prob = Pr;
	  else          Prob = Pr * CProb[id][2];

	  if (use_normtest) {
	    std::ostringstream sout;
	    sout << "[After recomb]: C2=" << C2
		 << ", Prob=" << Prob << ", w=" << PP[2]->F(2, C2);

	    PP[2]->normTest(1, sout.str());
	    PP[2]->normTest(2, sout.str());
	  }
	  
	  // Electron KE lost in recombination is radiated by does not
	  // change COM energy, but it reduces the KE in the free pool
	  //
	  rcbExtra[1] += iE2 * Pr;

	  // Electron KE radiated in recombination
	  double eE = iE2 * Pr * TreeDSMC::Eunit / (N0*eV);

	  if (RECOMB_IP) dE += ch.IonList[lQ(Z2, C2)]->ip * Pr;
	  if (energy_scale > 0.0) dE *= energy_scale;

	  ctd2->RR[id][0] += Prob;
	  ctd2->RR[id][1] += NN;
	  // if (not NOCOOL) ctd2->RR[id][2] += N0 * dE;
	  if (use_spectrum) spectrumAdd(id, interFlag, eE, NN);

	  PE[2] += {Prob, dE};
	  
	  // Add the KE from the recombined electron back to the free pool
	  //
	  if (NOCOOL and !NOCOOL_ELEC and C2==1 and use_cons>=0) {
	    double lKE = 0.0, fE = 0.5*PP[cid]->W1*atomic_weights[0];
	    for (size_t k=0; k<3; k++) {
	      double t = p2->dattrib[use_elec+k];
	      lKE += fE*t*t;
	    }
	    lKE *= Pr;
	    
	    NCXTRA += lKE;
	    
	    if (PP[cid]->q<1)
	      p2->dattrib[use_cons] += lKE;
	    else {
	      p1->dattrib[use_cons] += lKE * 0.5;
	      p2->dattrib[use_cons] += lKE * 0.5;
	    }
	  }
	  
	  if (IonRecombChk) {
	    if (recombCHK[id].find(k2) == recombCHK[id].end()) recombCHK[id][k2] = 0.0;
	    recombCHK[id][k2] += XS * (*cr);
	  }
	  
	} // END: swapped
	else {
	  //
	  // Ion is p1
	  //
	  if (use_normtest) {
	    std::ostringstream sout;
	    sout << "[Before recomb]: C1=" << C1
		 << ", Prob=" << Prob << ", w=" << PP[1]->F(1, C1);
	    PP[1]->normTest(1, sout.str());
	    PP[1]->normTest(2, sout.str());
	    if (C1<2 or C1>Z1+1) {
	      std::cout << "[recomb] bad C1=" << C1 << std::endl;
	    }
	  }

	  // The scaled-up interaction fraction
	  double Pr = HWswitch ? Prob : Prob/CProb[id][1];

	  if (Pr > 1.0 or Pr < 0.0) {
	    checkProb(id);
	    std::cout << "**ERROR: crazy prob <recomb 1>=" << Prob
		      << ", Pr=" << CProb[id][1] << std::endl;
	  }

	  if (Pr < PP[1]->F(1, P1)) {
	    PP[1]->F(1, P1  ) -= Pr;
	    PP[1]->F(1, P1-1) += Pr;
	  } else {
	    Pr = PP[1]->F(1, P1);
	    PP[1]->F(1, P1  )  = 0.0;
	    PP[1]->F(1, P1-1) += Pr;
	  }
	  
	  if (HWswitch) Prob = Pr;
	  else          Prob = Pr * CProb[id][1];

	  if (use_normtest) {
	    std::ostringstream sout;
	    sout << "[After recomb_1]: C1=" << C1
		 << ", Prob=" << Prob << ", w=" << PP[1]->F(1, P1);
	    PP[1]->normTest(1, sout.str());
	    PP[1]->normTest(2, sout.str());
	  }

	  // Electron KE lost in recombination is radiated by does not
	  // change COM energy, but it reduces the KE in the free pool
	  //
	  rcbExtra[0] += iE1 * Pr;

	  // Electron KE fraction in recombination
	  //
	  double eE = 0.0;
	  eE = iE1 * Pr * TreeDSMC::Eunit / (N0*eV);

	  if (RECOMB_IP) dE += ch.IonList[lQ(Z1, C1)]->ip * Pr;
	  if (energy_scale > 0.0) dE *= energy_scale;

	  ctd1->RR[id][0] += Prob;
	  ctd1->RR[id][1] += NN;
	  // if (not NOCOOL) ctd1->RR[id][2] += NN * kEe1[id];
	  if (use_spectrum) spectrumAdd(id, interFlag, eE, NN);

	  PE[1] += {Prob, dE};

	  // Add the KE from the recombined electron back to the free pool
	  //
	  if (NOCOOL and !NOCOOL_ELEC and C1==1 and use_cons>=0) {
	    double lKE = 0.0, fE = 0.5*PP[cid]->W1*atomic_weights[0];
	    for (size_t k=0; k<3; k++) {
	      double t = p1->dattrib[use_elec+k];
	      lKE += fE*t*t;
	    }
	    lKE *= Pr;
	    
	    NCXTRA += lKE;

	    if (PP[1]->q<1)
	      p1->dattrib[use_cons] += lKE;
	    else {
	      p1->dattrib[use_cons] += lKE * 0.5;
	      p2->dattrib[use_cons] += lKE * 0.5;
	    }
	  }

	  if (IonRecombChk) {
	    if (recombCHK[id].find(k1) == recombCHK[id].end()) recombCHK[id][k1] = 0.0;
	    recombCHK[id][k1] += XS * (*cr);
	  }
	}
	
      }

      // -----------------
      // ENERGY DIAGNOSTIC
      // -----------------
      
      if (PP[cid]->swap) {
	bool prior = std::isnan(ctd2->eV_av[id]);
	ctd2->eV_av[id] += kEe2[id] * Prob;
	if (std::isnan(ctd2->eV_av[id])) {
	  std::cout << "NAN eV_N[2]=" << ctd2->eV_N[id]
		    << ", prior=" << std::boolalpha << prior << std::endl;
	}
	ctd2->eV_N[id] += Prob;
	ctd2->eV_min[id] = std::min(ctd2->eV_min[id], kEe2[id]);
	ctd2->eV_max[id] = std::max(ctd2->eV_max[id], kEe2[id]);
	
	if (kEe2[id] > 10.2) { ctd2->eV_10[id] += Prob; }
	
      } else {
	
	bool prior = std::isnan(ctd1->eV_av[id]);
	ctd1->eV_av[id] += kEe1[id] * Prob;
	if (std::isnan(ctd1->eV_av[id])) {
	  std::cout << "NAN eV_N[1]=" << ctd1->eV_N[id]
		    << ", prior=" << std::boolalpha << prior << std::endl;
	}
	ctd1->eV_N[id] += Prob;
	ctd1->eV_min[id] = std::min(ctd1->eV_min[id], kEe1[id]);
	ctd1->eV_max[id] = std::max(ctd1->eV_max[id], kEe1[id]);
	
	if (kEe1[id] > 10.2) { ctd1->eV_10[id] += Prob;}
      }

      // Convert from eV per particle to system units per
      // superparticle
      //
      double Escl = N0 * eV/TreeDSMC::Eunit;

      if (PE[1][0]>0.0) {
	ctd1->dv[id][0] += Prob;
	ctd1->dv[id][1] += PP[1]->W2 * Prob;
	if (not NOCOOL) ctd1->dv[id][2] += PP[1]->W2 * PE[1][1];
	if (scatter_check) {
	  double m1 = atomic_weights[Z1];
	  double m2 = atomic_weights[ 0];
	  double mu = m1 * m2 / (m1 + m2);
	  double tK = 0.0;
	  for (size_t k=0; k<3; k++) {
	    double vr = p2->dattrib[use_elec + k] - p1->vel[k];
	    tK += vr * vr;
	  }
	  tK *= 0.5*PP[1]->W1*PP[1]->q*mu;

	  Italy[id][Z1*100+Z2][interFlag][0] += Prob;
	  Italy[id][Z1*100+Z2][interFlag][1] += dE * Escl;
	  Italy[id][Z1*100+Z2][interFlag][2] += tK;
	}
      }
      
      if (PE[0][0]>0.0) {
	if (scatter_check) {
	  double m1 = atomic_weights[Z1];
	  double m2 = atomic_weights[Z2];
	  double mu = m1 * m2 / (m1 + m2);
	  double tK = 0.0;
	  for (size_t k=0; k<3; k++) {
	    double vr = p2->vel[k] - p1->vel[k];
	    tK += vr * vr;
	  }
	  tK *= 0.5*PP[0]->W1*PP[0]->q*mu;

	  Italy[id][Z1*100+Z2][interFlag][0] += Prob;
	  Italy[id][Z1*100+Z2][interFlag][1] += dE * Escl;
	  Italy[id][Z1*100+Z2][interFlag][2] += tK;
	}
      }
      
      if (PE[2][0]>0.0) {
	ctd2->dv[id][0] += Prob;
	ctd2->dv[id][1] += PP[2]->W2 * Prob;
	if (not NOCOOL) ctd2->dv[id][2] += PP[2]->W2 * PE[2][1];
	if (scatter_check) {
	  double m1 = atomic_weights[0];
	  double m2 = atomic_weights[Z2];
	  double mu = m1 * m2 / (m1 + m2);
	  double tK = 0.0;
	  for (size_t k=0; k<3; k++) {
	    double vr = p1->dattrib[use_elec + k] - p2->vel[k];
	    tK += vr * vr;
	  }
	  tK *= 0.5*PP[2]->W1*PP[2]->q*mu;

	  Italy[id][Z1*100+Z2][interFlag][0] += Prob;
	  Italy[id][Z1*100+Z2][interFlag][1] += dE * Escl;
	  Italy[id][Z1*100+Z2][interFlag][2] += tK;
	}
      }
      
    } // END: compute this interaction [ok]

  } // END: interaction loop [hCross]

  // Deep debug
  //
  if (false) {
    std::cout << std::string(40, '-') << std::endl;
    for (auto I : hCross[id]) {
      int interFlag = std::get<0>(I.t);
      double XS     = I.crs;
      double Prob   = XS/totalXS;
      std::cout << std::setw(20) << std::left << interLabels[interFlag]
		<< std::setw(20) << std::left << Prob
		<< std::endl;
    }
    std::cout << std::string(40, '-') << std::endl;
  }

  // Convert to super particle (current in eV)
  //
  for (size_t cid=0; cid<3; cid++) {
    double N0 = PP[cid]->W2 * TreeDSMC::Munit / amu;
    PE[cid][1] *= N0;
  }

  // Debugging test
  //
  if (SAME_IONS_SCAT and Z1 != Z2) return ret;

  // Work vectors
  //
  std::vector<double> vrel(3), vcom(3), v1(3), v2(3);

  // Artifically prevent cooling by setting the energy removed from
  // the COM frame to zero
  //
  if (NOCOOL) {
    double Encl = 0.0;
    for (auto & v : PE) {
      Encl += v[1];
      v[1] = 0.0;
    }
    collD->addNoCool(Encl, id);
  }

  // Convert energy loss from eV to system units
  //
  for (auto & v : PE) v[1] *= eV / TreeDSMC::Eunit;

  // Normalize probabilities and sum inelastic energy changes
  //
  double probTot = 0.0;
  for (auto & v : PE) probTot += v[0];

  if (probTot > 0.0) {
    for (auto & v : PE) v[0] /= probTot;
  }

  //
  // Perform energy adjustment in ion, system COM frame with system
  // mass units
  //

  // Divide everything into three cases:
  // Ion(1)     and Electron(2)
  // Ion(2)     and Electron(1)
  // Neutral(1) and Neutral(2)

  if (use_normtest) {
    normTest(p1, "p1 [Before update]");
    normTest(p2, "p2 [Before update]");
  }

  unsigned short Jsav = 255;
  KE_ KE;
  double PPsav1 = 0.0, PPsav2 = 0.0;

  //
  // Perform the electronic interactions
  //
  if (use_elec) {
    //
    // Select interaction
    //
    double Pr = (*unit)();
    unsigned short J = 2;
    if (NO_ION_ION) {
      double tst = PE[1][0]/(PE[1][0]+PE[2][0]);
      if (Pr < tst) J = 1;
    } else if (NO_ION_ELECTRON) {
      J = 0;
    } else {
      if      (Pr < PE[0][0]) J = 0;
      else if (Pr < PE[1][0]) J = 1;
    }
    Jsav = J;

    // Parse for Coulombic interaction
    //
    if (maxInterFlag==ion_elec or maxInterFlag==ion_ion) {
    
      KE.Coulombic = true;

      double m1  = molP1[id]*amu;
      double m2  = molP2[id]*amu;
      double me  = atomic_weights[0]*amu;
  
      double mu0 = m1 * m2 / (m1 + m2);
      double mu1 = m1 * me / (m1 + me);
      double mu2 = me * m2 / (me + m2);

      double dT  = spTau[id] * TreeDSMC::Tunit;

      if (maxInterFlag == ion_elec) {
	if (maxI1.first == Interact::electron) {
	  double pVel = sqrt(2.0 * kEe2[id] * eV / mu2);
	  double afac = esu*esu/std::max<double>(2.0*kEe2[id]*eV, FloorEv*eV);
	  KE.Tau = ABrate[id][2]*afac*afac*pVel * dT;
	} else {
	  double pVel = sqrt(2.0 * kEe1[id] * eV / mu1);
	  double afac = esu*esu/std::max<double>(2.0*kEe1[id]*eV, FloorEv*eV);
	  KE.Tau = ABrate[id][1]*afac*afac*pVel * dT;
	}
      } else {
	  double pVel = sqrt(2.0 * kEi[id] * eV / mu0);
	  double afac = esu*esu/std::max<double>(2.0*kEi[id]*eV, FloorEv*eV);
	  KE.Tau = ABrate[id][0]*afac*afac*pVel * dT;
      }
    }

    //
    // Apply neutral-neutral scattering and energy loss
    //
    if (J==0 or HWswitch) {

      PP[0]->update();
      PP[0]->eUpdate();

      if (not PP[0]->wght) KEcheck(PP[0], KE_initl_check, ionExtra, rcbExtra);

      std::pair<double, double> KEinit = energyInPairPartial(p1, p2, Neutral);

      if (DBG_NewTest) {
	if (PP[0]->swap) {
	  std::cout << std::setprecision(14) << std::scientific
		    << "Ion-Ion [0]: W1=" << std::setw(22) << PP[0]->W2
		    << " W2=" << std::setw(22) << PP[0]->W1 << std::endl
		    << std::setprecision(5);
	} else {
	  std::cout << std::setprecision(14) << std::scientific
		    << "Ion-Ion [0]: W1=" << std::setw(22) << PP[0]->W1
		    << " W2=" << std::setw(22) << PP[0]->W2 << std::endl
		    << std::setprecision(5);
	}
      }
      
      double ke1 = 0.0, ke2 = 0.0;

      for (int k=0; k<3; k++) {
				// Both particles are neutrals or ions
	v1[k]  = p1->vel[k];
	v2[k]  = p2->vel[k];

	ke1   += v1[k] * v1[k];
	ke2   += v2[k] * v2[k];
      }

      // Only do interaction if both particles have pos ke (i.e. they
      // are moving)
      //

      PE[0][2] = PE[0][1];

      if (ke1 > 0.0 and ke2 > 0.0) {

	if (Z1 == Z2 or ALWAYS_APPLY) {
	  double DE1 = PE[0][0] * p1->dattrib[use_cons];
	  double DE2 = PE[0][0] * p2->dattrib[use_cons];
	  p1->dattrib[use_cons] -= DE1;
	  p2->dattrib[use_cons] -= DE2;
	  clrE[id] -= DE1 + DE2;
	  PE[0][2] += DE1 + DE2;
	} else {
	  if (W1 < W2) {
	    p2->dattrib[use_cons] += PE[0][1];
	    PPsav2 += PE[0][1];
	  } else {
	    p1->dattrib[use_cons] += PE[0][1];
	    PPsav1 += PE[0][1];
	  }

	  PE[0][2] = 0.0;
	}

	KE.delE0 = PE[0][1];
	KE.delE  = PE[0][2];

	collD->addLost(KE.delE0, 0.0, id);
	if (use_delt>=0) {
	  spEdel[id] += KE.delE; // HYBRID
	  if (KE.delE>0.0)
	    spEmax[id]  = std::min<double>(spEmax[id], KE.totE/KE.delE);
	}

	if (PP[0]->wght)
	  scatterHybrid(PE[0][0], PP[0], KE, &v1, &v2, id);
	else
	  scatterHybrid(1, PP[0], KE, &v1, &v2, id);

	checkEnergyHybrid(PP[0], KE, &v1, &v2, Neutral, id);

	dfrE[id] += KE.defer;	// HYBRID

	if (KE_DEBUG) testCnt[id]++;

	if (scatter_check and maxInterFlag>=0) {
	  TotlU[id][Z1*100+Z2][0]++;
	}

	for (int k=0; k<3; k++) {
	  // Particle 1 is ion
	  p1->vel[k] = v1[k];
	  // Particle 2 is ion
	  p2->vel[k] = v2[k];
	}
      }

      if (not NoExact) updateEnergyHybrid(PP[0], KE);

      testKE[id][3] += PE[0][1];
      testKE[id][4] += PE[0][1];

      if (KE_DEBUG and not NoExact) {

	double KE_final_check = energyInPair(p1, p2);

	std::pair<double, double> KEfinal;
	if (DBG_NewTest) 
	  KEfinal = energyInPairPartial(p1, p2, Neutral, "After neutral");
	else
	  KEfinal = energyInPairPartial(p1, p2, Neutral);

	double delE = KE_initl_check - KE_final_check
	  - (deltaSum += KE.delta)
	  - (delEsum  += KE.delE)
	  + (delEdfr  += KE.defer);

	delEloss += KE.delE0;

	std::pair<double, double> KEdif = KEinit - KEfinal;

	if (fabs(delE) > tolE*KE_initl_check) {
	  std::cout << "**ERROR [after neutral] dE = " << delE
		    << ", rel = "  << delE/KE_initl_check
		    << ", dKE = "  << deltaSum
		    << ", actR = " << (KEdif.first - KE.delta)/KEinit.first
		    << ", pasR = " << KEdif.second/KEinit.second
		    << ", actA = " << KEdif.first
		    << ", pasA = " << KEdif.second
		    << std::endl;
	} else {
	  if (DBG_NewTest)
	    std::cout << "**GOOD [after neutral] dE = " << delE
		      << ", rel = " << delE/KE_initl_check
		      << ", dKE = " << KE.delta
		      << ", actR = " << (KEdif.first- KE.delta)/KEinit.first
		      << ", pasR = " << KEdif.second/KEinit.second
		      << ", actA = " << KEdif.first
		      << ", pasA = " << KEdif.second
		      << std::endl;
	}
      }

    }
    // END: NeutFrac>0

    //
    // Apply ion/neutral-electron scattering and energy loss
    // Ion is Particle 1, Electron is Particle 2
    //
    if (J==1 or HWswitch) {

      PP[1]->update();
      PP[1]->eUpdate();

      if (not PP[1]->wght) KEcheck(PP[1], KE_initl_check, ionExtra, rcbExtra);

      std::pair<double, double> KEinit = energyInPairPartial(p1, p2, Ion1);

      if (DBG_NewTest) {
	if (PP[1]->swap)
	  std::cout << "Ion-Electron [0]:" << std::setprecision(14)
		    << " W1=" << std::setw(22) << PP[1]->W2
		    << " W2=" << std::setw(22) << PP[1]->W1 << std::endl;
	else
	  std::cout << "Ion-Electron [0]:" << std::setprecision(14)
		    << " W1=" << std::setw(22) << PP[1]->W1
		    << " W2=" << std::setw(22) << PP[1]->W2 << std::endl;
      }

      double ke1i = 0.0, ke2i = 0.0;

      for (int k=0; k<3; k++) { // Particle 1 is the ion
	  v1[k]  = p1->vel[k];
				// Particle 2 is the electron
	  v2[k]  = p2->dattrib[use_elec+k];

	  ke1i  += v1[k] * v1[k];
	  ke2i  += v2[k] * v2[k];
      }
      
      PE[1][2] = PE[1][1];

      if (ke1i > 0.0 and ke2i > 0.0) {
	
	if (Z1 == Z2 or ALWAYS_APPLY) {
	  double DE1 = PE[1][0] * p1->dattrib[use_cons];
	  double DE2 = 0.0;
	  p1->dattrib[use_cons] -= DE1;
	  if (elc_cons) {
	    DE2 = PE[1][0] * p2->dattrib[use_elec+3];
	    p2->dattrib[use_elec+3] -= DE2;
	  } else {
	    DE2 = PE[1][0] * p2->dattrib[use_cons];
	    p2->dattrib[use_cons  ] -= DE2;
	  }
	  clrE[id] -= DE1 + DE2;
	  PE[1][2] += DE1 + DE2;
	} else {
	  if (W1 < W2) {
	    p2->dattrib[use_cons] += PE[1][1];
	    PPsav2 += PE[1][1];
	  } else {
	    p1->dattrib[use_cons] += PE[1][1];
	    PPsav1 += PE[1][1];
	  }
	  
	  PE[1][2] = 0.0;
	}
	
	KE.delE0 = PE[1][1];
	KE.delE  = PE[1][2];

	collD->addLost(KE.delE0, rcbExtra[0] - ionExtra[0], id);
	if (use_delt>=0) {
	  spEdel[id] += KE.delE; // HYBRID
	  if (KE.delE>0.0)
	    spEmax[id]  = std::min<double>(spEmax[id], KE.totE/KE.delE);
	}
	
	if (PP[1]->wght)
	  scatterHybrid(PE[1][0], PP[1], KE, &v1, &v2, id);
	else
	  scatterHybrid(1, PP[1], KE, &v1, &v2, id);

	checkEnergyHybrid(PP[1], KE, &v1, &v2, Ion1, id);

	dfrE[id] += KE.defer;	// HYBRID

	if (KE_DEBUG) testCnt[id]++;
	
	if (scatter_check and maxInterFlag>=0) {
	  TotlU[id][Z1*100+Z2][1]++;
	}
	
	for (int k=0; k<3; k++) {
	  // Particle 1 is the ion
	  p1->vel[k] = v1[k];
	  // Particle 2 is the elctron
	  p2->dattrib[use_elec+k] = v2[k];
	}
	
	if (not NoExact) updateEnergyHybrid(PP[1], KE);

	testKE[id][3] += PE[1][1] - ionExtra[0] + rcbExtra[0];
	testKE[id][4] += PE[1][1];

	if (KE_DEBUG and not NoExact) {

	  double ke1f = 0.0, ke2f = 0.0;

	  for (int k=0; k<3; k++) {
	    ke1f += v1[k] * v1[k];
	    ke2f += v2[k] * v2[k];
	  }
	  
	  // Particle 2 electron
	  // -------------------
	  //            initial/orig----------------------------------+
	  //            initial/swap--------------+                   |
	  //                                      |                   |
	  //                                      v                   v
	  double eta2i = PP[1]->swap ? PP[1]->beg[0].eta : PP[1]->beg[1].eta;
	  double eta2f = PP[1]->swap ? PP[1]->end[0].eta : PP[1]->end[1].eta;
	  //                                      ^                   ^
	  //                                      |                   |
	  //            final/swap----------------+                   |
	  //            final/orig------------------------------------+

	  ke1i *= 0.5*p1->mass;
	  ke1f *= 0.5*p1->mass;

	  ke2i *= 0.5*p2->mass * eta2i * atomic_weights[0]/atomic_weights[Z2];
	  ke2f *= 0.5*p2->mass * eta2f * atomic_weights[0]/atomic_weights[Z2];

	  double delE = ke1i + ke2i - ke1f - ke2f - KE.delta - KE.delE + KE.defer;
	  if (fabs(delE) > tolE*(ke1i + ke2i)) {
	    std::cout << "**ERROR post scatter: relE = " << delE/(ke1i + ke2i)
		      << " del = "  << delE
		      << " dKE = "  << KE.delta
		      << " delE = " << KE.delE
		      << " miss = " << KE.miss
		      << " dfr = "  << KE.defer
		      << " KEi = "  << ke1i + ke2i
		      << " KEf = "  << ke1f + ke2f
		      << " et2i = " << eta2i << std::endl;
	  } else {
	    if (DBG_NewTest)
	      std::cout << "**GOOD post scatter: relE = " << delE/(ke1i + ke2i)
			<< std::scientific << std::setprecision(14)
			<< " del = "  << std::setw(14) << delE
			<< std::endl << std::setprecision(5);
	  }
	}
      }

      if (KE_DEBUG and not NoExact) {
	double KE_final_check = energyInPair(p1, p2);

	std::pair<double, double> KEfinal;
	if (DBG_NewTest) 
	  KEfinal = energyInPairPartial(p1, p2, Ion1, "After Ion1");
	else
	  KEfinal = energyInPairPartial(p1, p2, Ion1);

	double delE = KE_initl_check - KE_final_check
	  - (deltaSum += KE.delta)
	  - (delEsum  += KE.delE - ionExtra[0] + rcbExtra[0])
	  + (delEdfr  += KE.defer);

	delEfnl  += rcbExtra[0] - ionExtra[0];
	delEloss += KE.delE0;

	std::pair<double, double> KEdif = KEinit - KEfinal;

	if (fabs(delE) > tolE*KE_initl_check) {
	  std::cout << "**ERROR [after Ion1] dE = " << delE
		    << ", rel = "  << delE/KE_initl_check
		    << ", dKE = "  << deltaSum
		    << ", actR = " << (KEdif.first - KE.delta)/KEinit.first
		    << ", pasR = " << KEdif.second/KEinit.second
		    << ", actA = " << KEdif.first
		    << ", pasA = " << KEdif.second
		    << std::endl;
	} else {
	  if (DBG_NewTest)
	    std::cout << "**GOOD [after Ion1] dE = " << delE
		      << ", rel = "  << delE/KE_initl_check
		      << ", dKE = "  << deltaSum
		      << ", actR = " << (KEdif.first - KE.delta)/KEinit.first
		      << ", pasR = " << KEdif.second/KEinit.second
		      << ", actA = " << KEdif.first
		      << ", pasA = " << KEdif.second
		      << std::endl;
	}
      }

      // Secondary electron-ion scattering
      //
      for (unsigned n=0; n<SECONDARY_SCATTER; n++) secondaryScatter(p1);
      
    } // END: PE[1]

    //
    // Apply ion/neutral-electron scattering and energy loss
    // Ion is Particle 2, Electron is Particle 1
    //
    if (J==2 or HWswitch) {

      PP[2]->update();
      PP[2]->eUpdate();

      if (not PP[2]->wght) KEcheck(PP[2], KE_initl_check, ionExtra, rcbExtra);

      if (DBG_NewTest) {
	if (PP[2]->swap)
	  std::cout << std::setprecision(14) << std::scientific
		    << "Electron-Ion [0]: W1=" << std::setw(18) << PP[2]->W2
		    << " W2=" << std::setw(18) << PP[2]->W1 << std::endl
		    << std::setprecision(5);
	else
	  std::cout << std::setprecision(14) << std::scientific
		    << "Electron-Ion [0]: W1=" << std::setw(18) << PP[2]->W1
		    << " W2=" << std::setw(18) << PP[2]->W2 << std::endl
		    << std::setprecision(5);
      }

      std::pair<double, double> KEinit = energyInPairPartial(p1, p2, Ion2);

      double ke1i = 0.0, ke2i = 0.0;
      for (int k=0; k<3; k++) {	// Particle 1 is the elctron
	v1[k]  = p1->dattrib[use_elec+k];
				// Particle 2 is the ion
	v2[k]  = p2->vel[k];
	
	ke1i  += v1[k] * v1[k];
	ke2i  += v2[k] * v2[k];
      }

      PE[2][2] = PE[2][1];

      if (ke1i > 0.0 and ke2i > 0.0) {
	
	if (Z1 == Z2 or ALWAYS_APPLY) {
	  double DE1 = 0.0;
	  double DE2 = PE[2][0] * p2->dattrib[use_cons];
	  if (elc_cons) {
	    DE1 = PE[2][0] * p1->dattrib[use_elec+3];
	    p1->dattrib[use_elec+3] -= DE1;
	  } else {
	    DE1 = PE[2][0] * p1->dattrib[use_cons];
	    p1->dattrib[use_cons  ] -= DE1;
	  }
	  p2->dattrib[use_cons]   -= DE2;
	  clrE[id] -= DE1 + DE2;
	  PE[2][2] += DE1 + DE2;
	} else {
	  if (W1 < W2) {
	    p2->dattrib[use_cons] += PE[2][1];
	    PPsav2 += PE[2][1];
	  } else {
	    p1->dattrib[use_cons] += PE[2][1];
	    PPsav1 += PE[2][1];
	  }
	  
	  PE[2][2] = 0.0;
	}
	
	KE.delE0 = PE[2][1];
	KE.delE  = PE[2][2];

	collD->addLost(KE.delE0, rcbExtra[1] - ionExtra[1], id);
	if (use_delt>=0) {
	  spEdel[id] += KE.delE; // HYBRID
	  if (KE.delE>0.0)
	    spEmax[id]  = std::min<double>(spEmax[id], KE.totE/KE.delE);
	}
	
	if (PP[2]->wght)
	  scatterHybrid(PE[2][0], PP[2], KE, &v1, &v2, id);
	else
	  scatterHybrid(1, PP[2], KE, &v1, &v2, id);

	checkEnergyHybrid(PP[2], KE, &v1, &v2, Ion2, id);

	dfrE[id] += KE.defer;	// HYBRID
	
	if (KE_DEBUG) testCnt[id]++;
	
	if (scatter_check and maxInterFlag>=0) {
	  TotlU[id][Z1*100+Z2][2]++;
	}
	
	for (int k=0; k<3; k++) {
				// Particle 1 is the electron
	  p1->dattrib[use_elec+k] = v1[k];
				// Particle 2 is the ion
	  p2->vel[k] = v2[k];
	}
      
	if (not NoExact) updateEnergyHybrid(PP[2], KE);

	testKE[id][3] += PE[2][1] - ionExtra[1] + rcbExtra[1];
	testKE[id][4] += PE[2][1];

	if (KE_DEBUG and not NoExact) {

	  double ke1f = 0.0, ke2f =0.0;
	  for (int k=0; k<3; k++) {
	    ke1f += v1[k] * v1[k];
	    ke2f += v2[k] * v2[k];
	  }
	  
	  double ke1F = ke1f, ke2F = ke2f;

	  // Particle 1 electron
	  // -------------------
	  //            initial/orig----------------------------------+
	  //            initial/swap--------------+                   |
	  //                                      |                   |
	  //                                      v                   v
	  double eta1i = PP[2]->swap ? PP[2]->beg[1].eta : PP[2]->beg[0].eta;
	  double eta1f = PP[2]->swap ? PP[2]->end[1].eta : PP[2]->end[0].eta;
	  //                                      ^                   ^
	  //                                      |                   |
	  // Particle 1/final/swap----------------+                   |
	  // Particle 1/final/orig------------------------------------+


	  ke1i *= 0.5*p1->mass * eta1i * atomic_weights[0]/atomic_weights[Z1];
	  ke1f *= 0.5*p1->mass * eta1f * atomic_weights[0]/atomic_weights[Z1];

	  ke2i *= 0.5*p2->mass;
	  ke2f *= 0.5*p2->mass;

	  double delE = ke1i + ke2i - ke1f - ke2f - KE.delta - KE.delE + KE.defer;

	  if (fabs(delE) > tolE*(ke1i + ke2i)) {
	    std::cout << "**ERROR post scatter: relE = " << delE/(ke1i+ke2i)
		      << " del = "  << delE
		      << " dKE = "  << KE.delta
		      << " dfr = "  << KE.defer
		      << " KEi = "  << ke1i + ke2i
		      << " KEf = "  << ke1f + ke2f
		      << " k1f = "  << ke1F
		      << " k2f = "  << ke2F
		      << " et1i = " << eta1i << std::endl;
	  }
	}
      }

      if (KE_DEBUG and not NoExact) {
	double KE_final_check = energyInPair(p1, p2);

	std::pair<double, double> KEfinal;
	if (DBG_NewTest) 
	  KEfinal = energyInPairPartial(p1, p2, Ion2, "After Ion2");
	else
	  KEfinal = energyInPairPartial(p1, p2, Ion2);

	double delE = KE_initl_check - KE_final_check
	  - (deltaSum += KE.delta)
	  - (delEsum  += KE.delE - ionExtra[1] + rcbExtra[1])
	  + (delEdfr  += KE.defer);

	delEfnl  += rcbExtra[1] - ionExtra[1];
	delEloss += KE.delE0;

	std::pair<double, double> KEdif = KEinit - KEfinal;

	double actR = KEdif.first - KE.delta;
	actR = KEinit.first > 0.0 ? actR/KEinit.first : actR;

	double pasR = KEdif.second;
	pasR = KEinit.second > 0.0 ? pasR/KEinit.second : pasR;

	if (fabs(delE) > tolE*KE_initl_check) {
	  std::cout << "**ERROR [after Ion2] dE = " << delE
		    << ", rel = "  << delE/KE_initl_check
		    << ", dKE = "  << deltaSum
		    << ", actR = " << actR
		    << ", pasR = " << pasR
		    << ", actA = " << KEdif.first
		    << ", pasA = " << KEdif.second
		    << std::endl;
	} else {
	  if (DBG_NewTest)
	    std::cout << "**GOOD [after Ion2] dE = " << delE
		      << ", rel = "  << delE/KE_initl_check
		      << ", dKE = "  << deltaSum
		      << ", actR = " << actR
		      << ", pasR = " << pasR
		      << ", actA = " << KEdif.first
		      << ", pasA = " << KEdif.second
		      << std::endl;
	}
      }

      // Secondary electron-ion scattering
      //
      for (unsigned n=0; n<SECONDARY_SCATTER; n++) secondaryScatter(p2);

    }

    // Scatter tally for debugging
    //
    if (scatter_check) {
      for (size_t k=0; k<3; k++) TotlD[id][Z1*100+Z2][k] += PE[k][0];
    }

  } // END: interactions with atoms AND electrons


  // Update energy conservation
  //
  double EconsUpI = 0.0, EconsUpE = 0.0;
  for (size_t k=0; k<3; k++) {
    if (use_cons>=0) {
      PP[k]->p1->dattrib[use_cons] += PP[k]->E1[0];
      PP[k]->p2->dattrib[use_cons] += PP[k]->E2[0];
      EconsUpI += PP[k]->E1[0] + PP[k]->E2[0];
    }
    if (use_elec>=0 and elc_cons) {
      PP[k]->p1->dattrib[use_elec+3] += PP[k]->E1[1];
      PP[k]->p2->dattrib[use_elec+3] += PP[k]->E2[1];
      EconsUpE += PP[k]->E1[1] + PP[k]->E2[1];
    } else if (use_cons>=0) {
      PP[k]->p1->dattrib[use_cons  ] += PP[k]->E1[1];
      PP[k]->p2->dattrib[use_cons  ] += PP[k]->E2[1];
      EconsUpE += PP[k]->E1[1] + PP[k]->E2[1];
    }
  }

  if (scatter_check) {
    if (PE[0][0]>0.0) {
      double vi = 0.0;
      for (size_t k=0; k<3; k++) {
	double vrel = p1->vel[k] - p2->vel[k];
	vi += vrel* vrel;
      }

      double m1 = atomic_weights[Z1];
      double m2 = atomic_weights[Z2];
      double mu = m1 * m2/(m1 + m2);
      double kE = 0.5*W1*PP[0]->q*mu*vi;

      Ediag[id][0] += PE[0][2];
      Ediag[id][1] += kE * PE[0][0];
      Ediag[id][2] += PE[0][0];
    }

    if (PE[1][0]>0.0) {
      double vi = 0.0;
      for (size_t k=0; k<3; k++) {
	double vrel = p1->vel[k] - p2->dattrib[use_elec+k];
	vi += vrel* vrel;
      }

      double m1 = atomic_weights[Z1];
      double m2 = atomic_weights[ 0];
      double mu = m1 * m2/(m1 + m2);
      double kE = 0.5*W1*PP[1]->q*mu*vi;

      Ediag[id][0] += PE[1][2];
      Ediag[id][1] += kE * PE[1][0];
      Ediag[id][2] += PE[1][0];
    }

    if (PE[2][0]>0.0) {
      double vi = 0.0;
      for (size_t k=0; k<3; k++) {
	double vrel = p2->vel[k] - p1->dattrib[use_elec+k];
	vi += vrel* vrel;
      }

      double m1 = atomic_weights[ 0];
      double m2 = atomic_weights[Z2];
      double mu = m1 * m2/(m1 + m2);
      double kE = 0.5*W1*PP[2]->q*mu*vi;

      Ediag[id][0] += PE[2][2];
      Ediag[id][1] += kE * PE[2][0];
      Ediag[id][2] += PE[2][0];
    }

  }

  if (use_normtest) {
    normTest(p1, "p1 [After]");
    normTest(p2, "p2 [After]");
  }

  // Debug energy conservation
  // -------------------------
  // After the step, the energy of a single pair may have changed in
  // the following ways:
  // + Energy lost to the internal state or radiated
  // + Energy change owing to change in ionization state
  // + Deferred energy applied to the center-of-mass interaction
  //
  if (KE_DEBUG and not NoExact) {
    double KE_final_check = energyInPair(p1, p2);
    std::array<double, 2> KE_final_econs = {0.0, 0.0};

    if (use_cons>=0)
      KE_final_econs[0] += p1->dattrib[use_cons  ] + p2->dattrib[use_cons  ];

    if (use_elec>=0 and elc_cons)
      KE_final_econs[1] += p1->dattrib[use_elec+3] + p2->dattrib[use_elec+3];

    std::array<double, 2> dCons = KE_initl_econs - KE_final_econs;
    double dConSum = dCons[0] + dCons[1];

    double KEinitl = KE_initl_check;
    double KEfinal = KE_final_check;
    double delE1   = KEinitl - KEfinal - deltaSum - delEsum + delEmis + delEdfr;
    double delE2   = KEinitl - KEfinal - deltaSum - dConSum - delEsum - PPsav1 - PPsav2 - EconsUpI - EconsUpE;
    double delE3   = KEinitl - KEfinal - dConSum - PE[Jsav][1] - delEfnl;

    if (fabs(delE3) > tolE*KE_initl_check) {
      std::cout << "**ERROR inelasticHybrid dE = " << delE1 << ", " << delE2
		<< ", rel1 = " << delE1/KE_initl_check
		<< ", rel2 = " << delE2/KE_initl_check
		<< ", rel3 = " << delE3/KE_initl_check
		<< ",  dKE = " << KEinitl - KEfinal
		<< ", dCn1 = " << dCons[0]
		<< ", dCn2 = " << dCons[1]
		<< ", PPs1 = " << PPsav1
		<< ", PPs2 = " << PPsav2
		<< ", EcnI = " << EconsUpI
		<< ", EcnE = " << EconsUpE
		<< ", Elos = " << delEloss
		<< ", delS = " << deltaSum
		<< ", Esum = " << delEsum
		<< ", miss = " << delEmis
		<< ", defr = " << delEdfr
		<< ", clrE = " << clrE[id]
		<< ", q = "    << PP[Jsav]->q
		<< ", Jsav = " << Jsav
		<< std::endl;
    } else {
      if (DBG_NewTest)
	std::cout << "**GOOD inelasticHybrid dE = " << delE1 << ", " << delE2
		  << ", rel1 = " << delE1/KE_initl_check
		  << ", rel2 = " << delE2/KE_initl_check
		  << ",  dKE = " << KEinitl - KEfinal
		  << ", dCn1 = " << dCons[0]
		  << ", dCn2 = " << dCons[1]
		  << ", PPs1 = " << PPsav1
		  << ", PPs2 = " << PPsav2
		  << ", EcnI = " << EconsUpI
		  << ", EcnE = " << EconsUpE
		  << ", Elos = " << delEloss
		  << ", Efnl = " << delEfnl
		  << ", delS = " << deltaSum
		  << ", Esum = " << delEsum
		  << ", miss = " << delEmis
		  << ", defr = " << delEdfr
		  << ", clrE = " << clrE[id]
		  << ", Jsav = " << Jsav
		  << std::endl;
    }    
  }

  return ret;
}

// This does not change the state of the particles.  Check only.
void CollideIon::KEcheck
(PordPtr pp, double& KE_initl_check,
 std::array<double, 2>& ionExtra, std::array<double, 2>& rcbExtra)
{
  if (KE_DEBUG) {
    double KE_inter_check = energyInPair(pp->p1, pp->p2);
    double KDif = KE_initl_check - KE_inter_check;
    if (pp->P == Pord::ion_electron) {
      KDif += ionExtra[0]  - rcbExtra[0] ;
    } 
    if (pp->P == Pord::electron_ion) {
      KDif += ionExtra[1] - rcbExtra[1];
    } 
    if (fabs(KDif) > tolE*KE_initl_check) {
      // double tst1 = energyInPart(pp->p1);
      // double tst2 = energyInPart(pp->p2);
      std::cout << "**ERROR: KE energy check: del=" << KDif
		<< ", rel=" << KDif/KE_initl_check
		<< std::endl;
    }
  }
}

void CollideIon::scatterHybrid
(double P, PordPtr pp, KE_& KE,
 std::vector<double>* V1, std::vector<double>* V2, int id)
{
  std::vector<double>* v1 = V1;
  std::vector<double>* v2 = V2;

  if (pp->swap) zswap(v1, v2);

  // For energy conservation debugging
  //
  if (KE_DEBUG) {
    KE.i(1) = KE.i(2) = 0.0;
    for (auto v : *v1) KE.i(1) += v*v;
    for (auto v : *v2) KE.i(2) += v*v;
  }
  KE.bs.reset();

  // Split between components for testing (for ExactE algorithm only)
  //
  double alph = scatFac1;
  double beta = scatFac2;

  // Total effective mass in the collision (atomic mass units)
  //
  double mt = pp->m1 + pp->m2;

  // Reduced mass (atomic mass units)
  //
  double mu = pp->m1 * pp->m2 / mt;

  // Set COM frame
  //
  std::vector<double> vcom(3), vrel(3);
  double vi = 0.0;

  for (size_t k=0; k<3; k++) {
    vcom[k] = (pp->m1*(*v1)[k] + pp->m2*(*v2)[k])/mt;
    vrel[k] = (*v1)[k] - (*v2)[k];
    vi += vrel[k] * vrel[k];
  }

  // Compute the change of energy in the collision frame by computing
  // the velocity reduction factor
  //
  double  q = pp->W2/pp->W1;	// Full q

  double q1 = q * P;		// *ACTIVE* fraction in Species 1
  double q2 = P;		// *ACTIVE* fraction in Species 2
  double c1 = 1.0 - q1;		// INACTIVE fraction in Species 1
  double c2 = 1.0 - q2;		// INACTIVE fraction in Species 2

				// Energy in COM
  double kE   = 0.5*pp->W1*q1*mu*vi;
				// Energy reduced by loss
  double totE = kE - KE.delE;

  // KE is positive
  //
  if (kE>0.0) {
    // More loss energy requested than available?
    //
    if (totE < 0.0) {
      KE.miss = -totE;
      deferredEnergyHybrid(pp, -totE);
      KE.delE += totE;
      totE = 0.0;
    }
    // Update the outgoing energy in COM
    //
    KE.vfac = sqrt(totE/kE);
    KE.kE   = kE;
    KE.totE = totE;
    KE.bs.set(KE_Flags::Vfac);
  }
  // KE is zero (limiting case)
  //
  else {
    KE.vfac = 1.0;
    KE.kE   = kE;
    KE.totE = totE;

    if (KE.delE>0.0) {
      // Defer all energy loss
      //
      deferredEnergyHybrid(pp, KE.delE);
      KE.delE = 0.0;
    } else {
      // Apply delE to COM
      //
      vi = -2.0*KE.delE/(pp->W1*q1*mu);
    }
  }

  // Assign interaction energy variables
  //
  vrel = unit_vector();
  vi   = sqrt(vi);
  for (auto & v : vrel) v *= vi;
  //                         ^
  //                         |
  // Velocity in center of mass, computed from v1, v2 and adjusted
  // according to the inelastic energy loss
  //

  // Velocity working variables
  //
  std::vector<double> uu(3), vv(3);

  double v1i2 = 0.0, v2i2 = 0.0, vdif = 0.0, v2u2 = 0.0;
  double udif = 0.0, v1u1 = 0.0;


  for (size_t k=0; k<3; k++) {
				// From momentum conservation with
				// inelastic adjustment
    uu[k] = vcom[k] + pp->m2/mt*vrel[k] * KE.vfac;
    vv[k] = vcom[k] - pp->m1/mt*vrel[k] * KE.vfac;
				// Difference in Particle 1
    udif += ((*v1)[k] - uu[k]) * ((*v1)[k] - uu[k]);
				// Difference in Particle 2
    vdif += ((*v2)[k] - vv[k]) * ((*v2)[k] - vv[k]);
				// Normalizations
    v1i2 += (*v1)[k] * (*v1)[k];
    v2i2 += (*v2)[k] * (*v2)[k];
    v1u1 += (*v1)[k] * uu[k];
    v2u2 += (*v2)[k] * vv[k];
  }

  if (ExactE and q1 < 1.0) {

    double vrat = 1.0;

    KE.bs.set(KE_Flags::ExQ);

    double A = alph*alph*pp->m1*c1*c1*v1i2 + beta*beta*pp->m2*q*c2*c2*v2i2;

    if (A > 0.0) {

      KE.bs.set(KE_Flags::StdE);

      double B  =
	2.0*pp->m1*alph*c1*(c1*v1i2 + q1*v1u1) +
	2.0*pp->m2*beta*c2*(c2*v2i2 + q2*v2u2) * q;
      
      double C  = pp->m1*q1*c1*udif + pp->m2*q*q2*c2*vdif;

      // Quadratic solution without subtraction for numerical precision
      if (B > 0.0) {
	vrat = 2.0*C/(B + sqrt(B*B + 4*A*C));
      } else {
	vrat = (-B + sqrt(B*B + 4*A*C))/(2.0*A);
      }
      
      Vdiag[id][0] += P;
      Vdiag[id][1] += P * vrat;
      Vdiag[id][2] += P * vrat*vrat;

      // TEST
      if (KE_DEBUG and true) {
	
	double M1 = 0.5 * pp->W1 * pp->m1;
	double M2 = 0.5 * pp->W2 * pp->m2;

	double KE1f = 0.0, KE2f = 0.0;
	for (size_t k=0; k<3; k++) {
	  double w1 = c1*(*v1)[k]*(1.0 + alph*vrat) + q1*uu[k];
	  double w2 = c2*(*v2)[k]*(1.0 + beta*vrat) + q2*vv[k];
	  KE1f += w1 * w1;
	  KE2f += w2 * w2;
	}

	double ke1f = KE1f;
	double ke2f = KE2f;

	// Initial KE
	//
	double KE1i = M1 * KE.i(1);
	double KE2i = M2 * KE.i(2);

	// Final KE
	//
	KE1f *= M1;
	KE2f *= M2;
      
	// KE differences
	//
	double KEi   = KE1i + KE2i;
	double KEf   = KE1f + KE2f;
	double delEt = KEi  - KEf - KE.delE;
	
	if ( fabs(delEt)/std::min<double>(KEi, KEf) > 0.001*tolE) {
	  std::cout << "**ERROR scatter internal: delEt = " << delEt
		    << " rel = " << delEt/KEi
		    << " KEi = " << KEi
		    << " KEf = " << KEf
		    << "  dE = " << KE.delE
		    << "   P = " << P
		    << "   q = " << q
		    << "  q1 = " << q1
		    << "  q2 = " << q2
		    << std::endl;
	} else {
	  if (DBG_NewTest)
	    std::cout << "**GOOD scatter internal: rel = " << delEt/KEi
		      << " KEi = "  << KEi
		      << " KEf = "  << KEf
		      << " E1i = "  << KE1i
		      << " E2i = "  << KE2i
		      << " E1f = "  << KE1f
		      << " E2f = "  << KE2f
		      << "  dE = "  << KE.delE
		      << "   P = "  << P
		      << "   q = "  << q
		      << "  q1 = "  << q1
		      << "  q2 = "  << q2
		      << "  w1 = "  << pp->w1
		      << "  w2 = "  << pp->w2
		      << "  W1 = "  << pp->W1
		      << "  W2 = "  << pp->W2
		      << "  M1 = "  << pp->p1->mass
		      << "  M2 = "  << pp->p2->mass
		      << "  m1 = "  << pp->m1
		      << "  m2 = "  << pp->m2
		      << "  e1 = "  << pp->eta1
		      << "  e2 = "  << pp->eta2
		      << "  k1 = "  << ke1f
		      << "  k2 = "  << ke2f
		      << std::endl;
	}
      }

    } else {

      KE.bs.set(KE_Flags::zeroKE);

    }

    // Update post-collision velocities 
    // --------------------------------
    //
    // Compute new energy conservation updates
    //
    for (size_t k=0; k<3; k++) {
      //
      (*v1)[k] = c1*(*v1)[k]*(1.0 + alph*vrat) + q1*uu[k];
      (*v2)[k] = c2*(*v2)[k]*(1.0 + beta*vrat) + q2*vv[k];
    }

  }
  // END: ExactE algorithms
  // BEGIN: Momentum conservation
  else {

    KE.bs.set(KE_Flags::momC);

    double qKEfac1 = 0.5*pp->W1*pp->m1*q1*c1;
    double qKEfac2 = 0.5*pp->W2*pp->m2*q2*c2;

    KE.bs.set(KE_Flags::KEpos);

    // Update post-collision velocities.  In the electron version, the
    // momentum is assumed to be coupled to the ions, so the ion
    // momentum must be conserved.  Particle 2 is trace by construction.
    //
    KE.delta = 0.0;
    for (size_t k=0; k<3; k++) {
      double v01 = vcom[k] + pp->m2/mt*vrel[k];
      double v02 = vcom[k] - pp->m1/mt*vrel[k];

      KE.delta +=
	(v01 - (*v1)[k])*(v01 - (*v1)[k]) * qKEfac1 +
	(v02 - (*v2)[k])*(v02 - (*v2)[k]) * qKEfac2 ;

      v01 = vcom[k] + pp->m2/mt*vrel[k] * KE.vfac;
      v02 = vcom[k] - pp->m1/mt*vrel[k] * KE.vfac;

      (*v1)[k] = c1*(*v1)[k] + q1*v01;
      (*v2)[k] = c2*(*v2)[k] + q2*v02;
    }
    
  } // END: momentum conservation algorithm

  // Temporary deep debug
  //
  if (KE_DEBUG) {

    double M1 = 0.5 * pp->W1 * pp->m1;
    double M2 = 0.5 * pp->W2 * pp->m2;

    // Initial KE
    //
    double KE1i = M1 * KE.i(1);
    double KE2i = M2 * KE.i(2);

    double KE1f = 0.0, KE2f = 0.0;
    for (auto v : *v1) KE1f += v*v;
    for (auto v : *v2) KE2f += v*v;

    // Final KE
    //
    KE1f *= M1;
    KE2f *= M2;
      
    // KE differences
    //
    double KEi   = KE1i + KE2i;
    double KEf   = KE1f + KE2f;
    double delEt = KEi  - KEf - KE.delta - std::min<double>(kE, KE.delE);
    
    // Sanity test
    //
    double Ii1 = 0.0, Ii2 = 0.0, Ie1 = 0.0, Ie2 = 0.0;
    for (size_t k=0; k<3; k++) {
      Ii1 += pp->p1->vel[k] * pp->p1->vel[k];
      Ii2 += pp->p2->vel[k] * pp->p2->vel[k];
      if (use_elec>=0) {
	size_t K = use_elec + k;
	Ie1 += pp->p1->dattrib[K] * pp->p1->dattrib[K];
	Ie2 += pp->p2->dattrib[K] * pp->p2->dattrib[K];
      }
    }

    Ii1 *= 0.5 * pp->p1->mass;
    Ii2 *= 0.5 * pp->p2->mass;
    Ie1 *= 0.5 * pp->p1->mass * pp->eta1 * atomic_weights[0]/atomic_weights[pp->Z1];
    Ie2 *= 0.5 * pp->p2->mass * pp->eta2 * atomic_weights[0]/atomic_weights[pp->Z2];

    double Fi1 = Ii1, Fi2 = Ii2, Fe1 = Ie1, Fe2 = Ie2;

    if (pp->P == Pord::ion_ion) {
      Fi1 = 0.0; for (auto v : *v1) Fi1 += v*v;
      Fi2 = 0.0; for (auto v : *v2) Fi2 += v*v;
      Fi1 *= 0.5 * pp->p1->mass;
      Fi2 *= 0.5 * pp->p2->mass;
    }

    if (pp->P == Pord::ion_electron) {
      Fi1 = 0.0; for (auto v : *v1) Fi1 += v*v;
      Fe2 = 0.0; for (auto v : *v2) Fe2 += v*v;
      Fi1 *= 0.5 * pp->p1->mass;
      Fe2 *= 0.5 * pp->p2->mass * pp->eta2 * atomic_weights[0]/atomic_weights[pp->Z2];
    }

    if (pp->P == Pord::electron_ion) {
      Fe1 = 0.0; for (auto v : *v1) Fe1 += v*v;
      Fi2 = 0.0; for (auto v : *v2) Fi2 += v*v;
      Fe1 *= 0.5 * pp->p1->mass * pp->eta1 * atomic_weights[0]/atomic_weights[pp->Z1];
      Fi2 *= 0.5 * pp->p2->mass;
    }

    if ( fabs(delEt)/std::min<double>(KEi, KEf) > tolE) {
      std::cout << "**ERROR scatter: delEt = " << delEt
		<< " rel = "  << delEt/KEi
		<< " KEi = "  << KEi
		<< " KEf = "  << KEf
		<< " dif = "  << KEi - KEf
		<< "  kE = "  << kE
		<< "  dE = "  << KE.delE
		<< " dvf = "  << KE.delE/kE
		<< " tot = "  << totE
		<< " vfac = " << KE.vfac
		<< "    P = " << P
		<< "    q = " << q
		<< "   q2 = " << q1
		<< "   q1 = " << q2
		<< "   w1 = " << pp->w1
		<< "   w2 = " << pp->w2
		<< "   W1 = " << pp->W1
		<< "   W2 = " << pp->W2
		<< " flg = " << KE.decode()
		<< (pp->swap ? " [swapped]" : "")
		<< std::endl;
    } else {
      if (DBG_NewTest)
	std::cout << "**GOOD scatter: delEt = "
		  << std::setprecision(14) << std::scientific
		  << std::setw(22) << delEt
		  << " rel = "  << std::setw(22) << delEt/KEi << " kE = " << std::setw(22) << kE
		  << "   W1 = " << std::setw(22) << pp->W1
		  << "   W2 = " << std::setw(22) << pp->W2
		  << "  Ei1 = " << std::setw(22) << KE1i
		  << "  Ef1 = " << std::setw(22) << KE1f
		  << "  Ii1 = " << std::setw(22) << Ii1
		  << "  Ie1 = " << std::setw(22) << Ie1
		  << "  Ei2 = " << std::setw(22) << KE2i
		  << "  Ef2 = " << std::setw(22) << KE2f
		  << "  Ii2 = " << std::setw(22) << Ii2
		  << "  Ie2 = " << std::setw(22) << Ie2
		  << " delE = " << std::setw(22) << KE.delE << (pp->swap ? " [swapped]" : "")
		  << std::setprecision(5)  << std::endl;
    }

  } // END: temporary deep debug

  // Sanity check
  if (pp->W2 > pp->W1) {
    std::cout << "Backwards: w1=" << pp->w1
	      << " w2=" << pp->w2
	      << " W1=" << pp->W1
	      << " W2=" << pp->W2
	      << " m1=" << pp->m1
	      << " m2=" << pp->m2
	      << " M1=" << pp->p1->mass
	      << " M2=" << pp->p2->mass
	      << std::endl;
  }
      
} // END: CollideIon::scatterHybrid


void CollideIon::deferredEnergyHybrid(PordPtr pp, const double E)
{
  // Sanity check
  //
  if (E < 0.0) {
    std::cout << "**ERROR: negative deferred energy! E=" << E << std::endl;
    return;
  }

  // Save energy adjustments for next interation.  Split between like
  // species ONLY.
  //
  if (use_cons >= 0) {

    if (use_elec<0) {
      if (fabs(pp->W2/pp->W1 - 1.0) < 1.0e-16) {
	pp->E1[0] += 0.5*E;
	pp->E2[0] += 0.5*E;
      } else {
	if (pp->W1 > pp->W2) pp->E1[0] += E;
	else                 pp->E2[0] += E;
      }
    }
    else {
      double a = pp->w1, b = pp->w2;

      if (pp->m1 < 1.0) {
	if (E_split) a *= pp->eta1/pp->Z1; else a = 0;
	pp->E1[1] += a*E/(a + b);
	pp->E2[0] += b*E/(a + b);
      }
      else if (pp->m2 < 1.0) {
	if (E_split) b *= pp->eta2/pp->Z2; else b = 0;
	pp->E1[0] += a*E/(a + b);
	pp->E2[1] += b*E/(a + b);
      }
      else {
	pp->E1[0]  += a*E/(a + b);
	pp->E2[0]  += b*E/(a + b);
      }

    }

  } // END: use_cons >= 0

} // END: CollideIon::deferredEnergyHybrid


// Updates particle energies using Pord for energy conservation and
// does internal conservation checking
//
void CollideIon::checkEnergyHybrid
(PordPtr pp, KE_& KE,
 const std::vector<double>* V1, const std::vector<double>* V2,
 unsigned iType, int id)
{
  bool equal = (pp->Z1 == pp->Z2);

  bool swap = false;
  if ( (pp->P == Pord::ion_electron and  pp->swap) or
       (pp->P == Pord::electron_ion and !pp->swap) ) swap = true;

  const std::vector<double>* v1 = V1;
  const std::vector<double>* v2 = V2;

  if (pp->swap) zswap(v1, v2);

  // Apply momentum conservation energy offset
  //
  if ((equal or TRACE_OVERRIDE) and KE.delta>0.0) {
    if (pp->P == Pord::ion_ion) {
      pp->E1[0] -= KE.delta * 0.5;
      pp->E2[0] -= KE.delta * 0.5;
    } else {
      if (swap) {
	pp->E1[1] -= KE.delta * 0.5;
	pp->E2[0] -= KE.delta * 0.5;
      } else {
	pp->E1[0] -= KE.delta * 0.5;
	pp->E2[1] -= KE.delta * 0.5;
      }
    }
  }
  
  if (not equal and not TRACE_OVERRIDE) {
    if (TRACE_ELEC) {
      if (swap) {
	pp->E1[1] -= KE.delta * TRACE_FRAC;
	pp->E2[0] -= KE.delta * (1.0 - TRACE_FRAC);
      } else {
	pp->E1[0] -= KE.delta * (1.0 - TRACE_FRAC);
	pp->E2[1] -= KE.delta * TRACE_FRAC;
      }

    } else {

      if (pp->P == Pord::ion_ion) {
	if (pp->w1 > pp->w2)
	  pp->E1[0] -= KE.delta;
	else
	  pp->E2[0] -= KE.delta;
      }
      else if (pp->P == Pord::ion_electron) {
	if (pp->swap) {
	  if (pp->w1 > pp->w2)
	    pp->E1[1] -= KE.delta;
	  else
	    pp->E2[0] -= KE.delta;
	} else {
	  if (pp->w1 > pp->w2)
	    pp->E1[0] -= KE.delta;
	  else
	    pp->E2[1] -= KE.delta;
	}

      }
      else  {
	if (pp->swap) {
	  if (pp->w1 > pp->w2)
	    pp->E1[0] -= KE.delta;
	  else
	    pp->E2[1] -= KE.delta;
	} else {
	  if (pp->w1 > pp->w2)
	    pp->E1[1] -= KE.delta;
	  else
	    pp->E2[0] -= KE.delta;
	}
      }
    }
  }

  // KE debugging
  //
  if (KE_DEBUG) {
    KE.f(1) = KE.f(2) = 0.0;
    for (auto v : *v1) KE.f(1) += v*v;
    for (auto v : *v2) KE.f(2) += v*v;

				// Pre collision KE
    KE.i(1) *= 0.5*pp->W1*pp->m1;
    KE.i(2) *= 0.5*pp->W2*pp->m2;
				// Post collision KE
    KE.f(1) *= 0.5*pp->W1*pp->m1;
    KE.f(2) *= 0.5*pp->W2*pp->m2;

    double tKEi = KE.i(1) + KE.i(2);	// Total pre collision KE
    double tKEf = KE.f(1) + KE.f(2);	// Total post collision KE
    double dKE  = tKEi - tKEf;		// Kinetic energy balance

    if (pp->m1<1.0) {
      if (KE.i(1) > 0) keER[id].push_back((KE.i(1) - KE.f(1))/KE.i(1));
      if (KE.i(2) > 0) keIR[id].push_back((KE.i(2) - KE.f(2))/KE.i(2));
    }

    if (pp->m2<1.0) {
      if (KE.i(1) > 0) keIR[id].push_back((KE.i(1) - KE.f(1))/KE.i(1));
      if (KE.i(2) > 0) keER[id].push_back((KE.i(2) - KE.f(2))/KE.i(2));
    }

    // Check energy balance including excess from momentum algorithm
    //
    double testE = dKE - KE.delta;

    if (equal or ALWAYS_APPLY) testE -= KE.delE;

    misE[id] += KE.miss;

    if (fabs(testE) > tolE*(tKEi+tKEf) )
      std::cout << "**ERROR check deltaE ("<< pp->m1 << "," << pp->m2 << ") = "
		<< std::setw(14) << testE
		<< ": rel=" << std::setw(14) << testE/tKEi
		<< ", dKE=" << std::setw(14) << dKE
		<< ", com=" << std::setw(14) << KE.delta
		<< ", del=" << std::setw(14) << KE.delE
		<< ", mis=" << std::setw(14) << KE.miss
		<< ",  kE=" << std::setw(14) << KE.kE
		<< ", tot=" << std::setw(14) << KE.totE
		<< ", KEd=" << std::setw(14) << KE.dKE
		<< ", gam=" << std::setw(14) << KE.gamma
		<< ", wv1=" << std::setw(14) << KE.o1
		<< ", wv2=" << std::setw(14) << KE.o2
		<< ",  vf=" << std::setw(14) << KE.vfac
		<< ", "     << (equal ? "same" : "diff")
		<< ",   q=" << std::setw(14) << pp->W2/pp->W1
		<< ", flg=" << KE.decode()
		<< std::endl;
    else {
      if (DBG_NewTest)
	std::cout << "**GOOD check deltaE ("<< pp->m1 << "," << pp->m2 << ") = "
		  << std::setw(14) << testE
		  << ": rel=" << std::setw(14) << testE/tKEi
		  << ", " << HClabel.at(iType) << std::endl; 
    }

  } // Energy conservation debugging diagnostic (KE_DEBUG)

} // END: checkEnergyHybrid


void CollideIon::debugDeltaE(double delE, unsigned short Z, unsigned short C,
			     double KE, double prob, int interFlag)
{
  if (delE < 0.0)
    std::cout << " *** Neg deltaE=" << std::setw(12) << delE << ", (Z, C)=("
	      << std::setw(2) << Z << ", " << std::setw(2) << C << "), E="
	      << std::setw(12) << KE  << ", prob=" << std::setw(12) << prob
	      << " :: " << labels[interFlag] << std::endl;
}

// Compute the electron energy correction owing to random channel
// selection.
//
void CollideIon::updateEnergyHybrid(PordPtr pp, KE_& KE)
{
  // Compute final energy
  //
  pp->eFinal();

  if (pp->wght) return;
  if (pp->P == Pord::ion_ion) return;

  double tKEi = 0.0;		// Total pre collision KE
  double tKEf = 0.0;		// Total post collision KE
  double eta  = 0.0;
  bool error  = false;

  if (pp->P == Pord::ion_electron) {
    if (pp->swap) {
      tKEi = pp->mid[0].KEw + pp->mid[1].KEi;
      tKEf = pp->end[0].KEw + pp->end[1].KEi;
      //             ^                ^
      //             |                |
      // Particle 1/electron          |
      //                              |
      // Particle 2/ion---------------+

      eta = pp->eta1;
      if (eta<0.0 or eta > pp->Z1) error = true;
    } else {
      tKEi = pp->mid[0].KEi + pp->mid[1].KEw;
      tKEf = pp->end[0].KEi + pp->end[1].KEw;
      //             ^                ^
      //             |                |
      // Particle 1/ion               |
      //                              |
      // Particle 2/electron----------+
      eta = pp->eta2;
      if (eta<0.0 or eta > pp->Z2) error = true;
    }
  }

  if (pp->P == Pord::electron_ion) {
    if (pp->swap) {
      tKEi = pp->mid[0].KEi + pp->mid[1].KEw;
      tKEf = pp->end[0].KEi + pp->end[1].KEw;
      //             ^                ^
      //             |                |
      // Particle 1/ion               |
      //                              |
      // Particle 2/electron- --------+

      eta = pp->eta2;
      if (eta<0.0 or eta > pp->Z2) error = true;
    } else {
      tKEi = pp->mid[0].KEw + pp->mid[1].KEi;
      tKEf = pp->end[0].KEw + pp->end[1].KEi;
      //             ^                ^
      //             |                |
      // Particle 1/electron          |
      //                              |
      // Particle 2/ion---------------+

      eta = pp->eta1;
      if (eta<0.0 or eta > pp->Z1) error = true;
    }
  }

  // Want energy to be:
  //
  //   tKEf  = tKEi - KE.delE
  //
  // or
  //
  //   delKE = tKEi - tKEf - KE.delE
  //
  // Want delKE = 0.  Need to defer this amount of energy.

  // Kinetic energy balance
  //
  double testE = tKEi - tKEf - KE.delta - KE.delE;

  // For energy conservation checking
  //
  KE.defer -= testE;

  // Add to electron deferred energy
  //
  if (pp->P == Pord::ion_electron) {
    if (elc_cons) {
      if (pp->swap) pp->p1->dattrib[use_elec+3] -= testE;
      else          pp->p2->dattrib[use_elec+3] -= testE;
    } else {
      if (reverse_apply) {
	if (pp->swap) pp->p2->dattrib[use_cons] -= testE;
	else          pp->p1->dattrib[use_cons] -= testE;
      } else {
	if (TRACE_ELEC) {
	  if (pp->swap) {
	    pp->p1->dattrib[use_cons] -= testE*TRACE_FRAC;
	    pp->p2->dattrib[use_cons] -= testE*(1.0 - TRACE_FRAC);
	  } else {
	    pp->p1->dattrib[use_cons] -= testE*(1.0 - TRACE_FRAC);
	    pp->p2->dattrib[use_cons] -= testE*(TRACE_FRAC);
	  }
	} else {
	  if (pp->swap) pp->p1->dattrib[use_cons] -= testE;
	  else          pp->p2->dattrib[use_cons] -= testE;
	}
      }
    }
  }
  
  if (pp->P == Pord::electron_ion) {
    if (elc_cons) {
      if (pp->swap) pp->p2->dattrib[use_elec+3] -= testE;
      else          pp->p1->dattrib[use_elec+3] -= testE;
    } else {
      if (reverse_apply) {
	if (pp->swap) pp->p1->dattrib[use_cons] -= testE;
	else          pp->p2->dattrib[use_cons] -= testE;
      } else {
	if (TRACE_FRAC) {
	  if (pp->swap) {
	    pp->p1->dattrib[use_cons] -= testE*(1.0 - TRACE_FRAC);
	    pp->p2->dattrib[use_cons] -= testE*TRACE_FRAC;
	  } else {
	    pp->p1->dattrib[use_cons] -= testE*TRACE_FRAC;
	    pp->p2->dattrib[use_cons] -= testE*(1.0 - TRACE_FRAC);
	  }
	} else {
	  if (pp->swap) pp->p2->dattrib[use_cons] -= testE;
	  else          pp->p1->dattrib[use_cons] -= testE;
	}
      }
    }
  }

  if (DBG_NewTest and error)
    std::cout << "**ERROR in update: "
	      << ": KEi=" << std::setw(14) << tKEi
	      << ", KEf=" << std::setw(14) << tKEf
	      << ", eta=" << std::setw(14) << eta << std::setprecision(10)
	      << ", del=" << std::setw(14) << testE
	      << ", E1i=" << std::setw(18) << pp->end[0].KEi
	      << ", E2i=" << std::setw(18) << pp->end[1].KEi
	      << ", E1e=" << std::setw(18) << pp->end[0].KEw
	      << ", E2e=" << std::setw(18) << pp->end[1].KEw
	      << ", #1=" << pp->p1->indx
	      << ", #2=" << pp->p2->indx
	      << std::endl;
}



/*
  Algorithm for Trace


  -------------------

  1) All superparticles have the same mixture of elements (although the ionization
     state may vary particle to particle). Therefore, mean molecular weight is the 
     same for each particle.

  2) Ion-ion scattering is equal mass.

  3) Ion-electron scattering is performed with equal weights with
     cross section scaled by electron fraction

  This implementation is based on the hybrid algorithm, rather than the original.

*/
int CollideIon::inelasticTrace(int id, pCell* const c,
			       Particle* const _p1, Particle* const _p2,
			       double *cr)
{
  int ret         =  0;		// No error (flag)

  Particle* p1    = _p1;	// Copy pointers for swapping, if
  Particle* p2    = _p2;	// necessary

				// For convenience: proton key
  speciesKey proton(1, 2);

  typedef std::array<double, 3> Telem;
  std::array<Telem, 3> PE;
  for (auto & v : PE) v = {};	// Zero initialization


  // These are the electron KEs per amu for each superparticle used
  // for computing changes in electron KE from ionizaton and
  // recombination.
  //
  double iE1 = 0.0, iE2 = 0.0;

  if (use_elec) {
    for (size_t k=0; k<3; k++) {
      iE1 += p1->dattrib[use_elec+k] * p1->dattrib[use_elec+k];
      iE2 += p2->dattrib[use_elec+k] * p2->dattrib[use_elec+k];
    }
    iE1 *= 0.5*p1->mass * atomic_weights[0];
    iE2 *= 0.5*p2->mass * atomic_weights[0];
  }

  // Ion KE
  //
  double iI1 = 0.0, iI2 = 0.0;
  for (auto v : p1->vel) iI1 *= v*v;
  for (auto v : p2->vel) iI2 *= v*v;
  iI1 *= 0.5*p1->mass;
  iI2 *= 0.5*p2->mass;

  // Debug energy conservation
  //
  double KE_initl_check = 0.0;
  std::array<double, 2> KE_initl_econs = {0.0, 0.0};
  double deltaSum = 0.0, delEsum = 0.0, delEmis = 0.0, delEdfr = 0.0;
  double delEloss = 0.0, delEfnl = 0.0;
  
  KE_initl_check = energyInPair(p1, p2);

  if (KE_DEBUG) {
    if (use_cons>=0)
      KE_initl_econs[0] += p1->dattrib[use_cons  ] + p2->dattrib[use_cons  ];
    if (use_elec>=0 and elc_cons)
      KE_initl_econs[1] += p1->dattrib[use_elec+3] + p2->dattrib[use_elec+3];
  }

  // Proportional to number of true particles in each superparticle
  //
  double W1 = p1->mass/molP1[id];
  double W2 = p2->mass/molP2[id];

  std::array<PordPtr, 3> PP =
    { PordPtr(new Pord(this, p1, p2, W1, W2, Pord::ion_ion,      DBL_MAX) ),
      PordPtr(new Pord(this, p1, p2, W1, W2, Pord::ion_electron, DBL_MAX) ),
      PordPtr(new Pord(this, p1, p2, W1, W2, Pord::electron_ion, DBL_MAX) ) };

  Njsel[id]++;

  // Sanity check
  //
  if (use_normtest) {
    normTest(p1, "p1 [Before]");
    normTest(p2, "p2 [Before]");
  }

  // Collision count debugging
  //
  if (DEBUG_CNT >= 0) {
    p1->iattrib[DEBUG_CNT] += 1;
    p2->iattrib[DEBUG_CNT] += 1;
  }

  // For debugging diagnostics
  //
  if (elecDist) {
    elecEVsub[id].push_back(std::max<double>(kEe1[id], kEe2[id]));
  }

  // NOCOOL debugging
  //
  double NCXTRA = 0.0;

  int maxInterFlag = -1;
				// Record maximum prob interaction
  Interact::pElem maxI1, maxI2;

  double maxP      = 0.0;


  /*
  Notes on the recombination/ionization tracking logic:

  o Ionization increases the electron kinetic energy by the ionization
    increment times the existing electron KE.  This is a further loss
    from the COM KE; i.e. in addition to the ionization potential.

  o Recombination removes the ionization fraction times the electron
    KE.  This is taken from the recombining atom's electron KE.  The
    total energy is radiated but the ionization potential has no
    effect on the KE energy balance.

  These are both strictly inconsistent (the electron KE is taken from
  the ionized or recombined atom's electron).  This could be fixed,
  perhaps, by then exchanging the velocities of the interacting pair.
  */

  std::array<double, 2>  ionExtra {0, 0};
  std::array<double, 2>  rcbExtra {0, 0};
  std::pair<double, int> maxC(0.0, 0);
  
  // Run through all interactions in the cross-section map to include
  // ionization-state weightings.  Recall, the map contains values of
  // vrel*sigma*weight/cr.
  //
  double totalXS = 0.0;
  std::vector<size_t> order;
  size_t cnt = 0;
  for (auto I : hCross[id]) {
    order.push_back(cnt++);
    totalXS += I.crs;
  }

  // Randomize interaction order to prevent bias
  //
  std::random_shuffle(order.begin(), order.end());

  // Now, determine energy contribution for each interaction process.
  //
  for (auto O : order) {
    
    auto I             = hCross[id][O];
    int interFlag      = std::get<0>(I.t);
    double XS          = I.crs;
    double Prob        = XS/totalXS;

    if (Prob < 1.0e-14) continue;

    // The interaction
    //
    Interact::pElem I1 = std::get<1>(I.t);
    Interact::pElem I2 = std::get<2>(I.t);
      
    // Species keys
    //
    speciesKey k1 = I1.second;
    speciesKey k2 = I2.second;

    // Zx is the atomic number (charge), Px is the offset in
    // ionization state from 0, Cx is the _usual_ ionization level
    // (e.g. C=1 is neutral).
    //

    // Atomic number
    //
    unsigned short Z1 = k1.first;
    unsigned short Z2 = k2.first;

    if (SAME_INTERACT and Z1 != Z2) continue;
    if (DIFF_INTERACT and Z1 == Z2) continue;

    // Index of ionization level (which has offset of 1)
    //
    unsigned short  C1 = k1.second;
    unsigned short  C2 = k2.second;

    // Traditional ionization state (e.g. C1=1 is neutral)
    //
    unsigned short  P1 = C1 - 1; 
    unsigned short  P2 = C2 - 1; 

    // Compute class id
    //
    size_t cid = 0;
    if (std::get<2>(I.t).first == NTC::Interact::electron and
	std::get<1>(I.t).first != NTC::Interact::electron) cid = 1;

    if (std::get<1>(I.t).first == NTC::Interact::electron and
	std::get<2>(I.t).first != NTC::Interact::electron) cid = 2;

    if (std::get<2>(I.t).first != NTC::Interact::electron and
	std::get<1>(I.t).first != NTC::Interact::electron) cid = 0;

    if (std::get<2>(I.t).first == NTC::Interact::electron and
	std::get<1>(I.t).first == NTC::Interact::electron)
      {
	std::cout << "CRAZY pair: two electrons" << std::endl;
	cid = 0;
      }

    bool ok = false;		// Reject all interactions by default

    // Logic for selecting allowed interaction types
    //
    if (NoDelC)  {
      ok = true;
				// Pass events that are NOT ionization
				// or recombination, or both
      if (NoDelC & 0x1 and interFlag == recomb) ok = false;
      if (NoDelC & 0x2 and interFlag == ionize) ok = false;

    } else if (scatter) {
				// Only pass elastic scattering events
      if (interFlag < 6) ok = true;

				// Otherwise, pass all events . . .
    } else {
      ok = true;
    }
    
    // Following the selection logic above, do this interaction!
    //
    if (ok) {

      // Energy loss
      //
      double dE = 0.0;

      // Number interacting atoms
      //
      double N0 = PP[cid]->W2 * TreeDSMC::Munit / amu;

      // Temporary debugging
      //
      if (scatter_check) {
	unsigned short ZZ = (I1.first==Interact::electron ? Z2 : Z1);
	if (interFlag == ion_elec) Escat[id][ZZ] += Prob;
	Etotl[id][ZZ] += Prob;
      }
    
      lQ Q1(Z1, C1), Q2(Z2, C2);

      // Retrieve the diagnostic stanza for this species (correctly
      // including the ionization level)
      //
      collTDPtr ctd = (*collD)[Particle::defaultKey];
      
      // Select the maximum probability channel
      //
      if (Prob > maxP) {
	maxInterFlag = interFlag;
	maxP  = Prob;
	maxI1 = I1;
	maxI2 = I2;
      }

      //-----------------------------
      // Parse each interaction type
      //-----------------------------

      if (interFlag == neut_neut) {

	ctd->nn[id][0] += Prob;
	ctd->nn[id][1] += N0*Prob;
	
	PE[0][0] += Prob;
      }

      if (interFlag == neut_elec) {

	ctd->ne[id][0] += Prob;
	ctd->ne[id][1] += N0*Prob;

	if (I1.first == Interact::electron) {
	  PE[2][0] += Prob;
	} else {
	  PE[1][0] += Prob;
	}

      }

      if (interFlag == neut_prot) {
	
	ctd->np[id][0] += Prob;
	ctd->np[id][1] += N0*Prob;

	PE[0][0] += Prob;
      }

      if (interFlag == ion_elec) {

	ctd->ie[id][0] += Prob;
	ctd->ie[id][1] += N0*Prob;

	if (I1.first == Interact::electron) {
	  PE[2][0] += Prob;
	} else {
	  PE[1][0] += Prob;
	}
      }
      
      // Upscale inelastic cross sections by ratio of cross-section
      // and plasma rate scattering
      //
      Prob *= colCf[id];

      // Upscale inelastic cross sections by collision-limit ratio to
      // preserve the physical energy loss rate
      //
      if (collLim and collCor) Prob *= colSc[id];

      if (interFlag == free_free) {
	
	if (I1.first == Interact::electron) {
	  //
	  // Ion is p2
	  //
	  double tmpE = IS.selectFFInteract(I.CF);

	  dE = tmpE * Prob;

	  if (energy_scale > 0.0) dE *= energy_scale;
	  if (NO_FF_E) dE = 0.0;
	  
	  ctd->ff[id][0] += Prob;
	  ctd->ff[id][1] += N0*Prob;
	  if (not NOCOOL) ctd->ff[id][2] += N0 * dE;
	  if (use_spectrum) spectrumAdd(id, interFlag, tmpE, N0*Prob);

	  PE[2] += {Prob, dE};

	  if (maxC.first<dE) {
	    maxC.first = dE;
	    maxC.second = interFlag;
	  }

	} else {
	  //
	  // Ion is p1
	  //
	  double tmpE = IS.selectFFInteract(I.CF);

	  dE = tmpE * Prob;

	  if (energy_scale > 0.0) dE *= energy_scale;
	  if (NO_FF_E) dE = 0.0;
	  
	  ctd->ff[id][0] += Prob;
	  ctd->ff[id][1] += N0*Prob;
	  if (not NOCOOL) ctd->ff[id][2] += N0 * dE;
	  if (use_spectrum) spectrumAdd(id, interFlag, tmpE, N0*Prob);

	  PE[1] += {Prob, dE};

	  if (maxC.first<dE) {
	    maxC.first = dE;
	    maxC.second = interFlag;
	  }

	}

      }

      if (interFlag == colexcite) {

	if (I1.first == Interact::electron) {
	  //
	  // Ion is p2
	  //
	  double tmpE = IS.selectCEInteract(ch.IonList[Q2], I.CE);

	  dE = tmpE * Prob;

	  if (energy_scale > 0.0) dE *= energy_scale;
	    
	  ctd->CE[id][0] += Prob;
	  ctd->CE[id][1] += N0*Prob;
	  if (not NOCOOL) ctd->CE[id][2] += N0 * dE;
	  if (use_spectrum) spectrumAdd(id, interFlag, tmpE, N0*Prob);

	  PE[2] += {Prob, dE};

	  if (maxC.first<dE) {
	    maxC.first = dE;
	    maxC.second = interFlag;
	  }

	} else {
	  //
	  // Ion is p1
	  //
	  double tmpE = IS.selectCEInteract(ch.IonList[Q1], I.CE);

	  dE = tmpE * Prob;

	  if (energy_scale > 0.0) dE *= energy_scale;
	  
	  ctd->CE[id][0] += Prob;
	  ctd->CE[id][1] += N0*Prob;
	  if (not NOCOOL) ctd->CE[id][2] += N0 * dE;
	  if (use_spectrum) spectrumAdd(id, interFlag, tmpE, N0*Prob);

	  PE[1] += {Prob, dE};

	  if (maxC.first<dE) {
	    maxC.first = dE;
	    maxC.second = interFlag;
	  }

	}

      }

      if (interFlag == ionize) {

	if (I1.first == Interact::electron) {
	  //
	  // Ion is p2
	  //
	  if (use_normtest) {
	    std::ostringstream sout;
	    sout << "[Before ionize]: C2=" << P2
		 << ", Prob=" << Prob;
	    PP[2]->normTest(1, sout.str());
	    PP[2]->normTest(2, sout.str());
	    if (C2<1 or C2>Z2) {
	      std::cout << "[ionize] bad C2=" << C2
			<< " or C1=" << C1 << std::endl;
	    }
	  }

	  double WW = Prob * atomic_weights[Z2];

	  int pos = SpList[k2] - SpList.begin()->second;

	  if (WW < PP[2]->F(2, pos)) {
	    PP[2]->F(2, pos  ) -= WW;
	    PP[2]->F(2, pos+1) += WW;
	  } else {
	    WW = PP[2]->F(2, pos);
	    PP[2]->F(2, pos  )  = 0.0;
	    PP[2]->F(2, pos+1) += WW;
	  }

	  if (use_normtest) {
	    std::ostringstream sout;
	    sout << "[After ionize]: C2=" << C2-1 << ", Pr=" << WW
		 << ", Prob=" << Prob;
	    PP[2]->normTest(1, sout.str());
	    PP[2]->normTest(2, sout.str());
	  }
	  
	  Prob = WW;
	  
	  double tmpE = IS.DIInterLoss(ch.IonList[Q2]);
	  dE = tmpE * Prob;

	  // The kinetic energy of the ionized electron is lost
	  // from the COM KE
	  //
	  double Echg = iE2 * Prob / atomic_weights[Z2];
	  ionExtra[1] += Echg;

	  // Energy for ionized electron comes from COM
	  dE += Echg * TreeDSMC::Eunit / (N0*eV);

	  if (std::isinf(Echg)) {
	    std::cout << "**ERROR: crazy ion energy [2]=" << iE2
		      << ", Pr=" << Prob << std::endl;
	  }

	  if (energy_scale > 0.0) dE *= energy_scale;
	  if (NO_ION_E) dE = 0.0;
	    
	  ctd->CI[id][0] += Prob;
	  ctd->CI[id][1] += N0*Prob;
	  if (not NOCOOL) ctd->CI[id][2] += N0 * dE;
	  if (use_spectrum) spectrumAdd(id, interFlag, tmpE, N0*Prob);
	    
	  PE[2] += {Prob, dE};
	    
	  if (maxC.first<dE) {
	    maxC.first = dE;
	    maxC.second = interFlag;
	  }

	  if (IonRecombChk) {
	    if (ionCHK[id].find(k2) == ionCHK[id].end()) ionCHK[id][k2] = 0.0;
	    ionCHK[id][k2] += XS * (*cr);
	  }
	  
	} // END: electron-ion
	else {
	  //
	  // Ion is p1
	  //
	  if (use_normtest) {
	    std::ostringstream sout;
	    sout << "[Before ionize]: C1=" << P1
		 << ", Prob=" << Prob;
	    PP[1]->normTest(1, sout.str());
	    PP[1]->normTest(2, sout.str());
	    if (C1<1 or C1>Z1) {
	      std::cout << "[ionize] bad C1=" << C1
			<< " or C2=" << C2 << std::endl;
	    }
	  }

	  double WW = Prob * atomic_weights[Z1];

	  int pos = SpList[k1] - SpList.begin()->second;

	  if (WW < PP[1]->F(1, pos)) {
	    PP[1]->F(1, pos  ) -= WW;
	    PP[1]->F(1, pos+1) += WW;
	  } else {
	    WW = PP[1]->F(1, pos);
	    PP[1]->F(1, pos  )  = 0.0;
	    PP[1]->F(1, pos+1) += WW;
	  }

	  if (use_normtest) {
	    std::ostringstream sout;
	    sout << "[After ionize]: C1=" << C1-1 << ", Pr=" << WW
		 << ", Prob=" << Prob;
	    PP[1]->normTest(1, sout.str());
	    PP[1]->normTest(2, sout.str());
	  }
	    
	  Prob = WW;

	  double tmpE = IS.DIInterLoss(ch.IonList[Q1]);
	  dE = tmpE * Prob;
	  
	  // The kinetic energy of the ionized electron is lost
	  // from the COM KE
	  //
	  double Echg = iE1 * Prob / atomic_weights[Z1];
	  ionExtra[0] += Echg;

	  // Energy for ionized electron comes from COM
	  dE += Echg * TreeDSMC::Eunit / (N0*eV);

	  if (std::isinf(iE1 * Prob)) {
	    std::cout << "**ERROR: crazy ion energy [1]=" << iE1
		      << ", Pr=" << Prob << std::endl;
	  }

	  if (energy_scale > 0.0) dE *= energy_scale;
	  if (NO_ION_E) dE = 0.0;
	    
	  ctd->CI[id][0] += Prob;
	  ctd->CI[id][1] += N0*Prob;
	  if (not NOCOOL) ctd->CI[id][2] += N0 * dE;
	  if (use_spectrum) spectrumAdd(id, interFlag, tmpE, N0*Prob);
	  
	  PE[1] += {Prob, dE};
	  
	  if (maxC.first<dE) {
	    maxC.first = dE;
	    maxC.second = interFlag;
	  }

	  if (IonRecombChk) {
	    if (ionCHK[id].find(k1) == ionCHK[id].end()) ionCHK[id][k1] = 0.0;
	    ionCHK[id][k1] += XS * (*cr);
	  }
	}
      }
	
      if (interFlag == recomb) {

	if (I1.first == Interact::electron) {
	  //
	  // Ion is p2
	  //
	  if (use_normtest) {
	    std::ostringstream sout;
	    sout << "[Before recomb]: C2=" << C2
		 << ", Prob=" << Prob << ", w=" << PP[2]->F(2, C2);
	    PP[2]->normTest(1, sout.str());
	    PP[2]->normTest(2, sout.str());
				// Sanity check
	    if (C2<2 or C2>Z2+1) {
	      std::cout << "[recomb] bad C2=" << C2 << std::endl;
	    }
	  }
	  
	  double WW = Prob * atomic_weights[Z2];

	  int pos = SpList[k2] - SpList.begin()->second;

	  if (WW < PP[2]->F(2, pos)) {
	    PP[2]->F(2, pos  ) -= WW;
	    PP[2]->F(2, pos-1) += WW;
	  } else {
	    WW = PP[2]->F(2, pos);
	    PP[2]->F(2, pos  )  = 0.0;
	    PP[2]->F(2, pos-1) += WW;
	  }
	  
	  if (use_normtest) {
	    std::ostringstream sout;
	    sout << "[After recomb]: C2=" << C2 << ", Pr=" << WW
		 << ", Prob=" << Prob << ", w=" << PP[2]->F(2, C2);

	    PP[2]->normTest(1, sout.str());
	    PP[2]->normTest(2, sout.str());
	  }
	  
	  Prob = WW;		// Update to truncated value

	  // Electron KE lost in recombination is radiated by does not
	  // change COM energy
	  //
	  double Echg = iE2 * Prob / atomic_weights[Z2];
	  rcbExtra[1] += Echg;

	  // Electron KE radiated in recombination
	  double eE = Echg * TreeDSMC::Eunit / (N0*eV);

	  if (RECOMB_IP) dE += ch.IonList[lQ(Z2, C2)]->ip * Prob;
	  if (energy_scale > 0.0) dE *= energy_scale;

	  ctd->RR[id][0] += Prob;
	  ctd->RR[id][1] += N0*Prob;
	  // if (not NOCOOL) ctd->RR[id][2] += N0 * dE;
	  if (use_spectrum) spectrumAdd(id, interFlag, eE, N0*Prob);

	  PE[2] += {Prob, dE};
	  
	  if (maxC.first<dE) {
	    maxC.first = dE;
	    maxC.second = interFlag;
	  }

	  // For verbose diagnostic output only
	  //
	  if (elecDist and rcmbDist) {
	    double val = kEe2[id];
	    rcmbTotlAdd(val, WW, id);
	    if (rcmbDlog) val = log10(val);
	    elecRC[id].push_back(val);
	  }

	  // Add the KE from the recombined electron back to the free pool
	  //
	  if (NOCOOL and !NOCOOL_ELEC and C2==1 and use_cons>=0) {
	    double lKE = 0.0, fE = 0.5*PP[cid]->W1*atomic_weights[0];
	    for (size_t k=0; k<3; k++) {
	      double t = p2->dattrib[use_elec+k];
	      lKE += fE*t*t;
	    }
	    lKE *= Prob;
	    
	    NCXTRA += lKE;
	    
	    if (PP[cid]->q<1)
	      p2->dattrib[use_cons] += lKE;
	    else {
	      p1->dattrib[use_cons] += lKE * 0.5;
	      p2->dattrib[use_cons] += lKE * 0.5;
	    }
	  }
	  
	  if (IonRecombChk) {
	    if (recombCHK[id].find(k2) == recombCHK[id].end()) recombCHK[id][k2] = 0.0;
	    recombCHK[id][k2] += XS * (*cr);
	  }
	  
	} // END: swapped
	else {
	  //
	  // Ion is p1
	  //
	  if (use_normtest) {
	    std::ostringstream sout;
	    sout << "[Before recomb]: C1=" << C1
		 << ", Prob=" << Prob << ", w=" << PP[1]->F(1, C1);
	    PP[1]->normTest(1, sout.str());
	    PP[1]->normTest(2, sout.str());
	    if (C1<2 or C1>Z1+1) {
	      std::cout << "[recomb] bad C1=" << C1 << std::endl;
	    }
	  }

	  double WW = Prob * atomic_weights[Z1];

	  int pos = SpList[k1] - SpList.begin()->second;

	  if (WW < PP[1]->F(1, pos)) {
	    PP[1]->F(1, pos  ) -= WW;
	    PP[1]->F(1, pos-1) += WW;
	  } else {
	    WW = PP[1]->F(1, pos);
	    PP[1]->F(1, pos  )  = 0.0;
	    PP[1]->F(1, pos-1) += WW;
	  }
	  
	  if (use_normtest) {
	    std::ostringstream sout;
	    sout << "[After recomb]: C1=" << C1 << ", Pr=" << WW
		 << ", Prob=" << Prob << ", w=" << PP[1]->F(1, P1);
	    PP[1]->normTest(1, sout.str());
	    PP[1]->normTest(2, sout.str());
	  }

	  Prob = WW;		// Update to truncated value

	  // Electron KE lost in recombination is radiated by does not
	  // change COM energy
	  //
	  double Echg = iE1 * Prob / atomic_weights[Z1];
	  rcbExtra[0] += Echg;

	  // Electron KE fraction in recombination
	  //
	  double eE = Echg * TreeDSMC::Eunit / (N0*eV);

	  if (RECOMB_IP) dE += ch.IonList[lQ(Z1, C1)]->ip * Prob;
	  if (energy_scale > 0.0) dE *= energy_scale;

	  ctd->RR[id][0] += Prob;
	  ctd->RR[id][1] += N0*Prob;
	  // if (not NOCOOL) ctd->RR[id][2] += N0*Prob * kEe1[id];
	  if (use_spectrum) spectrumAdd(id, interFlag, eE, N0*Prob);

	  PE[1] += {Prob, dE};

	  if (maxC.first<dE) {
	    maxC.first = dE;
	    maxC.second = interFlag;
	  }

	  // For verbose diagnostic output only
	  //
	  if (elecDist and rcmbDist) {
	    double val = kEe1[id];
	    rcmbTotlAdd(val, WW, id);
	    if (rcmbDlog) val = log10(val);
	    elecRC[id].push_back(val);
	  }

	  // Add the KE from the recombined electron back to the free pool
	  //
	  if (NOCOOL and !NOCOOL_ELEC and C1==1 and use_cons>=0) {
	    double lKE = 0.0, fE = 0.5*PP[cid]->W1*atomic_weights[0];
	    for (size_t k=0; k<3; k++) {
	      double t = p1->dattrib[use_elec+k];
	      lKE += fE*t*t;
	    }
	    lKE *= Prob;
	    
	    NCXTRA += lKE;

	    if (PP[1]->q<1)
	      p1->dattrib[use_cons] += lKE;
	    else {
	      p1->dattrib[use_cons] += lKE * 0.5;
	      p2->dattrib[use_cons] += lKE * 0.5;
	    }
	  }

	  if (IonRecombChk) {
	    if (recombCHK[id].find(k1) == recombCHK[id].end()) recombCHK[id][k1] = 0.0;
	    recombCHK[id][k1] += XS * (*cr);
	  }
	}
	
      } // END: recomb

      // -----------------
      // ENERGY DIAGNOSTIC
      // -----------------
      
      if (PP[cid]->swap) {
	bool prior = std::isnan(ctd->eV_av[id]);
	ctd->eV_av[id] += kEe2[id] * Prob;
	if (std::isnan(ctd->eV_av[id])) {
	  std::cout << "NAN eV_N[2]=" << ctd->eV_N[id]
		    << ", prior=" << std::boolalpha << prior << std::endl;
	}
	ctd->eV_N[id] += Prob;
	ctd->eV_min[id] = std::min(ctd->eV_min[id], kEe2[id]);
	ctd->eV_max[id] = std::max(ctd->eV_max[id], kEe2[id]);
	
	if (kEe2[id] > 10.2) { ctd->eV_10[id] += Prob; }
	
      } else {
	
	bool prior = std::isnan(ctd->eV_av[id]);
	ctd->eV_av[id] += kEe1[id] * Prob;
	if (std::isnan(ctd->eV_av[id])) {
	  std::cout << "NAN eV_N[1]=" << ctd->eV_N[id]
		    << ", prior=" << std::boolalpha << prior << std::endl;
	}
	ctd->eV_N[id] += Prob;
	ctd->eV_min[id] = std::min(ctd->eV_min[id], kEe1[id]);
	ctd->eV_max[id] = std::max(ctd->eV_max[id], kEe1[id]);
	
	if (kEe1[id] > 10.2) { ctd->eV_10[id] += Prob;}
      }

      // Convert from eV per particle to system units per
      // superparticle
      //
      double Escl = N0 * eV/TreeDSMC::Eunit;

      if (PE[1][0]>0.0) {
	ctd->dv[id][0] += Prob;
	ctd->dv[id][1] += PP[1]->W2 * Prob;
	if (not NOCOOL) ctd->dv[id][2] += PP[1]->W2 * PE[1][1];
	if (scatter_check) {
	  double m1 = atomic_weights[Z1];
	  double m2 = atomic_weights[ 0];
	  double mu = m1 * m2 / (m1 + m2);
	  double tK = 0.0;
	  for (size_t k=0; k<3; k++) {
	    double vr = p2->dattrib[use_elec + k] - p1->vel[k];
	    tK += vr * vr;
	  }
	  tK *= 0.5*PP[1]->W1*PP[1]->q*mu;

	  Italy[id][Z1*100+Z2][interFlag][0] += Prob;
	  Italy[id][Z1*100+Z2][interFlag][1] += dE * Escl;
	  Italy[id][Z1*100+Z2][interFlag][2] += tK;
	  Italy[id][Z1*100+Z2][interFlag][3] += 1;
	}
      }
      
      if (PE[0][0]>0.0) {
	if (scatter_check) {
	  double m1 = atomic_weights[Z1];
	  double m2 = atomic_weights[Z2];
	  double mu = m1 * m2 / (m1 + m2);
	  double tK = 0.0;
	  for (size_t k=0; k<3; k++) {
	    double vr = p2->vel[k] - p1->vel[k];
	    tK += vr * vr;
	  }
	  tK *= 0.5*PP[0]->W1*PP[0]->q*mu;

	  Italy[id][Z1*100+Z2][interFlag][0] += Prob;
	  Italy[id][Z1*100+Z2][interFlag][1] += dE * Escl;
	  Italy[id][Z1*100+Z2][interFlag][2] += tK;
	  Italy[id][Z1*100+Z2][interFlag][3] += 1;
	}
      }
      
      if (PE[2][0]>0.0) {
	ctd->dv[id][0] += Prob;
	ctd->dv[id][1] += PP[2]->W2 * Prob;
	if (not NOCOOL) ctd->dv[id][2] += PP[2]->W2 * PE[2][1];
	if (scatter_check) {
	  double m1 = atomic_weights[0];
	  double m2 = atomic_weights[Z2];
	  double mu = m1 * m2 / (m1 + m2);
	  double tK = 0.0;
	  for (size_t k=0; k<3; k++) {
	    double vr = p1->dattrib[use_elec + k] - p2->vel[k];
	    tK += vr * vr;
	  }
	  tK *= 0.5*PP[2]->W1*PP[2]->q*mu;

	  Italy[id][Z1*100+Z2][interFlag][0] += Prob;
	  Italy[id][Z1*100+Z2][interFlag][1] += dE * Escl;
	  Italy[id][Z1*100+Z2][interFlag][2] += tK;
	  Italy[id][Z1*100+Z2][interFlag][3] += 1;
	}
      }
      
    } // END: compute this interaction [ok]

  } // END: interaction loop [hCross]

  // Deep debug
  //
  if (false) {
    std::cout << std::string(40, '-') << std::endl;
    for (auto I : hCross[id]) {
      int interFlag = std::get<0>(I.t);
      double XS     = I.crs;
      double Prob   = XS/totalXS;
      std::cout << std::setw(20) << std::left << interLabels[interFlag]
		<< std::setw(20) << std::left << Prob
		<< std::endl;
    }
    std::cout << std::string(40, '-') << std::endl;
  }

  // Update energy ratio diagnostic list for histogram
  //
  if (KE_initl_check>0.0) {
    double rat = (p1->dattrib[use_cons] + p2->dattrib[use_cons])/KE_initl_check + 1.0e-10;
    if (rat>0.0) {
      double lfc = 0.43429448190325176*log(rat);
      crsD[id].push_back(lfc);
    }
    std::get<0>(energyA[id])[0] =
      std::min<double>(std::get<0>(energyA[id])[0], rat);
    if (rat>std::get<0>(energyA[id])[1]) {
      std::get<0>(energyA[id])[1] = rat;
      std::get<0>(energyA[id])[2] = KE_initl_check;
      std::get<0>(energyA[id])[3] = p1->dattrib[use_cons] + p2->dattrib[use_cons];
      std::get<1>(energyA[id]) = maxC.second;
    }
  }
  
  // Convert to super particle (current in eV)
  //
  for (size_t cid=0; cid<3; cid++) {
    double N0 = PP[cid]->W2 * TreeDSMC::Munit / amu;
    PE[cid][1] *= N0;
  }

  // Convert energy loss from eV to system units
  //
  for (auto & v : PE) v[1] *= eV / TreeDSMC::Eunit;

  // Work vectors
  //
  std::vector<double> vrel(3), vcom(3), v1(3), v2(3);

  // Artifically prevent cooling by setting the energy removed from
  // the COM frame to zero
  //
  if (NOCOOL) {
    double Encl = 0.0;
    for (auto & v : PE) {
      Encl += v[1];
      v[1] = 0.0;
    }
    collD->addNoCool(Encl, id);
  }

  // Normalize probabilities and sum inelastic energy changes
  //
  double probTot = 0.0;
  for (auto & v : PE) probTot += v[0];

  if (probTot > 0.0) {
    for (auto & v : PE) v[0] /= probTot;
  }

  // Total energy change for all interactions
  //
  double totalDE = 0.0;
  for (auto v : PE) totalDE += v[1];

  //
  // Perform energy adjustment in ion, system COM frame with system
  // mass units
  //

  // Divide everything into three cases:
  // Ion(1)     and Electron(2)
  // Ion(2)     and Electron(1)
  // Neutral(1) and Neutral(2)

  if (use_normtest) {
    normTest(p1, "p1 [Before update]");
    normTest(p2, "p2 [Before update]");
  }

  unsigned short Jsav = 255;
  KE_ KE;
  double PPsav1 = 0.0, PPsav2 = 0.0;

  //
  // Perform the electronic interactions
  //
  if (use_elec) {
    //
    // Select interaction
    //
    double Pr = (*unit)();
    unsigned short J = 2;
    if (NO_ION_ION) {
      double tst = PE[1][0]/(PE[1][0]+PE[2][0]);
      if (Pr < tst) J = 1;
    } else if (NO_ION_ELECTRON) {
      J = 0;
    } else {
      if      (Pr < PE[0][0]) J = 0;
      else if (Pr < PE[1][0]) J = 1;
    }
    Jsav = J;


    // Parse for Coulombic interaction
    //
    if (maxInterFlag==ion_elec or maxInterFlag==ion_ion) {
    
      KE.Coulombic = true;

      double m1  = molP1[id]*amu;
      double m2  = molP2[id]*amu;
      double me  = atomic_weights[0]*amu;
  
      double mu0 = m1 * m2 / (m1 + m2);
      double mu1 = m1 * me / (m1 + me);
      double mu2 = me * m2 / (me + m2);

      double dT  = spTau[id] * TreeDSMC::Tunit;

      size_t nbods = c->bods.size();
      double  volc = c->Volume();
      double  dfac = TreeDSMC::Munit/amu/pow(TreeDSMC::Lunit, 3.0) *
	p1->mass/molP1[id];

      if (maxInterFlag == ion_elec) {
	if (maxI1.first == Interact::electron) {
	  double eVel = sqrt(2.0 * kEe2[id] * eV / mu2);
	  double afac = esu*esu/std::max<double>(2.0*kEe2[id]*eV, FloorEv*eV);
	  KE.Tau = ABrate[id][2]*afac*afac*eVel * dT;
	  double Cfrac  = nbods * nbods * dfac/ (PiProb[id][2] * volc) / spNsel[id];
	  
	  tauIon[id][0] += 1;
	  tauIon[id][1] += KE.Tau;
	  tauIon[id][2] += KE.Tau * KE.Tau;
	  tauIon[id][3] += Cfrac;
	} else {
	  double eVel = sqrt(2.0 * kEe1[id] * eV / mu1);
	  double afac = esu*esu/std::max<double>(2.0*kEe1[id]*eV, FloorEv*eV);
	  KE.Tau = ABrate[id][1]*afac*afac*eVel * dT;
	  double Cfrac  = nbods * nbods * dfac/ (PiProb[id][1] * volc) / spNsel[id];
	  
	  tauIon[id][0] += 1;
	  tauIon[id][1] += KE.Tau;
	  tauIon[id][2] += KE.Tau * KE.Tau;
	  tauIon[id][3] += Cfrac;
	}
      } else {
	double eVel = sqrt(2.0 * kEi[id] * eV / mu0);
	double afac = esu*esu/std::max<double>(2.0*kEi[id]*eV, FloorEv*eV);
	KE.Tau = ABrate[id][0]*afac*afac*eVel * dT;
	double Cfrac  = nbods * nbods * dfac/ (PiProb[id][0] * volc) / spNsel[id];
	  
	tauIon[id][0] += 1;
	tauIon[id][1] += KE.Tau;
	tauIon[id][2] += KE.Tau * KE.Tau;
	tauIon[id][3] += Cfrac;
      }
    }

    //
    // Apply neutral-neutral scattering and energy loss
    //
    if (J==0) {

      PP[0]->update();
      PP[0]->eUpdate();

      KEcheck(PP[0], KE_initl_check, ionExtra, rcbExtra);

      std::pair<double, double> KEinit = energyInPairPartial(p1, p2, Neutral);

      double ke1 = 0.0, ke2 = 0.0;

      for (int k=0; k<3; k++) {
				// Both particles are neutrals or ions
	v1[k]  = p1->vel[k];
	v2[k]  = p2->vel[k];

	ke1   += v1[k] * v1[k];
	ke2   += v2[k] * v2[k];
      }

      double DE1 = p1->dattrib[use_cons];
      double DE2 = p2->dattrib[use_cons];
      p1->dattrib[use_cons] = 0.0;
      p2->dattrib[use_cons] = 0.0;
      clrE[id] -= DE1 + DE2;
      PE[0][2]  = totalDE + DE1 + DE2;
      
      KE.delE0 = totalDE;
      KE.delE  = PE[0][2];
      
      collD->addLost(KE.delE0, 0.0, id);
      dfrE[id] += KE.delE0;	// TRACE
      
      scatterTrace(PP[0], KE, &v1, &v2, id);

      // Time-step computation
      //
      if (use_delt>=0) {	  
	spEdel[id] += KE.delE0;
	if (KE.delE0>0.0)
	  spEmax[id]  = std::min<double>(spEmax[id], KE.totE/KE.delE0);
      }
      
      if (KE_DEBUG) testCnt[id]++;

      if (scatter_check and maxInterFlag>=0) {
	TotlU[id][1][0]++;
      }
      
      for (int k=0; k<3; k++) {
	// Both particles are ions
	p1->vel[k] = v1[k];
	p2->vel[k] = v2[k];
      }
      
      updateEnergyTrace(PP[0], KE);
      
      testKE[id][3] += PE[0][1];
      testKE[id][4] += PE[0][1];
      
      if (KE_DEBUG) {

	double KE_final_check = energyInPair(p1, p2);
	
	std::pair<double, double> KEfinal;
	if (DBG_NewTest) 
	  KEfinal = energyInPairPartial(p1, p2, Neutral, "After neutral");
	else
	  KEfinal = energyInPairPartial(p1, p2, Neutral);
	
	double delE = KE_initl_check - KE_final_check
	  - (deltaSum += KE.delta)
	  - (delEsum  += KE.delE)
	  + (delEdfr  += KE.defer);
	
	delEloss += KE.delE0;
	
	std::pair<double, double> KEdif = KEinit - KEfinal;
	
	double actR = KEdif.first - KE.delta;
	actR = KEinit.first >0.0 ? actR/KEinit.first : actR;
	
	double pasR = KEdif.second;
	pasR = KEinit.second>0.0 ? pasR/KEinit.second : pasR;
	
	if (fabs(delE) > tolE*KE_initl_check) {
	  std::cout << "**ERROR [after neutral] dE = " << delE
		    << ", rel = "  << delE/KE_initl_check
		    << ", dKE = "  << deltaSum
		    << ", actR = " << actR
		    << ", pasR = " << pasR
		    << ", actA = " << KEdif.first
		    << ", pasA = " << KEdif.second
		    << std::endl;
	} else {
	  if (DBG_NewTest)
	    std::cout << "**GOOD [after neutral] dE = " << delE
		      << ", rel = "  << delE/KE_initl_check
		      << ", dKE = "  << KE.delta
		      << ", actR = " << actR
		      << ", pasR = " << pasR
		      << ", actA = " << KEdif.first
		      << ", pasA = " << KEdif.second
		      << std::endl;
	}
	
      } // END: KE debug
      
    } // END: PE[0] (Atom-atom interaction)

    //
    // Apply ion/neutral-electron scattering and energy loss
    // Ion is Particle 1, Electron is Particle 2
    //
    if (J==1) {

      PP[1]->update();
      PP[1]->eUpdate();

      KEcheck(PP[1], KE_initl_check, ionExtra, rcbExtra);

      std::pair<double, double> KEinit = energyInPairPartial(p1, p2, Ion1);

      double ke1i = 0.0, ke2i = 0.0;

      for (int k=0; k<3; k++) { // Particle 1 is the ion
	v1[k]  = p1->vel[k];
				// Particle 2 is the electron
	v2[k]  = p2->dattrib[use_elec+k];

	ke1i  += v1[k] * v1[k];
	ke2i  += v2[k] * v2[k];
      }
      
      double DE1 = p1->dattrib[use_cons];
      double DE2 = 0.0;

      p1->dattrib[use_cons] = 0.0;

      if (elc_cons) {
	DE2 = p2->dattrib[use_elec+3];
	p2->dattrib[use_elec+3] = 0.0;
      } else {
	DE2 = p2->dattrib[use_cons];
	p2->dattrib[use_cons] = 0.0;
      }
      
      clrE[id] -= DE1 + DE2;
      PE[1][2]  = totalDE + DE1 + DE2;
      
      KE.delE0 = totalDE;
      KE.delE  = PE[1][2];
      
      collD->addLost(KE.delE0, rcbExtra[0] - ionExtra[0], id);
      dfrE[id] += KE.delE0;	// TRACE
	
      scatterTrace(PP[1], KE, &v1, &v2, id);

      // Time-step computation
      //
      if (use_delt>=0) {	  
	spEdel[id] += KE.delE0;
	if (KE.delE0>0.0)
	  spEmax[id]  = std::min<double>(spEmax[id], KE.totE/KE.delE0);
      }
      
      if (KE_DEBUG) testCnt[id]++;
      
      if (scatter_check and maxInterFlag>=0) {
	TotlU[id][1][1]++;
      }
      
      for (int k=0; k<3; k++) {
	// Particle 1 is the ion
	p1->vel[k] = v1[k];

	// Particle 2 is the elctron
	p2->dattrib[use_elec+k] = v2[k];
      }
      
      updateEnergyTrace(PP[1], KE);
      
      testKE[id][3] += PE[1][1] - ionExtra[0] + rcbExtra[0];
      testKE[id][4] += PE[1][1];
      
      if (KE_DEBUG) {

	double ke1f = 0.0, ke2f = 0.0;

	for (int k=0; k<3; k++) {
	  ke1f += v1[k] * v1[k];
	  ke2f += v2[k] * v2[k];
	}
	
	// Particle 2 electron
	// -------------------
	//            initial/orig----------------------------------+
	//            initial/swap--------------+                   |
	//                                      |                   |
	//                                      v                   v
	double eta2i = PP[1]->swap ? PP[1]->beg[0].eta : PP[1]->beg[1].eta;
	double eta2f = PP[1]->swap ? PP[1]->end[0].eta : PP[1]->end[1].eta;
	//                                      ^                   ^
	//                                      |                   |
	//            final/swap----------------+                   |
	//            final/orig------------------------------------+

	ke1i *= 0.5*p1->mass;
	ke1f *= 0.5*p1->mass;

	ke2i *= 0.5*p2->mass * eta2i * atomic_weights[0]/molP2[id];
	ke2f *= 0.5*p2->mass * eta2f * atomic_weights[0]/molP2[id];
	
	double delE = ke1i + ke2i - ke1f - ke2f - KE.delta - KE.delE + KE.defer;
	if (fabs(delE) > tolE*(ke1i + ke2i)) {
	  std::cout << "**ERROR post scatter [1]: relE = " << delE/(ke1i + ke2i)
		    << " del = "  << delE
		    << " dKE = "  << KE.delta
		    << " delE = " << KE.delE
		    << " miss = " << KE.miss
		    << " dfr = "  << KE.defer
		    << " KEi = "  << ke1i + ke2i
		    << " KEf = "  << ke1f + ke2f
		    << " et2i = " << eta2i << std::endl;
	} else {
	  if (DBG_NewTest)
	    std::cout << "**GOOD post scatter [1]: relE = " << delE/(ke1i + ke2i)
		      << std::scientific << std::setprecision(14)
		      << " del = "  << std::setw(14) << delE
		      << std::endl << std::setprecision(5);
	}
	
	double KE_final_check = energyInPair(p1, p2);

	std::pair<double, double> KEfinal;
	if (DBG_NewTest) 
	  KEfinal = energyInPairPartial(p1, p2, Ion1, "After Ion1");
	else
	  KEfinal = energyInPairPartial(p1, p2, Ion1);
      
	delE = KE_initl_check - KE_final_check
	  - (deltaSum += KE.delta)
	  - (delEsum  += KE.delE - ionExtra[0] + rcbExtra[0])
	  + (delEdfr  += KE.defer);
      
	delEfnl  += rcbExtra[0] - ionExtra[0];
	delEloss += KE.delE0;
	
	std::pair<double, double> KEdif = KEinit - KEfinal;
	
	double actR = KEdif.first - KE.delta;
	actR = KEinit.first>0.0 ? actR/KEinit.first : actR;

	double pasR = KEdif.second;
	pasR = KEinit.second>0.0 ? pasR/KEinit.second : pasR;

	if (fabs(delE) > tolE*KE_initl_check) {
	  std::cout << "**ERROR [after Ion1] dE = " << delE
		    << ", rel = "  << delE/KE_initl_check
		    << ", dKE = "  << deltaSum
		    << ", actR = " << actR
		    << ", pasR = " << pasR
		    << ", actA = " << KEdif.first
		    << ", pasA = " << KEdif.second
		    << std::endl;
	} else {
	  if (DBG_NewTest)
	    std::cout << "**GOOD [after Ion1] dE = " << delE
		      << ", rel = "  << delE/KE_initl_check
		      << ", dKE = "  << deltaSum
		      << ", actR = " << actR
		      << ", pasR = " << pasR
		      << ", actA = " << KEdif.first
		      << ", pasA = " << KEdif.second
		      << std::endl;
	}
      } // END: KE debug
    
      // Secondary electron-ion scattering
      //
      for (unsigned n=0; n<SECONDARY_SCATTER; n++) secondaryScatter(p1);
      
    } // END: PE[1] (Ion-electron interaction)

    //
    // Apply ion/neutral-electron scattering and energy loss
    // Ion is Particle 2, Electron is Particle 1
    //
    if (J==2) {

      PP[2]->update();
      PP[2]->eUpdate();

      KEcheck(PP[2], KE_initl_check, ionExtra, rcbExtra);

      std::pair<double, double> KEinit = energyInPairPartial(p1, p2, Ion2);

      double ke1i = 0.0, ke2i = 0.0;
      for (int k=0; k<3; k++) {	// Particle 1 is the elctron
	v1[k]  = p1->dattrib[use_elec+k];
				// Particle 2 is the ion
	v2[k]  = p2->vel[k];
	
	ke1i  += v1[k] * v1[k];
	ke2i  += v2[k] * v2[k];
      }

      double DE1 = 0.0;
      double DE2 = p2->dattrib[use_cons];

      p2->dattrib[use_cons] = 0.0;

      if (elc_cons) {
	DE1 = p1->dattrib[use_elec+3];
	p1->dattrib[use_elec+3] = 0.0;
      } else {
	DE1 = p1->dattrib[use_cons];
	p1->dattrib[use_cons] = 0.0;
      }
      
      clrE[id] -= DE1 + DE2;
      PE[2][2]  = totalDE + DE1 + DE2;
      
      KE.delE0 = totalDE;
      KE.delE  = PE[2][2];
      
      collD->addLost(KE.delE0, rcbExtra[1] - ionExtra[1], id);
      dfrE[id] += KE.delE0;	// TRACE
      
      scatterTrace(PP[2], KE, &v1, &v2, id);

      // Time-step computation
      //
      if (use_delt>=0) {	  
	spEdel[id] += KE.delE0;
	if (KE.delE0 > 0.0)
	  spEmax[id]  = std::min<double>(spEmax[id], KE.kE/KE.delE0);
      }
      
      if (KE_DEBUG) testCnt[id]++;
	
      if (scatter_check and maxInterFlag>=0) {
	TotlU[id][1][2]++;
      }
	
      for (int k=0; k<3; k++) {
				// Particle 1 is the electron
	p1->dattrib[use_elec+k] = v1[k];
				// Particle 2 is the ion
	p2->vel[k] = v2[k];
      }
      
      updateEnergyTrace(PP[2], KE);

      testKE[id][3] += PE[2][1] - ionExtra[1] + rcbExtra[1];
      testKE[id][4] += PE[2][1];

      if (KE_DEBUG) {

	double ke1f = 0.0, ke2f = 0.0;
	for (int k=0; k<3; k++) {
	  ke1f += v1[k] * v1[k];
	  ke2f += v2[k] * v2[k];
	}
	  
	double ke1F = ke1f, ke2F = ke2f;

	// Particle 1 electron
	// -------------------
	//            initial/orig----------------------------------+
	//            initial/swap--------------+                   |
	//                                      |                   |
	//                                      v                   v
	double eta1i = PP[2]->swap ? PP[2]->beg[1].eta : PP[2]->beg[0].eta;
	double eta1f = PP[2]->swap ? PP[2]->end[1].eta : PP[2]->end[0].eta;
	//                                      ^                   ^
	//                                      |                   |
	//            final/swap----------------+                   |
	//            final/orig------------------------------------+

	ke1i *= 0.5*p1->mass * eta1i * atomic_weights[0]/molP1[id];
	ke1f *= 0.5*p1->mass * eta1f * atomic_weights[0]/molP1[id];

	ke2i *= 0.5*p2->mass;
	ke2f *= 0.5*p2->mass;

	double delE = ke1i + ke2i - ke1f - ke2f - KE.delta - KE.delE + KE.defer;

	if (fabs(delE) > tolE*(ke1i + ke2i)) {
	  std::cout << "**ERROR post scatter [2]: relE = " << delE/(ke1i+ke2i)
		    << " del = "  << delE
		    << " dKE = "  << KE.delta
		    << " dfr = "  << KE.defer
		    << " KEi = "  << ke1i + ke2i
		    << " KEf = "  << ke1f + ke2f
		    << " k1f = "  << ke1F
		    << " k2f = "  << ke2F
		    << " et1i = " << eta1i << std::endl;
	} else {
	  if (DBG_NewTest)
	    std::cout << "**GOOD post scatter [2]: relE = " << delE/(ke1i + ke2i)
		      << std::scientific << std::setprecision(14)
		      << " del = "  << std::setw(14) << delE
		      << std::endl << std::setprecision(5);
	}
	
	double KE_final_check = energyInPair(p1, p2);

	std::pair<double, double> KEfinal;
	if (DBG_NewTest) 
	  KEfinal = energyInPairPartial(p1, p2, Ion2, "After Ion2");
	else
	  KEfinal = energyInPairPartial(p1, p2, Ion2);

	delE = KE_initl_check - KE_final_check
	  - (deltaSum += KE.delta)
	  - (delEsum  += KE.delE - ionExtra[1] + rcbExtra[1])
	  + (delEdfr  += KE.defer);

	delEfnl  += rcbExtra[1] - ionExtra[1];
	delEloss += KE.delE0;

	std::pair<double, double> KEdif = KEinit - KEfinal;
	
	double actR = KEdif.first - KE.delta;
	actR = KEinit.first>0.0 ? actR/KEinit.first : actR;

	double pasR = KEdif.second;
	pasR = KEinit.second>0.0 ? pasR/KEinit.second : pasR;

	if (fabs(delE) > tolE*KE_initl_check) {
	  std::cout << "**ERROR [after Ion2] dE = " << delE
		    << ", rel = "  << delE/KE_initl_check
		    << ", dKE = "  << deltaSum
		    << ", actR = " << actR
		    << ", pasR = " << pasR
		    << ", actA = " << KEdif.first
		    << ", pasA = " << KEdif.second
		    << std::endl;
	} else {
	  if (DBG_NewTest)
	    std::cout << "**GOOD [after Ion2] dE = " << delE
		      << ", rel = "  << delE/KE_initl_check
		      << ", dKE = "  << deltaSum
		      << ", actR = " << actR
		      << ", pasR = " << pasR
		      << ", actA = " << KEdif.first
		      << ", pasA = " << KEdif.second
		      << std::endl;
	}

      } // END: KE debug
      
      // Secondary electron-ion scattering
      //
      for (unsigned n=0; n<SECONDARY_SCATTER; n++) secondaryScatter(p2);

    } // END: Electron-Ion interaction

    // Scatter tally for debugging
    //
    if (scatter_check) {
      for (size_t k=0; k<3; k++) TotlD[id][1][k] += PE[k][0];
    }

  } // END: interactions with atoms AND electrons

  // Diagnostic event counting
  //
  if (p1->dattrib[use_cons] > 0.0 or p2->dattrib[use_cons] > 0.0)
    {
      Nmis[id] ++;
    }
  Ncol[id] ++;

  // Update energy conservation
  //
  double EconsUpI = 0.0, EconsUpE = 0.0;
  for (size_t k=0; k<3; k++) {
    if (use_cons>=0) {
      PP[k]->p1->dattrib[use_cons] += PP[k]->E1[0];
      PP[k]->p2->dattrib[use_cons] += PP[k]->E2[0];
      EconsUpI += PP[k]->E1[0] + PP[k]->E2[0];
      updE[id] += PP[k]->E1[0] + PP[k]->E2[0];
    }
    if (use_elec>=0 and elc_cons) {
      PP[k]->p1->dattrib[use_elec+3] += PP[k]->E1[1];
      PP[k]->p2->dattrib[use_elec+3] += PP[k]->E2[1];
      EconsUpE += PP[k]->E1[1] + PP[k]->E2[1];
      updE[id] += PP[k]->E1[1] + PP[k]->E2[1];
    } else if (use_cons>=0) {
      PP[k]->p1->dattrib[use_cons  ] += PP[k]->E1[1];
      PP[k]->p2->dattrib[use_cons  ] += PP[k]->E2[1];
      EconsUpE += PP[k]->E1[1] + PP[k]->E2[1];
      updE[id] += PP[k]->E1[1] + PP[k]->E2[1];
    }
  }

  if (scatter_check) {
    if (PE[0][0]>0.0) {
      double vi = 0.0;
      for (size_t k=0; k<3; k++) {
	double vrel = p1->vel[k] - p2->vel[k];
	vi += vrel* vrel;
      }

      double m1 = molP1[id];
      double m2 = molP2[id];
      double mu = m1 * m2/(m1 + m2);
      double kE = 0.5*W1*PP[0]->q*mu*vi;

      Ediag[id][0] += PE[0][2];
      Ediag[id][1] += kE * PE[0][0];
      Ediag[id][2] += PE[0][0];
    }

    if (PE[1][0]>0.0) {
      double vi = 0.0;
      for (size_t k=0; k<3; k++) {
	double vrel = p1->vel[k] - p2->dattrib[use_elec+k];
	vi += vrel* vrel;
      }

      double m1 = molP1[id];
      double m2 = atomic_weights[0];
      double mu = m1 * m2/(m1 + m2);
      double kE = 0.5*W1*PP[1]->q*mu*vi;

      Ediag[id][0] += PE[1][2];
      Ediag[id][1] += kE * PE[1][0];
      Ediag[id][2] += PE[1][0];
    }

    if (PE[2][0]>0.0) {
      double vi = 0.0;
      for (size_t k=0; k<3; k++) {
	double vrel = p2->vel[k] - p1->dattrib[use_elec+k];
	vi += vrel* vrel;
      }

      double m1 = atomic_weights[0];
      double m2 = molP2[id];
      double mu = m1 * m2/(m1 + m2);
      double kE = 0.5*W1*PP[2]->q*mu*vi;

      Ediag[id][0] += PE[2][2];
      Ediag[id][1] += kE * PE[2][0];
      Ediag[id][2] += PE[2][0];
    }

  }

  if (use_normtest) {
    normTest(p1, "p1 [After]");
    normTest(p2, "p2 [After]");
  }

  // Photoionizing background?
  //
  if (use_photoIB) {
    // Sanity check: check probability on first step
    // ---------------------------------------------
    // Time step is decreased automatically, if possible.  Otherwise,
    // exception is thrown.
    //
    static bool first = true;
    if (first) {
      const double trgProb = 0.05;
      const double maxProb = 0.2;

      double maxSoFar = 0.0;
      bool   good     = true;

      std::ostringstream sout;
      for (auto s : SpList) {
	double Pr = ch.IonList[s.first]->photoIonizationRate().first * 
	  TreeDSMC::Tunit * spTau[id];
	maxSoFar = std::max<double>(Pr, maxSoFar);
	if (Pr>maxProb) good = false;
	sout << "["
	     << std::setw(2)  << s.first.first  << ", "
	     << std::setw(2)  << s.first.second << "] "
	     << std::setw(16) << Pr << std::endl;
      }

      if (not good) {		// Trigger time-step reduction
	if (use_delt>=0 and maxSoFar<0.99) {
	  double newTs = trgProb/maxProb * spTau[id];
	  p1->dattrib[use_delt] = p2->dattrib[use_delt] = newTs;
	  if (myid==0)
	    std::cout << std::setw(70) << std::setfill('-') << '-' << std::endl
		      << std::setw(70) << "-----photoIB: WARNING"  << std::endl
		      << std::setw(70) << std::setfill('-') << '-' << std::endl
		      << sout.str() << std::setw(70) << '-'        << std::endl
		      << std::setfill(' ');
	} else {		// Otherwise, throw exception
	  std::ostringstream serr;
	  serr << "CollideIon::inelasticTrace: maxProb=" << maxSoFar
	       << ", time step too small for photoionization";
	  if (myid==0) std::cout << serr.str() << std::endl << sout.str();
	  throw std::runtime_error(serr.str());
	}
      } else {
	if (myid==0)
	  std::cout << std::setw(70) << std::setfill('-') << '-' << std::endl
		    << std::setw(70) << "-----photoIB values"    << std::endl
		    << std::setw(70) << std::setfill('-') << '-' << std::endl
		    << sout.str() << std::setw(70) << '-'        << std::endl
		    << std::setfill(' ');
      }
      first = false;
    }
    //
    // end sanity check
  }

  // Debug energy conservation
  // -------------------------
  // After the step, the energy of a single pair may have changed in
  // the following ways:
  // + Energy lost to the internal state or radiated
  // + Energy change owing to change in ionization state
  // + Deferred energy applied to the center-of-mass interaction
  //
  if (KE_DEBUG) {
    double KE_final_check = energyInPair(p1, p2);
    std::array<double, 2> KE_final_econs = {0.0, 0.0};

    if (use_cons>=0)
      KE_final_econs[0] += p1->dattrib[use_cons  ] + p2->dattrib[use_cons  ];

    if (use_elec>=0 and elc_cons)
      KE_final_econs[1] += p1->dattrib[use_elec+3] + p2->dattrib[use_elec+3];

    std::array<double, 2> dCons = KE_initl_econs - KE_final_econs;
    double dConSum = dCons[0] + dCons[1];

    double KEinitl = KE_initl_check;
    double KEfinal = KE_final_check;
    double delE1   = KEinitl - KEfinal - deltaSum - delEsum + delEmis + delEdfr;
    double delE2   = KEinitl - KEfinal - deltaSum - dConSum - delEsum - PPsav1 - PPsav2 - EconsUpI - EconsUpE;
    double delE3   = KEinitl - KEfinal - dConSum - totalDE - delEfnl;

    if (fabs(delE3) > tolE*KE_initl_check) {
      std::cout << "**ERROR inelasticTrace dE = " << delE1 << ", " << delE2
		<< ", rel1 = " << delE1/KE_initl_check
		<< ", rel2 = " << delE2/KE_initl_check
		<< ", rel3 = " << delE3/KE_initl_check
		<< ",  dKE = " << KEinitl - KEfinal
		<< ", dCn1 = " << dCons[0]
		<< ", dCn2 = " << dCons[1]
		<< ", PPs1 = " << PPsav1
		<< ", PPs2 = " << PPsav2
		<< ", EcnI = " << EconsUpI
		<< ", EcnE = " << EconsUpE
		<< ", Elos = " << delEloss
		<< ", delS = " << deltaSum
		<< ", Esum = " << delEsum
		<< ", miss = " << delEmis
		<< ", defr = " << delEdfr
		<< ", clrE = " << clrE[id]
		<< ", q = "    << PP[Jsav]->q
		<< ", Jsav = " << Jsav
		<< std::endl;
    } else {
      if (DBG_NewTest)
	std::cout << "**GOOD inelasticTrace dE = " << delE1 << ", " << delE2
		  << ", rel1 = " << delE1/KE_initl_check
		  << ", rel2 = " << delE2/KE_initl_check
		  << ",  dKE = " << KEinitl - KEfinal
		  << ", dCn1 = " << dCons[0]
		  << ", dCn2 = " << dCons[1]
		  << ", PPs1 = " << PPsav1
		  << ", PPs2 = " << PPsav2
		  << ", EcnI = " << EconsUpI
		  << ", EcnE = " << EconsUpE
		  << ", Elos = " << delEloss
		  << ", Efnl = " << delEfnl
		  << ", delS = " << deltaSum
		  << ", Esum = " << delEsum
		  << ", miss = " << delEmis
		  << ", defr = " << delEdfr
		  << ", clrE = " << clrE[id]
		  << ", Jsav = " << Jsav
		  << std::endl;
    }    
  }

  if (use_photoIB and photoIBType == perCollision) {

    std::map<void *, double> dT;
    for (auto p : {_p1, _p2}) dT[p] = tnow - p->dattrib[use_photon];

    // Photoionize all subspecies
    //
    for (auto s : SpList) {
      lQ Q    = s.first;
      int pos = s.second;
      
      for (auto p : {_p1, _p2}) {
	// Skip updated particles
	if (dT[p]<=0.0) continue;
	
	// Pick a new photon for each particle
	CFreturn ff  = ch.IonList[Q]->photoIonizationRate();
	  
	if (ff.first>0.0) {
	  // Increment total count
	  photoStat[id][Q][0]++;

	  // Compute the probability and get the residual electron energy
	  double Pr = ff.first * TreeDSMC::Tunit * dT[p];
	  double Ep = ff.second;
	  double ww = p->dattrib[pos] * Pr;
	  double Gm = TreeDSMC::Munit/(amu*atomic_weights[Q.first]);

	  
	  if (Pr >= 1.0) {	// Limiting case
	    ww = p->dattrib[pos];
	    p->dattrib[pos  ]  = 0.0;
	    p->dattrib[pos+1] += ww;
	    // Increment oab count and mean value
	    photoStat[id][Q][1] += 1;
	    photoStat[id][Q][2] += Pr;
	  } else {		// Normal case
	    p->dattrib[pos  ] -= ww;
	    p->dattrib[pos+1] += ww;
	  }
	  photoStat[id][Q][3]  = std::max<double>(photoStat[id][Q][3], Pr);
	  photoW[id][s.first] += ww;
	  photoN[id][s.first] += ww * p->mass * Gm;
	
	  scatterPhotoTrace(p, Q, ww, Ep);
	}

	p->dattrib[use_photon] = tnow;
      }
    }

  } // End: photoionizing background


  return ret;

} // END: inelasticTrace


void CollideIon::scatterTrace
(PordPtr pp, KE_& KE, std::vector<double>* V1, std::vector<double>* V2, int id)
{
  if (NO_HSCAT) return;

  if (MeanMass) {
    scatterTraceMM(pp, KE, V1, V2, id);
    return;
  }

  // Make v1 correspond to the primary, W1>W2
  //
  std::vector<double>* v1 = V1;
  std::vector<double>* v2 = V2;

  if (pp->swap) zswap(v1, v2);

  // For energy conservation debugging
  //
  if (KE_DEBUG) {
    KE.i(1) = KE.i(2) = 0.0;
    for (auto v : *v1) KE.i(1) += v*v;
    for (auto v : *v2) KE.i(2) += v*v;
  }
  
  // Reset bit flags
  //
  KE.bs.reset();

  // Total effective mass in the collision (atomic mass units)
  //
  double mt = pp->m1 + pp->m2;

  // Reduced mass (atomic mass units)
  //
  double mu = pp->m1 * pp->m2 / mt;

  // Set COM frame
  //
  std::vector<double> vcom(3), vrel(3);
  double vi = 0.0;

  for (size_t k=0; k<3; k++) {
    vcom[k] = (pp->m1*(*v1)[k] + pp->m2*(*v2)[k])/mt;
    vrel[k] = (*v1)[k] - (*v2)[k];
    vi += vrel[k] * vrel[k];
  }
				// Energy in COM
  double kE = 0.5*pp->W2*mu*vi;
				// Energy reduced by loss
  double totE = kE - KE.delE;

  // KE is positive
  //
  if (kE>0.0) {
    // More loss energy requested than available?
    //
    if (totE < 0.0) {
      KE.miss = -totE;
      // Add to energy bucket for these particles
      //
      deferredEnergyTrace(pp, -totE, id);
      KE.delE += totE;
      totE = 0.0;
    }
    // Update the outgoing energy in COM
    //
    KE.vfac = sqrt(totE/kE);
    KE.kE   = kE;
    KE.totE = totE;
    KE.bs.set(KE_Flags::Vfac);
    KE.bs.set(KE_Flags::KEpos);
  }
  // KE is zero (limiting case)
  //
  else {
    KE.vfac = 1.0;
    KE.kE   = kE;
    KE.totE = totE;

    if (KE.delE>0.0) {
      KE.miss = KE.delE;
      // Defer all energy loss
      //
      deferredEnergyTrace(pp, KE.delE, id);
      KE.delE = 0.0;
    } else {
      // Apply delE to COM
      //
      vi = -2.0*KE.delE/(pp->W1*mu);
    }
  }

  // Assign interaction energy variables
  //
  if (KE.Coulombic)
    vrel = coulomb_vector(vrel, pp->W1, pp->W2, KE.Tau);
  else
    vrel = unit_vector();
  
  vi   = sqrt(vi);
  for (auto & v : vrel) v *= vi;
  //                         ^
  //                         |
  // Velocity in center of mass, computed from v1, v2 and adjusted
  // according to the inelastic energy loss
  //

  KE.delta = 0.0;

  if (ExactE) {

    // BEGIN: energy conservation algorithm

    KE.bs.set(KE_Flags::ExQ);

    std::vector<double> uu(3), vv(3), w1(3, 0.0);
    double v1i2 = 0.0, b1f2 = 0.0;
    double qT   = 0.0, vrat = 1.0, q = pp->q, cq = 1.0 - pp->q;
    double udif = 0.0, gamm = 0.0, vcm2 = 0.0;
    bool  algok = false;

    // For deep checking/debugging
    //
    if (DBG_HSCAT) {
      static bool first = true;
      static double m1T, m2T, W1T, W2T;
      if (pp->m1 < 1 or pp->m2 < 1) {
	if (first) {
	  m1T = pp->m1;
	  m2T = pp->m2;
	  W1T = pp->W1;
	  W2T = pp->W2;
	  first = false;
	  std::cout << "INITIAL [" << std::setw(3) << myid << "]:"
		    << "  m1=" << m1T << " , " << pp->m1 << " : " << m1T - pp->m1
		    << "  m2=" << m2T << " , " << pp->m2 << " : " << m2T - pp->m2
		    << "  W1=" << W1T << " , " << pp->W1 << " : " << W1T - pp->W1
		    << "  W2=" << W2T << " , " << pp->W2 << " : " << W1T - pp->W2
		    << "  KE.vfac=" << KE.vfac << "  vrat=" << vrat
		    << std::endl;
	} else {
	  bool err = false;
	  if (fabs(m1T - pp->m1) > 1.0e-12) err = true;
	  if (fabs(m2T - pp->m2) > 1.0e-12) err = true;
	  if (fabs(W1T - pp->W1) > 1.0e-12) err = true;
	  if (fabs(W2T - pp->W2) > 1.0e-12) err = true;
	  if (fabs(KE.vfac -1.0) > 1.0e-12) err = true;
	  if (fabs(vrat    -1.0) > 1.0e-12) err = true;
	  if (err) {
	    std::cout << "MISMATCH [" << std::setw(3) << myid << "]:"
		      << "  m1=" << m1T << " , " << pp->m1 << " : " << m1T - pp->m1
		      << "  m2=" << m2T << " , " << pp->m2 << " : " << m2T - pp->m2
		      << "  W1=" << W1T << " , " << pp->W1 << " : " << W1T - pp->W1
		      << "  W2=" << W2T << " , " << pp->W2 << " : " << W1T - pp->W2
		      << "  KE.vfac=" << KE.vfac << "  vrat=" << vrat
		      << std::endl;
	  }
	}
      }
    }

    if (cq > 0.0 and q < 1.0) {

      for (size_t i=0; i<3; i++) {
	uu[i] = vcom[i] + pp->m2/mt*vrel[i]*KE.vfac;
	vv[i] = vcom[i] - pp->m1/mt*vrel[i]*KE.vfac;
	vcm2 += vcom[i] * vcom[i];
	v1i2 += (*v1)[i] * (*v1)[i];
	b1f2 += uu[i] * uu[i];
	qT   += (*v1)[i] * uu[i];
	udif += ((*v1)[i] - uu[i]) * ((*v1)[i] - uu[i]);
      }
      
      // Alternative "orthogonal" energy algorithm
      //
      if (AlgOrth) {

	KE.bs.set(KE_Flags::AlgO);

	// Cross product to determine orthgonal direction
	//
	w1 = uu ^ (*v1);

	// Normalize
	//
	double wnrm = 0.0;
	for (auto v : w1) wnrm += v*v;
	
	// Version with checks for tiny norm
	//
	if (false) {

	  const double tol = 1.0e-12;

	  // Generate random vector if |u|~0 or |v1|~0
	  if (v1i2 < tol*b1f2 or b1f2 < tol*v1i2) {
	    for (auto & v : w1) v = (*norm)();
	  }
	  
	  // Choose random orthogonal vector if uu || v1
	  else if (wnrm < tol*v1i2) {
	    auto t3 = zorder(*v1);
	    int i0 = std::get<0>(t3), i1 = std::get<1>(t3), i2 = std::get<2>(t3);
	    w1[i0] = (*norm)();
	    w1[i1] = (*norm)();
	    w1[i2] = -(w1[i0]*(*v1)[i0] + w1[i1]*(*v1)[i1])/(*v1)[i2];
	    wnrm = 0.0; for (auto v : w1) wnrm += v*v;
	  }

	  // Sanity check on norm |w|
	  if (wnrm > tol*sqrt(vcm2)) {
	    for (auto & v : w1) v *= 1.0/sqrt(wnrm);
	    gamm = sqrt(q*(1.0 - q)*udif);
	    algok = true;
	  }

	} else {		// No norm checks . . . 
	  wnrm = 1.0/sqrt(wnrm);
	  if ((*unit)()<0.5) wnrm *= -1.0;
	  if (not std::isinf(wnrm)) {
	    for (auto & v : w1) v *= wnrm;
	    gamm = sqrt(q*(1.0 - q)*udif);
	    algok = true;
	  }
	}
      }

      // Standard "parallel" energy algorithm
      //
      if (!AlgOrth or !algok) {
      
	if (v1i2 > 0.0 and b1f2 > 0.0) qT *= q/v1i2;
      
	vrat = 
	  ( -qT + std::copysign(1.0, qT)*sqrt(qT*qT + cq*(q*b1f2/v1i2 + 1.0)) )/cq;
      }

      if (std::isnan(vrat)) {
	std::cout << "Vrat problem" << std::endl;
      }
    }

    // Assign new velocities
    //
    for (int i=0; i<3; i++) {
      double v0 = vcom[i] + pp->m2/mt*vrel[i]*KE.vfac;
    
      (*v1)[i] = cq*(*v1)[i]*vrat + q*v0 + w1[i]*gamm;
      (*v2)[i] = vcom[i] - pp->m1/mt*vrel[i]*KE.vfac;
    }
    
    // END: energy conservation algorithm
  } else {

    // BEGIN: momentum conservation algorithm

    KE.bs.set(KE_Flags::momC);

    for (size_t k=0; k<3; k++) {
      (*v1)[k] = vcom[k] + pp->m2/mt*vrel[k] * KE.vfac;
      (*v2)[k] = vcom[k] - pp->m1/mt*vrel[k] * KE.vfac;
    }
    
    // END: momentum conservation algorithm
  }

  misE[id] += KE.miss;

  // Temporary deep debug
  //
  if (KE_DEBUG) {

    double M1 = 0.5 * pp->W1 * pp->m1;
    double M2 = 0.5 * pp->W2 * pp->m2;

    // Initial KE
    //
    double KE1i = M1 * KE.i(1);
    double KE2i = M2 * KE.i(2);

    double KE1f = 0.0, KE2f = 0.0;
    for (auto v : *v1) KE1f += v*v;
    for (auto v : *v2) KE2f += v*v;

    // Final KE
    //
    KE1f *= M1;
    KE2f *= M2;
      
    // KE differences
    //
    double KEi   = KE1i + KE2i;
    double KEf   = KE1f + KE2f;
    double delEt = KEi  - KEf - KE.delta - std::min<double>(kE, KE.delE);
    
    // Sanity test
    //
    double Ii1 = 0.0, Ii2 = 0.0, Ie1 = 0.0, Ie2 = 0.0;
    for (size_t k=0; k<3; k++) {
      Ii1 += pp->p1->vel[k] * pp->p1->vel[k];
      Ii2 += pp->p2->vel[k] * pp->p2->vel[k];
      if (use_elec>=0) {
	size_t K = use_elec + k;
	Ie1 += pp->p1->dattrib[K] * pp->p1->dattrib[K];
	Ie2 += pp->p2->dattrib[K] * pp->p2->dattrib[K];
      }
    }

    Ii1 *= 0.5 * pp->p1->mass;
    Ii2 *= 0.5 * pp->p2->mass;
    Ie1 *= 0.5 * pp->p1->mass * pp->eta1 * atomic_weights[0]/molP1[id];
    Ie2 *= 0.5 * pp->p2->mass * pp->eta2 * atomic_weights[0]/molP2[id];

    double Fi1 = Ii1, Fi2 = Ii2, Fe1 = Ie1, Fe2 = Ie2;

    if (pp->P == Pord::ion_ion) {
      Fi1 = 0.0; for (auto v : *v1) Fi1 += v*v;
      Fi2 = 0.0; for (auto v : *v2) Fi2 += v*v;
      Fi1 *= 0.5 * pp->p1->mass;
      Fi2 *= 0.5 * pp->p2->mass;
    }

    if (pp->P == Pord::ion_electron) {
      Fi1 = 0.0; for (auto v : *v1) Fi1 += v*v;
      Fe2 = 0.0; for (auto v : *v2) Fe2 += v*v;
      Fi1 *= 0.5 * pp->p1->mass;
      Fe2 *= 0.5 * pp->p2->mass * pp->eta2 * atomic_weights[0]/molP2[id];
    }

    if (pp->P == Pord::electron_ion) {
      Fe1 = 0.0; for (auto v : *v1) Fe1 += v*v;
      Fi2 = 0.0; for (auto v : *v2) Fi2 += v*v;
      Fe1 *= 0.5 * pp->p1->mass * pp->eta1 * atomic_weights[0]/molP1[id];
      Fi2 *= 0.5 * pp->p2->mass;
    }

    if ( fabs(delEt)/std::min<double>(KEi, KEf) > tolE) {
      std::cout << "**ERROR scatter: delEt = " << delEt
		<< " rel = "  << delEt/KEi
		<< " KEi = "  << KEi
		<< " KEf = "  << KEf
		<< " dif = "  << KEi - KEf
		<< "  kE = "  << kE
		<< "  dE = "  << KE.delE
		<< " dvf = "  << KE.delE/kE
		<< " tot = "  << totE
		<< " vfac = " << KE.vfac
		<< "   w1 = " << pp->w1
		<< "   w2 = " << pp->w2
		<< "   W1 = " << pp->W1
		<< "   W2 = " << pp->W2
		<< " flg = " << KE.decode()
		<< std::endl;
    } else {
      if (DBG_NewTest)
	std::cout << "**GOOD scatter: delEt = "
		  << std::setprecision(14) << std::scientific
		  << std::setw(22) << delEt
		  << " rel = "  << std::setw(22) << delEt/KEi << " kE = " << std::setw(22) << kE
		  << "   W1 = " << std::setw(22) << pp->W1
		  << "   W2 = " << std::setw(22) << pp->W2
		  << "  Ei1 = " << std::setw(22) << KE1i
		  << "  Ef1 = " << std::setw(22) << KE1f
		  << "  Ii1 = " << std::setw(22) << Ii1
		  << "  Ie1 = " << std::setw(22) << Ie1
		  << "  Ei2 = " << std::setw(22) << KE2i
		  << "  Ef2 = " << std::setw(22) << KE2f
		  << "  Ii2 = " << std::setw(22) << Ii2
		  << "  Ie2 = " << std::setw(22) << Ie2
		  << " delE = " << std::setw(22) << KE.delE
		  << std::setprecision(5)  << std::endl;
    }

  } // END: temporary deep debug

  // Sanity check
  if (pp->W2 > pp->W1) {
    std::cout << "Backwards: w1=" << pp->w1
	      << " w2=" << pp->w2
	      << " W1=" << pp->W1
	      << " W2=" << pp->W2
	      << " m1=" << pp->m1
	      << " m2=" << pp->m2
	      << " M1=" << pp->p1->mass
	      << " M2=" << pp->p2->mass
	      << std::endl;
  }
      
} // END: CollideIon::scatterTrace


void CollideIon::scatterTraceMM
(PordPtr pp, KE_& KE, std::vector<double>* v1, std::vector<double>* v2, int id)
{
  // Swap velocities?
  //
  if (pp->swap) zswap(v1, v2);

  // For energy conservation debugging
  //
  if (KE_DEBUG) {
    KE.i(1) = KE.i(2) = 0.0;
    for (auto v : *v1) KE.i(1) += v*v;
    for (auto v : *v2) KE.i(2) += v*v;
  }
  
  // Reset bit flags
  //
  KE.bs.reset();

  // Particle masses
  //
  double m1 = pp->m1;
  double m2 = pp->m2;

  if (m1<1.0) m1 *= pp->eta1;
  if (m2<1.0) m2 *= pp->eta2;

  // Total effective mass in the collision
  //
  double mt = m1 + m2;

  // Reduced mass (atomic mass units)
  //
  double mu = m1 * m2 / mt;

  // Set COM frame
  //
  std::vector<double> vcom(3), vrel(3);
  double vi = 0.0;

  for (size_t k=0; k<3; k++) {
    vcom[k] = (m1*(*v1)[k] + m2*(*v2)[k])/mt;
    vrel[k] = (*v1)[k] - (*v2)[k];
    vi += vrel[k] * vrel[k];
  }
				// Energy in COM
  double kE = 0.5*pp->w2*mu*vi;
				// Energy reduced by loss
  double totE = kE - KE.delE;

  // KE is positive
  //
  if (kE>0.0) {
    // More loss energy requested than available?
    //
    if (totE < 0.0) {
      KE.miss = -totE;
      // Add to energy bucket for these particles
      //
      deferredEnergyTrace(pp, -totE, id);
      KE.delE += totE;
      totE = 0.0;
    }
    // Update the outgoing energy in COM
    //
    KE.vfac = sqrt(totE/kE);
    KE.kE   = kE;
    KE.totE = totE;
    KE.bs.set(KE_Flags::Vfac);
    KE.bs.set(KE_Flags::KEpos);
  }
  // KE is zero (limiting case)
  //
  else {
    KE.vfac = 1.0;
    KE.kE   = kE;
    KE.totE = totE;

    if (KE.delE>0.0) {
      KE.miss = KE.delE;
      // Defer all energy loss
      //
      deferredEnergyTrace(pp, KE.delE, id);
      KE.delE = 0.0;
    } else {
      // Apply delE to COM
      //
      vi = -2.0*KE.delE/(pp->w1*mu);
    }
  }

  // Assign interaction energy variables
  //
  if (KE.Coulombic)
    vrel = coulomb_vector(vrel, 1.0, 1.0, KE.Tau);
  else
    vrel = unit_vector();
  
  vi   = sqrt(vi);
  for (auto & v : vrel) v *= vi;
  //                         ^
  //                         |
  // Velocity in center of mass, computed from v1, v2 and adjusted
  // according to the inelastic energy loss
  //

  KE.delta = 0.0;

  
  // BEGIN: momentum conservation algorithm

  KE.bs.set(KE_Flags::momC);

  for (size_t k=0; k<3; k++) {
    (*v1)[k] = vcom[k] + m2/mt*vrel[k] * KE.vfac;
    (*v2)[k] = vcom[k] - m1/mt*vrel[k] * KE.vfac;
  }
    
  misE[id] += KE.miss;

  // Temporary deep debug
  //
  if (KE_DEBUG) {

    double M1 = 0.5 * pp->W1 * pp->m1;
    double M2 = 0.5 * pp->W2 * pp->m2;

    // Initial KE
    //
    double KE1i = M1 * KE.i(1);
    double KE2i = M2 * KE.i(2);

    double KE1f = 0.0, KE2f = 0.0;
    for (auto v : *v1) KE1f += v*v;
    for (auto v : *v2) KE2f += v*v;

    // Final KE
    //
    KE1f *= M1;
    KE2f *= M2;
      
    // KE differences
    //
    double KEi   = KE1i + KE2i;
    double KEf   = KE1f + KE2f;
    double delEt = KEi  - KEf - KE.delta - std::min<double>(kE, KE.delE);
    
    // Sanity test
    //
    double Ii1 = 0.0, Ii2 = 0.0, Ie1 = 0.0, Ie2 = 0.0;
    for (size_t k=0; k<3; k++) {
      Ii1 += pp->p1->vel[k] * pp->p1->vel[k];
      Ii2 += pp->p2->vel[k] * pp->p2->vel[k];
      if (use_elec>=0) {
	size_t K = use_elec + k;
	Ie1 += pp->p1->dattrib[K] * pp->p1->dattrib[K];
	Ie2 += pp->p2->dattrib[K] * pp->p2->dattrib[K];
      }
    }

    Ii1 *= 0.5 * pp->p1->mass;
    Ii2 *= 0.5 * pp->p2->mass;
    Ie1 *= 0.5 * pp->p1->mass * pp->eta1 * atomic_weights[0]/molP1[id];
    Ie2 *= 0.5 * pp->p2->mass * pp->eta2 * atomic_weights[0]/molP2[id];

    double Fi1 = Ii1, Fi2 = Ii2, Fe1 = Ie1, Fe2 = Ie2;

    if (pp->P == Pord::ion_ion) {
      Fi1 = 0.0; for (auto v : *v1) Fi1 += v*v;
      Fi2 = 0.0; for (auto v : *v2) Fi2 += v*v;
      Fi1 *= 0.5 * pp->p1->mass;
      Fi2 *= 0.5 * pp->p2->mass;
    }

    if (pp->P == Pord::ion_electron) {
      Fi1 = 0.0; for (auto v : *v1) Fi1 += v*v;
      Fe2 = 0.0; for (auto v : *v2) Fe2 += v*v;
      Fi1 *= 0.5 * pp->p1->mass;
      Fe2 *= 0.5 * pp->p2->mass * pp->eta2 * atomic_weights[0]/molP2[id];
    }

    if (pp->P == Pord::electron_ion) {
      Fe1 = 0.0; for (auto v : *v1) Fe1 += v*v;
      Fi2 = 0.0; for (auto v : *v2) Fi2 += v*v;
      Fe1 *= 0.5 * pp->p1->mass * pp->eta1 * atomic_weights[0]/molP1[id];
      Fi2 *= 0.5 * pp->p2->mass;
    }

    if ( fabs(delEt)/std::min<double>(KEi, KEf) > tolE) {
      std::cout << "**ERROR scatter: delEt = " << delEt
		<< " rel = "  << delEt/KEi
		<< " KEi = "  << KEi
		<< " KEf = "  << KEf
		<< " dif = "  << KEi - KEf
		<< "  kE = "  << kE
		<< "  dE = "  << KE.delE
		<< " dvf = "  << KE.delE/kE
		<< " tot = "  << totE
		<< " vfac = " << KE.vfac
		<< "   w1 = " << pp->w1
		<< "   w2 = " << pp->w2
		<< "   W1 = " << pp->W1
		<< "   W2 = " << pp->W2
		<< " flg = " << KE.decode()
		<< std::endl;
    } else {
      if (DBG_NewTest)
	std::cout << "**GOOD scatter: delEt = "
		  << std::setprecision(14) << std::scientific
		  << std::setw(22) << delEt
		  << " rel = "  << std::setw(22) << delEt/KEi << " kE = " << std::setw(22) << kE
		  << "   m1 = " << std::setw(22)  << m1
		  << "   m2 = " << std::setw(22)  << m2
		  << "   W1 = " << std::setw(22)  << pp->W1
		  << "   W2 = " << std::setw(22)  << pp->W2
		  << "  Ei1 = " << std::setw(22)  << KE1i
		  << "  Ef1 = " << std::setw(22)  << KE1f
		  << "  Ei2 = " << std::setw(22)  << KE2i
		  << "  Ef2 = " << std::setw(22)  << KE2f
		  << "  Ii1 = " << std::setw(22)  << Ii1
		  << "  Ie1 = " << std::setw(22)  << Ie1
		  << "  Ii2 = " << std::setw(22)  << Ii2
		  << "  Ie2 = " << std::setw(22)  << Ie2
		  << " delE = " << std::setw(22)  << KE.delE
		  << " swap = " << std::boolalpha << pp->swap
		  << std::setprecision(5)  << std::endl;
    }

  } // END: temporary deep debug

  // Sanity check
  if (pp->W2 > pp->W1) {
    std::cout << "Backwards: w1=" << pp->w1
	      << " w2=" << pp->w2
	      << " W1=" << pp->W1
	      << " W2=" << pp->W2
	      << " m1=" << pp->m1
	      << " m2=" << pp->m2
	      << " M1=" << pp->p1->mass
	      << " M2=" << pp->p2->mass
	      << std::endl;
  }
      
} // END: CollideIon::scatterTraceMM


void CollideIon::scatterPhotoTrace
(Particle* p, lQ Q, double Pr, double dE)
{
  // Compute Eta and Mu
  //
  double Eta = 0.0, Mu = 0.0;
  for (auto s : SpList) {
    double frac = p->dattrib[s.second] / atomic_weights[s.first.first];
    Eta += frac * (s.first.second - 1);
    Mu  += frac;
  }
  Eta /= Mu;
  Mu   = 1.0/Mu;

  // Number interacting atoms
  //
  double N0 = p->mass * TreeDSMC::Munit * Pr/ (atomic_weights[Q.first] * amu);

  // Convert from eV per particle to system units per
  // superparticle
  //
  double Ep = dE * N0 * eV/TreeDSMC::Eunit;

  // Total effective mass in the collision (atomic mass units)
  //
  double m1 = atomic_weights[Q.first];
  double m2 = atomic_weights[0];
  double mt = m1 + m2;

  // Reduced mass (atomic mass units)
  //
  double mu = m1 * m2 / mt;

  // Set COM frame
  //
  std::vector<double> vcom(3), vrel(3), v1(3), v2(3);
  double vi = 0.0, v12 = 0.0, v22 = 0.0;

  for (size_t k=0; k<3; k++) {
    v1[k]   = p->vel[k];
    v2[k]   = p->dattrib[use_elec+k];

    vcom[k] = (m1*v1[k] + m2*v2[k])/mt;
    vrel[k] = v1[k] - v2[k];

    vi     += vrel[k] * vrel[k];

    v12    += v1[k] * v1[k];
    v22    += v2[k] * v2[k];
  }
				// Energy in COM
  double kEcom = 0.5 * N0 * mu * vi * amu/TreeDSMC::Munit;
				// Energy reduced by loss
  double totE  = kEcom + Ep;

  // Assign interaction energy variables
  //
  vrel = unit_vector();
  vi   = sqrt(vi*totE/kEcom);
  for (auto & v : vrel) v *= vi;
  //                         ^
  //                         |
  // Velocity in center of mass, computed from v1, v2 and adjusted
  // according to the inelastic energy gain
  //

  std::vector<double> u1(3), u2(3), w1(3), w2(3);
  double u12 = 0.0, u22 = 0.0, vdvb = 0.0, udub = 0.0;

  for (size_t k=0; k<3; k++) {
    // New velocities in COM
    //
    u1[k] = vcom[k] + m2/mt*vrel[k];
    u2[k] = vcom[k] - m1/mt*vrel[k];

    // For energy solution
    //
    u12  += u1[k] * u1[k];
    u22  += u2[k] * u2[k];
    vdvb += v1[k] * u1[k];
    udub += v2[k] * u2[k];
  }
  
  // Solve quadratic for energy conservation
  //
  double qq = Pr/m1 * Mu, pp = qq/Eta, me = m2/Mu;
  double qb = 1.0 - qq;
  double pb = 1.0 - pp;

  double a = qb*qb*v12 + me*Eta*pb*pb*v22;
  double b = 2.0*qq*qb*vdvb + 2.0*pp*pb*me*Eta*udub;
  double c = qq*qq*u12 - v12 + me*Eta*(pp*pp*u22 - v22) - 2.0*Ep/(p->mass);

  double gam1 = 0.0, gam2 = 0.0, gam  = 0.0;
  double rad  = b*b - 4.0*a*c;

  // Sanity check
  //
  if (rad<0.0) {
    std::cout << "CollideIon::tracePhotoScatter: ERROR in consE solution"
	      << std::endl;
    rad = 0.0;
  } else {
    rad = sqrt(rad);
  }

  // Reconfigure to eliminate subtraction in quadradic solution
  //
  if (b>0.0) {
    gam1 = 2.0*c/(-b - rad);
    gam2 = (-b - rad)/(2.0*a);
  } else {
    gam1 = (-b + rad)/(2.0*a);
    gam2 = 2.0*c/(-b + rad);
  }

  // Choose best branch
  //
  if      (gam1 > 0.0 and gam2 < 0.0) gam = gam1;
  else if (gam1 < 0.0 and gam2 > 0.0) gam = gam2;
  else if (gam1 >=0.0 and gam2 >=0.0) {
    if (fabs(gam1 - 1.0) < fabs(gam2 - 1.0)) gam = gam1;
    else gam = gam2;
  } else {
    if (-gam1 > -gam2) gam = gam1;
    else gam = gam2;
  }

  // Final velocities
  //
  double w12 = 0.0, w22 = 0.0;
  for (size_t k=0; k<3; k++) {
    w1[k] = qb*v1[k]*gam + qq*u1[k];
    w2[k] = pb*v2[k]*gam + pp*u2[k];
    w12  += w1[k] * w1[k];
    w22  += w2[k] * w2[k];
  }

  if (KE_DEBUG) {

    // Initial total KE
    //
    double KEi1 = 0.5*p->mass * v12;
    double KEi2 = 0.5*p->mass * me * Eta * v22;
    
    // Final total KE
    //
    double KEf1 = 0.5*p->mass * w12; 
    double KEf2 = 0.5*p->mass * me * Eta * w22;
    
    double KEi    = KEi1 + KEi2;
    double KEf    = KEf1 + KEf2;
    double deltaE = KEf  - KEi - Ep;
    
    if (fabs(deltaE/KEi) > 1.0e-10) {
      std::cout << "CollideIon::tracePhotoScatter: ENERGY CONSERVATION ERROR"
		<< std::endl;
    }
  }
    
} // END: CollideIon::scatterPhotoTrace


void CollideIon::deferredEnergyTrace(PordPtr pp, const double E, int id)
{
  // Sanity check
  //
  if (E < 0.0) {
    std::cout << "**ERROR: negative deferred energy! E=" << E << std::endl;
    return;
  }

  // Save energy adjustments for next interation.  Split between like
  // species ONLY.
  //
  if (use_cons >= 0) {

    if (use_elec<0 or not elc_cons) {
      pp->E1[0] += 0.5*E;
      pp->E2[0] += 0.5*E;
    } else {
      double a = pp->w1, b = pp->w2;

      if (pp->m1 < 1.0) {
	if (E_split) a *= pp->eta1/molP1[id]; else a = 0;
	pp->E1[1] += a*E/(a + b);
	pp->E2[0] += b*E/(a + b);
      }
      else if (pp->m2 < 1.0) {
	if (E_split) b *= pp->eta2/molP2[id]; else b = 0;
	pp->E1[0] += a*E/(a + b);
	pp->E2[1] += b*E/(a + b);
      }
      else {
	pp->E1[0]  += a*E/(a + b);
	pp->E2[0]  += b*E/(a + b);
      }

    }

  } // END: use_cons >= 0

} // END: CollideIon::deferredEnergyTrace



// Compute the electron energy correction owing to random channel
// selection.
//
void CollideIon::updateEnergyTrace(PordPtr pp, KE_& KE)
{
  // Compute final energy
  //
  pp->eFinal();

  if (MeanMass or ExactE)     return;
  if (pp->P == Pord::ion_ion) return;

  double tKEi = 0.0;		// Total pre collision KE
  double tKEf = 0.0;		// Total post collision KE
  double eta  = 0.0;
  bool error  = false;

  if (pp->P == Pord::ion_electron) {
    tKEi = pp->mid[0].KEi + pp->mid[1].KEw;
    tKEf = pp->end[0].KEi + pp->end[1].KEw;
    //             ^                ^
    //             |                |
    // Particle 1/ion               |
    //                              |
    // Particle 2/electron----------+

    eta = pp->eta2;
    if (eta<0.0) error = true;
  }

  if (pp->P == Pord::electron_ion) {
    tKEi = pp->mid[0].KEw + pp->mid[1].KEi;
    tKEf = pp->end[0].KEw + pp->end[1].KEi;
    //             ^                ^
    //             |                |
    // Particle 1/electron          |
    //                              |
    // Particle 2/ion---------------+
    
    eta = pp->eta1;
    if (eta<0.0) error = true;
  }

  // Want energy to be:
  //
  //   tKEf  = tKEi - KE.delE
  //
  // or
  //
  //   delKE = tKEi - tKEf - KE.delE
  //
  // Want delKE = 0.  Need to defer this amount of energy.

  // Kinetic energy balance
  //
  double testE = tKEi - tKEf - KE.delta - KE.delE;

  // For energy conservation checking
  //
  KE.defer -= testE;

  // Add to electron deferred energy
  //
  if (pp->P == Pord::ion_electron) {
    if (elc_cons) {

      double wght1 = 0.5, wght2 = 0.5;
      if (ke_weight) {
	double denom = pp->KE1[0] + pp->KE2[1];
	wght1 = pp->KE1[0]/denom;
	wght2 = pp->KE2[1]/denom;
      }
	
      if (elec_balance) {
	pp->E1[0] -= testE * wght1;
	pp->E2[1] -= testE * wght2;
      } else if (TRACE_ELEC) {
	double ww1 = wght1 * (1.0 - TRACE_FRAC);
	double ww2 = wght2 * TRACE_FRAC;
	pp->E1[0] -= testE * ww1/(ww1 + ww2);
	pp->E2[1] -= testE * ww2/(ww1 + ww2);
      } else if (reverse_apply) {
	pp->E1[0] -= testE;
      } else {
	pp->E2[1] -= testE;
      }
    } else {
      pp->E1[0] -= testE;
    }
  }
  
  if (pp->P == Pord::electron_ion) {
    if (elc_cons) {

      double wght1 = 0.5, wght2 = 0.5;
      if (ke_weight) {
	double denom = pp->KE1[1] + pp->KE2[0];
	wght1 = pp->KE1[1]/denom;
	wght2 = pp->KE2[0]/denom;
      }

      if (elec_balance) {
	pp->E1[1] -= testE * wght1;
	pp->E2[0] -= testE * wght2;
      } else if (TRACE_FRAC) {
	double ww1 = wght1 * (1.0 - TRACE_FRAC);
	double ww2 = wght2 * TRACE_FRAC;
	pp->E1[1] -= testE * ww1/(ww1 + ww2);
	pp->E2[0] -= testE * ww2/(ww1 + ww2);
      } else if (reverse_apply) {
	pp->E1[1] -= testE;
      } else {
	pp->E2[0] -= testE;
      }
    } else {
      pp->E2[0] -= testE;
    }
  }
  
    

  if (DBG_NewTest and error)
    std::cout << "**ERROR in update: "
	      << ": KEi=" << std::setw(14) << tKEi
	      << ", KEf=" << std::setw(14) << tKEf
	      << ", eta=" << std::setw(14) << eta << std::setprecision(10)
	      << ", del=" << std::setw(14) << testE
	      << ", E1i=" << std::setw(18) << pp->end[0].KEi
	      << ", E2i=" << std::setw(18) << pp->end[1].KEi
	      << ", E1e=" << std::setw(18) << pp->end[0].KEw
	      << ", E2e=" << std::setw(18) << pp->end[1].KEw
	      << ", #1=" << pp->p1->indx
	      << ", #2=" << pp->p2->indx
	      << std::endl;
  
} // END: updateEnergyTrace


double CollideIon::Etotal()
{
  double ret = totalSoFar;
  totalSoFar = 0.0;
  return ret;
}

double CollideIon::Mtotal()
{
  double ret = massSoFar;
  massSoFar = 0.0;
  return ret;
}

void CollideIon::Elost(double* collide, double* epsm)
{
  double ret1=0.0;
  for (int n=0; n<nthrds; n++) {
    ret1 += lostSoFar[n];
    lostSoFar[n] = 0.0;
  }
  *collide = ret1;
  *epsm    = 0.0;		// EPSM to be implemented . . .
}


void * CollideIon::timestep_thread(void * arg)
{
  int id     = (int)((tstep_pass_arguments*)arg)->id;

  thread_timing_beg(id);

  // Loop over cells, cell time-of-flight time for each particle
  //
  pCell *c;
  Particle *p;
  double L, DT, mscale;

  // Loop over cells, processing collisions in each cell
  //
  for (unsigned j=0; j<cellist[id].size(); j++ ) {

    // Number of particles in this cell
    //
    c = cellist[id][j];
    L = c->Scale();

    // Look for collCount in cell attributes
    //
    std::map<std::string, int>::iterator itc = c->iattrib.find("collCount");
    double DTcoll = DBL_MAX;
    if (itc != c->iattrib.end()) {
      if (itc->second > 0)
	DTcoll = spTau[id] * collTnum / itc->second;
    }

    double volc  = c->Volume();

    sKeyDmap   densM, lambdaM, crossM;
    sKey2Amap  crossIJ;

    crossIJ = totalScatteringCrossSections(0, c, id);

    for (auto it1 : c->count) {
      speciesKey i1 = it1.first;
      densM[i1] = c->Mass(i1)/volc;
    }

    double meanLambda = 0.0;

    if (MFPTS) meanLambda = selMFP[c];

    for (auto i : c->bods) {

      // Current particle
      //
      p = tree->Body(i);

      // Compute time of flight criterion and assign cell scale to
      // characteristic size
      //
      DT     = 1.0e34;
      mscale = 1.0e34;

      double vtot = 0.0;
      for (unsigned k=0; k<3; k++) {
	mscale = std::min<double>(pHOT::sides[k]*L, mscale);
	vtot += p->vel[k]*p->vel[k];
      }
      vtot = sqrt(vtot) + 1.0e-34;

      // Compute collision time criterion
      //
      if (MFPTS) {
	for (unsigned k=0; k<3; k++)
	  DT = std::min<double>(meanLambda/vtot, DT);
      }

      // Collision target
      //
      DT = std::min<double>(DT, DTcoll);

      // Time-of-flight size scale for multistep timestep calc.
      //
      p->scale = mscale;

      // Compute cooling criterion timestep
      //
      if (use_delt>=0) {
	double v = p->dattrib[use_delt];
	if (v>0.0) DT = min<double>(DT, v);
      }

      p->dtreq = DT;
    }
  }

  thread_timing_end(id);

  return (NULL);
}

void CollideIon::eEdbg()
{
  if (fabs(tot2[0] - tot2[1])/tot2[0] > 1.0e-14) {

    std::ofstream out(tpaths[itp++ % 4].c_str());
    for (auto m : data[0]) {
      out << std::setw(10) << m.first
	  << std::setw(14) << std::get<0>(m.second)
	  << std::setw(14) << std::get<1>(m.second)
	  << std::setw(14) << std::get<0>(data[1][m.first])
	  << std::setw(14) << std::get<1>(data[1][m.first])
	  << std::endl;
    }
    out << std::setw(10) << "***"
	<< std::setw(14) << tot1[0]
	<< std::setw(14) << tot2[0]
	<< std::setw(14) << tot1[1]
	<< std::setw(14) << tot2[1]
	<< std::endl;
  }
}

double CollideIon::electronEnergy(pCell* const cell, int dbg)
{
  double Eengy = 0.0;
  for (auto b : cell->bods) {
    Particle *p = c0->Tree()->Body(b);

    KeyConvert k(p->iattrib[use_key]);
    double numb = 0.0;

    // Electron fraction for trace model
    //
    if (aType == Trace) {
      for (auto s : SpList) {
	double wgt = p->dattrib[s.second] / atomic_weights[s.first.first];
	numb += wgt * (s.first.second - 1);
      }
    }
    // Electron fraction for hybrid model
    //
    else if (aType == Hybrid) {
      double cnt = 0.0;
      const unsigned short Z = k.getKey().first;
      for (unsigned short C=0; C<=Z; C++)
	cnt += p->dattrib[spc_pos + C] * C;
      numb = p->mass/atomic_weights[k.Z()] * cnt;
    }
    // All other models . . .
    //
    else if (k.C() - 1 > 0) {
      numb = p->mass/atomic_weights[k.Z()];
    }

    if (numb>0.0) {
      for (unsigned j=0; j<3; j++) {
	double v = p->dattrib[use_elec+j];
	Eengy += 0.5 * v*v * numb;
      }
    }
  }

  if (dbg>=0) {

    data[dbg].clear();
    tot1[dbg] = 0.0;
    tot2[dbg] = 0.0;

    for (auto b : cell->bods) {
      Particle *p = c0->Tree()->Body(b);
      KeyConvert k(p->iattrib[use_key]);
      if (k.C() - 1 > 0) {
	double numb = p->mass/atomic_weights[k.Z()];
	double E = 0.0;
	for (unsigned j=0; j<3; j++) {
	  double v = p->dattrib[use_elec+j];
	  E += 0.5 * v*v * numb;
	}
	std::get<0>(data[dbg][b]) = 2.0*E/numb;
	std::get<1>(data[dbg][b]) = E;
	tot1[dbg] += 2.0*E/numb;
	tot2[dbg] += E;
      }
    }
  }

  return Eengy * atomic_weights[0];
}

void CollideIon::finalize_cell(pCell* const cell, sKeyDmap* const Fn,
			       double kedsp, double tau, int id)
{
  if (mlev==0) {		// Add electronic potential energy
    collD->addCellPotl(cell, id);
  }

  double massC = cell->Mass();	 // Mass in cell
  double volC  = cell->Volume(); // Volume in cell
  double densC = massC/volC;	 // Density in system units

  //======================================================================
  // Collision cell energy conservation debugging
  //======================================================================
  //
  if (aType == Hybrid and KE_DEBUG and not NoExact) {

    double totKEf = 0.0;
    double totMas = 0.0;
    double totIon = 0.0;
    double totElc = 0.0;
    
    for (auto b : cell->bods) {
      Particle *p      = tree->Body(b);
      unsigned short Z = KeyConvert(p->iattrib[use_key]).getKey().first;

				// Electron fraction per particle
      double eta = 0.0;
      if (use_elec >= 0) {
	for (unsigned short C=0; C<=Z; C++)
	  eta += p->dattrib[spc_pos + C] * C;
	eta *= atomic_weights[0]/atomic_weights[Z];
      }
				// Compute kinetic energy
      double KE = 0.0;
      for (unsigned j=0; j<3; j++) {
	KE += p->vel[j] * p->vel[j];
	if (use_elec >= 0) {
	  KE += eta * p->dattrib[use_elec+j] * p->dattrib[use_elec+j];
	}
      }

      totKEf += 0.5 * p->mass * KE;
      totMas += p->mass;

      if (use_cons>=0)
	totIon += p->dattrib[use_cons  ];
      if (use_elec>=0 and elc_cons)
	totElc += p->dattrib[use_elec+3];
    }

    /*
      Conservation logic:

      Initial energy = <initial KE> - <initial ion deficit> - <initial electron deficit>
      Final energy   = <final KE> - <final ion deficit> - <final electron deficit> + <Lost Energy>

      COM loss       = <radiated energy> + <KE energy in ionized electron>

      This is included in KE computation:

      KE gain        = <KE energy in ionized electron> - <KE energy in recombined electron>

      So KE of ionized electron should balance COM loss

      Lost energy    = <Energy removed from COM> + <Energy of recombined electron>
     */

    if (testKE[id][0] > 0.0) {

      double Cinit  = testKE[id][1] + testKE[id][2];   // Initial conservation deficits
      double Einit  = testKE[id][0] - Cinit;           // Initial energy minus deficit
      double Cfinal = totIon + totElc;                 // Final conservation deficits
      double Efinal = totKEf - Cfinal + testKE[id][3]; // Final energy minus deficit and loss
      double delE   = Efinal/Einit - 1.0;	       // Ratio
      
      if (false) {		// Include deferred energy
	KElost[id][0] += Einit;
	KElost[id][1] += Einit - (Efinal - testKE[id][3]);
      } else {			// No deferred energy
	KElost[id][0] += totKEf;
	KElost[id][1] += testKE[id][0] - totKEf;
      }
      
      if ( fabs(delE) > 1.0e-4 ) {
	std::cout << "**ERROR KE conservation:"
		  << std::left << std::setprecision(10)
		  << " T="        << std::setw(18)  << tnow
		  << " Einit="    << std::setw(18)  << Einit
		  << " Efinal="   << std::setw(18)  << Efinal
		  << " Delta="    << std::setw(18)  << Einit - Efinal
		  << " DelKE="    << std::setw(18)  << testKE[id][0] - totKEf
		  << " dCons="    << std::setw(18)  << Cfinal - Cinit
		  << " before="   << testKE[id]
		  << " after="    << std::setw(18)  << totKEf
		  << " rel err="  << std::setw(18)  << delE
		  << " KE init="  << std::setw(18)  << testKE[id][0]
		  << " cons I=["  << totIon << ", " << testKE[id][1]
		  << "] cons E=[" << totElc << ", " << testKE[id][2]
		  << "] delE="    << std::setw(18)  << testKE[id][3]
		  << " mass="     << std::setw(18)  << totMas
		  << " # coll="   << std::setw(18)  << testCnt[id]
		  << " bodies="   << cell->bods.size()
		  << std::endl;
	cellEb[id]++;
	dEratb[id] += fabs(delE);
      } else {
	if (DBG_NewTest)
	  std::cout << "**GOOD KE conservation:"
		    << std::left << std::setprecision(10)
		    << " T="        << std::setw(18)  << tnow
		    << " Einit="    << std::setw(18)  << Einit
		    << " Efinal="   << std::setw(18)  << Efinal
		    << " Delta="    << std::setw(18)  << Einit - Efinal
		    << " before="   << testKE[id]
		    << " after="    << std::setw(18)  << totKEf
		    << " rel err="  << std::setw(18)  << delE
		    << " cons I=["  << totIon << ", " << testKE[id][1]
		    << "] cons E=[" << totElc << ", " << testKE[id][2]
		    << "] delE="    << std::setw(18)  << testKE[id][3]
		    << " mass="     << std::setw(18)  << totMas
		    << " # coll="   << std::setw(18)  << testCnt[id]
		    << " bodies="   << cell->bods.size()
		    << std::endl;
	cellEg[id]++;
	dEratg[id] += fabs(delE);
      }
    }

  } // End: energy conservation debugging, Hybrid method


  //======================================================================
  // Count scattering interactions for debugging output
  //======================================================================
  //
  std::map<speciesKey, unsigned> countE;
  std::ofstream outdbg;
  if (debugFC) {
    ostringstream ostr;
    ostr << outdir << runtag << ".eScatter." << myid;
    outdbg.open(ostr.str().c_str(), ios::out | ios::app);
    if (outdbg) outdbg << "Cell=" << cell->mykey
		       << " electron scattering BEGIN" << std::endl;
  }
  

  //======================================================================
  // Compute photoionization (currently only for Trace)
  //======================================================================
  //
  if (aType == Trace and use_photoIB and photoIBType == perParticle) {

    // Photoionize all subspecies
    //
    for (auto s : SpList) {
      lQ Q    = s.first;
      int pos = s.second;
      
      for (auto b : cell->bods) {
	// The particle
	Particle *p  = tree->Body(b);

	// Pick a new photon for each body
	CFreturn ff  = ch.IonList[Q]->photoIonizationRate();

	if (ff.first>0.0) {
	  // Increment total count
	  photoStat[id][Q][0]++;

	  // Compute the probability and get the residual electron energy
	  double Pr = ff.first * TreeDSMC::Tunit * spTau[id];
	  double Ep = ff.second;
	  double ww = p->dattrib[pos] * Pr;
	  double Gm = TreeDSMC::Munit/(amu*atomic_weights[Q.first]);

	  if (Pr >= 1.0) {	// Limiting case
	    ww = p->dattrib[pos];
	    p->dattrib[pos  ]  = 0.0;
	    p->dattrib[pos+1] += ww;
				// Increment oab count and mean value
	    photoStat[id][Q][1] += 1;
	    photoStat[id][Q][2] += Pr;
	  } else {		// Normal case
	    p->dattrib[pos  ] -= ww;
	    p->dattrib[pos+1] += ww;
	  }
	  photoStat[id][Q][3]  = std::max<double>(photoStat[id][Q][3], Pr);     
	  photoW[id][s.first] += ww;
	  photoN[id][s.first] += ww * p->mass * Gm;

	  scatterPhotoTrace(p, Q, ww, Ep);
	}
      }
    }
  } // End: photoionizing background


  // RMS energy diagnostic for debugFC
  //
  std::vector<std::pair<double, double> > EconsV;
  std::vector<double> EconsQ;
  double rhosig = 0.0;
  unsigned int Nrhosig = 0;

  // Deep debug
  //
  typedef std::array<double, 3> D3;
  typedef std::tuple<D3, D3, double> cacheElem;
  std::map<unsigned long, cacheElem> cacheEvel;
  static bool DeepDebug = false;

  if (DeepDebug) {
    for (auto i : cell->bods) {
      Particle *p = cell->Body(i);
      for (int k=0; k<3; k++) {
	std::get<0>(cacheEvel[i])[k] = p->vel[k];
	std::get<1>(cacheEvel[i])[k] = p->dattrib[use_elec+k];
      }
      std::get<2>(cacheEvel[i]) = p->dattrib[use_elec+3];
    }
  }

  //======================================================================
  // Do electron interactions separately
  //======================================================================
  //
  if (use_elec>=0 and esType != always and esType != none) {

    if (outdbg) outdbg << "in electron interaction loop" << std::endl;

    const double cunit = 1e-14 * TreeDSMC::Munit /
      (TreeDSMC::Lunit * TreeDSMC::Lunit);
      
    std::vector<unsigned long> bods;
    std::map<unsigned long, double> molW, Eta1;

    // Eta will be the true # of electrons
    //
    double eta  = 0.0, crsvel = 0.0;
    double me   = atomic_weights[0]*amu;
    double efrc = 0.0, ewgt = 0.0;

    // For EPSM and cross section default
    //
    std::array<double, 3> Emom {0, 0, 0}, Emom2 {0, 0, 0};

    // Momentum diagnostic distribution
    //
    std::vector<double> pdif;
    std::map<unsigned long, double> Eta;

    // Compute list of particles in cell with electrons
    //
    for (auto i : cell->bods) {
      Particle *p = cell->Body(i);
      double eta1 = 0.0;
				// Ionization-fraction-weighted charge
      if (aType == Hybrid) {
	KeyConvert k(p->iattrib[use_key]);
	bods.push_back(i);
	for (unsigned short C=1; C<=k.Z(); C++) {
	  eta1 += p->mass/atomic_weights[k.Z()] *
	    (*Fn)[k.getKey()] * p->dattrib[spc_pos+C]*C;
	}
				// Use SpList to compute eta
      } else if (aType == Trace) {
	bods.push_back(i);
	double wght = 0.0;
	for (auto s : SpList) {
	  unsigned short Z = s.first.first, P = s.first.second - 1;
	  eta1 += p->dattrib[s.second] / atomic_weights[Z] * P;
	  wght += p->dattrib[s.second] / atomic_weights[Z];
	}
	Eta1[i] = eta1/wght;	// Number of electrons per particle
	molW[i] = 1.0/wght;	// Molecular weight
	efrc   += eta1*p->mass;	// Number of electrons
	ewgt   += wght*p->mass; // Number of particles
	eta1   *= p->mass/amu;	// Number of electrons per amu
      } // END: Trace
      else {			
	KeyConvert k(p->iattrib[use_key]);
	if (k.C()>1) {		// Mean charge
	  bods.push_back(i);
	  eta1  = p->mass/atomic_weights[k.Z()] * (*Fn)[k.getKey()] * (k.C()-1);
	  efrc += p->mass/atomic_weights[k.Z()] * (k.C()-1);
	  ewgt += p->mass/atomic_weights[k.Z()];
	}
      }

				// Momentum and rms per cell
      for (size_t j=0; j<3; j++) {
	double v = p->dattrib[use_elec+j];
	Emom [j] += eta1 * v;
	Emom2[j] += eta1 * v*v;
      }
				// Cache electrons per amu for this particle
      Eta[i] = eta1;
				// Accumulate electrons per amu for all particles in the cell
      eta   += eta1;
    }
    // end: body loop


    // Mean interparticle spacing
    //
    double   ips = DBL_MAX;
    if (IPS) ips = pow(volC/numEf[id], 0.333333) * TreeDSMC::Lunit * 1.0e7;

    // Compute cross section
    //
    if (use_elec >=0 and ewgt>0.0) {

      double kE = 0.0;
      for (auto v2 : Emom2) kE += v2/eta;
      double Tvel = sqrt(kE);
      kE *= 0.25 * me;
			       
      double b = 0.5*esu*esu /
	std::max<double>(kE*eV, FloorEv*eV) * 1.0e7; // nm

      b = std::min<double>(b, ips);

      if (coulInter) {
	double b_max = sqrt(1.0/(M_PI*pow(numIf[id], 2.0/3.0)));
	std::min<double>(b, b_max);
      }

      efrc /= ewgt;
      crsvel = M_PI*b*b * efrc * efrc * 4.0 * logL * Tvel;
    }

    // Sample cell
    //
    pCell    *samp = cell->sample;
    key_type  ckey = cell->mykey;

    if (samp) ckey = samp->mykey;

    if (use_ntcdb) {

      pthread_mutex_lock(&tlock);
      if (ntcdb[ckey].Ready(electronKey, elecElec)) {
	crsvel = ntcdb[ckey].CrsVel(electronKey, elecElec, ntcThresh) * ntcFactor;
      } else {
	ntcdb[ckey].Add(electronKey, elecElec, crsvel);
      }
      pthread_mutex_unlock(&tlock);

    } else if (use_elec >= 0) {

      size_t nbods = cell->bods.size();

      double CrsVel = 0.0;

      for (size_t i=0; i<nbods; i++) {
	for (size_t j=i+1; j<nbods; j++) {
	  Particle *p1 = tree->Body(cell->bods[i]);
	  Particle *p2 = tree->Body(cell->bods[j]);

	  double kE = 0.0;
	  for (size_t k=0; k<3; k++) {
	    double dvel = p1->dattrib[use_elec+k] - p2->dattrib[use_elec+k];
	    kE += dvel*dvel;
	  }

	  double cvel = sqrt(kE);
	  kE *= 0.25 * me;
			       
	  double e1 = 0.0, e2 = 0.0; // Compute electron fractions
	  for (auto s : SpList) {
	    unsigned short Z = s.first.first, P = s.first.second - 1;
	    e1 += p1->dattrib[s.second] / atomic_weights[Z] * P;
	    e2 += p2->dattrib[s.second] / atomic_weights[Z] * P;
	  }

	  // Coulombic impact parameter
	  double b = 0.5*esu*esu /
	    std::max<double>(kE*eV, FloorEv*eV) * 1.0e7; // nm

	  b = std::min<double>(b, ips);
	  
	  if (coulInter) {
	    double b_max = sqrt(1.0/(M_PI*pow(numIf[id], 2.0/3.0)));
	    std::min<double>(b, b_max);
	  }

	  // Max sigma*cr
	  CrsVel = std::max<double>(CrsVel,
				    M_PI*b*b * e1 * e2 * 4.0 * logL * cvel);
	}
      }

      crsvel = CrsVel;
    }


    // Probability of an interaction of between particles of type 1
    // and 2 for a given particle of type 2
    //
    double  Prob = eta * cunit * crsvel * tau / volC;
    double Cfrac = 1.0;
    size_t nbods = bods.size();

    double   selcM = 0.5 * nbods * (nbods-1) *  Prob;
    unsigned nselM = static_cast<unsigned>(floor(selcM));

    if (aType == Trace) {
      double afac = esu*esu/std::max<double>(2.0*kEee[id], FloorEv*eV);
      double eVel = sqrt(2.0 * kEee[id] * eV / (0.5*me) );

      double  dfac = TreeDSMC::Munit/amu/pow(TreeDSMC::Lunit, 3.0) /
	molP1[id];

      selcM = nbods * efrc * densC * dfac / PiProb[id][3];

      Prob = 2.0 * ABrate[id][3] * afac*afac * eVel / PiProb[id][3] * tau * TreeDSMC::Tunit;
      nselM = static_cast<unsigned>(floor(selcM));

      Cfrac = 0.5 * nbods * (nbods-1) * Prob / selcM;

      tauElc[id][0] += 1;
      tauElc[id][1] += 0.5*Prob;
      tauElc[id][2] += 0.25*Prob*Prob;
      tauElc[id][3] += Cfrac;
    }


    if (selcM - nselM > (*unit)()) nselM++;

    //======================================================================
    // EPSM
    //======================================================================
    //
    if (ElectronEPSM and eta>0.0 and nbods>1) {

      totlES[id]++;

      if (nselM > esNum) {
	
	nselM = 0;

	// Compute RMS
	//
	double sigma = 0.0, keBeg = 0.0;
	for (size_t j=0; j<3; j++) {
	  Emom [j] /= eta;
	  Emom2[j] /= eta;
	  sigma    += Emom2[j] - Emom[j]*Emom[j];
	  keBeg    += Emom2[j];
	}
	sigma = sigma>0.0 ? sqrt(sigma) : 0.0;
	
	// Pass through to get normal random variables with zero mean
	// and unit mean-squared amplitude.  These will be used to 
	// generate new electron velocities
	//
	std::array<double, 3> mu {0, 0, 0}, gam {0, 0, 0};
	double v;
	for (auto i : cell->bods) {
	  Particle *p = cell->Body(i);
	  double eta1 = Eta[i]/eta;
	  for (size_t j=0; j<3; j++) {
	    p->dattrib[use_elec+j] = v = (*norm)();
	    mu [j] += eta1 * v;
	    gam[j] += eta1 * v*v;
	  }
	} // END: body loop

	// Variance of random distribution
	//
	double gamma = 0.0;
	for (size_t j=0; j<3; j++)
	  gamma += gam[j] - mu[j]*mu[j];
	gamma = gamma>0.0 ? sqrt(gamma) : 0.0;

	// For testing mean and variance of updated distribution
	//
	std::array<double, 3> Etst {0, 0, 0}, Etst2 {0, 0, 0};
	  
	// Update electron velocities conserving momentum and energy
	//
	double vfac = (gamma>0.0 ? sigma/gamma : 0.0);

	for (auto i : cell->bods) {
	  Particle *p = cell->Body(i);
	  double eta1 = Eta[i];
	  
	  for (size_t j=0; j<3; j++) {
	    p->dattrib[use_elec+j] = v = Emom[j] + vfac*(p->dattrib[use_elec+j] - mu[j]);
	    // For debugging . . . 
	    Etst [j] += eta1/eta * v;
	    Etst2[j] += eta1/eta * v*v;
	  }
	}
	  
	epsmES[id]++;

	// Debugging output
	//
	if (debugFC and outdbg) {
	  outdbg << "electronEPSM: T(K)="
		 << keBeg/3.0*atomic_weights[0]*amu * TreeDSMC::Vunit*TreeDSMC::Vunit/boltz
		 << std::endl;
	  
	  double sig2 = 0.0, keEnd = 0.0;
	  for (size_t j=0; j<3; j++) {
	    sig2  += Etst2[j] - Etst[j]*Etst[j];
	    keEnd += Etst2[j];
	  }
	  
	  double diff2 = (keEnd - keBeg)/std::min<double>(keEnd, keBeg);
	  
	  if (fabs(diff2) > 1.0e-8) {
	    
	    outdbg << "--Edif  = " << diff2 << std::endl;
	    outdbg << "--Ncell = " << nbods << std::endl;
	    outdbg << "--Sigma = " << sigma << std::endl;
	    outdbg << "--Gamma = " << gamma << std::endl;

	    outdbg << "--Pbeg  = ";
	    for (auto v : Emom) outdbg << std::setw(16) << v;
	    outdbg << std::endl;

	    outdbg << "--Pend  = ";
	    for (auto v : Etst) outdbg << std::setw(16) << v;
	    outdbg << std::endl;

	    outdbg << "--Ebeg  = ";
	    for (auto v : Emom2) outdbg << std::setw(16) << v;
	    double sum = 0.0; for (auto v : Emom2) sum += v;
	    outdbg << " | " << std::setw(16) << sum << std::endl;
	    
	    outdbg << "--Eend  = ";
	    for (auto v : Etst2) outdbg << std::setw(16) << v;
	    sum = 0.0; for (auto v : Etst2) sum += v;
	    outdbg << " | " << std::setw(16) << sum << std::endl;

	    outdbg << "--Sbeg  = ";
	    for (size_t j=0; j<3; j++)
	      outdbg << std::setw(16) << Emom2[j] - Emom[j]*Emom[j];
	    outdbg << std::endl;
	    
	    outdbg << "--Send  = ";
	    for (size_t j=0; j<3; j++)
	      outdbg << std::setw(16) << Etst2[j] - Etst[j]*Etst[j];
	    outdbg << std::endl;
	  }
	} // END: debugFC output
	  
      } // END: nselM > esNum

    } // END: equilibrium particle simulation method (EPSM)


    // nselM clamping
    //
    if (esType == limited)
      nselM = std::min<unsigned>(nselM, esNum);
    if (esType == fixed)
      nselM = esNum;

    unsigned chosen = 0;

    // The collision selection loop
    //
    for (unsigned n=0; n<nselM; n++) {

      // Pick two particles with electrons at random out of this cell.
      //
      size_t l1, l2;

      l1 = static_cast<size_t>(floor((*unit)()*nbods));
      l1 = std::min<size_t>(l1, nbods-1);

      l2 = static_cast<size_t>(floor((*unit)()*(nbods-1)));
      l2 = std::min<size_t>(l2, nbods-2);

      if (l2 >= l1) l2++;

      // Get index from body map for the cell
      //
      unsigned long i1 = bods[l1];
      unsigned long i2 = bods[l2];

      Particle* p1 = cell->Body(i1);
      Particle* p2 = cell->Body(i2);

      KeyConvert k1, k2;
      if (use_key>=0) {
	k1 = KeyConvert(p1->iattrib[use_key]);
	k2 = KeyConvert(p2->iattrib[use_key]);
      }

      // Find the trace ratio
      //
      double W1, W2;

      if (aType == Trace) {
	if (MeanMass) {
	  W1 = p1->mass/molW[i1];
	  W2 = p2->mass/molW[i2];
	} else {
	  W1 = p1->mass*Eta1[i1]/molW[i1];
	  W2 = p2->mass*Eta1[i2]/molW[i2];
	}
      } else {
	if (SAME_ELEC_SCAT) if (k1.Z() != k2.Z()) continue;
	if (DIFF_ELEC_SCAT) if (k1.Z() == k2.Z()) continue;
	W1 = p1->mass / atomic_weights[k1.Z()];
	W2 = p2->mass / atomic_weights[k2.Z()];
      }

      // Default (not valid for Hybrid method)
      //
      double ne1 = 0.0, ne2 = 0.0;

      // Compute species-weighted electron number for Hybrid method
      //
      if (aType == Hybrid) {

	for (unsigned short C=1; C<=k1.Z(); C++)
	  ne1 += p1->dattrib[spc_pos+C]*C;

	for (unsigned short C=1; C<=k2.Z(); C++)
	  ne2 += p2->dattrib[spc_pos+C]*C;

	W1 *= ne1;
	W2 *= ne2;
      }
      // Electron number for trace species
      //
      else if (aType == Trace) {
	// Get molecular weights and electron fractions
	//
	double mol1 = 0.0, eta1 = 0.0;
	double mol2 = 0.0, eta2 = 0.0;
	for (auto s : SpList) {
				// Molecular weight
	  mol1 += p1->dattrib[s.second]/atomic_weights[s.first.first]; // 
	  mol2 += p2->dattrib[s.second]/atomic_weights[s.first.first];
				// Electron fraction
	  unsigned P =  s.first.second - 1;
	  eta1 += p1->dattrib[s.second]*P/atomic_weights[s.first.first];
	  eta2 += p2->dattrib[s.second]*P/atomic_weights[s.first.first];
	}
	// Electrons per particle
	//
	ne1 = eta1/mol1;
	ne2 = eta2/mol2;
      }
      // All others
      //
      else {
	ne1 = k1.C() - 1;
	ne2 = k2.C() - 1;
      }

      // Threshold for electron-electron scattering
      //
      if (ne1 < ESthresh or ne2 < ESthresh) continue;

      // Swap particles so that p2 is the trace element
      //
      if (W1 < W2) {
	// Swap the particle pointers
	//
	zswap(p1, p2);

	// Reassign the keys and species indices
	//
	zswap(k1, k2);

	// Swap electron fraction and particle count
	//
	zswap(ne1, ne2);
	zswap(W1, W2);
      }

      if (esThr > 0.0) {
	if (W2/W1 > esThr) W1 = W2 = 1.0;
	else continue;
      }

      double  q = W2 / W1;
      double m1 = atomic_weights[0];
      double m2 = atomic_weights[0];
      if (MeanMass) { m1 *= ne1; m2 *= ne2; }
      double mt = m1 + m2;
      double mu = m1 * m2 / mt;

      // Calculate pair's relative speed (pre-collision)
      //
      vector<double> vcom(3), vrel(3), v1(3), v2(3);
      double vr = 0.0;
      for (int k=0; k<3; k++) {
	v1[k]   = p1->dattrib[use_elec+k];
	v2[k]   = p2->dattrib[use_elec+k];
	vcom[k] = (m1*v1[k] + m2*v2[k])/mt;
	vrel[k] = v1[k] - v2[k];
	vr     += (p1->vel[k] - p2->vel[k]) * (p1->vel[k] - p2->vel[k]);
      }

      // Compute relative speed
      //
      double vi = 0.0;
      for (auto v : vrel) vi += v*v;

      // No point in inelastic collsion for zero velocity . . .
      //
      if (vi == 0.0) continue;

      // Relative speed
      //
      vi = sqrt(vi);
      vr = sqrt(vr);

      double cr = vi * TreeDSMC::Vunit;

      // Kinetic energy in eV
      //
      double kEee = 0.5 * mu * amu * cr * cr / eV;

      // Compute the cross section
      //
      double scrs = 0.0;
      
      // Collision flag
      //
      bool ok = true;

      if (aType != Trace) {

	if (esType == classical or esType == limited) {
	
	  if (use_elec >=0 and ne1 > 0 and ne2 > 0) {
	    double b = 0.5*esu*esu /
	      std::max<double>(kEee*eV, FloorEv*eV) * 1.0e7; // nm

	    b = std::min<double>(b, ips);

	    if (coulInter) {
	      double b_max = sqrt(1.0/(M_PI*pow(numIf[id], 2.0/3.0)));
	      std::min<double>(b, b_max);
	    }
	    
	    scrs = M_PI*b*b * ne1 * ne2 * 4.0 * logL;
	  }
	  
	  // Accumulate lambda
	  //
	  rhosig += 0.5*(W1 + W2)/amu * scrs * 1.0e-14 / volC *
	    TreeDSMC::Munit/pow(TreeDSMC::Lunit, 3.0);
	  Nrhosig++;

	  // Accept or reject candidate pair according to relative speed
	  //
	  double prod = (IonElecRate ? vr : vi) * scrs;
	  double targ = prod/crsvel;
	  
	  ok = (targ > (*unit)() );
	  
	  //  +-- Disable verbose debugging
	  //  |
	  //  v
	  if (false and ok) {
	    double prob = 0.5*(W1 + W2)/amu / volC * prod * tau * cunit;
	    
	    std::cout << "[" << myid << "]"
		      << " nsel=" << std::setw(10)  << std::left << nselM
		      << " ssel=" << std::setw(10)  << std::left << selcM
		      << " vr="   << std::setw(12)  << std::left << vr
		      << " vi="   << std::setw(12)  << std::left << vi
		      << " prod=" << std::setw(12)  << std::left << prod
		      << " time=" << std::setw(12)  << std::left << tnow
		      << " crsV=" << std::setw(12)  << std::left << crsvel
		      << " targ=" << std::setw(12)  << std::left << targ
		      << " Prob=" << std::setw(12)  << std::left << Prob
		      << " prob=" << std::setw(12)  << std::left << prob
		      << " okay=" << std::boolalpha << ok
		      << std::endl;
	  }
	  
	  // Over NTC max average
	  //
	  if (targ >= 1.0) elecOvr[id]++;
	  
	  
	  // Used / Total
	  //
	  if (ok) elecAcc[id]++;
	  elecTot[id]++;

	  // Update v_max and cross_max for NTC
	  //
	  pthread_mutex_lock(&tlock);
	  ntcdb[ckey].Add(electronKey, elecElec, prod);
	  pthread_mutex_unlock(&tlock);
	  
	} // END: collsion selection

      } // END: not trace algorithm


      double deltaKE = 0.0, dKE = 0.0;
      double KEi1 = 0.0, KEi2 = 0.0; // For debugging

      if (KE_DEBUG) {

	for (auto v : v1) KEi1 += v*v;
	for (auto v : v2) KEi2 += v*v;

	KEi1 *= 0.5*W1*m1;
	KEi2 *= 0.5*W2*m2;
      }

      // For normalizing orthogonal direction unit vector
      //
      double wnrm = 0.0;

      // Scatter electrons
      //
      if (ok) {

	// For diagnostic reporting only
	//
	chosen++;

	if (aType == Trace) {
	  double afac = esu*esu/std::max<double>(2.0*kEee*eV, FloorEv*eV);
	  double Tau  = ABrate[id][3] * afac*afac * cr * tau * TreeDSMC::Tunit;

	  if (MeanMass)
	    vrel = coulomb_vector(vrel, 1.0, 1.0, Tau);
	  else
	    vrel = coulomb_vector(vrel, W1, W2, Tau);
	}
	else
	  vrel = unit_vector();

	for (auto & v : vrel) v *= vi;

	if (MeanMass) {
	  
	  // Set COM frame
	  //
	  std::vector<double> vcom(3), vrel(3);
	  double KEcom = 0.0;
	  
	  for (size_t k=0; k<3; k++) {
	    vcom[k] = (m1*v1[k] + m2*v2[k])/mt;
	    vrel[k] = v1[k] - v2[k];
	    KEcom += vrel[k] * vrel[k];
	  }

	  // Energy deficit correction
	  //
	  KEcom *= 0.5*mu;
	  double delE = 0.0;
	  if (elc_cons)
	    delE = p1->dattrib[use_elec+3] + p2->dattrib[use_elec+3];
	  else
	    delE = p1->dattrib[use_cons  ] + p2->dattrib[use_cons  ];

	  double vfac = 0.0;
	  if (KEcom>delE) {
	    vfac = sqrt(1.0 - delE/KEcom);
	    if (elc_cons)
	      p1->dattrib[use_elec+3] = p2->dattrib[use_elec+3] = 0.0;
	    else
	      p1->dattrib[use_cons  ] = p2->dattrib[use_cons  ] = 0.0;
	  } else {
	    if (elc_cons)
	      p1->dattrib[use_elec+3] = p2->dattrib[use_elec+3] = 0.5*(delE - KEcom);
	    else
	      p1->dattrib[use_cons  ] = p2->dattrib[use_cons  ] = 0.5*(delE - KEcom);
	  }

	  // Assign new electron velocities
	  //
	  for (int k=0; k<3; k++) {
	    p1->dattrib[use_elec+k] = vcom[k] + m2/mt*vrel[k] * vfac;
	    p2->dattrib[use_elec+k] = vcom[k] - m1/mt*vrel[k] * vfac;
	  }

	}
	// Explicit energy conservation using splitting
	//
	else if (ExactE and q < 1.0) {

	  bool  algok = false;
	  double vrat = 1.0;
	  double gamm = 0.0;

	  std::vector<double> uu(3), vv(3), w1(v1);
	  for (size_t k=0; k<3; k++) {
				// New velocities in COM
	    uu[k] = vcom[k] + 0.5*vrel[k];
	    vv[k] = vcom[k] - 0.5*vrel[k];
	  }

	  double v1i2 = 0.0, b1f2 = 0.0, b2f2 = 0.0;
	  double udif = 0.0, vcm2 = 0.0, v1u1 = 0.0;

	  for (size_t k=0; k<3; k++) {
				// Difference in Particle 1
	    udif += (v1[k] - uu[k]) * (v1[k] - uu[k]);
				// COM norm
	    vcm2 += vcom[k] * vcom[k];
				// Normalizations
	    v1i2 += v1[k]*v1[k];
	    b1f2 += uu[k]*uu[k];
	    b2f2 += vv[k]*vv[k];
	    v1u1 += v1[k]*uu[k];
	  }

	  if (AlgOrth) {

	    // Cross product to determine orthgonal direction
	    //
	    w1 = uu ^ v1;

	    // Normalize
	    //
	    wnrm = 0.0;
	    for (auto v : w1) wnrm += v*v;

	    const double tol = 1.0e-12;
	    // Generate random vector if |u|~0 or |v1|~0
	    if (v1i2 < tol*b1f2 or b1f2 < tol*v1i2) {
	      for (auto & v : w1) v = (*norm)();
	    }
	    // Choose random orthogonal vector if uu || v1
	    else if (wnrm < tol*v1i2) {
	      auto t3 = zorder(v1);
	      int i0 = std::get<0>(t3), i1 = std::get<1>(t3), i2 = std::get<2>(t3);
	      w1[i0] = (*norm)();
	      w1[i1] = (*norm)();
	      w1[i2] = -(w1[i0]*v1[i0] + w1[i1]*v1[i1])/v1[i2];
	      wnrm = 0.0; for (auto v : w1) wnrm += v*v;
	    }

	    // Sanity check on norm |w|
	    if (wnrm > tol*sqrt(vcm2)) {
	      for (auto & v : w1) v *= 1.0/sqrt(wnrm);
	      gamm = sqrt(q*(1.0 - q)*udif);
	      algok = true;
	    }
	  }

	  if (AlgWght) {
	    // Compute initial and final energy after combination to get
	    // energy excess
	    //
	    double KEi1 = 0.0, KEi2 = 0.0, KEf1 = 0.0, KEf2 = 0.0;
	    for (size_t k=0; k<3; k++) {
	      KEi1 += v1[k]*v1[k];
	      KEi2 += v2[k]*v2[k];
	      double ww = (1.0 - q)*v1[k] + q*uu[k];
	      KEf1 += ww*ww;
	      KEf2 += vv[k]*vv[k];
	    }
	    
	    KEi1 *= 0.5*W1*m1;
	    KEi2 *= 0.5*W2*m2;
	    KEf1 *= 0.5*W1*m1;
	    KEf2 *= 0.5*W2*m2;

	    double R1    = W1*Fwght;
	    double R2    = W2*(1.0 - Fwght);
	    double R12   = R1 + R2;

	    double difE  = KEi1 + KEi2 - KEf1 - KEf2;
	    double difE1 = difE * R1/R12;
	    double difE2 = difE * R2/R12;
	    double totEf = KEf1 + KEf2;
	    
	    if (ALG_WGHT_MASS) {
	      double ms1 = p1->mass, ms2 = p2->mass;
	      difE1 = difE * ms1/(ms1 + ms2);
	      difE2 = difE * ms2/(ms1 + ms2);
	    }

	    algok = true;

	    if (totEf + difE < 0.0) {
	      algok = false;
	    } else if (difE1 + KEf1 < 0.0) {
	      vrat = 0.0;
	      double vfac = sqrt((totEf + difE)/KEf2);
	      for (auto & v : vv) v *= vfac;
	    } else if (difE2 + KEf2 < 0.0) {
	      vrat = sqrt((totEf + difE)/KEf1);
	      for (auto & v : uu) v *= vrat;
	      for (auto & v : vv) v  = 0.0;
	    } else {
	      vrat = sqrt(1.0 + difE1/KEf1);
	      for (auto & v : uu) v *= vrat;
	      double vfac = sqrt(1.0 + difE2/KEf2);
	      for (auto & v : vv) v *= vfac;
	    }

	    // Temporary deep debug
	    //
	    if (algok) {
	      
	      double M1 = 0.5 * W1 * m1;
	      double M2 = 0.5 * W2 * m2;
	      // Initial KE
	      double KE1i = 0.0, KE2i = 0.0;
	      double KE1f = 0.0, KE2f = 0.0;
	      for (auto v : v1) KE1i += v*v;
	      for (auto v : v2) KE2i += v*v;
	      for (size_t k=0; k<3; k++) {
		double w  = (1.0 - q)*v1[k]*vrat + q*uu[k];
		KE1f += w*w;
	      }
	      for (auto v : vv) KE2f += v*v;
	      
	      KE1i *= M1;
	      KE2i *= M2;
	      KE1f *= M1;
	      KE2f *= M2;
	      
	      // KE differences
	      double KEi   = KE1i + KE2i;
	      double KEf   = KE1f + KE2f;
	      double delEt = KEi  - KEf;
	      
	      if ( fabs(delEt)/std::min<double>(KEi, KEf) > tolE) {
		std::cout << "**ERROR elec-elec: delEt = " << delEt << std::endl;
	      }
	    }

	  } // end: AlgWght

	  if (!algok or (!AlgOrth and !AlgWght)) {
	    double qT = v1u1 * q;
	    if (v1i2 > 0.0) qT /= v1i2;
	    // Disable Taylor expansion.  Something is not working
	    // correctly with expansion.  Convergence?
	    //
	    // if (q<0.999) {
	    if (q<1.1) {
	      vrat = ( -qT + std::copysign(1.0, qT) *
		  sqrt(qT*qT + (1.0 - q)*(q*b1f2/v1i2 + 1.0) ) )/(1.0 - q);
	    } else {		// Taylor expansion
	      double x = v1u1, y = b1f2, ep = 1.0 - q;
	      if (v1i2 > 0.0) { x /= v1i2; y /= v1i2; }
	      double x2 = x  * x,   y2 = y  * y,    ep2 = ep * ep;
	      double x3 = x2 * x,   x4 = x2 * x2,    y3 = y2 * y;
	      double x5 = x2 * x3,  x6 = x3 * x3,    y4 = y2 * y2;
	      double x7 = x3 * x4, ep3 = ep * ep2;

	      vrat = 0.5*(1.0 + y)/x -
		0.125*(1.0 - 4.0*x2 + 2.0*y + y2)*ep/x3 +
		0.0625*(1.0 - 6.0*x2 + 8.0*x4 + (3.0 - 8.0*x2)*y +
			(3.0 - 2*x2)*y2 + y3)*ep2/x5 -
		0.0078125*(5.0 - 40.0*x2 + 96.0*x4 - 64.0*x6 +
			   (96.0*x4 - 96.0*x2 + 20.0)*y +
			   (16.0*x4 - 72.0*x2 + 30.0)*y2 +
			   (20.0 - 16.0*x2)*y3 + 5.0*y4)*ep3/x7;
	    }
	  }

	  // New velocities in inertial frame
	  //
	  std::vector<double> u1(3), u2(3);
	  for (size_t k=0; k<3; k++) {
	    u1[k] = (1.0 - q)*v1[k]*vrat + w1[k]*gamm + q*uu[k];
	    u2[k] = vv[k];
	  }

	  // These are all for diagnostics
	  //
	  double pi2 = 0.0, dp2 = 0.0, Ebeg = 0.0, Efin = 0.0;

	  // DIAGNOSTIC: energies
	  for (int k=0; k<3; k++) {
	    Ebeg += 0.5*W1*m1*v1[k]*v1[k] + 0.5*W2*m2*v2[k]*v2[k];
	    Efin += 0.5*W1*m1*u1[k]*u1[k] + 0.5*W2*m2*u2[k]*u2[k];
	  }

	  // Assign new electron velocities
	  //
	  for (int k=0; k<3; k++) {
	    p1->dattrib[use_elec+k] = u1[k];
	    p2->dattrib[use_elec+k] = u2[k];

	    // DIAGNOSTIC: initial and final momenta
	    double dpi = W1*m1*v1[k] + W2*m2*v2[k];
	    double dpf = W1*m1*u1[k] + W2*m2*u2[k];

	    // DIAGNOSTIC: rms momentum difference
	    dp2  += (dpi - dpf)*(dpi - dpf);
	    pi2  += dpi*dpi;
	  }

	  // Check for energy conservation
	  //
	  if (DebugE) momD[id].push_back(sqrt(dp2/pi2));

	  double deltaEn = Efin - Ebeg;

	  if (debugFC) {
	    EconsV.push_back(std::pair<double, double>(deltaEn, Ebeg));
	    EconsQ.push_back(q);
	  }

	  if ( fabs(deltaEn) > 1.0e-8*(Ebeg) ) {
	    std::cout << "**ERROR in energy conservation,"
		      << " Ebeg="  << Ebeg
		      << " Efin="  << Efin
		      << " Edif="  << Efin/Ebeg - 1.0
		      << " pcons=" << sqrt(dp2/pi2)
		      << "     q=" << q
		      << " wnorm=" << wnrm
		      << "  b1f2=" << b1f2/v1i2
		      << "  v1u1=" << v1u1/v1i2
		      << "  vrat=" << vrat
		      << " AlgOr=" << std::boolalpha << AlgOrth
		      << " AlgWg=" << std::boolalpha << AlgWght
		      << " algok=" << std::boolalpha << algok
		      << std::endl;
	  }

	  // Upscale electron energy
	  //
	  //  +------Turn off energy conservation correction, for now
	  //  |
	  //  v
	  if (false and k1.Z() == k2.Z()) {

	    double m1    = p1->mass*atomic_weights[0]/atomic_weights[k1.Z()] * ne1;
	    double m2    = p2->mass*atomic_weights[0]/atomic_weights[k2.Z()] * ne2;
	    double mt    = m1 + m2;
	    double mu    = m1 * m2 / mt;
	    double KEcom = 0.0;

	    for (int k=0; k<3; k++) {
	      vcom[k] = (m1*u1[k] + m2*u2[k])/mt;
	      vrel[k] = u1[k] - u2[k];
	      KEcom  += vrel[k] * vrel[k];
	    }
	    KEcom *= 0.5*mu;

	    double delE = 0.0;
	    if (elc_cons)
	      delE = p1->dattrib[use_elec+3] + p2->dattrib[use_elec+3];
	    else
	      delE = p1->dattrib[use_cons  ] + p2->dattrib[use_cons  ];

	    double vfac = 0.0;
	    if (KEcom>delE) {
	      vfac = sqrt(1.0 - delE/KEcom);
	      if (elc_cons)
		p1->dattrib[use_elec+3] = p2->dattrib[use_elec+3] = 0.0;
	      else
		p1->dattrib[use_cons  ] = p2->dattrib[use_cons  ] = 0.0;
	    } else {
	      if (elc_cons)
		p1->dattrib[use_elec+3] = p2->dattrib[use_elec+3] = 0.5*(delE - KEcom);
	      else
		p1->dattrib[use_cons  ] = p2->dattrib[use_cons  ] = 0.5*(delE - KEcom);
	    }

	    for (int k=0; k<3; k++) {
	      p1->dattrib[use_elec+k] = vcom[k] + m2/mt*vrel[k]*vfac;
	      p2->dattrib[use_elec+k] = vcom[k] - m1/mt*vrel[k]*vfac;
	    }
	    
	  } // end: Z1 == Z1

	} // end: ExactE
	
	// Explicit momentum conservation
	//
	else {

	  bool equal = fabs(q - 1.0) < 1.0e-14;

	  double vfac = 1.0;
	  if (equal or TRACE_OVERRIDE) {
	    const double tol = 0.95; // eps = 0.05, tol = 1 - eps
	    double KE0 = 0.5*W1*m1*m2/mt * vi*vi;
	    if (elc_cons)
	      dKE = p1->dattrib[use_elec+3] + p2->dattrib[use_elec+3];
	    else
	      dKE = p1->dattrib[use_cons  ] + p2->dattrib[use_cons  ];
	    // Too much KE to be removed, clamp to tol*KE0
	    // 
	    if (dKE/KE0 > tol) {
	      // Therefore, remaining excess is:
	      // dKE' = dKE - tol*KE0 = dKE*(1 - tol*KE0/dKE);
	      //
	      double ratk = tol*KE0/dKE;
	      if (elc_cons) {
		p1->dattrib[use_elec+3] *= (1.0 - ratk);
		p2->dattrib[use_elec+3] *= (1.0 - ratk);
	      } else {
		p1->dattrib[use_cons  ] *= (1.0 - ratk);
		p2->dattrib[use_cons  ] *= (1.0 - ratk);
	      }

	      // Sanity check
	      double test = 0.0;
	      {
		double orig = dKE;

		double finl = tol*KE0;
		if (elc_cons)
		  finl += p1->dattrib[use_elec+3] + p2->dattrib[use_elec+3];
		else
		  finl += p1->dattrib[use_cons  ] + p2->dattrib[use_cons  ];
		test = orig - finl;
	      }
	      
	      if (fabs(test/KE0) > 1.0e-10) {
		std::cout << std::setw(20) << "Energy excess error: "
			  << std::setw(12) << test/KE0
			  << std::setw(12) << ", " << ratk
			  << std::setw(12) << ", " << KE0
			  << std::setw(12) << ", " << test
			  << std::endl;
	      }
	      dKE = tol*KE0;
	    } else {
	      if (elc_cons)
		p1->dattrib[use_elec+3] = p2->dattrib[use_elec+3] = 0.0;
	      else
		p1->dattrib[use_cons  ] = p2->dattrib[use_cons  ] = 0.0;
	    }
	    
	    vfac = sqrt(1.0 - dKE/KE0);
	  }
	  
	  double qKEfac = 0.5*W1*m1*q*(1.0 - q);
	  deltaKE = 0.0;
	  for (int k=0; k<3; k++) {
	    double v0 = vcom[k] + m2/mt*vrel[k]*vfac;
	    deltaKE += (v0 - v1[k])*(v0 - v1[k]) * qKEfac;
	    p1->dattrib[use_elec+k] = (1.0 - q)*v1[k] + q*v0;
	    p2->dattrib[use_elec+k] = vcom[k] - m1/mt*vrel[k]*vfac;
	  }
				// Correct energy for conservation
	  if (!equal and elc_cons) {
	    
	    double KE1e = 0.0, KE2e = 0.0;
	    for (int k=0; k<3; k++) {
	      KE1e += p1->dattrib[use_elec+k] * p1->dattrib[use_elec+k];
	      KE2e += p2->dattrib[use_elec+k] * p2->dattrib[use_elec+k];
	    }
	  
	    if (aType == Trace) {

	      double eta1 = 0.0, eta2 = 0.0; // electron fraction
	      for (auto s : SpList) {
		unsigned P =  s.first.second - 1;
		eta1 += p1->dattrib[s.second]*P/atomic_weights[s.first.first];
		eta2 += p2->dattrib[s.second]*P/atomic_weights[s.first.first];
	      }

	      KE1e *= 0.5 * p1->mass * eta1 * atomic_weights[0];
	      KE2e *= 0.5 * p2->mass * eta2 * atomic_weights[0];

	    } else {

	      speciesKey k1 = KeyConvert(p1->iattrib[use_key]).getKey();
	      speciesKey k2 = KeyConvert(p2->iattrib[use_key]).getKey();
  
	      // Get atomic numbers
	      //
	      unsigned short Z1 = k1.first;
	      unsigned short Z2 = k2.first;
	      
	      if (aType == Hybrid) {

		double eta1 = 0.0, eta2 = 0.0; // electron fraction
		for (unsigned short C=1; C<=Z1; C++)
		  eta1 += p1->dattrib[spc_pos+C]*C;
		for (unsigned short C=1; C<=Z2; C++)
		  eta2 += p2->dattrib[spc_pos+C]*C;

		KE1e *= 0.5 * p1->mass * eta1 * atomic_weights[0] / atomic_weights[Z1];
		KE2e *= 0.5 * p2->mass * eta2 * atomic_weights[0] / atomic_weights[Z2];

	      } else {
		KE1e *= 0.5 * p1->mass * atomic_weights[0] / atomic_weights[Z1];
		KE2e *= 0.5 * p2->mass * atomic_weights[0] / atomic_weights[Z2];
	      }
	    }
	    
	    double wght1 = 0.5;
	    double wght2 = 0.5;

	    if (ke_weight) {
	      wght1 = KE1e/(KE1e + KE2e);
	      wght2 = KE2e/(KE1e + KE2e);
	    }

	    if (elec_balance) {
	      p1->dattrib[use_elec+3] -= deltaKE * wght1;
	      p2->dattrib[use_elec+3] -= deltaKE * wght2;
	    } else if (TRACE_ELEC) {
	      p1->dattrib[use_elec+3] -= deltaKE * wght1 * TRACE_FRAC;
	      p1->dattrib[use_cons]   -= deltaKE * wght1 * (1.0 - TRACE_FRAC);
	      p2->dattrib[use_elec+3] -= deltaKE * wght2 * TRACE_FRAC;
	      p2->dattrib[use_cons]   -= deltaKE * wght2 * (1.0 - TRACE_FRAC);
	    } else {
	      p1->dattrib[use_cons]   -= deltaKE * wght1;
	      p2->dattrib[use_cons]   -= deltaKE * wght2;
	    }
	  }

	} // end: momentum conservation

	// For debugging
	//
	if (debugFC) {
	  countE[k1.getKey()]++;
	  countE[k2.getKey()]++;
	}

      } // end: scatter
      
      if (KE_DEBUG) {

	double KEf1 = 0.0;
	double KEf2 = 0.0;

	for (int k=0; k<3; k++) {
	  double v1 = p1->dattrib[use_elec+k];
	  double v2 = p2->dattrib[use_elec+k];
	  KEf1 += v1*v1;
	  KEf2 += v2*v2;
	}

	KEf1 *= 0.5*W1*m1;
	KEf2 *= 0.5*W2*m2;

	double KEi = KEi1 + KEi2;
	double KEf = KEf1 + KEf2;

	double testE = KEi - KEf - deltaKE - dKE;

	if (fabs(testE) > tolE*KEi) {
	  std::cout << "**ERROR delta E elec ("
		    << k1.Z() << ","
		    << k2.Z() << ") = "
		    << std::setw(14) << testE
		    << ", KEi=" << std::setw(14) << KEi
		    << ", KEf=" << std::setw(14) << KEf
		    << ", exs=" << std::setw(14) << deltaKE
		    << ", dKE=" << std::setw(14) << dKE
		    << std::endl;
	}

      }

    } // loop over particles

    // nselM and reporting
    //
    if (esType == limited or esType == fixed) {
      if (debugFC and outdbg) {
	std::ostringstream sout;
	sout << nselM << "/" << esNum;
	outdbg << "nselM=" << std::setw(12) << std::left << sout.str()
	       << " chosen=" << std::setw(9) << std::left << chosen
	       << " time=" << tnow << std::endl;
      }
    } else {
      if (debugFC and outdbg) {
	outdbg << "nselM=" << std::setw(10) << std::left << nselM
	       << " chosen=" << std::setw(9) << std::left << chosen
	       << " time=" << tnow << std::endl;
      }
    }

  } // END: electron interactions for Direct, Weight, or Hybrid for use_elec>=0


  rhoSigV[id] += rhosig;
  rhoSigN[id] += Nrhosig;

  //======================================================================
  // Momentum adjustment
  //======================================================================
  //
  if (aType == Hybrid and enforceMOM) {
    CellMom fMoms = {0, 0, 0, 0};

    for (auto b : cell->bods) {
      Particle *p = tree->Body(b);
      fMoms[3] += p->mass;
      for (size_t k=0; k<3; k++)
	fMoms[k] += p->mass*p->vel[k];
	
      if (use_elec >= 0 and ExactE) {
	unsigned short Z = KeyConvert(p->iattrib[use_key]).getKey().first;

	double cnt = 0.0;
	for (unsigned short C=0; C<=Z; C++)
	  cnt += p->dattrib[spc_pos + C] * C;

	double eta = p->mass * atomic_weights[0]/atomic_weights[Z] * cnt;
	fMoms[3] += eta;
	for (size_t k=0; k<3; k++)
	  fMoms[k] += eta * p->dattrib[use_elec+k];
      }
    }
    
    if (cMoms[id][3]>0.0) {
      for (size_t k=0; k<3; k++) cMoms[id][k] /= cMoms[id][3];
    }
    
    if (fMoms[3]>0.0) {
      for (size_t k=0; k<3; k++) fMoms[k] /= fMoms[3];
    }
    
    for (auto b : cell->bods) {
      Particle *p = tree->Body(b);
      for (size_t k=0; k<3; k++) {
	p->vel[k] += cMoms[id][k] - fMoms[k];
	if (use_elec) p->dattrib[use_elec+k] += cMoms[id][k] - fMoms[k];
      }
    }

  } // END: enforceMOM
  

  //======================================================================
  // For debugging
  //======================================================================
  //
  if (debugFC and outdbg) {
    if (countE.size()) {
      outdbg << "Per species scatters" << std::endl;
      for (auto i : countE) {
	outdbg << "("    << std::setw(3) << i.first.first
	       << ", "   << std::setw(3) << i.first.second
	       << ") = " << i.second     << std::endl;
      }
    }

    size_t count = EconsV.size();
    if (count) {
      std::sort(EconsV.begin(), EconsV.end());

      const std::vector<float> qv =
	{0.01, 0.05, 0.1, 0.2, 0.5, 0.8, 0.9, 0.95, 0.99};
      
      typedef std::pair<double, double> ddP;
      std::map<float, ddP> qq;
      for (auto v : qv) qq[v] = EconsV[std::floor(count*v)];
      
      outdbg << endl << "Delta Econs [" << count << "]" << endl
	     << std::setw(8)  << std::right << "quantile" << "|  "
	     << std::setw(18) << std::right << "delta E"
	     << std::setw(18) << std::right << "(delta E)/E"
	     << std::endl
	     << std::setw(8)  << std::right << "--------" << "|  "
	     << std::setw(18) << std::right << "-------------"
	     << std::setw(18) << std::right << "-------------"
	     << std::endl;
      for (auto v : qq)
	outdbg << std::setw(8)  << std::right  << std::right << v.first << "|  "
	       << std::setw(18) << std::right  << v.second.first
	       << std::setw(18) << std::right  << v.second.first/v.second.second
	       << std::endl;
    }

    count = EconsQ.size();
    if (count) {
      std::sort(EconsQ.begin(), EconsQ.end());

      const std::vector<float> qv =
	{0.01, 0.05, 0.1, 0.2, 0.5, 0.8, 0.9, 0.95, 0.99};
      
      std::map<float, double> qq;
      for (auto v : qv) qq[v] = EconsQ[std::floor(count*v)];
      
      outdbg << endl << "q values [" << count << "]" << endl
	     << std::setw(8)  << std::right << "quantile" << "|  "
	     << std::setw(18) << std::right << "q"
	     << std::endl
	     << std::setw(8)  << std::right << "--------" << "|  "
	     << std::setw(18) << std::right << "-------------"
	     << std::endl;
      for (auto v : qq)
	outdbg << std::setw(8)  << std::right  << std::right << v.first << "|  "
	       << std::setw(18) << std::right  << v.second
	       << std::endl;
    }

    if (rhosig>0.0) {
      outdbg << std::endl << "Mean MFP=" << Nrhosig/rhosig << std::endl;
    }

    outdbg << std::endl << "Cell=" << cell->mykey << " electron scattering DONE"
	   << std::endl << printDivider << std::endl;
  }

  //======================================================================
  // Cross-section debugging
  //======================================================================
  //
  if (CROSS_DBG && id==0) {
    if (nextTime_dbg <= tnow && nCnt_dbg < nCel_dbg)
      {
	speciesKey i;

	if (aType==Direct or aType==Weight)
	  i = cell->count.begin()->first;
	else
	  i = SpList.begin()->first;

	if (!csections[id][i][i]) {
	  cross2_dbg.push_back(csections[id][i][i]());
	  nCnt_dbg++;
	} else {
	  for (auto v : csections[id][i][i].v) {
	    cross2_dbg.push_back(v.second);
	    nCnt_dbg++;
	  }
	}
	if (nCnt_dbg == nCel_dbg) write_cross_debug();
      }
  }

  //======================================================================
  // Collision diagnostics
  //======================================================================
  //
  double KEtot, KEdspC, KEdspE = 0.0;

  cell->KE(KEtot, KEdspC);	// KE in cell

				// Update temperature field, if
				// specified
  if (use_temp>=0) {
				// Energy to temperature
    const double Tfac = 2.0*TreeDSMC::Eunit/3.0 * amu  /
      TreeDSMC::Munit/boltz;

    double Temp = KEdspC * Tfac * molWeight(cell);

    for (auto b : cell->bods) c0->Tree()->Body(b)->dattrib[use_temp] = Temp;
  }
				// Add cell energy to diagnostic
				// handler.  Only do this at top level
				// time step since these values are
				// not cumulative
  if (mlev==0) {
    if (1) {			// Alternative KE comptuation
      KEtot = 0.0;
      for (auto i : cell->bods) {
	double ke = 0.0;
	Particle *p = cell->Body(i);
	for (auto v : p->vel) ke += v*v;
	KEtot += 0.5 * p->mass * ke;
      }
      collD->addCell(KEtot, id);
    } else {
      collD->addCell(KEtot*massC, id);
    }
				// Add electron stats to diagnostic
				// handler
    KEdspE = collD->addCellElec(cell, use_elec, id);
  } else {
    KEdspE = computeEdsp(cell).second;
  }

  // Per cell energy diagnostics that are reset on every cell update
  // call.  Each of these (clrE, misE, dfrE, updE, Ncol, Nmis) will
  // be zeroed by initialize_cell()
  //
  collD->addCellEdiag(clrE[id], misE[id], dfrE[id], updE[id],
		      Ncol[id], Nmis[id], id);

  //======================================================================
  // Assign cooling time steps
  //======================================================================
  //
  double totalKE = KEdspE + massC*KEdspC;

  if (use_delt>=0) {
    double dtE = DBL_MAX, ratio = 0.0;
    if (spEdel[id] > 0.0) {
      if (TSESUM) ratio = totalKE/spEdel[id];
      else        ratio = spEmax[id];
      dtE = std::max<double>(ratio*TSCOOL, TSFLOOR) * spTau[id];
      if (false and ratio<1.0) { // Sanity check for debugging
	std::cout << "[" << std::setw(4) << myid << "] "
		  << std::hex << std::setw(10) << cell
		  << ": " << ratio << std::endl << std::dec;
      }
    }
    spEdel[id] = 0.0;
    spEmax[id] = DBL_MAX;

    for (auto i : cell->bods) {
      double cur = cell->Body(i)->dattrib[use_delt];
      cell->Body(i)->dattrib[use_delt] = std::min<double>(dtE, cur);
    }
  }

  if (DeepDebug) {		// BEGIN: DeepDebug
    std::array<double, 3> maxDel {0, 0, 0};
    unsigned badCnt = 0;
    for (auto i : cell->bods) {
      Particle *p = cell->Body(i);

      D3 iVel = std::get<0>(cacheEvel[i]);
      D3 eVel = std::get<1>(cacheEvel[i]);

      std::array<double, 3> dif {0, 0, 0};
      for (int k=0; k<3; k++) {
	dif[0] +=
	  (iVel[k] - p->vel[k]) *
	  (iVel[k] - p->vel[k]) ;
	dif[1] +=
	  (eVel[k] - p->dattrib[use_elec+k]) *
	  (eVel[k] - p->dattrib[use_elec+k]) ;
      }
      dif[2] = fabs(std::get<2>(cacheEvel[i]) - p->dattrib[use_elec+3]);

      for (int k=0; k<3; k++) {
	double del = sqrt(dif[k]);
	maxDel[k] = std::max<double>(maxDel[k], del);
	if (del > 1.0e-48) badCnt++;
      }
    }
				// Diagnostic output
    if (badCnt) {
      std::cout << "ERROR [" << myid << "]: cnt=" << badCnt << " max=";
      for (int k=0; k<3; k++) std::cout << " " << maxDel[k];
      std::cout << std::endl;
    }

  } // END: DeepDebug

  //======================================================================
  // DONE
  //======================================================================
}

void CollideIon::KElossGather()
{
  for (int t=1; t<nthrds; t++) KElost[0] += KElost[t];

  std::array<double, 2> total;
  MPI_Reduce(&KElost[0][0], &total[0], 2, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

  KElossSoFar[0]  = total[0];
  KElossSoFar[1] += total[1];

  for (auto & v : KElost) v[0] = v[1] = 0.0;
}

void CollideIon::KEloss(std::ostream& out)
{
  if (myid==0) {
    out << printDivider << std::endl
	<< "Kinetic energy loss from cells" << std::endl
	<< printDivider << std::endl
	<< "T KE dKE = "
	<< std::scientific << std::setprecision(8)
	<< std::setw(16)   << tnow
	<< std::setw(16)   << KElossSoFar[0]
	<< std::setw(16)   << KElossSoFar[1]
	<< std::endl
	<< printDivider << std::endl;
  }
}

std::pair<double, double> CollideIon::computeEdsp(pCell* cell)
{
  std::vector<double> ev1(3, 0.0), ev2(3, 0.0);
  double m = 0.0, KEdspE = 0.0;
  std::set<unsigned long> bodies = cell->Bodies();

  for (auto n : bodies) {
    Particle * p = cell->Body(n);
    double mass = 0.0;

    if (aType == CollideIon::Trace) {

      // Compute mean charge and mol weight
      double cnt = 0.0, mu = 0.0;
      for (auto s : SpList) {
	speciesKey k = s.first;
	cnt += p->dattrib[s.second] * k.second;
	mu  += p->dattrib[s.second] / atomic_weights[k.first];
      }

      mass = p->mass * atomic_weights[0] * cnt * mu;

    } else {
      speciesKey k = KeyConvert(p->iattrib[use_key]).getKey();
      unsigned short Z = k.first;

      mass = p->mass * atomic_weights[0]/atomic_weights[Z];
      
      if (aType == CollideIon::Hybrid) {
	double cnt = 0.0;
	for (unsigned short C=1; C<Z+1; C++)
	  cnt += p->dattrib[spc_pos+1]*C;
	mass *= cnt;
      } else {
	mass *= static_cast<double>(k.second - 1);
      }
    }
    
    m += mass;
    for (size_t j=0; j<3; j++) {
      double v = p->dattrib[use_elec+j];
      ev1[j] += mass * v;
      ev2[j] += mass * v*v;
    }
  }
  
  if (m>0.0) {
    for (size_t j=0; j<3; j++) {
	ev1[j] /= m;
	ev2[j] /= m;
	KEdspE += 0.5 * m * (ev2[j] - ev1[j]*ev1[j]);
    }
  }

  return std::pair<double, double>(m, KEdspE);
}

// Help class that maintains database of diagnostics
//
collDiag::collDiag(CollideIon* caller) : p(caller)
{
  // Initialize the map
  //
  if (p->SpList.size()) {
				// Trace method
    (*this)[Particle::defaultKey] = collTDPtr(new CollisionTypeDiag());

  } else if (p->ZList.size()) {
				// All other methods
    for (auto n : p->ZList) {

      unsigned short Z = n;

      for (unsigned short C=1; C<Z+2; C++) {
	speciesKey k(Z, C);
	(*this)[k] = collTDPtr(new CollisionTypeDiag());
      }
    }
  } else {			// Sanity check
    if (myid==0) {
      std::cerr << "collDiag:CollDiag: species list or map is "
		<< "not initialized" << std::endl;
    }
    MPI_Abort(MPI_COMM_WORLD, 57);
  }

  Esum.resize(nthrds, 0.0);
  Elos.resize(nthrds, 0.0);
  Klos.resize(nthrds, 0.0);
  Elec.resize(nthrds, 0.0);
  Edsp.resize(nthrds, 0.0);
  Efrc.resize(nthrds, 0.0);
  Emas.resize(nthrds, 0.0);
  Epot.resize(nthrds, 0.0);
  clrE.resize(nthrds, 0.0);
  misE.resize(nthrds, 0.0);
  dfrE.resize(nthrds, 0.0);
  updE.resize(nthrds, 0.0);
  Encl.resize(nthrds, 0.0);
  Ncol.resize(nthrds, 0  );
  Nmis.resize(nthrds, 0  );
  Etot_c = 0.0;
  Ktot_c = 0.0;

  // Initialize the output file
  //
  initialize();
}

double collDiag::addCellElec(pCell* cell, int ue, int id)
{
  if (ue<0) return 0.0;		// Zero electron energy in cell

  std::vector<double> ev1(3, 0.0), ev2(3, 0.0);
  double m = 0.0;
  for (auto n : cell->bods) {
    Particle * s = cell->Body(n);
    double mass = 0.0;

    if (p->aType == CollideIon::Trace) {
      for (auto t : p->SpList) {
	unsigned short Z = t.first.first;
	unsigned short P = t.first.second - 1;
	mass += s->mass * p->atomic_weights[0]/p->atomic_weights[Z] *
	  s->dattrib[t.second] * P;
      }
      
      Efrc[id] += mass;

    } else {
      speciesKey k = KeyConvert(s->iattrib[p->use_key]).getKey();
      unsigned short Z = k.first;

      mass = s->mass * p->atomic_weights[0]/p->atomic_weights[Z];

      if (p->aType == CollideIon::Hybrid) {
	double cnt = 0.0;
	for (unsigned short C=1; C<=Z; C++)
	  cnt += s->dattrib[p->spc_pos+1]*C;
	mass *= cnt;
	Efrc[id] += mass;
      } else {
	mass *= static_cast<double>(k.second - 1);
      }
    }
    
    m += mass;
    for (size_t j=0; j<3; j++) {
      double v = s->dattrib[ue+j];
      ev1[j] += mass * v;
      ev2[j] += mass * v*v;
    }
  }

  if (m>0.0) {
    for (size_t j=0; j<3; j++) {
      if (ev2[j]>0.0) {
	Elec[id] += 0.5 * ev2[j];
	ev1[j] /= m;
	ev2[j] /= m;
	Edsp[id] += 0.5 * m * (ev2[j] - ev1[j]*ev1[j]);
      }
    }
    Emas[id] += m;
  }

  return Edsp[id];		// Return computed electron energy in cell
}

void collDiag::addCellPotl(pCell* cell, int id)
{
  if (RECOMB_IP) {

    const double cvrt = TreeDSMC::Munit/amu * eV/TreeDSMC::Eunit;
    
    for (auto n : cell->bods) {
    
      Particle * s = cell->Body(n);

      // Ion electronic potential energy
      //
      if (p->aType == CollideIon::Trace) {
	for (auto t : p->SpList) {
	  speciesKey     k = t.first;
	  unsigned short Z = k.first;
	  unsigned short P = k.second - 1;
	  double emfac     = s->mass/Collide::atomic_weights[Z] * cvrt;
	  double IP        = p->ch.IonList[lQ(Z, P)]->ip;
	  Epot[id]        += emfac * IP * s->dattrib[t.second];
	}
      }
      else if (p->aType == CollideIon::Hybrid) {
	speciesKey k     = KeyConvert(s->iattrib[p->use_key]).getKey();
	unsigned short Z = k.first;
	double emfac     = s->mass/Collide::atomic_weights[Z] * cvrt;
	for (unsigned short CC=Z+1; CC>1; CC--) {
	  double IP = p->ch.IonList[lQ(Z, CC-1)]->ip;
	  Epot[id] += emfac * IP * s->dattrib[p->spc_pos+CC-1] ;
	}
      } else {
	speciesKey k     = KeyConvert(s->iattrib[p->use_key]).getKey();
	unsigned short Z = k.first, C = k.second;
	double emfac     = s->mass/Collide::atomic_weights[Z] * cvrt;
	for (unsigned short CC=C; CC>1; CC--) {
	  Epot[id] +=  emfac * p->ch.IonList[lQ(Z, CC-1)]->ip;
	}
      }
    } // END: body loop

  } // END: RECOMB_IP

} // END: addCellPotl


// Get the total energy conservation excess
//
void collDiag::getEcons()
{
  if (p->use_cons >= 0) {
    double EconsI = 0.0, EconsE = 0.0;
    bool elec = p->elc_cons and p->use_elec >= 0;

    // Particle loop
    for (auto v : p->Particles()) {
      EconsI += v.second->dattrib[p->use_cons];
      if (elec) EconsE += v.second->dattrib[p->use_elec+3];
    }

    MPI_Reduce(&EconsI, &delI_s, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&EconsE, &delE_s, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
  }
}


// Gather statistics from all processes
//
void collDiag::gather()
{
  for (auto it : *this) {
    collTDPtr ctd = it.second;
    ctd->sumUp();
    ctd->sync();
  }

  getEcons();

  Esum_s = std::accumulate(Esum.begin(), Esum.end(), 0.0);
  Elos_s = std::accumulate(Elos.begin(), Elos.end(), 0.0);
  Klos_s = std::accumulate(Klos.begin(), Klos.end(), 0.0);
  Elec_s = std::accumulate(Elec.begin(), Elec.end(), 0.0);
  Epot_s = std::accumulate(Epot.begin(), Epot.end(), 0.0);
  Edsp_s = std::accumulate(Edsp.begin(), Edsp.end(), 0.0);
  Efrc_s = std::accumulate(Efrc.begin(), Efrc.end(), 0.0);
  Emas_s = std::accumulate(Emas.begin(), Emas.end(), 0.0);
  clrE_s = std::accumulate(clrE.begin(), clrE.end(), 0.0);
  misE_s = std::accumulate(misE.begin(), misE.end(), 0.0);
  dfrE_s = std::accumulate(dfrE.begin(), dfrE.end(), 0.0);
  updE_s = std::accumulate(updE.begin(), updE.end(), 0.0);
  Encl_s = std::accumulate(Encl.begin(), Encl.end(), 0.0);
  Ncol_s = std::accumulate(Ncol.begin(), Ncol.end(), 0  );
  Nmis_s = std::accumulate(Nmis.begin(), Nmis.end(), 0  );

  double z;
  unsigned u;

  MPI_Reduce(&(z=Esum_s), &Esum_s,  1, MPI_DOUBLE,   MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&(z=Elos_s), &Elos_s,  1, MPI_DOUBLE,   MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&(z=Klos_s), &Klos_s,  1, MPI_DOUBLE,   MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&(z=Elec_s), &Elec_s,  1, MPI_DOUBLE,   MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&(z=Epot_s), &Epot_s,  1, MPI_DOUBLE,   MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&(z=Edsp_s), &Edsp_s,  1, MPI_DOUBLE,   MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&(z=Efrc_s), &Efrc_s,  1, MPI_DOUBLE,   MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&(z=Emas_s), &Emas_s,  1, MPI_DOUBLE,   MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&(z=clrE_s), &clrE_s,  1, MPI_DOUBLE,   MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&(z=misE_s), &misE_s,  1, MPI_DOUBLE,   MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&(z=dfrE_s), &dfrE_s,  1, MPI_DOUBLE,   MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&(z=updE_s), &updE_s,  1, MPI_DOUBLE,   MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&(z=Encl_s), &Encl_s,  1, MPI_DOUBLE,   MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&(u=Ncol_s), &Ncol_s,  1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&(u=Nmis_s), &Nmis_s,  1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);
  
  MPI_Barrier(MPI_COMM_WORLD);

  (*barrier)("collDiag::gather complete", __FILE__, __LINE__);
}

// Zero out the counters
//
void collDiag::reset()
{
				// Reset CollisionTypeDiag
  for (auto it : *this) it.second->reset();
				// Reset cumulative values
  std::fill(Esum.begin(), Esum.end(), 0.0);
  std::fill(Elos.begin(), Elos.end(), 0.0);
  std::fill(Klos.begin(), Klos.end(), 0.0);
  std::fill(Elec.begin(), Elec.end(), 0.0);
  std::fill(Epot.begin(), Epot.end(), 0.0);
  std::fill(Edsp.begin(), Edsp.end(), 0.0);
  std::fill(Efrc.begin(), Efrc.end(), 0.0);
  std::fill(Emas.begin(), Emas.end(), 0.0);
  std::fill(clrE.begin(), clrE.end(), 0.0);
  std::fill(misE.begin(), misE.end(), 0.0);
  std::fill(dfrE.begin(), dfrE.end(), 0.0);
  std::fill(updE.begin(), updE.end(), 0.0);
  std::fill(Encl.begin(), Encl.end(), 0.0);
}

void collDiag::initialize()
{
  if (myid) return;

  {
    // Generate the file name
    std::ostringstream sout;
    sout << outdir << runtag << ".ION_coll";
    coll_file_debug = sout.str();

    // Check for existence
    std::ifstream in(coll_file_debug.c_str());

    if (in.fail()) {

      // Write a new file
      std::ofstream out(coll_file_debug.c_str());
      if (out) {

	out << "# Variable      key                      " << std::endl
	    << "# ------------  -------------------------" << std::endl
	    << "# N(nn)         number of neut-neut scat " << std::endl
	    << "# N(ne)         number of neut-elec scat " << std::endl
	    << "# N(np)         number of neut-prot scat " << std::endl
	    << "# N(ie)         number of ion-elec scat  " << std::endl
	    << "# N(ff)         number of free-free      " << std::endl
	    << "# W(ff)         summed wght of free-free " << std::endl
	    << "# E(ff)         cum energy in free-free  " << std::endl
	    << "# N(ce)         number of collisions     " << std::endl
	    << "# W(ce)         summed wght of collision " << std::endl
	    << "# E(ce)         cum energy in collisions " << std::endl
	    << "# N(ci)         number of ionizations    " << std::endl
	    << "# W(ci)         summed wght of ionized   " << std::endl
	    << "# E(ci)         cum energy in ionizations" << std::endl
	    << "# N(rr)         number of rad recombs    " << std::endl
	    << "# W(rr)         summed wght of recombs   " << std::endl
	    << "# E(rr)         energy in rad recombs    " << std::endl
	    << "# d(KE)         mean energy change       " << std::endl
	    << "# Elost         total energy loss        " << std::endl
	    << "# ElosC         cumulative energy loss   " << std::endl
	    << "# Klost         kinetic energy (KE) loss " << std::endl
	    << "# KlosC         cumulative KE loss       " << std::endl
	    << "# EkeI          ion kinetic energy       " << std::endl
	    << "# EkeE          electron kinetic energy  " << std::endl
	    << "# PotI          ion potential energy     " << std::endl
	    << "# delI          ion excess energy        " << std::endl
	    << "# delE          electron excess energy   " << std::endl
	    << "# clrE          cleared excess energy    " << std::endl
	    << "# misE          missed excess energy     " << std::endl
	    << "# dfrE          deferred excess energy   " << std::endl
	    << "# updE          updated excess energy    " << std::endl
	    << "# Ncol          # of collisions          " << std::endl
	    << "# Nmis          # missed excess energy   " << std::endl
	    << "# EdspE         electron E dispersion    " << std::endl
	    << "# Efrac         electron number fraction " << std::endl
	    << "# Etotl         total kinetic energy     " << std::endl
	    << "#"                                         << std::endl;

	// Species labels
	//
	out << "#" << std::setw(11+12*2) << std::right << "Species==>" << " | ";
	for (auto it : *this) {
	  ostringstream sout, sout2;
	  sout  << "(" << it.first.first << ", " << it.first.second << ")";
	  size_t w =17*12, l = sout.str().size();
	  sout2 << std::setw((w-l)/2) << ' ' << sout.str();
	  out   << std::setw(w) << sout2.str() << " | ";
	}
	out << std::setw(18*12) << ' ' << " |" << std::endl;

	// Header line
	//
	out << std::setfill('-') << std::right;
	out << "#" << std::setw(11) << '+'
	    << std::setw(12) << '+' << std::setw(12) << '+' << " | ";
	for (auto it : *this) {
	  for (int i=0; i<17; i++) out << std::setw(12) << '+';
	  out << " | ";
	}
	for (int i=0; i<18; i++)  out << std::setw(12) << '+';
	out << " |" << std::setfill(' ') << std::endl;

	// Column labels
	//
	out << "#"
	    << std::setw(11) << "Time |"
	    << std::setw(12) << "Temp |"
	    << std::setw(12) << "Disp |" << " | ";
	for (auto it : *this) {
	  out << std::setw(12) << "N(nn) |"
	      << std::setw(12) << "N(ne) |"
	      << std::setw(12) << "N(np) |"
	      << std::setw(12) << "N(ie) |"
	      << std::setw(12) << "N(ff) |"
	      << std::setw(12) << "W(ff) |"
	      << std::setw(12) << "E(ff) |"
	      << std::setw(12) << "N(ce) |"
	      << std::setw(12) << "W(ce) |"
	      << std::setw(12) << "E(ce) |"
	      << std::setw(12) << "N(ci) |"
	      << std::setw(12) << "W(ci) |"
	      << std::setw(12) << "E(ci) |"
	      << std::setw(12) << "N(rr) |"
	      << std::setw(12) << "W(rr) |"
	      << std::setw(12) << "E(rr) |"
	      << std::setw(12) << "d(KE) |"
	      << " | ";
	}
	out << std::setw(12) << "Elost |"
	    << std::setw(12) << "ElosC |"
	    << std::setw(12) << "Klost |"
	    << std::setw(12) << "KlosC |"
	    << std::setw(12) << "EkeI  |"
	    << std::setw(12) << "EkeE  |"
	    << std::setw(12) << "PotI  |"
	    << std::setw(12) << "delI  |"
	    << std::setw(12) << "delE  |"
	    << std::setw(12) << "clrE  |"
	    << std::setw(12) << "misE  |"
	    << std::setw(12) << "dfrE  |"
	    << std::setw(12) << "updE  |"
	    << std::setw(12) << "Ncol  |"
	    << std::setw(12) << "Nmis  |"
	    << std::setw(12) << "EdspE |"
	    << std::setw(12) << "Efrac |"
	    << std::setw(12) << "Etotl |"
	    << " | " << std::endl;

	// Column numbers
	//
	std::ostringstream st;
	unsigned int cnt = 0;
	st << "[" << ++cnt << "] |";
	out << "#" << std::setw(11) << st.str();
	st.str("");
	st << "[" << ++cnt << "] |";
	out << std::setw(12) << st.str();
	st.str("");
	st << "[" << ++cnt << "] |";
	out << std::setw(12) << st.str() << " | ";
	for (auto it : *this) {
	  for (size_t l=0; l<17; l++) {
	    st.str("");
	    st << "[" << ++cnt << "] |";
	    out << std::setw(12) << std::right << st.str();
	  }
	  out << " | ";
	}
	for (size_t l=0; l<18; l++) {
	  st.str("");
	  st << "[" << ++cnt << "] |";
	  out << std::setw(12) << std::right << st.str();
	}
	out << " |" << std::endl;

	// Header line
	//
	out << std::setfill('-') << std::right;
	out << "#" << std::setw(11+12+12) << '+' << " | ";
	for (auto it : *this) {
	  for (int i=0; i<17; i++) out << std::setw(12) << '+';
	  out << " | ";
	}
	for (int i=0; i<18; i++)  out << std::setw(12) << '+';
	out << " |" << std::setfill(' ') << std::endl;
      }
    }
    in.close();
  }

  {
    // Generate the file name
    std::ostringstream sout;
    sout << outdir << runtag << ".ION_energy";
    energy_file_debug = sout.str();

    // Check for existence
    std::ifstream in(energy_file_debug.c_str());

    if (in.fail()) {
      // Write a new file
      std::ofstream out(energy_file_debug.c_str());
      if (out) {

	out << "# Variable      key                      " << std::endl
	    << "# ------------  -------------------------" << std::endl
	    << "# avg           mean collision energy    " << std::endl
	    << "# num           number of collisions     " << std::endl
	    << "# min           minimum collison energy  " << std::endl
	    << "# max           maximum collision energy " << std::endl
	    << "# over10        number > 10.2 eV         " << std::endl
	    << "#"                                         << std::endl;

				// Species labels
	out << "#" << std::setw(11) << "Species==>" << " | ";
	for (auto it : *this) {
	  ostringstream sout, sout2;
	  sout  << "(" << it.first.first << ", " << it.first.second << ")";
	  size_t w = 5*12, l = sout.str().size();
	  sout2 << std::setw((w-l)/2) << ' ' << sout.str();
	  out   << std::setw(w) << sout2.str() << " | ";
	}
	out << std::endl;

	// Header line
	//
	out << std::setfill('-') << std::right;
	out << "#" << std::setw(11) << '+' << " | ";
	for (auto it : *this) {
	  for (int i=0; i<5; i++) out << std::setw(12) << '+';
	  out << " | ";
	}
	out << std::setfill(' ') << std::endl;

	// Column labels
	//
	out << "#" << std::setw(11) << "Time" << " | ";
	for (auto it : *this) {
	  out << std::setw(12) << "avg |"
	      << std::setw(12) << "num |"
	      << std::setw(12) << "min |"
	      << std::setw(12) << "max |"
	      << std::setw(12) << "over10 |"
	      << " | ";
	}
	out << std::endl;

	// Column numbers
	//
	std::ostringstream st;
	unsigned int cnt = 0;
	st << "[" << ++cnt << "] |";
	out << "#" << std::setw(11) << st.str() << " | ";
	for (auto it : *this) {
	  for (size_t l=0; l<5; l++) {
	    st.str("");
	    st << "[" << ++cnt << "] |";
	    out << std::setw(12) << std::right << st.str();
	  }
	  out << " | ";
	}
	out << std::endl;

	// Header line
	//
	out << std::setfill('-') << std::right;
	out << "#" << std::setw(11) << '+' << " | ";
	for (auto it : *this) {
	  for (int i=0; i<5; i++) out << std::setw(12) << '+';
	  out << " | ";
	}
	out << std::setfill(' ') << std::endl;
      }
    }
    in.close();
  }

}

void collDiag::print()
{
  if (myid) return;

  {
    std::ofstream out(coll_file_debug.c_str(), ios::out | ios::app);
    out << std::scientific << std::setprecision(3);
    if (out) {
      double cvrt   = eV/TreeDSMC::Eunit;

      out << std::setw(12) << tnow
	  << std::setw(12) << p->tM[0]
	  << std::setw(12) << p->tM[1] << " | ";
      for (auto it : *this) {
	collTDPtr ctd = it.second;
	out << std::setw(12) << ctd->nn_s[0]
	    << std::setw(12) << ctd->ne_s[0]
	    << std::setw(12) << ctd->np_s[0]
	    << std::setw(12) << ctd->ie_s[0]
	    << std::setw(12) << ctd->ff_s[0]
	    << std::setw(12) << ctd->ff_s[1]
	    << std::setw(12) << ctd->ff_s[2] * cvrt
	    << std::setw(12) << ctd->CE_s[0]
	    << std::setw(12) << ctd->CE_s[1]
	    << std::setw(12) << ctd->CE_s[2] * cvrt
	    << std::setw(12) << ctd->CI_s[0]
	    << std::setw(12) << ctd->CI_s[1]
	    << std::setw(12) << ctd->CI_s[2] * cvrt
	    << std::setw(12) << ctd->RR_s[0]
	    << std::setw(12) << ctd->RR_s[1]
	    << std::setw(12) << ctd->RR_s[2] * cvrt;

	if (ctd->dv_s[1]>0.0)
	  out << std::setw(12) << ctd->dv_s[2]/ctd->dv_s[1] << " | ";
	else
	  out << std::setw(12) << 0.0 << " | ";
      }

      double Elost = Elos_s;
      if (fabs(Encl_s)>0.0) Elost = Encl_s;

      Etot_c += Elos_s;
      Ktot_c += Klos_s;
      out << std::setw(12) << Elost
	  << std::setw(12) << Etot_c
	  << std::setw(12) << Klos_s
	  << std::setw(12) << Ktot_c
	  << std::setw(12) << Esum_s
	  << std::setw(12) << Elec_s
	  << std::setw(12) << Epot_s
	  << std::setw(12) << delI_s
	  << std::setw(12) << delE_s
	  << std::setw(12) << clrE_s
	  << std::setw(12) << misE_s
	  << std::setw(12) << dfrE_s
	  << std::setw(12) << updE_s
	  << std::setw(12) << Ncol_s
	  << std::setw(12) << Nmis_s
	  << std::setw(12) << Edsp_s
	  << std::setw(12) << (Emas_s>0.0 ? Efrc_s/Emas_s : 0.0)
	  << std::setw(12) << Etot_c + Ktot_c + Esum_s + Elec_s - delI_s - delE_s
	  << " |" << std::endl;
    }
  }

  {
    std::ofstream out(energy_file_debug.c_str(), ios::out | ios::app);
    if (out) {
      out << std::setw(12) << tnow << " | ";
      for (auto it : *this) {
	collTDPtr ctd = it.second;
	out << std::setw(12) << (ctd->eV_N_s ? ctd->eV_av_s/ctd->eV_N_s : 0.0)
	    << std::setw(12) << ctd->eV_N_s
	    << std::setw(12) << ctd->eV_min_s
	    << std::setw(12) << ctd->eV_max_s
	    << std::setw(12) << ctd->eV_10_s
	    << " | ";
      }
      out << std::endl;
    }
  }

}

void CollideIon::parseSpecies(const std::string& map)
{
  unsigned char nOK = 0;

  //
  // Default values
  //
  use_cons   = -1;
  use_elec   = -1;
  spc_pos    = -1;

  //
  // Let root node ONLY do the reading
  //
  if (myid==0) {

    std::ifstream in(map.c_str());
    if (in.bad())
      {
	std::cerr << "CollideIon::parseSpecies: definition file <"
		  << map << "> could not be opened . . . quitting"
		  << std::endl;
	nOK = 1;
      }


    // Ok, read the first line to get the implementation type

    if (nOK == 0) {

      const int nline = 2048;
      char line[nline];

      in.getline(line, nline);
      std::string type(line);

      if (type.compare("direct")==0) {

	aType = Direct;

	if (use_key<0) {
	  std::cerr << "CollideIon: species key position is not defined in "
		    << "Component" << std::endl;
	  nOK = 1;
	}

	in.getline(line, nline);

	if (in.good()) {
	  std::istringstream sz(line);
	  sz >> use_elec;
	} else {
	  nOK = 1;		// Can't read position flag
	}

	if (nOK == 0) {

	  int Z;
	  while (1) {
	    in.getline(line, nline);
	    if (in.good()) {
	      std::istringstream sz(line);
	      sz >> Z;		// Add to the element list
	      if (!sz.bad()) ZList.insert(Z);
	    } else {
	      break;
	    }
	  }
	}

      } else if (type.compare("weight")==0) {

	aType = Weight;

	if (use_key<0) {
	  std::cerr << "CollideIon: species key position is not defined in "
		    << "Component" << std::endl;
	  nOK = 1;
	}

	in.getline(line, nline);

	if (in.good()) {
	  std::istringstream sz(line);
	  sz >> use_cons;
	  if (sz.good()) {
	    sz >> use_elec;
	  }
	} else {
	  nOK = 1;		// Can't read electrons or use_cons value, fatal
	}

				// Print warning, not fatal
	if (use_cons<0) {
	  std::cout << "CollideIon: energy key position is not defined, "
		    << "you are using weighting but NOT imposing energy conservation"
		    << std::endl;
	}

	if (use_elec<0) {
	  std::cout << "CollideIon: electron key position is not defined, "
		    << "you are using weighting WITHOUT explicit electron velocities"
		    << std::endl;
	}

	if (nOK == 0) {

	  int Z;
	  double W, M;
	  while (1) {
	    in.getline(line, nline);
	    if (in.good()) {
	      std::istringstream sz(line);
	      sz >> Z;
	      sz >> W;
	      sz >> M;		// Add to the element list
	      if (!sz.bad()) {
		ZList.insert(Z);
		ZWList[Z] = W;
		ZMList[Z] = M;
	      }
	    } else {
	      break;
	    }
	  }

	  // Find the largest weight (assume fiducial)
	  double sMax = 0.0;
	  for (auto v : ZWList) {
	    if (v.second > sMax) {
	      sFid = v.first;
	      sMax = v.second;
	    }
	  }

	}

      } else if (type.compare("hybrid")==0) {

	aType = Hybrid;

	if (use_key<0) {
	  std::cerr << "CollideIon: species key position is not defined in "
		    << "Component" << std::endl;
	  nOK = 1;
	}

	in.getline(line, nline);

	if (in.good()) {
	  std::istringstream sz(line);
	  sz >> use_cons;
	  if (sz.good()) {
	    sz >> spc_pos;
	  }
	  if (sz.good()) {
	    sz >> use_elec;
	  }
	} else {
	  nOK = 1;		// Can't read electrons or use_cons value, fatal
	}

				// Print warning, not fatal
	if (use_cons<0) {
	  std::cout << "CollideIon: energy key position is not defined, "
		    << "you are using hybrid weighting but NOT imposing energy conservation"
		    << std::endl;
	}

	if (use_elec<0) {
	  std::cout << "CollideIon: electron key position is not defined, "
		    << "you are using hybrid weighting WITHOUT explicit electron velocities"
		    << std::endl;
	}

	if (spc_pos<0) {
	  std::cout << "CollideIon: ionization start index for hybrid algorithm is not defined, "
		    << "this is fatal!"
		    << std::endl;
	  nOK = 1;
	}

	if (nOK == 0) {

	  int Z;
	  double W, M;
	  while (1) {
	    in.getline(line, nline);
	    if (in.good()) {
	      std::istringstream sz(line);
	      sz >> Z;
	      sz >> W;
	      sz >> M;		// Add to the element list
	      if (!sz.bad()) {
		ZList.insert(Z);
		ZWList[Z] = W;
		ZMList[Z] = M;
	      }
	    } else {
	      break;
	    }
	  }

	  // Find the largest weight (assume fiducial)
	  double sMax = 0.0;
	  for (auto v : ZWList) {
	    if (v.second > sMax) {
	      sFid = v.first;
	      sMax = v.second;
	    }
	  }

	}

      } else if (type.compare("trace")==0) {

	// Sanity check
	if (c0->keyPos >= 0) {
	  std::ostringstream sout;
	  sout << "[" << myid 
	       << "] CollideIon::parse_species: method <trace> is requested but keyPos="
	       << c0->keyPos << " and should be < 0 for consistency" << std::endl;
	  throw std::runtime_error(sout.str());
	}

	aType = Trace;

	in.getline(line, nline);

	if (in.good()) {
	  std::istringstream sz(line);
	  sz >> use_cons;
	  if (sz.good()) {
	    sz >> use_elec;
	  }
	} else {
	  nOK = 1;		// Can't read electrons or use_cons value, fatal
	}

	if (nOK == 0) {

	  speciesKey key;
	  int pos;
	  while (1) {
	    in.getline(line, nline);
	    if (in.good()) {
	      std::istringstream sz(line);
	      sz >> key.first;
	      sz >> key.second;
	      sz >> pos;
	      // Add to the species list
	      if (!sz.bad()) {
		SpList[key] = pos;
		ZList.insert(key.first);
	      }
	    } else {
	      break;
	    }
	  }
	}

      } else {
	std::cerr << "CollideIon::parseSpecies: implementation type <"
		  << type << "> is not recognized . . . quitting"
		  << std::endl;
	nOK = 1;
      }
    }

  }

  MPI_Bcast(&nOK, 1, MPI_UNSIGNED_CHAR, 0, MPI_COMM_WORLD);


  if (nOK) MPI_Abort(MPI_COMM_WORLD, 55);

  int is = aType;
  MPI_Bcast(&is, 1, MPI_INT, 0, MPI_COMM_WORLD);
  if (myid) {
    switch (is) {
    case Direct:
      aType = Direct;
      break;
    case Weight:
      aType = Weight;
      break;
    case Hybrid:
      aType = Hybrid;
      break;
    case Trace:
      aType = Trace;
      break;
    default:
      std::cout << "Proc " << myid << ": error in enum <" << is << ">"
		<< std::endl;
      MPI_Abort(MPI_COMM_WORLD, 56);
    }
  }

  // Sanity check.  So far, the full pair-wise cross-section census is
  // implemented for the Trace method only
  if (!use_ntcdb and aType!=Trace) use_ntcdb = true; 

  unsigned int sz = ZList.size();
  MPI_Bcast(&sz, 1, MPI_UNSIGNED, 0, MPI_COMM_WORLD);

  unsigned short z;

  if (myid==0) {
    for (auto it : ZList) {
      z = it;
      MPI_Bcast(&z, 1, MPI_UNSIGNED_SHORT, 0, MPI_COMM_WORLD);
    }
  } else {
    for (unsigned j=0; j<sz; j++) {
      MPI_Bcast(&z, 1, MPI_UNSIGNED_SHORT, 0, MPI_COMM_WORLD);
      ZList.insert(z);
    }
  }


  sz = ZWList.size();
  MPI_Bcast(&sz, 1, MPI_UNSIGNED, 0, MPI_COMM_WORLD);

  double v;

  if (myid==0) {
    for (auto it : ZWList) {
      z = it.first;
      v = it.second;
      MPI_Bcast(&z, 1, MPI_UNSIGNED_SHORT, 0, MPI_COMM_WORLD);
      MPI_Bcast(&v, 1, MPI_DOUBLE,         0, MPI_COMM_WORLD);
    }

    for (auto it : ZMList) {
      z = it.first;
      v = it.second;
      MPI_Bcast(&z, 1, MPI_UNSIGNED_SHORT, 0, MPI_COMM_WORLD);
      MPI_Bcast(&v, 1, MPI_DOUBLE,         0, MPI_COMM_WORLD);
    }

  } else {
    for (unsigned j=0; j<sz; j++) {
      MPI_Bcast(&z, 1, MPI_UNSIGNED_SHORT, 0, MPI_COMM_WORLD);
      MPI_Bcast(&v, 1, MPI_DOUBLE,         0, MPI_COMM_WORLD);
      ZWList[z] = v;
    }

    for (unsigned j=0; j<sz; j++) {
      MPI_Bcast(&z, 1, MPI_UNSIGNED_SHORT, 0, MPI_COMM_WORLD);
      MPI_Bcast(&v, 1, MPI_DOUBLE,         0, MPI_COMM_WORLD);
      ZMList[z] = v;
    }
  }

  MPI_Bcast(&use_cons,   1, MPI_INT,       0, MPI_COMM_WORLD);
  MPI_Bcast(&use_elec,   1, MPI_INT,       0, MPI_COMM_WORLD);
  MPI_Bcast(&spc_pos, 1, MPI_INT,       0, MPI_COMM_WORLD);

  if (aType == Trace) {

    speciesKey key;
    int pos;

    sz = SpList.size();
    MPI_Bcast(&sz, 1, MPI_UNSIGNED, 0, MPI_COMM_WORLD);

    if (myid==0) {
      for (auto it : SpList) {

	key = it.first;
	pos = it.second;

	MPI_Bcast(&key.first,  1, MPI_UNSIGNED_SHORT, 0, MPI_COMM_WORLD);
	MPI_Bcast(&key.second, 1, MPI_UNSIGNED_SHORT, 0, MPI_COMM_WORLD);
	MPI_Bcast(&pos,        1, MPI_INT,            0, MPI_COMM_WORLD);
      }
    } else {
      for (unsigned j=0; j<sz; j++) {
	MPI_Bcast(&key.first,  1, MPI_UNSIGNED_SHORT, 0, MPI_COMM_WORLD);
	MPI_Bcast(&key.second, 1, MPI_UNSIGNED_SHORT, 0, MPI_COMM_WORLD);
	MPI_Bcast(&pos,        1, MPI_INT,            0, MPI_COMM_WORLD);
	SpList[key] = pos;
      }
    }
  }

  // Record algorithm type in stdout log file
  if (myid==0) {
    std::cout << printDivider << std::endl
	      << "--- CollideIon: collision algorithm type is <"
	      << AlgorithmLabels[aType] << ">" << std::endl
	      << printDivider << std::endl;
  }

  (*barrier)("CollideIon::parseSpecies complete", __FILE__, __LINE__);
}

Collide::sKey2Amap CollideIon::generateSelection
(pCell* const c, sKeyDmap* const Fn, double crm, double tau, int id,
 double& meanLambda, double& meanCollP, double& totalNsel)
{
  if (aType == Direct)
    return generateSelectionDirect(c, Fn, crm, tau, id,
				   meanLambda, meanCollP, totalNsel);
  else if (aType == Weight)
    return generateSelectionWeight(c, Fn, crm, tau, id,
				   meanLambda, meanCollP, totalNsel);
  else if (aType == Hybrid)
    return generateSelectionHybrid(c, Fn, crm, tau, id,
				   meanLambda, meanCollP, totalNsel);
  else
    return generateSelectionTrace(c, Fn, crm, tau, id,
				  meanLambda, meanCollP, totalNsel);
}

Interact
CollideIon::generateSelectionSub(int id, Particle* const p1, Particle* const p2,
				 Interact::T& maxT, sKeyDmap* const Fn,
				 double *cr, double tau)
{
  if (aType == Hybrid)
    return generateSelectionHybridSub(id, p1, p2, maxT, Fn, cr, tau);
  else
    return Interact();	// Empty map
}

Collide::sKey2Amap CollideIon::generateSelectionDirect
(pCell* const c, sKeyDmap* const Fn, double crm, double tau, int id,
 double& meanLambda, double& meanCollP, double& totalNsel)
{
  sKeyDmap  densM, collPM, lambdaM, crossM;
  sKey2Amap selcM;

  // Volume in the cell
  //
  double volc = c->Volume();

  //
  // Cross-section debugging [BEGIN]
  //
  if (CROSS_DBG && id==0) {
    if (nextTime_dbg <= tnow && nCnt_dbg < nCel_dbg) {
      speciesKey i = c->count.begin()->first;
      cross1_dbg.push_back(csections[id][i][i]());
    }
  }
  //
  // Done
  //

  for (auto it1 : c->count) {
    speciesKey i1 = it1.first;
    densM[i1] = c->Mass(i1)/volc / atomic_weights[i1.first];
    //                             ^
    //                             |
    // Number density--------------+
    //
  }

  double meanDens = 0.0;
  meanLambda      = 0.0;
  meanCollP       = 0.0;

  for (auto it1 : c->count) {

    speciesKey i1 = it1.first;
    crossM [i1]   = 0.0;

    for (auto it2 : c->count) {

      speciesKey i2 = it2.first;

      if (i2>=i1) {
	crossM[i1] += (*Fn)[i2]*densM[i2]*csections[id][i1][i2]();
      } else
	crossM[i1] += (*Fn)[i2]*densM[i2]*csections[id][i2][i1]();

      if (csections[id][i1][i2]() <= 0.0 || std::isnan(csections[id][i1][i2]())) {
	cout << "INVALID CROSS SECTION! :: " << csections[id][i1][i2]()
	     << " #1 = (" << i1.first << ", " << i1.second << ")"
	     << " #2 = (" << i2.first << ", " << i2.second << ")"
	     << " sigma = " << csections[id][i1][i2]() << std::endl;
	csections[id][i1][i2]() = 0.0; // Zero out
      }

      if (csections[id][i2][i1]() <= 0.0 || std::isnan(csections[id][i2][i1]())) {
	cout << "INVALID CROSS SECTION! :: " << csections[id][i2][i1]()
	     << " #1 = (" << i2.first << ", " << i2.second << ")"
	     << " #2 = (" << i1.first << ", " << i1.second << ")"
	     << " sigma = " << csections[id][i2][i1]() << std::endl;
	csections[id][i2][i1]() = 0.0; // Zero out
      }

    }

    if (it1.second>0 && (crossM[i1] == 0 || std::isnan(crossM[i1]))) {
      cout << "INVALID CROSS SECTION! ::"
	   << " crossM = " << crossM[i1]
	   << " densM = "  <<  densM[i1]
	   << " Fn = "     <<  (*Fn)[i1] << endl;
    }

    lambdaM[i1] = 1.0/crossM[i1];
    collPM [i1] = crossM[i1] * crm * tau;

    meanDens   += densM[i1] ;
    meanCollP  += densM[i1] * collPM[i1];
    meanLambda += densM[i1] * lambdaM[i1];
  }

  // This is the number density-weighted MFP (used for diagnostics
  // only)
  //
  meanLambda /= meanDens;

  // Number-density weighted collision probability (used for
  // diagnostics only)
  //
  meanCollP  /= meanDens;

  // This is the per-species N_{coll}
  //
  totalNsel = 0.0;

  std::map<speciesKey, unsigned>::iterator it1, it2;

  for (it1=c->count.begin(); it1!=c->count.end(); it1++) {
    speciesKey i1 = it1->first;

    for (it2=it1; it2!=c->count.end(); it2++) {
      speciesKey i2 = it2->first;

      // Probability of an interaction of between particles of type 1
      // and 2 for a given particle of type 2
      //
      double Prob = (*Fn)[i2] * densM[i2] * csections[id][i1][i2]() * NTCfac * crm * tau;

      // Count _pairs_ of identical particles only
      //                 |
      //                 v
      if (i1==i2)
	selcM[i1][i2]() = 0.5 * (it1->second-1) *  Prob;
      else
	selcM[i1][i2]() = it1->second * Prob;
      //
      // For double-summing of species A,B and B,A interactions
      // when A != B is list orders A<B and therefore does not double
      // count (see line 951 in Collide.cc)

      totalNsel += selcM[i1][i2]();
    }
  }

  return selcM;
}

Collide::sKey2Amap CollideIon::generateSelectionWeight
(pCell* const c, sKeyDmap* const Fn, double crm, double tau, int id,
 double& meanLambda, double& meanCollP, double& totalNsel)
{
  sKeyDmap            eta, densM, densN, collP, nsigmaM, ncrossM;
  sKey2Amap           selcM;

  // Convert from CHIANTI to system units
  //
  const double cunit = 1e-14/(TreeDSMC::Lunit*TreeDSMC::Lunit);

  // Sample cell
  //
  pCell    *samp = c->sample;
  key_type  ckey = c->mykey;
  if (samp) ckey = samp->mykey;

  // Volume in the cell
  //
  double volc = c->Volume();

  //
  // Cross-section debugging [BEGIN]
  //
  if (CROSS_DBG && id==0) {
    if (nextTime_dbg <= tnow && nCnt_dbg < nCel_dbg) {
      speciesKey i = c->count.begin()->first;
      cross1_dbg.push_back(csections[id][i][i]());
    }
  }
  //
  // Done
  //

  for (auto it1 : c->count) {

    // Only compute if particles of this species is in the cell
    //
    if (it1.second) {
      speciesKey i1 = it1.first;

      // Trace weight, Eta_b
      //
      eta[i1] = ZMList[i1.first] / atomic_weights[i1.first];

      // Mass density scaled by atomic weight in amu.  In the
      // algorithm notes, this is N_b * Eta_b / V.
      //
      densM[i1] = c->Mass(i1) / atomic_weights[i1.first] / volc;

      // Number density of superparticles
      //
      densN[i1] = static_cast<double>(c->Count(i1))/volc;
    }
  }

  if (DEBUG_SL) {

    std::cout << std::endl
	      << printDivider << std::endl
	      << "Cell stats"
	      << ", #=" << c->bods.size() << std::endl
	      << printDivider << std::endl
	      << std::setw(10) << "Species"
	      << std::setw(16) << "eta"
	      << std::setw(16) << "n dens"
	      << std::setw(16) << "m dens"
	      << std::setw(16) << "sp mass"
	      << std::setw(10) << "n count"
	      << std::endl
	      << std::setw(10) << "---------"
	      << std::setw(16) << "---------"
	      << std::setw(16) << "---------"
	      << std::setw(16) << "---------"
	      << std::setw(16) << "---------"
	      << std::setw(10) << "---------"
	      << std::endl;

    for (auto it : c->count) {
      std::ostringstream sout;
      sout << "(" << it.first.first << ", " << it.first.second << ")";
      std::cout << std::setw(10) << sout.str()
		<< std::setw(16) << eta  [it.first]
		<< std::setw(16) << densN[it.first]
		<< std::setw(16) << densM[it.first]
		<< std::setw(16) << c->Mass(it.first)
		<< std::setw(10) << c->Count(it.first)
		<< std::endl;
    }

    std::cout << std::endl
	      << printDivider << std::endl
	      << "Interaction stats"
	      << ", eVel=" << Evel[id]
	      << ", crm="  << crm << std::endl
	      << printDivider << std::endl
	      << std::setw(20) << "Species"
	      << std::setw(16) << "Cross"
	      << std::endl
	      << std::setw(20) << "---------"
	      << std::setw(16) << "---------"
	      << std::endl;

    std::map<speciesKey, unsigned>::iterator it1, it2;

    for (it1=c->count.begin(); it1!=c->count.end(); it1++) {

      if (it1->second==0) continue;

      speciesKey k1 = it1->first;

      for (it2=it1; it2!=c->count.end(); it2++) {

	if (it2->second==0) continue;

	speciesKey k2 = it2->first;

	std::ostringstream sout;
	sout << "<"
	     << k1.first << "," << k1.second << "|"
	     << k2.first << "," << k2.second << ">";
	std::cout << std::setw(20) << sout.str()
		  << std::setw(16) << csections[id][k1][k2]() / cunit
		  << std::endl;
      }
    }
  }

  double meanDens = 0.0;
  meanLambda      = 0.0;
  meanCollP       = 0.0;

  for (auto it1 : c->count) {

    // Only compute if particles of this species is in the cell
    //
    if (it1.second) {

      speciesKey i1 = it1.first;
      ncrossM[i1]   = 0.0;
      nsigmaM[i1]   = 0.0;

      for (auto it2 : c->count) {

	// Only compute if particles of this species is in the cell
	//
	if (it2.second) {

	  speciesKey i2 = it2.first;

	  // Compute the computational cross section (that is, true
	  // cross seciton scaled by number of true particles per
	  // computational particle)

	  double crossT = 0.0;
	  if (i2>=i1)
	    crossT = csections[id][i1][i2]();
	  else
	    crossT = csections[id][i2][i1]();

	  // Choose the trace species of the two (may be neither in
	  // which case it doesn't matter)

	  if (densM[i2] <= densM[i1]) {
	    crossT      *= (*Fn)[i2] * eta[i2];
	    ncrossM[i1] += crossT;
	    nsigmaM[i1] += densN[i2] * crossT;
	  } else {
	    crossT      *= (*Fn)[i1] * eta[i1];
	    ncrossM[i2] += crossT;
	    nsigmaM[i2] += densN[i1] * crossT;
	  }

	  // So, ncrossM is the superparticle cross section for each species

	  // Sanity check debugging
	  //
	  if (csections[id][i1][i2]() <= 0.0 || std::isnan(csections[id][i1][i2]())) {
	    cout << "INVALID CROSS SECTION! :: " << csections[id][i1][i2]()
		 << " #1 = (" << i1.first << ", " << i1.second << ")"
		 << " #2 = (" << i2.first << ", " << i2.second << ")"
		 << " sigma = " << csections[id][i1][i2]() << std::endl;

	    csections[id][i1][i2]() = 0.0; // Zero out
	  }

	  // Sanity check debugging
	  //
	  if (csections[id][i2][i1]() <= 0.0 || std::isnan(csections[id][i2][i1]())) {
	    cout << "INVALID CROSS SECTION! :: " << csections[id][i2][i1]()
		 << " #1 = (" << i2.first << ", " << i2.second << ")"
		 << " #2 = (" << i1.first << ", " << i1.second << ")"
		 << " sigma = " << csections[id][i2][i1]() << std::endl;

	    csections[id][i2][i1]() = 0.0; // Zero out
	  }
	}
      }
    }
  }

  for (auto it1 : c->count) {

    // Only compute if particles of this species is in the cell
    //
    if (it1.second) {

      speciesKey i1 = it1.first;

      // Sanity check debugging
      //
      if (ncrossM[i1] == 0 || std::isnan(ncrossM[i1])) {
	cout << "INVALID CROSS SECTION! ::"
	     << " (" << i1.first << ", " << i1.second << ")"
	     << " nsigmaM = " << nsigmaM [i1]
	     << " ncrossM = " << ncrossM [i1]
	     << " Fn = "      <<   (*Fn) [i1] << endl;

	std::cout << std::endl
		  << std::setw(10) << "Species"
		  << std::setw( 6) << "Inter"
		  << std::setw(16) << "x-section"
		  << std::setw(16) << "sp mass"
		  << std::setw(16) << "n*sigma"
		  << std::setw(16) << "n*cross"
		  << std::endl
		  << std::setw(10) << "---------"
		  << std::setw(16) << "---------"
		  << std::setw(16) << "---------"
		  << std::setw(16) << "---------"
		  << std::setw(16) << "---------"
		  << std::endl;

	for (auto it : csections[id][i1]) {
	  std::ostringstream sout;
	  sout << "(" << it.first.first << ", " << it.first.second << ")";
	  if (!it.second) {
	      cout << std::setw(10) << sout.str()
		   << std::setw(16) << ""
		   << std::setw(16) << c->Mass(it.first)
		   << std::setw(16) << nsigmaM[it.first]
		   << std::setw(16) << ncrossM[it.first]
		   << std::endl;

	  } else {
	    for (auto jt : it.second.v) {
	      cout << std::setw(10) << sout.str()
		   << std::setw(16) << std::get<0>(jt.first)
		   << std::setw(16) << c->Mass(it.first)
		   << std::setw(16) << nsigmaM[it.first]
		   << std::setw(16) << ncrossM[it.first]
		   << std::endl;
	    }
	  }
	}
      }

      collP[i1]   = nsigmaM[i1] * crm * tau;

      meanDens   += densN[i1];
      meanCollP  += densN[i1] * collP  [i1];
      meanLambda += densN[i1] * nsigmaM[i1];
    }
  }


  if (DEBUG_SL) {
    std::cout << std::endl
	      << std::setw(10) << "Species"
	      << std::setw(16) << "count"
	      << std::setw(16) << "sp mass"
	      << std::setw(16) << "n*sigma"
	      << std::setw(16) << "n*cross"
	      << std::setw(16) << "Prob"
	      << std::endl
	      << std::setw(10) << "---------"
	      << std::setw(16) << "---------"
	      << std::setw(16) << "---------"
	      << std::setw(16) << "---------"
	      << std::setw(16) << "---------"
	      << std::setw(16) << "---------"
	      << std::endl;

    for (auto it : c->count) {

      // Only output if particles of this species is in the cell
      //
      if (it.second) {
	double prob = densN[it.first] * ncrossM[it.first] * crm * tau;
	std::ostringstream sout;
	sout << "(" << it.first.first << ", " << it.first.second << ")";
	cout << std::setw(10) << sout.str()
	     << std::setw(16) << it.second
	     << std::setw(16) << c->Mass(it.first)
	     << std::setw(16) << nsigmaM[it.first]
	     << std::setw(16) << ncrossM[it.first]
	     << std::setw(16) << prob
	     << std::endl;
      }
    }
  }

  // This is the number density-weighted MFP (used for diagnostics
  // only)
  //
  meanLambda  = meanDens/meanLambda;

  // Number-density weighted collision probability (used for
  // diagnostics only)
  //
  meanCollP  /= meanDens;

  // This is the per-species N_{coll}
  //
  totalNsel = 0.0;

  std::map<speciesKey, unsigned>::iterator it1, it2;

  for (it1=c->count.begin(); it1!=c->count.end(); it1++) {

    // Only compute if particles of this species is in the cell
    if (it1->second) {

      speciesKey i1 = it1->first;
      
      for (it2=it1; it2!=c->count.end(); it2++) {

	// Only compute if particles of this species is in the cell
	if (it2->second) {

	  speciesKey i2 = it2->first;

	  double crsvel = 0.0;

	  sKeyPair k(i1, i2);
	  if (i1>=i2) k = sKeyPair(i2, i1);

	  pthread_mutex_lock(&tlock);
	  try {
	    crsvel = ntcdb[ckey].CrsVel(k, ntcThresh) * ntcFactor;
	  }
	  catch (NTC::NTCitem::Error &error) {
	    if (i2>=i1)
	      crsvel = csections[id][i1][i2]() * crm;
	    else
	      crsvel = csections[id][i2][i1]() * crm;
	  }
	  pthread_mutex_unlock(&tlock);

	  // Probability of an interaction of between particles of type 1
	  // and 2 for a given particle of type 2
	  //
	  double Prob = 0.0;

	  if (densM[i1]>=densM[i2]) {
	    Prob = (*Fn)[i2] * eta[i2] * cunit * crsvel * tau / volc;
	  } else {
	    Prob = (*Fn)[i1] * eta[i1] * cunit * crsvel * tau / volc;
	  }

	  // Count _pairs_ of identical particles only
	  //                 |
	  //                 |
	  if (i1==i2) //     v
	    selcM[i1][i2]() = 0.5 * it1->second * (it2->second-1) *  Prob;
	  else
	    selcM[i1][i2]() = it1->second * it2->second * Prob;

	  // For debugging only
	  //
	  if (DEBUG_SL) {
	    if (selcM[i1][i2]()>10000.0) {
	      if (ntcdb[ckey].Ready(k)) {
		  double cv1, cv2, cv3;
		  pthread_mutex_lock(&tlock);
		  cv1 = ntcdb[ckey].CrsVel(k, 0.50);
		  cv2 = ntcdb[ckey].CrsVel(k, 0.90);
		  cv3 = ntcdb[ckey].CrsVel(k, 0.95);
		  pthread_mutex_unlock(&tlock);

		  std::cout << std::endl
			    << "Too many collisions: collP=" << meanCollP
			    << ", MFP=" << meanLambda << ", P=" << Prob
			    << ", <sigma*vel>=" << crsvel
			    << ", N=" << selcM[i1][i2]()
			    << ", q(0.5, 0.9, 0.95) = (" << cv1 << ", "
			    << cv2 << ", " << cv3 << "), iVels=("
			    << cVels[id].first[0] << ", "
			    << cVels[id].first[1] << ", "
			    << cVels[id].first[2] << "), eVels=("
			    << cVels[id].second[0] << ", "
			    << cVels[id].second[1] << ", "
			    << cVels[id].second[2] << ")" << std::endl;
	      } else {
		  std::cout << std::endl
			    << "Too many collisions: collP=" << meanCollP
			    << ", MFP=" << meanLambda << ", P=" << Prob
			    << ", <sigma*vel>=" << crsvel
			    << ", N=" << selcM[i1][i2]()
			    << ", NTC not ready"          << std::endl;
	      }
	    }
	  }

	  //
	  // For double-summing of species A,B and B,A interactions
	  // when A != B is list orders A<B and therefore does not double
	  // count (see line 951 in Collide.cc)

	  totalNsel += selcM[i1][i2]();
	}
      }
    }
  }


  if (0) {
    unsigned nbods  = c->bods.size();
    double totalEst = 0.5 * meanCollP * nbods * (nbods-1);
    if (totalNsel > 200.0 && totalNsel > 5.0 * totalEst) {
      std::cout << "Total pairs: " << totalNsel  << std::endl
		<< "  Est pairs: " << totalEst   << std::endl
		<< "     mean P: " << meanCollP  << std::endl
		<< "     bodies: " << nbods      << std::endl;
    }
  }

  if (DEBUG_SL) {

    std::cout << std::endl
	      << std::endl     << std::right
	      << std::setw(16) << "Interact"
	      << std::setw(16) << "N sel"
	      << std::setw(16) << "Prob 0"
	      << std::setw(16) << "Prob 1"
	      << std::endl
	      << std::setw(16) << "--------"
	      << std::setw(16) << "--------"
	      << std::setw(16) << "--------"
	      << std::setw(16) << "--------"
	      << std::endl;

    for (it1=c->count.begin(); it1!=c->count.end(); it1++) {

      // Only output if particles of this species is in the cell
      //
      if (it1->second) {

	for (it2=it1; it2!=c->count.end(); it2++) {

	  // Only output if particles of this species is in the cell
	  //
	  if (it2->second) {

	    speciesKey i1 = it1->first;
	    speciesKey i2 = it2->first;
	    sKeyPair   k(i1, i2);

	    double crsvel = 0.0;
	    pthread_mutex_lock(&tlock);
	    try {
	      crsvel = ntcdb[ckey].CrsVel(k, ntcThresh) * ntcFactor;
	    }
	    catch (NTC::NTCitem::Error &error) {
	      if (i2>=i1)
		crsvel = csections[id][i1][i2]() * crm;
	      else
		crsvel = csections[id][i2][i1]() * crm;
	    }
	    pthread_mutex_unlock(&tlock);

	    double Prob0 = 0.0, Prob1 = 0.0;

	    if (densM[i1]>=densM[i2]) {
	      Prob0 = densM[i2] * (*Fn)[i2] * cunit * crsvel * tau;
	      Prob1 = nsigmaM[i2] * crm * tau;
	    } else {
	      Prob0 = densM[i1] * (*Fn)[i1] * cunit * crsvel * tau;
	      Prob1 = nsigmaM[i1] * crm * tau;
	    }

	    std::cout << "("
		      << std::setw(2)  << i1.first << ","
		      << std::setw(2)  << i1.second << ") ("
		      << std::setw(2)  << i2.first << ","
		      << std::setw(2)  << i2.second << ")  "
		      << std::setw(16) << selcM[i1][i2]()
		      << std::setw(16) << Prob0
		      << std::setw(16) << Prob1
		      << std::endl;
	  }
	}
      }
    }
    std::cout << std::endl
	      << "  Mean Coll P = " << meanCollP
	      << "  Mean Lambda = " << meanLambda
	      << "  MFP/L = "       << meanLambda/pow(volc, 0.333333333)
	      << "  totalNsel = "   << totalNsel
	      << std::endl << std::endl;
  }

  return selcM;
}

Collide::sKey2Amap CollideIon::generateSelectionHybrid
(pCell* const c, sKeyDmap* const Fn, double crm, double tau, int id,
 double& meanLambda, double& meanCollP, double& totalNsel)
{
  sKeyDmap            eta, densM, densN, collP, nsigmaM, ncrossM;
  sKey2Amap           selcM;

  // Convert from CHIANTI to system units
  //
  const double cunit = 1e-14/(TreeDSMC::Lunit*TreeDSMC::Lunit);

  // Sample cell
  //
  pCell    *samp = c->sample;
  key_type  ckey = c->mykey;
  if (samp) ckey = samp->mykey;

  // Volume in the cell
  //
  double volc = c->Volume();

  // Cross-section debugging [BEGIN]
  //
  if (CROSS_DBG && id==0) {
    if (nextTime_dbg <= tnow && nCnt_dbg < nCel_dbg) {
      speciesKey i = c->count.begin()->first;
      for (auto v : csectionsH[id][i][i].v)
	cross1_dbg.push_back(v.second);
    }
  }
  // END: cross-section debugging

  // Compute mean densities
  //
  for (auto it1 : c->count) {

    // Only compute if particles of this species is in the cell
    //
    if (it1.second) {
      speciesKey i1 = it1.first;

      // Trace weight, Eta_b
      //
      eta[i1] = ZMList[i1.first] / atomic_weights[i1.first];

      // Mass density scaled by atomic weight in amu (i.e. number
      // density).  In the algorithm notes, this is N_b * Eta_b / V.
      //
      densM[i1] = c->Mass(i1) / atomic_weights[i1.first] / volc;

      // Number density of superparticles
      //
      densN[i1] = static_cast<double>(c->Count(i1))/volc;
    }
  }

  // DEBUG_SL diagnostic output
  //
  if (DEBUG_SL) {

    std::cout << std::endl
	      << printDivider  << std::endl
	      << "Cell stats"
	      << ", #=" << c->bods.size() << std::endl
	      << printDivider  << std::endl
	      << std::setw(10) << "Species"
	      << std::setw(16) << "eta"
	      << std::setw(16) << "n dens"
	      << std::setw(16) << "m dens"
	      << std::setw(16) << "sp mass"
	      << std::setw(10) << "n count"
	      << std::endl
	      << std::setw(10) << "---------"
	      << std::setw(16) << "---------"
	      << std::setw(16) << "---------"
	      << std::setw(16) << "---------"
	      << std::setw(16) << "---------"
	      << std::setw(10) << "---------"
	      << std::endl;

    for (auto it : c->count) {
      std::ostringstream sout;
      sout << "(" << it.first.first << ", " << it.first.second << ")";
      std::cout << std::setw(10) << sout.str()
		<< std::setw(16) << eta  [it.first]
		<< std::setw(16) << densN[it.first]
		<< std::setw(16) << densM[it.first]
		<< std::setw(16) << c->Mass(it.first)
		<< std::setw(10) << c->Count(it.first)
		<< std::endl;
    }

    std::cout << std::endl
	      << printDivider << std::endl
	      << "Interaction stats"
	      << ", eVel=" << Evel[id]
	      << ", crm="  << crm << std::endl
	      << printDivider << std::endl
	      << std::setw(30) << "Species"
	      << std::setw(16) << "Cross"
	      << std::endl
	      << std::setw(30) << "--------------"
	      << std::setw(16) << "---------"
	      << std::endl;

    std::map<speciesKey, unsigned>::iterator it1, it2;

    for (it1=c->count.begin(); it1!=c->count.end(); it1++) {

      if (it1->second==0) continue;

      speciesKey k1 = it1->first;

      for (it2=c->count.begin(); it2!=c->count.end(); it2++) {

	if (it2->second==0) continue;

	speciesKey k2 = it2->first;

	for (auto v : csectionsH[id][k1][k2].v) {

	  if (std::isnan(v.second))
	    {
	      std::cout << "Crazy cross section" << std::endl;
	    }

	  std::ostringstream sout;
	  sout << "<"
	       << k1.first << "|" << k2.first << ">"
	       << " : " << v.first;

	  std::cout << std::setw(30) << sout.str()
		    << std::setw(16) << v.second / cunit
		    << std::endl;
	}
      }
    }

  } // END: DEBUG_SL diagnostic output

  // Cache time step for estimating "over" cooling timestep is use_delt>=0
  //
  spTau[id]  = tau;

  // This is the per-species N_{coll}
  //
  totalNsel  = 0.0;

  // This is the per-species N_{coll} _without_ any applied collision
  // limitation.  Used to compute inelastic rate correction.
  //
  double uncutNsel = 0.0;

  // Diagnostics to be returned
  //
  meanLambda = 0.0;
  meanCollP  = 0.0;

  std::map<speciesKey, unsigned>::iterator it1, it2;

  for (it1=c->count.begin(); it1!=c->count.end(); it1++) {

    // Only compute if particles of this species is in the cell
    if (it1->second) {

      speciesKey i1 = it1->first;

      for (it2=c->count.begin(); it2!=c->count.end(); it2++) {

	// Only compute if particles of this species is in the cell
	if (it2->second) {

	  speciesKey i2 = it2->first;

	  double crsvel = 0.0, sumNsel = 0.0, fulNsel = 0.0;

	  sKeyPair k(i1, i2);

	  for (auto & v : csectionsH[id][k.first][k.second].v) {

	    if (csectionsH[id][k.first][k.second][v.first] <= 0.0) {
	      continue;
	    }

	    double crs0 = csectionsH[id][k.first][k.second][v.first];

	    double curNsel = 0.0;
	    double iniNsel = 0.0;

	    crsvel = crs0/cunit * NTCfac * crm;

	    pthread_mutex_lock(&tlock);
	    if (ntcdb[ckey].Ready(k, v.first))
	      crsvel = ntcdb[ckey].CrsVel(k, v.first, ntcThresh) * ntcFactor;
	    pthread_mutex_unlock(&tlock);

	    // Probability of an interaction of between particles of type 1
	    // and 2 for a given particle of type 1
	    //
	    double Prob = (*Fn)[i2] * eta[i2] * cunit * crsvel * tau / volc;

	    speciesKey t1(i1);
	    t1.second = std::get<1>(v.first).second.second;

	    meanCollP += meanF[id][t1] * densM[i2] * (*Fn)[i2] * crs0;


	    // Count _pairs_ of identical particles only
	    //                                   |
	    //                                   v
	    if (i1==i2 and std::get<0>(v.first)==neut_neut)
	      curNsel = 0.5 * it1->second * (it2->second-1) *  Prob;
	    else
	      curNsel = it1->second * it2->second * Prob;

	    iniNsel = curNsel;

	    if (maxCoul<UINT_MAX) {
	      // Look for Coulombic interactions only
	      if (std::get<0>(v.first) == ion_elec or
		  std::get<0>(v.first) == ion_ion  ) {
		if (curNsel > maxCoul) {
		  epsmIE[id]++;
		  curNsel = maxCoul;
		}
		totlIE[id]++;
	      }
	    }
	    
	    // For debugging only
	    //
	    if (DEBUG_SL) {
	      if (curNsel > 10000.0) {
		double crsdef =
		  csectionsH[id][k.first][k.second][v.first]/cunit
		  *  NTCfac * crm;
		double cv1=crsdef, cv2=crsdef, cv3=crsdef;
		pthread_mutex_lock(&tlock);
		if (ntcdb[ckey].Ready(k, v.first)) {
		  cv1 = ntcdb[ckey].CrsVel(k, v.first, 0.50);
		  cv2 = ntcdb[ckey].CrsVel(k, v.first, 0.90);
		  cv3 = ntcdb[ckey].CrsVel(k, v.first, 0.95);
		}
		pthread_mutex_unlock(&tlock);

		std::ostringstream sout;
		sout << v.first;

		std::cout << std::endl
			  << "Too many collisions: collP=" << meanCollP
			  << ", MFP=" << meanLambda << ", P=" << Prob
			  << ", <sigma*vel>=" << crsvel
			  << ", I=" << sout.str()
			  << ", N=" << curNsel
			  << ", q(0.5, 0.9, 0.95) = (" << cv1 << ", "
			  << cv2 << ", " << cv3 << "), iVels=("
			  << cVels[id].first[0] << ", "
			  << cVels[id].first[1] << ", "
			  << cVels[id].first[2] << "), eVels=("
			  << cVels[id].second[0] << ", "
			  << cVels[id].second[1] << ", "
			  << cVels[id].second[2] << ")" << std::endl;
	      }
	    } // END: debugging output

	    sumNsel += curNsel;
	    fulNsel += iniNsel;
	  }

	  // For double-summing of species A,B and B,A interactions
	  // when A != B is list orders A<B and therefore does not
	  // double count (see line 951 in Collide.cc)
	  //
	  selcM[i1][i2]() = sumNsel;
	  totalNsel += sumNsel;
	  uncutNsel += fulNsel;
	}
      }
    }
  }

  // This is the number density-weighted MFP (used for diagnostics
  // only)
  //
  meanLambda = 1.0/meanCollP;

  if (0) {
    unsigned nbods  = c->bods.size();
    double totalEst = 0.5 * meanCollP * nbods * (nbods-1);
    if (totalNsel > 200.0 && totalNsel > 5.0 * totalEst) {
      std::cout << "Total pairs: " << totalNsel  << std::endl
		<< "  Est pairs: " << totalEst   << std::endl
		<< "     mean P: " << meanCollP  << std::endl
		<< "     bodies: " << nbods      << std::endl;
    }
  }


  if (collLim) {		// Sanity clamp

    unsigned     nbods  = c->bods.size();
    double       cpbod  = static_cast<double>(totalNsel)/nbods;

    if (totalNsel > maxSel) {
      std::get<0>(clampdat[id]) ++;
      std::get<1>(clampdat[id]) += cpbod;
      std::get<2>(clampdat[id])  = std::max<double>(cpbod, std::get<2>(clampdat[id]));

      totalNsel = 0;
      for (auto & u : selcM) {
	for (auto & v : u.second) {
	  // Clamp to threshold for each interaction type
	  v.second.d = std::min<double>(maxSel, v.second.d);
	  totalNsel += v.second.d;
	}
      }
    }
  }

  if (DEBUG_SL) {

    std::cout << std::endl
	      << std::endl     << std::right
	      << std::setw(20) << "Species"
	      << std::setw(20) << "Interact"
	      << std::setw(16) << "N sel"
	      << std::setw(16) << "Prob 0"
	      << std::setw(16) << "Prob 1"
	      << std::setw(16) << "Dens"
	      << std::setw(16) << "Crs*Vel"
	      << std::endl
	      << std::setw(20) << "--------"
	      << std::setw(20) << "--------"
	      << std::setw(16) << "--------"
	      << std::setw(16) << "--------"
	      << std::setw(16) << "--------"
	      << std::setw(16) << "--------"
	      << std::setw(16) << "--------"
	      << std::endl;

    for (it1=c->count.begin(); it1!=c->count.end(); it1++) {

      // Only output if particles of this species is in the cell
      //
      if (it1->second) {

	for (it2=c->count.begin(); it2!=c->count.end(); it2++) {

	  // Only output if particles of this species is in the cell
	  //
	  if (it2->second) {

	    speciesKey i1 = it1->first;
	    speciesKey i2 = it2->first;
	    sKeyPair   k(i1, i2);

	    for (auto v : selcM[i1][i2].v) {

	      double crsvel =
		csectionsH[id][k.first][k.second][v.first]/cunit
		* NTCfac * crm;

	      pthread_mutex_lock(&tlock);
	      if (ntcdb[ckey].Ready(k, v.first))
		crsvel = ntcdb[ckey].CrsVel(k, v.first, ntcThresh) * ntcFactor;
	      pthread_mutex_unlock(&tlock);
		
	      double Prob0 = 0.0, Prob1 = 0.0, Dens = 0.0;

	      if (densM[i1]>=densM[i2]) {
		Prob0 = densM[i2] * (*Fn)[i2] * cunit * crsvel * tau;
		Prob1 = nsigmaM[i2] * crm * tau;
		Dens  = densM[i2];
	      } else {
		Prob0 = densM[i1] * (*Fn)[i1] * cunit * crsvel * tau;
		Prob1 = nsigmaM[i1] * crm * tau;
		Dens  = densM[i1];
	      }

	      std::ostringstream sout1;
	      sout1 << '(' << std::setw(2)  << i1.first
		    << '|' << std::setw(2)  << i2.first
		    << ')';

	      std::ostringstream sout2;
	      sout2 << v.first;

	      std::cout << std::setw(20) << sout1.str()
			<< std::setw(20) << sout2.str()
			<< std::setw(16) << v.second
			<< std::setw(16) << Prob0
			<< std::setw(16) << Prob1
			<< std::setw(16) << Dens
			<< std::setw(16) << crsvel
			<< std::endl;
	    }
	  }
	}
      }
    }
    std::cout << std::endl
	      << "  Mean Coll P = " << meanCollP
	      << "  Mean Lambda = " << meanLambda
	      << "  MFP/L = "       << meanLambda/pow(volc, 0.333333333)
	      << "  totalNsel = "   << totalNsel
	      << std::endl << std::endl;

  } // END: DEBUG_SL output


  //* BEGIN DEEP DEBUG *//
  if (init_dbg and myid==0 and tnow > init_dbg_time) {

    std::ofstream out(runtag + ".heplus_test_cross", ios::out | ios::app);

    out << std::endl
	<< printDivider <<  std::endl
	<< "Time = " << tnow << std::endl
	<< printDivider << std::endl
	<< std::right
	<< std::setw(20) << "Species"
	<< std::setw(20) << "Interact"
	<< std::setw(16) << "N sel"
	<< std::setw(16) << "Prob 0"
	<< std::setw(16) << "Prob 1"
	<< std::setw(16) << "Dens"
	<< std::setw(16) << "Crs*Vel"
	<< std::setw(16) << "Crs*Vel [0]"
	<< std::endl
	<< std::setw(20) << "--------"
	<< std::setw(20) << "--------"
	<< std::setw(16) << "--------"
	<< std::setw(16) << "--------"
	<< std::setw(16) << "--------"
	<< std::setw(16) << "--------"
	<< std::setw(16) << "--------"
	<< std::setw(16) << "--------"
	<< std::endl;

    for (it1=c->count.begin(); it1!=c->count.end(); it1++) {

      // Only output if particles of this species is in the cell
      //
      if (it1->second) {

	for (it2=c->count.begin(); it2!=c->count.end(); it2++) {

	  // Only output if particles of this species is in the cell
	  //
	  if (it2->second) {

	    speciesKey i1 = it1->first;
	    speciesKey i2 = it2->first;
	    sKeyPair   k(i1, i2);

	    for (auto v : selcM[i1][i2].v) {

	      double crsvel1 =
		csections[id][k.first][k.second][v.first]/cunit
		* NTCfac * crm;

	      double crsvel0 = csectionsH[id][k.first][k.second][v.first];

	      double crsvel = crsvel1;

	      pthread_mutex_lock(&tlock);
	      if (ntcdb[ckey].Ready(k, v.first))
		crsvel = ntcdb[ckey].CrsVel(k, v.first, ntcThresh) * ntcFactor;
	      pthread_mutex_unlock(&tlock);

	      double Prob0 = 0.0, Prob1 = 0.0, Dens = 0.0;


	      if (densM[i1]>=densM[i2]) {
		Prob0 = (*Fn)[i1] * eta[i1] * cunit * crsvel  * tau / volc;
		Prob1 = (*Fn)[i1] * eta[i1] * cunit * crsvel1 * tau / volc;
		Dens  = densM[i2];
	      } else {
		Prob0 = (*Fn)[i2] * eta[i2] * cunit * crsvel  * tau / volc;
		Prob1 = (*Fn)[i2] * eta[i2] * cunit * crsvel1 * tau / volc;
		Dens  = densM[i1];
	      }

	      std::ostringstream sout1;
	      sout1 << '(' << std::setw(2)  << i1.first
		    << '|' << std::setw(2)  << i2.first
		    << ')';
	      std::ostringstream sout2;
	      sout2 << v.first;

	      out << std::setw(20) << sout1.str()
		  << std::setw(20) << sout2.str()
		  << std::setw(16) << v.second
		  << std::setw(16) << Prob0
		  << std::setw(16) << Prob1
		  << std::setw(16) << Dens
		  << std::setw(16) << crsvel
		  << std::setw(16) << crsvel0
		  << std::endl;
	    }
	  }
	}
      }
    }

    out << std::endl
	<< "  Mean Coll P = " << meanCollP
	<< "  Mean Lambda = " << meanLambda
	<< "  MFP/L = "       << meanLambda/pow(volc, 0.333333333)
	<< "  totalNsel = "   << totalNsel
	<< std::endl << printDivider <<  std::endl
	<< std::setw(4)  << "Z"
	<< std::setw(4)  << "C"
	<< std::setw(12) << "Fraction"
	<< std::endl;

    for (auto v : meanF[id]) {
      out << std::setw(4)  << v.first.first
	  << std::setw(4)  << v.first.second
	  << std::setw(12) << v.second
	  << std::endl;
    }

    out << std::endl << std::left
	<< std::setw(14) << "Species"
	<< std::setw( 8) << "Count"
	<< std::endl;

    for (auto it=c->count.begin(); it!=c->count.end(); it++) {
      std::ostringstream sout;
      sout << "(" << it->first.first << ", " << it->first.second << ")";
      out << std::setw(14) << sout.str()
	  << std::setw( 8) << it->second
	  << std::endl;
    }

    out << printDivider <<  std::endl;
  }
  //* END DEEP DEBUG *//

  nselRat[id] = uncutNsel/totalNsel;
  //
  // std::cout << std::setw(10) << c->mykey << ": " << nselRat[id] << std::endl;
  //

  return selcM;
}


//
// Return map of accessible inelastic interations for substates of
// particles p1 and p2
//
Interact CollideIon::generateSelectionHybridSub
(int id, Particle* const p1, Particle* const p2, Interact::T& maxT,
 sKeyDmap* const Fn, double *cr, double tau)
{
  // Map all allowed inelastic interactions for particles p1 and p2
  //
  Interact ret;

  // Convert from CHIANTI to system units and from system to physical
  // time units
  //
  const double cunit = 1e-14 * TreeDSMC::Tunit;

  speciesKey      k1 = KeyConvert(p1->iattrib[use_key]).getKey();
  speciesKey      k2 = KeyConvert(p2->iattrib[use_key]).getKey();
  unsigned short  Z1 = k1.first, Z2 = k2.first;

  // Compute the reduced mass (probably accuracy overkill)
  //
  double          m1 = atomic_weights[Z1] * amu;
  double          me = atomic_weights[0 ] * amu;
  double          mu = me * m1 / (me + m1);

  // Get relative velocity and energy between ion from p1 and electron
  // from p2
  //
  double eVel = 0.0;
  *cr = 0.0;
  for (unsigned i=0; i<3; i++) {
    double rvel = p2->dattrib[use_elec+i] - p1->vel[i];
    eVel += rvel * rvel;

    rvel = p2->vel[i] - p1->vel[i];
    *cr += rvel * rvel;
  }
  eVel = sqrt(eVel) * TreeDSMC::Vunit;
  *cr  = sqrt(*cr);

  // Available COM energy in eV
  //
  double ke = std::max<double>(0.5*mu*eVel*eVel/eV, FloorEv);

				// Cache assigned energy
  kEe1[id] = kEe2[id] = ke;

				// For debugging diagnostics
  if (elecDist) elecEVsub[id].push_back(ke);

  // Loop through ionization levels in p1.  p2 only donates an
  // electron velocity and its weight does not matter.
  //
  for (unsigned Q1=0; Q1<=Z1; Q1++) {

				// Ion key: lq(Z, C) where C=1 is the
    lQ Q(Z1, Q1+1);		// ground state

    unsigned PP1 = std::get<1>(maxT).second.second-1;
    unsigned PP2 = std::get<2>(maxT).second.second-1;

    //-------------------------------
    // *** Free-free
    //-------------------------------
    if (Q1>0) {

      FFm[id][Q]  = ch.IonList[Q]->freeFreeCross(ke, id);
      double Prob = densE[id][k2] * FFm[id][Q].first * cunit * eVel * tau;

      if (suppress_maxT and std::get<0>(maxT) == free_free and PP1 == Q1)
	{
	  Prob *= 1.0 - p2->dattrib[spc_pos + PP2];
	}

      if (Prob > 0.0) {
	Interact::T t(free_free, {Interact::ion, speciesKey(Z1, Q1+1)}, {Interact::electron, speciesKey(Z2, 0)});
	ret[t] = Prob;
      }
    }

    //-------------------------------
    // *** Collisional excitation
    //-------------------------------
    if (Q1 < Z1) {

      CEm[id][Q]  = ch.IonList[Q]->collExciteCross(ke, id);
      double crs  = CEm[id][Q].back().first;
      double Prob = densE[id][k2] * crs * cunit * eVel * tau;

      if (suppress_maxT and std::get<0>(maxT) == colexcite and PP1 == Q1)
	{
	  Prob *= 1.0 - p2->dattrib[spc_pos + PP2];
	}

      if (Prob > 0.0) {
	Interact::T t(colexcite, {Interact::ion, speciesKey(Z1, Q1+1)}, {Interact::electron, speciesKey(Z2, 0)});
	ret[t] = Prob;
      }
    }

    //-------------------------------
    // *** Ionization cross section
    //-------------------------------
    if (Q1 < Z1) {

      double crs  = ch.IonList[Q]->directIonCross(ke, id);
      double Prob = densE[id][k2] * crs * cunit * eVel * tau;

      if (suppress_maxT and std::get<0>(maxT) == ionize and PP1 == Q1)
	{
	  Prob *= 1.0 - p2->dattrib[spc_pos + PP2];
	}

      if (Prob > 0.0) {
	Interact::T t(ionize, {Interact::ion, speciesKey(Z1, Q1+1)}, {Interact::electron, speciesKey(Z2, 0)});
	ret[t] = Prob;
      }
    }

    //-------------------------------
    // *** Radiative recombination
    //-------------------------------
    if (Q1 > 0) {

      std::vector<double> RE1 = ch.IonList[Q]->radRecombCross(ke, id);
      double crs = RE1.back();
      double Prob = densE[id][k2] * crs * cunit * eVel * tau;

      if (suppress_maxT and std::get<0>(maxT) == recomb and PP1 == Q1)
	{
	  Prob *= 1.0 - p2->dattrib[spc_pos + PP1];
	}

      if (Prob > 0.0) {
	Interact::T t(recomb, {Interact::ion, speciesKey(Z1, Q1+1)}, {Interact::electron, speciesKey(Z2, 0)});
	ret[t] = Prob;
      }
    }
  }

  return ret;
}


Collide::sKey2Amap CollideIon::generateSelectionTrace
(pCell* const c, sKeyDmap* const Fn, double crm, double tau, int id,
 double& meanLambda, double& meanCollP, double& totalNsel)
{
  speciesKey key(Particle::defaultKey);

  // Convert from NTCdb to system units
  //
  const double crs_units = 1e-14 / (TreeDSMC::Lunit*TreeDSMC::Lunit);

  // For NTCdb <cross section>*<relative velocity>
  //
  const sKeyPair defKeyPair(Particle::defaultKey, Particle::defaultKey);

  // Mass density in the cell
  //
  double volc = c->Volume();
  double dens = c->Mass() / volc;

  // Number of bodies in this cell
  //
  unsigned num = static_cast<unsigned>(c->bods.size());
  
  //
  // Cross-section debugging [BEGIN]
  //

  if (CROSS_DBG && id==0) {
    if (nextTime_dbg <= tnow && nCnt_dbg < nCel_dbg) {
      speciesKey i = c->count.begin()->first;
      cross1_dbg.push_back(csections[id][i][i]());
    }
  }
  // Done

  // Sanity check
  //
  if (std::isnan(csections[id][key][key]()) or csections[id][key][key]() < 0.0) {
    cout << "[" << myid << "] INVALID CROSS SECTION! :: "
	 << csections[id][key][key]() << std::endl;

    // Verbose debugging
    //
    if (EXCESS_DBG) {
      cout << "[" << myid << "] SpList size :: " << SpList .size() << std::endl;
      cout << "[" << myid << "] C body size :: " << c->bods.size() << std::endl;

      keyWghts mW;
      for (auto s : SpList) mW[s.first] = 0.0;
      double massP = 0.0;
      for (auto b : c->bods) {
	Particle *p = tree->Body(b);
	massP += p->mass;
	for (auto s : SpList)
	  mW[s.first] += p->mass * p->dattrib[s.second];
      }
      for (auto s : SpList)
	std::cout << std::setw(3) << s.first.first
		  << std::setw(3) << s.first.second
		  << " : "
		  << std::setw(18) << mW[s.first]/massP
		  << std::endl;
    }

    // Zero out
    //
    csections[id][key][key]() = 0.0;
  }

  // Cache relative velocity
  //
  spCrm[id] = crm;


  // Cross section selection
  //
  double crossRat = csections[id][key][key](), crossRatDB = 0.0;


  // Use NTCdb?
  //
  if (use_ntcdb) {
    pthread_mutex_lock(&tlock);
    if (ntcdb[c->mykey].Ready(defKeyPair, NTC::Interact::single))
      crossRatDB = ntcdb[c->mykey].CrsVel(defKeyPair, NTC::Interact::single, ntcThresh) * ntcFactor * crs_units/crm;
    pthread_mutex_unlock(&tlock);

    // This is a kludgy sanity check . . . 
    //
    if (crossRatDB>tolCS*crossRat) crossRat = crossRatDB;
    else {
      if (crossRatDB == 0.0) crZero[id]++;
      else                   crMiss[id]++;
    }
  } else {
    crossRatDB = crossRat;
  }
  
  crTotl[id]++;

  // Compute collision rates in system units
  //
  double crossM = (*Fn)[key] * dens * crossRat;
  double collPM = crossM * crm * tau;

  // Interaction rate
  //
  double rateF = (*Fn)[key] * crm * tau;

  // Cache time step for estimating "over" cooling timestep is use_delt>=0
  //
  spTau[id]  = tau;

  // For Collide diagnostics
  //
  meanLambda = 1.0/crossM;
  meanCollP  = collPM;

  double Prob  = dens * rateF * crossRat;
  double selcM = (num-1) * Prob * 0.5;
  //              ^
  //              |
  //              +--- For correct Poisson statistics
  //

  double  dfac = TreeDSMC::Munit/amu/pow(TreeDSMC::Lunit, 3.0) / molP1[id];
  double totPairs = num * dens * dfac *
    (1.0/PiProb[id][0] + 1.0/PiProb[id][1] + 1.0/PiProb[id][2]);

  totPairs = 0.5*(num + 0.5);

  colCf[id] = selcM/totPairs;
  selcM = totPairs;

  colUps[id][0] += 1;
  colUps[id][1] += selcM;
  colUps[id][2] += selcM * selcM;
  colUps[id][3] += colCf[id];

  if (collLim) {		// Sanity clamp

    unsigned     nbods  = c->bods.size();
    double       cpbod  = selcM/nbods;

    colSc[id] = 1.0;

    if (selcM > maxSel) {
      std::get<0>(clampdat[id]) ++;
      std::get<1>(clampdat[id]) += cpbod;
      std::get<2>(clampdat[id])  = std::max<double>(cpbod, std::get<2>(clampdat[id]));

      colSc[id] = selcM/maxSel;
      selcM = std::min<double>(maxSel, selcM);
    }
  }

  // Cache probability of an interaction of between the particles pair
  // and number of pairs predicted for use in inelasticTrace
  //
  spProb[id] = Prob;
  spNsel[id] = selcM;
  totalNsel  = selcM;

  sKey2Amap ret;
  ret[Particle::defaultKey][Particle::defaultKey]() = selcM;

  return ret;
}


void CollideIon::write_cross_debug()
{
  std::ofstream out(cross_debug.c_str(), ios::out | ios::app);
  for (int i=0; i<nCel_dbg; i++) {
    double diff = cross2_dbg[i] - cross1_dbg[i];
    if (cross1_dbg[i]>0.0) diff /= cross1_dbg[i];
    out << std::setw( 8) << i+1
	<< std::setw(18) << tnow
	<< std::setw(18) << cross1_dbg[i]
	<< std::setw(18) << cross2_dbg[i]
	<< std::setw(18) << diff
	<< std::endl;
  }
  nextTime_dbg += delTime_dbg;
  nCnt_dbg = 0;
  cross1_dbg.clear();
  cross2_dbg.clear();
}


void CollideIon::gatherSpecies()
{
  double mass  = 0.0;

  consE = 0.0;
  consG = 0.0;
  tM    = {0.0, 0.0, 0.0, 0.0};

  specI.clear();
  specE.clear();

  DTup dtup_0(0.0, 0.0);
  std::array<DTup, 3> a_0 = {dtup_0, dtup_0, dtup_0};
  ZTup ztup_0(a_0, 0.0);

  // Recombination diagnostics
  //
  if (scatter_check and recomb_check) {
    StatsMPI::Return comb = recombA[0].stats();

    for (int n=1; n<nthrds; n++) { // Merge into first thread
      StatsMPI::Return ret = recombA[n].stats();
      for (auto u : ret) {
	speciesKey k = u.first;
	if (comb.find(k) == comb.end()) {
	  comb[k] = u.second;
	} else {
	  for (int j=0; j<4; j++) comb[k][j] += u.second[j];
	}
      }
    }
    
    for (int n=0; n<nthrds; n++) // Reset the counters
      recombA[n].clear();

    recombTally.clear();
    if (myid==0) recombTally = comb;

    for (int n=1; n<numprocs; n++) {
      if (myid==n) {
	int num = comb.size();
	MPI_Send(&num,               1, MPI_INT,           0, 2000, MPI_COMM_WORLD);
	for (auto v : comb) {
	  unsigned short Q[2];
	  Q[0] = v.first.first;
	  Q[1] = v.first.second;
	  MPI_Send(&Q[0],        2, MPI_UNSIGNED,      0, 2001, MPI_COMM_WORLD);
	  MPI_Send(&v.second[0], 4, MPI_DOUBLE,        0, 2002, MPI_COMM_WORLD);
	}
      }
      
      if (myid==0) {
	int num;
	MPI_Recv(&num,           1, MPI_INT,           n, 2000, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	for (int i=0; i<num; i++) {
	  unsigned short Q[2];
	  std::array<double, 4> v3;
	  MPI_Recv(&Q[0],        2, MPI_UNSIGNED,      n, 2001, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  MPI_Recv(&v3,          4, MPI_DOUBLE,        n, 2002, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  
	  speciesKey k(Q[0], Q[1]);
	  if (recombTally.find(k) == recombTally.end()) {
	    for (int j=0; j<4; j++) recombTally[k][j]  = v3[j];
	  } else {
	    for (int j=0; j<4; j++) recombTally[k][j] += v3[j];
	  }
	}
      }
    }
  }

  // specM is the mass in each internal state
  //
  if (aType==Hybrid) {
    specM.clear();
    for (auto Z : ZList) {
      for (speciesKey k(Z, 1);  k.second<Z+2; k.second++) {
	specM[k] = 0.0;
      }
    }
  }

  if (aType==Trace) {
    specM.clear();
    for (auto s : SpList) {
      specM[s.first] = 0.0;
    }
  }

  // Iterate through all cells
  //
  pHOT_iterator itree(*c0->Tree());
  
  while (itree.nextCell()) {

    pCell *cell = itree.Cell();

    double KEi=0.0, numbI=0.0;
    double KEe=0.0, numbE=0.0;
    
    for (auto b : cell->bods) {
      Particle *p = c0->Tree()->Body(b);
      double   mu = 0.0;	// Inverse molecular weight

      mass += p->mass;		// Mass accumulation

      if (aType==Trace) {
	for (auto s : SpList) {
	  speciesKey     k = s.first;
	  unsigned short Z = k.first;
	  mu += p->dattrib[s.second] / atomic_weights[Z];
	}
      } else {
	KeyConvert k(p->iattrib[use_key]);
	mu = 1.0/atomic_weights[k.Z()];
      }

      for (int k=0; k<3; k++) {
	double vi = p->vel[k];
	KEi += 0.5 * p->mass * vi * vi;
      }

      numbI += p->mass * mu;
    }

    tM[0] += KEi;
    tM[1] += numbI;

    if (use_cons >= 0) {
      for (auto b : cell->bods) {
	Particle *p  = c0->Tree()->Body(b);

	consE += p->dattrib[use_cons];
	if (use_elec>=0 and elc_cons)
	  consG += p->dattrib[use_elec+3];
      }
    }
    
    if (aType==Hybrid) {
      for (auto b : cell->bods) {
	Particle *p  = c0->Tree()->Body(b);
	speciesKey k = KeyConvert(p->iattrib[use_key]).getKey();
	for (k.second=1; k.second<=k.first+1; k.second++) {
	  specM[k] += p->mass * p->dattrib[spc_pos+k.second-1];
	}
      }
    }

    if (aType==Trace) {
      for (auto b : cell->bods) {
	Particle *p  = c0->Tree()->Body(b);
	for (auto v : SpList) {
	  specM[v.first] += p->mass * p->dattrib[v.second];
	}
      }
    }

    // Compute electron temperature
    //
    if (use_elec >= 0) {
      
      for (auto b : cell->bods) {
	Particle *p = c0->Tree()->Body(b);
	double countE = 0.0;	// Number of electrons
	double mu = 0.0;	// Inverse molecular weight
	  
	// Compute effective number of electrons
	//
	if (aType==Trace) {
	  
	  for (auto s : SpList) {
	    unsigned short Z = s.first.first;
	    unsigned short P = s.first.second - 1;
	    countE += p->dattrib[s.second] / atomic_weights[Z] * P;
	    mu     += p->dattrib[s.second] / atomic_weights[Z];
	  }
	  
	  countE *= p->mass;
	  mu     *= p->mass;
	} else {
	  KeyConvert k(p->iattrib[use_key]);
	    
	  if (aType==Hybrid) {
	    for (unsigned short C=1; C<=k.Z(); C++)
	      countE += p->dattrib[spc_pos+C] * C;
	  } else {
	    countE = k.C() - 1;
	  }
	  countE *= p->mass/atomic_weights[k.Z()];
	  mu      = p->mass/atomic_weights[k.Z()];
	}

	for (unsigned k=0; k<3; k++) {
	  double ve = p->dattrib[use_elec+k];
	  KEe += 0.5 * countE * atomic_weights[0] * ve*ve;
	}

	if (MeanMass)  numbE += mu;
	else           numbE += countE;
      }
      // END: body loop

      tM[2] += KEe;
      tM[3] += numbE;

      // Temp computation
      // ----------------
      // IONS:
      //
      // N_i   = \sum_j count_j where count_j = m_j/(mu_j * m_a)
      // KE_i  = \sum_k \sum_j count_j * mu_j * m_a * v_{i,j,k)^2
      // 3/2 N_i k T_i = KE_i
      // Temperature = T_i = 2/(3*k) * \sum_k \sum_j count_j v_{i,j,k)^2 / N_i
      //
      // ELECTRONS:
      //
      // N_e   = \sum_j count_j where count_j = m_j*eta_j/(mu_j * m_a)
      //                      and eta_j is the number of electrons per ion
      // eta_j = \sum_i f_i C_i/w_i / sum_i f_i/w_i
      //                      where f_i is the mass fraction in state i, w_i is the atomic weight
      //                      and C_i is the charge
      // mu_j  = 1.0/[\sum_i f_i/w_i]  so eta_j/mu_j = \sum_i f_i C_i/w_i
      // KE_e  = \sum_k \sum_j count_j * m_e * v_{e,j,k)^2
      // 3/2 N_e k T = KE_e
      // Temperature = T_e = 2/(3*k) * \sum_k \sum_j count_j v_{e,j,k)^2 / N_e


      // Compute ion and electron kinetic energy per element
      //
      for (auto b : cell->bods) {
	Particle *p = c0->Tree()->Body(b);
	double masI = p->mass;
	
	if (aType == Hybrid) {
	  
	  unsigned Z  = KeyConvert(p->iattrib[use_key]).Z();
	  double masE = p->mass * atomic_weights[0] / atomic_weights[Z];

	  if (specI.find(Z) == specI.end()) specI[Z] = ztup_0;
	  if (specE.find(Z) == specE.end()) specE[Z] = ztup_0;
	    
	  for (size_t j=0; j<3; j++) {
	    double v = p->vel[j];
	    std::get<0>(std::get<0>(specI[Z])[j]) += masI*v;
	    std::get<1>(std::get<0>(specI[Z])[j]) += masI*v*v;
	  }
	  std::get<1>(specI[Z]) += masI;
	    
	  double eta = 0.0;
	  for (unsigned short C=1; C<=Z; C++)
	    eta += p->dattrib[spc_pos+C] * C;

	  masE *= eta;
	      
	  for (size_t j=0; j<3; j++) {
	    double v = p->dattrib[use_elec+j];
	    std::get<0>(std::get<0>(specE[Z])[j]) += masE*v;
	    std::get<1>(std::get<0>(specE[Z])[j]) += masE*v*v;
	  }
	  std::get<1>(specE[Z]) += masE;
	  
	} // end: Hybrid
	else if (aType == Trace) {

	  if (specI.find(0) == specI.end()) specI[0] = ztup_0;
	  if (specE.find(0) == specE.end()) specE[0] = ztup_0;

	  for (size_t j=0; j<3; j++) {
	    double v = p->vel[j];
	    std::get<0>(std::get<0>(specI[0])[j]) += masI*v;
	    std::get<1>(std::get<0>(specI[0])[j]) += masI*v*v;
	  }
	  std::get<1>(specI[0]) += masI;
	  
	  double masE = 0.0;
	  for (auto s : SpList) {
	    masE += p->dattrib[s.second] * (s.first.second - 1) *
	      atomic_weights[0] /atomic_weights[s.first.first];
	  }
	  
	  for (size_t j=0; j<3; j++) {
	    double v = p->dattrib[use_elec+j];
	    std::get<0>(std::get<0>(specE[0])[j]) += masE*v;
	    std::get<1>(std::get<0>(specE[0])[j]) += masE*v*v;
	  }
	  std::get<1>(specE[0]) += masE;

	} // end: Trace
	else {
	  
	  unsigned Z  = KeyConvert(p->iattrib[use_key]).Z();
	  double masE = p->mass * atomic_weights[0] / atomic_weights[Z];
	  
	  if (specI.find(Z) == specI.end()) specI[Z] = ztup_0;
	  
	  for (size_t j=0; j<3; j++) {
	    double v = p->vel[j];
	    std::get<0>(std::get<0>(specI[Z])[j]) += masI*v;
	    std::get<1>(std::get<0>(specI[Z])[j]) += masI*v*v;
	  }
	  std::get<1>(specI[Z]) += masI;

	  if (KeyConvert(p->iattrib[use_key]).C()==1) continue;

	  if (specE.find(Z) == specE.end()) specE[Z] = ztup_0;

	  masE *= (KeyConvert(p->iattrib[use_key]).C() - 1);

	  for (size_t j=0; j<3; j++) {
	    double v = p->dattrib[use_elec+j];
	    std::get<0>(std::get<0>(specE[Z])[j]) += masE*v;
	    std::get<1>(std::get<0>(specE[Z])[j]) += masE*v*v;
	  }
	  std::get<1>(specE[Z]) += masE;
	  
	} // end: collision methods

      } // end: cell body loop

    } // end: use_elec>=0

  } // end: cell loop

    
  if (ElectronEPSM) {
				// Get combined counts from all threads
    unsigned totl = 0, epsm = 0;
    for (auto & v : totlES) { totl += v; v = 0; }
    for (auto & v : epsmES) { epsm += v; v = 0; }
				// Accumulate at root
    MPI_Reduce(&totl, &totlES0, 1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&epsm, &epsmES0, 1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);
  }

  if ( (aType==Hybrid or aType==Trace) and maxCoul < UINT_MAX) {
				// Get combined counts from all threads
    unsigned totl = 0, epsm = 0;
    for (auto & v : totlIE) { totl += v; v = 0; }
    for (auto & v : epsmIE) { epsm += v; v = 0; }
    // Accumulate at root
    MPI_Reduce(&totl, &totlIE0, 1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&epsm, &epsmIE0, 1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);
  }

  // Send values to root
  //
  double val1 = 0.0, val3 = 0.0, val4 = 0.0;
  std::array<double, 4> v;

  if (aType!=Hybrid and aType!=Trace and COLL_SPECIES) {
    for (int t=1; t<nthrds; t++) {
      for (auto s : collCount[t]) {
	if (collCount[0].find(s.first) == collCount[0].end())
	  collCount[0][s.first]  = s.second;
	else {
	  collCount[0][s.first][0] += s.second[0];
	  collCount[0][s.first][1] += s.second[1];
	}
      }
    }
  }

  for (int i=1; i<numprocs; i++) {

    if (i == myid) {
				// Mass
      MPI_Send(&mass,  1, MPI_DOUBLE, 0, 331, MPI_COMM_WORLD);
				// Energies
      MPI_Send(&tM[0], 4, MPI_DOUBLE, 0, 332, MPI_COMM_WORLD);
				// Conservation counters
      MPI_Send(&consE, 1, MPI_DOUBLE, 0, 333, MPI_COMM_WORLD);
      MPI_Send(&consG, 1, MPI_DOUBLE, 0, 334, MPI_COMM_WORLD);

				// Energies
      if (use_elec >= 0) {
				// Local ion map size
	int sizm = specE.size();
	MPI_Send(&sizm,  1, MPI_INT,    0, 338, MPI_COMM_WORLD);

				// Send local ion map
	for (auto i : specI) {
	  unsigned short Z = i.first;
	  MPI_Send(&Z, 1, MPI_UNSIGNED_SHORT, 0, 339, MPI_COMM_WORLD);
	  std::vector<double> tmp(6);
	  for (size_t j=0; j<3; j++) {
	    tmp[j+0] = std::get<0>(std::get<0>(i.second)[j]);
	    tmp[j+3] = std::get<1>(std::get<0>(i.second)[j]);
	  }
	  double count = std::get<1>(i.second);


	  MPI_Send(&tmp[0], 6, MPI_DOUBLE,    0, 340, MPI_COMM_WORLD);
	  MPI_Send(&count,  1, MPI_DOUBLE,    0, 341, MPI_COMM_WORLD);
	}

				// Local electron map size
	sizm = specE.size();
	MPI_Send(&sizm,  1, MPI_INT,    0, 342, MPI_COMM_WORLD);

				// Send local electron map
	for (auto e : specE) {
	  unsigned short Z = e.first;
	  MPI_Send(&Z, 1, MPI_UNSIGNED_SHORT, 0, 343, MPI_COMM_WORLD);
	  std::vector<double> tmp(6);
	  for (size_t j=0; j<3; j++) {
	    tmp[j+0] = std::get<0>(std::get<0>(e.second)[j]);
	    tmp[j+3] = std::get<1>(std::get<0>(e.second)[j]);
	  }
	  double count = std::get<1>(e.second);

	  MPI_Send(&tmp[0], 6, MPI_DOUBLE,    0, 344, MPI_COMM_WORLD);
	  MPI_Send(&count,  1, MPI_DOUBLE,    0, 345, MPI_COMM_WORLD);
	}

      } // end: use_elec>=0


      if (aType==Hybrid or aType==Trace) {

	int siz = specM.size();

	MPI_Send(&siz, 1, MPI_INT,            0, 352, MPI_COMM_WORLD);
	
	for (auto e : specM) {
	  unsigned short Z = e.first.first;
	  unsigned short C = e.first.second;
	  double         M = e.second;
	  
	  MPI_Send(&Z, 1, MPI_UNSIGNED_SHORT, 0, 353, MPI_COMM_WORLD);
	  MPI_Send(&C, 1, MPI_UNSIGNED_SHORT, 0, 354, MPI_COMM_WORLD);
	  MPI_Send(&M, 1, MPI_DOUBLE,         0, 355, MPI_COMM_WORLD);
	}
      }

      if (aType!=Hybrid and aType!=Trace and COLL_SPECIES) {
	for (auto s : collCount[0]) {
	  speciesKey k1 = s.first.first;
	  speciesKey k2 = s.first.second;
	  MPI_Send(&k1.first,    1, MPI_UNSIGNED_SHORT, 0, 346,
		   MPI_COMM_WORLD);
	  MPI_Send(&k1.second,   1, MPI_UNSIGNED_SHORT, 0, 347,
		   MPI_COMM_WORLD);
	  MPI_Send(&k2.first,    1, MPI_UNSIGNED_SHORT, 0, 348,
		   MPI_COMM_WORLD);
	  MPI_Send(&k2.second,   1, MPI_UNSIGNED_SHORT, 0, 349,
		   MPI_COMM_WORLD);
	  MPI_Send(&s.second[0], 1, MPI_UNSIGNED_LONG,  0, 350,
		   MPI_COMM_WORLD);
	  MPI_Send(&s.second[1], 1, MPI_UNSIGNED_LONG,  0, 351,
		   MPI_COMM_WORLD);
	}
	unsigned short Z = 255;
	MPI_Send(&Z, 1, MPI_UNSIGNED_SHORT, 0, 346, MPI_COMM_WORLD);
      }

    }	// end: myid>0

				// Root receives from Node i
    if (0 == myid) {

      MPI_Recv(&val1, 1, MPI_DOUBLE, i, 331, MPI_COMM_WORLD,
	       MPI_STATUS_IGNORE);
      MPI_Recv(&v[0], 4, MPI_DOUBLE, i, 332, MPI_COMM_WORLD,
	       MPI_STATUS_IGNORE);
      MPI_Recv(&val3, 1, MPI_DOUBLE, i, 333, MPI_COMM_WORLD,
	       MPI_STATUS_IGNORE);
      MPI_Recv(&val4, 1, MPI_DOUBLE, i, 334, MPI_COMM_WORLD,
	       MPI_STATUS_IGNORE);

      if (use_elec >= 0) {

	int sizm;
				// Receive ion map size
	MPI_Recv(&sizm, 1, MPI_INT, i, 338, MPI_COMM_WORLD,
		 MPI_STATUS_IGNORE);

				// Receive ion map
	for (int j=0; j<sizm; j++) {

	  double count;
	  unsigned short Z;
	  std::vector<double> tmp(6);

	  MPI_Recv(&Z, 1, MPI_UNSIGNED_SHORT, i, 339, MPI_COMM_WORLD,
		   MPI_STATUS_IGNORE);
	  MPI_Recv(&tmp[0], 6, MPI_DOUBLE,    i, 340, MPI_COMM_WORLD,
		   MPI_STATUS_IGNORE);
	  MPI_Recv(&count,  1, MPI_DOUBLE,    i, 341, MPI_COMM_WORLD,
		   MPI_STATUS_IGNORE);

	  if (specI.find(Z) == specI.end()) specI[Z] = ztup_0;

	  for (size_t j=0; j<3; j++) {
	    std::get<0>(std::get<0>(specI[Z])[j]) += tmp[j+0];
	    std::get<1>(std::get<0>(specI[Z])[j]) += tmp[j+3];
	  }
	  std::get<1>(specI[Z]) += count;
	}

				// Receive electron map size
	MPI_Recv(&sizm, 1, MPI_INT, i, 342, MPI_COMM_WORLD,
		 MPI_STATUS_IGNORE);

				// Receive electron map
	for (int j=0; j<sizm; j++) {

	  double count;
	  unsigned short Z;
	  std::vector<double> tmp(6);

	  MPI_Recv(&Z, 1, MPI_UNSIGNED_SHORT, i, 343, MPI_COMM_WORLD,
		   MPI_STATUS_IGNORE);
	  MPI_Recv(&tmp[0], 6, MPI_DOUBLE,    i, 344, MPI_COMM_WORLD,
		   MPI_STATUS_IGNORE);
	  MPI_Recv(&count,  1, MPI_DOUBLE,    i, 345, MPI_COMM_WORLD,
		   MPI_STATUS_IGNORE);

	  if (specE.find(Z) == specE.end()) specE[Z] = ztup_0;

	  for (size_t j=0; j<3; j++) {
	    std::get<0>(std::get<0>(specE[Z])[j]) += tmp[j+0];
	    std::get<1>(std::get<0>(specE[Z])[j]) += tmp[j+3];
	  }
	  std::get<1>(specE[Z]) += count;
	}
	
      }

      if (aType==Hybrid or aType==Trace) {
	speciesKey k;
	double V;
	int siz;
	
	MPI_Recv(&siz,        1, MPI_INT,            i, 352, MPI_COMM_WORLD,
		 MPI_STATUS_IGNORE);

	for (int j=0; j<siz; j++) {
	  MPI_Recv(&k.first,  1, MPI_UNSIGNED_SHORT, i, 353, MPI_COMM_WORLD,
		   MPI_STATUS_IGNORE);
	  MPI_Recv(&k.second, 1, MPI_UNSIGNED_SHORT, i, 354, MPI_COMM_WORLD,
		   MPI_STATUS_IGNORE);
	  MPI_Recv(&V,        1, MPI_DOUBLE,         i, 355, MPI_COMM_WORLD,
		   MPI_STATUS_IGNORE);

	  specM[k] += V;
	}
      }

      if (aType!=Hybrid and aType!=Trace and COLL_SPECIES) {
	speciesKey k1, k2;
	CollCounts N;
	while (1) {
	  MPI_Recv(&k1.first,  1, MPI_UNSIGNED_SHORT, i, 346, MPI_COMM_WORLD,
		   MPI_STATUS_IGNORE);
	  if (k1.first==255) break;
	  MPI_Recv(&k1.second, 1, MPI_UNSIGNED_SHORT, i, 347, MPI_COMM_WORLD,
		   MPI_STATUS_IGNORE);
	  MPI_Recv(&k2.first,  1, MPI_UNSIGNED_SHORT, i, 348, MPI_COMM_WORLD,
		   MPI_STATUS_IGNORE);
	  MPI_Recv(&k2.second, 1, MPI_UNSIGNED_SHORT, i, 349, MPI_COMM_WORLD,
		   MPI_STATUS_IGNORE);
	  MPI_Recv(&N[0],      1, MPI_UNSIGNED_LONG,  i, 350, MPI_COMM_WORLD,
		   MPI_STATUS_IGNORE);
	  MPI_Recv(&N[1],      1, MPI_UNSIGNED_LONG,  i, 351, MPI_COMM_WORLD,
		   MPI_STATUS_IGNORE);
	  
	  dKey k(k1, k2);
	  if (collCount[0].find(k) == collCount[0].end())
	    collCount[0][k] = N;
	  else {
	    collCount[0][k][0] += N[0];
	    collCount[0][k][1] += N[1];
	  }
	}
      }
      
      mass  += val1;
      tM[0] += v[0];
      tM[1] += v[1];
      tM[2] += v[2];
      tM[3] += v[3];
      consE += val3;
      consG += val4;
      
    } // end: myid==0
    
  } // end: numprocs

  if (mass>0.0) {
    if (aType == Hybrid or aType == Trace) {
      for (auto & e : specM) e.second /= mass;
    }
  }
  
  // Temporary debug for elastic scattering counts
  //
  if (scatter_check and (aType==Hybrid or aType==Trace)) {

    // Sum over all threads
    TypeMap0 scat, totl;
    TypeMap1 totu;
    TypeMap2 totd;
    std::map<int, TypeMap2> taly;
    std::array<double, 3>   Etots {0, 0, 0};
    std::array<double, 3>   Vtots {0, 0, 0};
    std::array<unsigned, 2> Ntot  {0, 0   }, NT;
    std::array<unsigned, 3> CStt  {0, 0, 0}, NC;

    for (int t=0; t<nthrds; t++) {
      for (auto v : Escat[t]) scat[v.first] += v.second;
      for (auto v : Etotl[t]) totl[v.first] += v.second;
      for (auto v : TotlU[t]) totu[v.first] += v.second;
      for (auto v : TotlD[t]) totd[v.first] += v.second;
      for (auto v : Italy[t]) {
	for (auto u : v.second) {
	  for (size_t k=0; k<4; k++)
	    taly[v.first][u.first][k] += u.second[k];
	}
      }
      for (auto v : Ediag) {
	for (size_t k=0; k<3; k++) Etots[k] += v[k];
      }
      for (auto v : Vdiag) {
	for (size_t k=0; k<3; k++) Vtots[k] += v[k];
      }
      Ntot += { Nwght[t],  Njsel[t]};
      CStt += {crZero[t], crMiss[t], crTotl[t]};
    }
    
    // Send to root node
    //
    for (int i=1; i<numprocs; i++) {
      unsigned numZ, numU, ZZ, UU, NN;
      arrayU3 AA;
      arrayD4 FF;
      double  DD;

      if (myid==i) {
	MPI_Send(&(numZ=scat.size()),  1, MPI_UNSIGNED, 0, 554, MPI_COMM_WORLD);
	for (auto v : scat) {	
	  MPI_Send(&(ZZ=v.first),      1, MPI_UNSIGNED, 0, 555, MPI_COMM_WORLD);
	  MPI_Send(&(DD=v.second),     1, MPI_DOUBLE,   0, 556, MPI_COMM_WORLD);
	}

	MPI_Send(&(numZ=totl.size()),  1, MPI_UNSIGNED, 0, 557, MPI_COMM_WORLD);
	for (auto v : totl) {	
	  MPI_Send(&(ZZ=v.first),      1, MPI_UNSIGNED, 0, 558, MPI_COMM_WORLD);
	  MPI_Send(&(DD=v.second),     1, MPI_DOUBLE,   0, 559, MPI_COMM_WORLD);
	}

	MPI_Send(&(numZ=taly.size()),  1, MPI_UNSIGNED, 0, 560, MPI_COMM_WORLD);
	for (auto v : taly) {	
	  MPI_Send(&(ZZ=v.first),      1, MPI_UNSIGNED, 0, 561, MPI_COMM_WORLD);
	  numU = v.second.size();
	  MPI_Send(&numU,              1, MPI_UNSIGNED, 0, 562, MPI_COMM_WORLD);
	  for (auto u : v.second) {
	    MPI_Send(&(UU=u.first),    1, MPI_UNSIGNED, 0, 563, MPI_COMM_WORLD);
	    MPI_Send(&u.second[0],     4, MPI_DOUBLE,   0, 564, MPI_COMM_WORLD);
	  }
	}

	MPI_Send(&(numZ=totu.size()),  1, MPI_UNSIGNED, 0, 566, MPI_COMM_WORLD);
	for (auto v : totu) {	
	  MPI_Send(&(NN=v.first),      1, MPI_UNSIGNED, 0, 567, MPI_COMM_WORLD);
	  MPI_Send(&v.second[0],       3, MPI_UNSIGNED, 0, 568, MPI_COMM_WORLD);
	}

	MPI_Send(&(numZ=totd.size()),  1, MPI_UNSIGNED, 0, 569, MPI_COMM_WORLD);
	for (auto v : totd) {
	  MPI_Send(&(NN=v.first),      1, MPI_UNSIGNED, 0, 570, MPI_COMM_WORLD);
	  MPI_Send(&v.second[0],       3, MPI_DOUBLE,   0, 571, MPI_COMM_WORLD);
	}

	DD = std::get<0>(Etots);
	MPI_Send(&DD,                  1, MPI_DOUBLE,   0, 572, MPI_COMM_WORLD);
	DD = std::get<1>(Etots);
	MPI_Send(&DD,                  1, MPI_DOUBLE,   0, 573, MPI_COMM_WORLD);
	DD = std::get<2>(Etots);
	MPI_Send(&DD,                  1, MPI_DOUBLE,   0, 574, MPI_COMM_WORLD);

	DD = std::get<0>(Vtots);
	MPI_Send(&DD,                  1, MPI_DOUBLE,   0, 575, MPI_COMM_WORLD);
	DD = std::get<1>(Vtots);
	MPI_Send(&DD,                  1, MPI_DOUBLE,   0, 576, MPI_COMM_WORLD);
	DD = std::get<2>(Vtots);
	MPI_Send(&DD,                  1, MPI_DOUBLE,   0, 577, MPI_COMM_WORLD);

	MPI_Send(&Ntot[0],             2, MPI_UNSIGNED, 0, 578, MPI_COMM_WORLD);
	MPI_Send(&CStt[0],             3, MPI_UNSIGNED, 0, 579, MPI_COMM_WORLD);
      }

      if (myid==0) {
	MPI_Recv(&numZ, 1, MPI_UNSIGNED, i, 554, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	for (unsigned z=0; z<numZ; z++) {
	  MPI_Recv(&ZZ, 1, MPI_UNSIGNED, i, 555, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  MPI_Recv(&DD, 1, MPI_DOUBLE,   i, 556, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  scat[ZZ] += DD;
	}

	MPI_Recv(&numZ, 1, MPI_UNSIGNED, i, 557, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	for (unsigned z=0; z<numZ; z++) {
	  MPI_Recv(&ZZ, 1, MPI_UNSIGNED, i, 558, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  MPI_Recv(&DD, 1, MPI_DOUBLE,   i, 559, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  totl[ZZ] += DD;
	}

	MPI_Recv(&numZ, 1, MPI_UNSIGNED, i, 560, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	for (unsigned z=0; z<numZ; z++) {
	  MPI_Recv(&ZZ,   1, MPI_UNSIGNED, i, 561, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  MPI_Recv(&numU, 1, MPI_UNSIGNED, i, 562, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  for (unsigned u=0; u<numU; u++) {
	    MPI_Recv(&UU, 1, MPI_UNSIGNED, i, 563, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	    MPI_Recv(&FF, 4, MPI_DOUBLE,   i, 564, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	    for (size_t k=0; k<4; k++) taly[ZZ][UU][k] += FF[k];
	  }
	}

	MPI_Recv(&numZ, 1, MPI_UNSIGNED, i, 566, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	for (unsigned z=0; z<numZ; z++) {
	  MPI_Recv(&ZZ,    1, MPI_UNSIGNED, i, 567, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  MPI_Recv(&AA[0], 3, MPI_UNSIGNED, i, 568, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  totu[ZZ] += AA;
	}

	MPI_Recv(&numZ, 1, MPI_UNSIGNED, i, 569, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	for (unsigned z=0; z<numZ; z++) {
	  MPI_Recv(&ZZ,    1, MPI_UNSIGNED, i, 570, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  MPI_Recv(&FF[0], 4, MPI_DOUBLE,   i, 571, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  totd[ZZ] += FF;
	}

	MPI_Recv(&DD,      1, MPI_DOUBLE,   i, 572, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	std::get<0>(Etots) += DD;
	MPI_Recv(&DD,      1, MPI_DOUBLE,   i, 573, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	std::get<1>(Etots) += DD;
	MPI_Recv(&DD,      1, MPI_DOUBLE,   i, 574, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	std::get<2>(Etots) += DD;

	MPI_Recv(&DD,      1, MPI_DOUBLE,   i, 575, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	std::get<0>(Vtots) += DD;
	MPI_Recv(&DD,      1, MPI_DOUBLE,   i, 576, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	std::get<1>(Vtots) += DD;
	MPI_Recv(&DD,      1, MPI_DOUBLE,   i, 577, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	std::get<2>(Vtots) += DD;

	MPI_Recv(&NT[0],   2, MPI_UNSIGNED, i, 578, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	Ntot += NT;

	MPI_Recv(&NC[0],   3, MPI_UNSIGNED, i, 579, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	CStt += NC;
      }
    }
    
    // Root prints the diagnostic
    //
    if (myid==0) {
      std::cout << std::endl
		<< std::string(4+10+10+12, '-')   << std::endl
		<< "Scatter check: time=" << tnow << std::endl;
      if (ElectronEPSM) {
	double R = totlES0>0 ? static_cast<double>(epsmES0)/totlES0 : epsmES0; 
	std::cout << "Electron EPSM: " << epsmES0 << "/" << totlES0 << " [="
		  << R << "]" << std::endl;
      }
      if ((aType==Hybrid or aType==Trace) and maxCoul < UINT_MAX) {
	double R = totlIE0>0 ? static_cast<double>(epsmIE0)/totlIE0 : epsmIE0;
	std::cout << "Ion-Elec EPSM: " << epsmIE0 << "/" << totlIE0 << " [="
		  << R << "]" << std::endl;
      }

      std::cout << std::string(4+10+16+16, '-')   << std::endl
		<< std::right
		<< std::setw(4)  << "Elem"
		<< std::setw(10) << "Scatter"
		<< std::setw(16) << "Total"
		<< std::setw(16) << "Fraction"
		<< std::endl
		<< std::setw(4)  << "----"
		<< std::setw(10) << "--------"
		<< std::setw(16) << "--------"
		<< std::setw(16) << "--------"
		<< std::endl;
      // One line for each element
      for (auto v : totl) {
	unsigned NS=0; double frac = 0.0;
	if (v.second > 0) {
	  if (scat.find(v.first) != scat.end()) NS = scat[v.first];
	  frac = static_cast<double>(NS)/v.second;
	}
	std::cout << std::setw( 4) << v.first
		  << std::setw(10) << NS
		  << std::setw(16) << v.second
		  << std::setw(16) << frac << std::endl;
      }

      double totalII = 0.0;
      for (auto v : totd) {
	for (size_t k=0; k<3; k++) totalII += v.second[k];
      }

      std::cout << std::string(4+10+16+16, '-') << std::endl << std::endl
		<< "Counts" << std::endl
		<< std::string(4+4+10+10+10, '-')   << std::endl
		<< std::right
		<< std::setw(4)  << "Z1"
		<< std::setw(4)  << "Z2"
		<< std::setw(10) << "Neutral"
		<< std::setw(10) << "Ion1"
		<< std::setw(10) << "Ion2"
		<< std::endl
		<< std::setw(4)  << "--"
		<< std::setw(4)  << "--"
		<< std::setw(10) << "--------"
		<< std::setw(10) << "--------"
		<< std::setw(10) << "--------"
		<< std::endl;
      // One line for each entry
      for (auto v : totu) {
	unsigned Z1 = v.first/100;
	unsigned Z2 = v.first - Z1*100;
	std::cout << std::setw( 4) << Z1
		  << std::setw( 4) << Z2;
	for (size_t k=0; k<3; k++)
	  std::cout << std::setw(10) << v.second[k];
	std::cout << std::endl;
      }

      std::cout << std::string(4+4+10+10+10, '-') << std::endl << std::endl
		<< "Weights" << std::endl
		<< std::string(4+4+14+14+14, '-')   << std::endl
		<< std::right
		<< std::setw(4)  << "Z1"
		<< std::setw(4)  << "Z2"
		<< std::setw(14) << "Neutral"
		<< std::setw(14) << "Ion1"
		<< std::setw(14) << "Ion2"
		<< std::endl
		<< std::setw(4)  << "--"
		<< std::setw(4)  << "--"
		<< std::setw(14) << "--------"
		<< std::setw(14) << "--------"
		<< std::setw(14) << "--------"
		<< std::endl;
      // One line for each entry
      for (auto v : totd) {
	unsigned Z1 = v.first/100;
	unsigned Z2 = v.first - Z1*100;
	std::cout << std::setw( 4) << Z1
		  << std::setw( 4) << Z2
		  << std::setprecision(5);
	for (size_t k=0; k<3; k++) {
	  if (totalII>0.0)
	    std::cout << std::setw(14) << v.second[k]/totalII;
	  else
	    std::cout << std::setw(14) << 0.0;
	}
	std::cout << std::endl;
      }

      std::cout << std::string(4+4+20+4+10+14+14+10, '-') << std::endl
		<< std::endl
		<< std::setw( 4) << "Z1"
		<< std::setw( 4) << "Z2"
		<< std::setw(20) << "Type"
		<< std::setw( 4) << "#"
		<< std::setw(14) << "Count"
		<< std::setw(14) << "Prob"
		<< std::setw(14) << "Frac"
		<< std::setw(14) << "Energy"
		<< std::setw(14) << "dE/totE"
		<< std::setw(14) << "totE"
		<< std::endl
		<< std::setw( 4) << "--"
		<< std::setw( 4) << "--"
		<< std::setw(20) << "--------"
		<< std::setw( 4) << "--"
		<< std::setw(14) << "--------"
		<< std::setw(14) << "--------"
		<< std::setw(14) << "--------"
		<< std::setw(14) << "--------"
		<< std::setw(14) << "--------"
		<< std::setw(14) << "--------"
		<< std::endl     << std::setprecision(5);


      double dSum = 0.0, uSum = 0.0, dTot = 0.0, nSum = 0.0;

      for (auto v : taly) {
	if (v.second.size()) {
	  for (auto u : v.second) {
	    dTot += u.second[0];
	  }
	}
      }

      std::map<unsigned, std::array<double, 3> > byType;

      for (auto v : taly) {
	if (v.second.size()) {
	  unsigned short Z1 = v.first / 100;
	  unsigned short Z2 = v.first % 100;
	  bool first = true;
	  for (auto u : v.second) {
	    if (first) {
	      std::cout << std::setw( 4) << Z1
			<< std::setw( 4) << Z2
			<< std::setw(20) << interLabels[u.first]
			<< std::setw( 4) << u.first
			<< std::setw(14) << u.second[3]
			<< std::setw(14) << u.second[0]
			<< std::setw(14) << u.second[0]/dTot
			<< std::setw(14) << u.second[1]
			<< std::setw(14) << u.second[1]/u.second[2]
			<< std::setw(14) << u.second[2] << std::endl;
	      first = false;
	    } else {
	      std::cout << std::setw(28) << interLabels[u.first]
			<< std::setw( 4) << u.first
			<< std::setw(14) << u.second[3]
			<< std::setw(14) << u.second[0]
			<< std::setw(14) << u.second[0]/dTot
			<< std::setw(14) << u.second[1]
			<< std::setw(14) << u.second[1]/u.second[2]
			<< std::setw(14) << u.second[2] << std::endl;
	    }
	    dSum += u.second[1];
	    uSum += u.second[2];
	    nSum += u.second[3];

	    if (dTot>0.0) {
	      if (byType.find(u.first) == byType.end()) {
		byType[u.first][0]  = u.second[0];
		byType[u.first][1]  = u.second[1];
		byType[u.first][2]  = u.second[2];
	      }
	      else {
		byType[u.first][0] += u.second[0];
		byType[u.first][1] += u.second[1];
		byType[u.first][2] += u.second[2];
	      }
	    }
	  }
	}
      }

      std::cout << std::string(4+4+20+4+6*14, '-') << std::endl;
      std::cout << std::setw(8)    << "Totals"
		<< std::setw(24)   << ' '
		<< std::setw(14)   << nSum
		<< std::setw(14)   << dTot
		<< std::setw(14)   << 1.0
		<< std::setw(14)   << dSum;
      if (uSum > 0.0)
	std::cout << std::setw(14) << dSum/uSum
		  << std::setw(14) << uSum << std::endl;
      else
	std::cout << std::setw(14) << dSum
		  << std::setw(14) << 0.0  << std::endl;
      std::cout << std::string(4+4+20+4+6*14, '-') << std::endl;

      if (byType.size() > 0) {
	std::array<double, 3> bSum;
	for (auto v : byType) {
	  for (size_t i=0; i<bSum.size(); i++)  bSum[i] += v.second[i];
	}
	for (auto & v : bSum) {
	  if (v == 0.0) v = 1.0e-18;
	}

	std::cout << std::endl
		  << std::string(20+4*14, '-' ) << std::endl
		  << std::setw(20) << "Type"
		  << std::setw(14) << "Sum"
		  << std::setw(14) << "Energy"
		  << std::setw(14) << "Delta E"
		  << std::setw(14) << "Energy/totE"
		  << std::endl
		  << std::setw(20) << "------"
		  << std::setw(14) << "------"
		  << std::setw(14) << "------"
		  << std::setw(14) << "------"
		  << std::setw(14) << "------"
		  << std::endl;

	for (auto u : byType) {
	  std::cout << std::setw(20) << interLabels[u.first]
		    << std::setw(14) << u.second[0]/bSum[0]
		    << std::setw(14) << u.second[1]
		    << std::setw(14) << u.second[1]/bSum[1]
		    << std::setw(14) << u.second[1]/bSum[2]
		    << std::endl;
	}
	std::cout << std::string(20+4*14, '-' ) << std::endl
		  << std::setw(20) << "Total"
		  << std::setw(14) << bSum[0]
		  << std::setw(14) << bSum[1]
		  << std::setw(14) << 1.0
		  << std::setw(14) << bSum[1]/bSum[2]
		  << std::endl << std::endl;
      }

      // Prevent divide by zero
      double Ebot = Etots[2]!=0.0 ? Etots[2] : 1.0;

      std::cout << "           " << std::setw(14) << "Tot"
		<< std::setw(14) << "Norm" << std::endl
		<< "Total dE = "
		<< std::setw(14) << Etots[0]
		<< std::setw(14) << Etots[0]/Ebot << std::endl
		<< "Total KE = "
		<< std::setw(14) << Etots[1]
		<< std::setw(14) << Etots[1]/Ebot << std::endl
		<< "Total #  = "
		<< std::setw(14) << Etots[2] << std::endl;
      if (Vtots[0]>0.0) {
	Vtots[1] /= Vtots[0];
	Vtots[2] /= Vtots[0];
	std::cout << "Mean VF  = "
		  << std::setw(14) << Vtots[1]
		  << std::endl << "Disp VF  = "
		  << std::setw(14) << sqrt(Vtots[2] - Vtots[1]*Vtots[1])
		  << std::endl << "Total P  = "
		  << std::setw(14) << Vtots[0] << std::endl;
      }
      
      std::cout << std::endl
		<< std::string(24, '-')   << std::endl
		<< "---- Selection stats" << std::endl
		<< std::string(24, '-')   << std::endl
		<< std::setw(8) << std::left << "Nwght"
		<< " = " << std::setw(12) << Ntot[0] << std::endl
		<< std::setw(8) << std::left << "Njsum"
		<< " = " << std::setw(12) << Ntot[1] << std::endl;
      unsigned Nsum = Ntot[0] + Ntot[1];
      if (Nsum) std::cout << std::setw(8) << std::left << "Ratio"
			  << " = " << std::setw(12)
			  << static_cast<double>(Ntot[0])/Nsum << std::endl;
      if (CStt[2]) {
	std::cout << std::setw(8) << std::left << "CS zero"
		  << " = " << std::setw(12) << CStt[0] << std::endl
		  << std::setw(8) << std::left << "CS miss"
		  << " = " << std::setw(12) << CStt[1] << std::endl
		  << std::setw(8) << std::left << "CS totl"
		  << " = " << std::setw(12) << CStt[2] << std::endl
		  << std::setw(8) << std::left << "% zero"
		  << " = " << std::setw(12)
		  << static_cast<double>(CStt[0])*100.0/CStt[2] << std::endl
		  << std::setw(8) << std::left << "% miss"
		  << " = " << std::setw(12)
		  << static_cast<double>(CStt[1])*100.0/CStt[2] << std::endl;
      }
      std::cout << std::string(24, '-') << std::endl;

      // Recombination diagnostics
      //
      if (recomb_check and recombTally.size()) {
	std::cout << std::endl
		  << std::string(8+4*16, '-') << std::endl
		  << "---- Recombination coefficient" << std::endl
		  << std::string(8+4*16, '-') << std::endl
		  << std::setw( 8) << "Species"
		  << std::setw(16) << "Weight"
		  << std::setw(16) << "Mean"
		  << std::setw(16) << "Min"
		  << std::setw(16) << "Max"
		  << std::endl
		  << std::setw( 8) << "-------"
		  << std::setw(16) << "-------"
		  << std::setw(16) << "-------"
		  << std::setw(16) << "-------"
		  << std::setw(16) << "-------"
		  << std::endl;
	for (auto v : recombTally) {
	  std::ostringstream slab;
	  double wgt = v.second[0], avg = 0.0;
	  if (wgt>0.0) avg = v.second[1]/wgt;
	  slab << v.first.first << ", " << v.first.second;
	  std::cout << std::setw( 8) << slab.str()
		    << std::setw(16) << wgt
		    << std::setw(16) << avg
		    << std::setw(16) << v.second[2]
		    << std::setw(16) << v.second[3]
		    << std::endl;
	}
	std::cout << std::string(8+4*16, '-') << std::endl;

	if (use_photoIB) {
	  std::cout << std::endl
		    << std::string(8+18+10, '-') << std::endl
		    << "---- Photoionization coefficient" << std::endl
		    << std::string(8+18+10, '-') << std::endl;
	  for (auto s : SpList) {
	    double Rate = ch.IonList[s.first]->photoIonizationRate().first;
	    if (Rate>0.0) {
	      std::ostringstream slab;
	      slab << s.first.first << ", " << s.first.second;
	      std::cout << std::setw( 8) << slab.str()
			<< std::setw(18) << Rate
			<< std::endl;
	    }
	  }
	  std::cout << std::string(8+18+10, '-') << std::endl;
	}

      }
      // End: recombination
    }
    
    // Clear the counters
    //
    for (int t=0; t<nthrds; t++) {
      for (auto & v : Escat[t]) v.second = 0;
      for (auto & v : Etotl[t]) v.second = 0;
      for (auto & v : TotlU[t]) v.second = {0, 0, 0};
      for (auto & v : TotlD[t]) v.second = {0, 0, 0};
      for (auto & v : Italy[t]) v.second.clear();
      for (auto & v : Ediag[t]) v = 0.0;
      for (auto & v : Vdiag[t]) v = 0.0;
      Nwght[t] = Njsel[t] = 0;
      crZero[t] = crMiss[t] = crTotl[t] = 0;
    }
  }

  (*barrier)("CollideIon::gatherSpecies complete", __FILE__, __LINE__);
}


// Print out species counts
//
void CollideIon::printSpecies
(std::map<speciesKey, unsigned long>& spec, double T)
{
  if (myid) return;

  if (aType == Direct) {	// Call the generic printSpecies member
    if (use_elec<0) Collide::printSpecies(spec, T);
    else printSpeciesElectrons(spec, tM);
  } else if (aType == Weight) {	// Call the weighted printSpecies version
    printSpeciesElectrons(spec, tM);
    printSpeciesColl();
  } else if (aType == Hybrid) {	// For hybrid, skip collision counting
    printSpeciesElectrons(spec, tM);
  } else {			// Call the trace fraction version
    printSpeciesTrace();
    printSpeciesColl();
  }
}

void CollideIon::printSpeciesColl()
{
  if (COLL_SPECIES) {

    unsigned long sum = 0;
    for (auto i : collCount[0]) {
      for (auto j : i.second) sum += j;
    }

    if (sum) {

      ostringstream sout;
      sout << outdir << runtag << ".DSMC_spc_log";
      ofstream mout(sout.str().c_str(), ios::app);

      const double Tfac = 2.0*TreeDSMC::Eunit/3.0 * amu  /
	TreeDSMC::Munit/boltz;

      double Ti = 0.0, Te = 0.0;
      if (tM[1]>0.0) Ti = Tfac*tM[0]/tM[1];
      if (tM[3]>0.0) Te = Tfac*tM[2]/tM[3];

      // Print the header
      //
      mout << std::left
	   << std::setw(12) << "Time"      << std::setw(19) << tnow  << std::endl
	   << std::setw(12) << "Temp(ion)" << std::setw(18) << Ti    << std::endl
	   << std::setw(12) << "Disp(ion)" << std::setw(18) << tM[0] << std::endl
	   << std::setw(12) << "Temp(elc)" << std::setw(18) << Te    << std::endl
	   << std::endl << std::right
	   << std::setw(20) << "<Sp 1|Sp 2> "
	   << std::setw(14) << "Scatter"
	   << std::setw(18) << "Frac scat"
	   << std::setw(14) << "Inelast"
	   << std::setw(18) << "Frac inel"
	   << std::endl << std::right
	   << std::setw(20) << "------------- " << std::right
	   << std::setw(10) << "---------"      << std::right
	   << std::setw(18) << "------------"   << std::right
	   << std::setw(10) << "-------"        << std::right
	   << std::setw(18) << "------------"   << std::endl;

      for (auto i : collCount[0]) {
      std::ostringstream sout;
      speciesKey k1 = i.first.first;
      speciesKey k2 = i.first.second;
      sout << "<" << std::setw(2) << k1.first
	   << "," << std::setw(2) << k1.second
	   << "|" << std::setw(2) << k2.first
	   << "," << std::setw(2) << k2.second << "> ";

      mout << std::setw(20) << std::right << sout.str()
	   << std::setw(14) << i.second[0]
	   << std::setw(18) << static_cast<double>(i.second[0])/sum
	   << std::setw(14) << i.second[1]
	   << std::setw(18) << static_cast<double>(i.second[1])/sum
	   << std::endl;
      }
      mout << std::string(86, '-') << std::endl;
    }

    for (auto s : collCount) s.clear();
  }
}

void CollideIon::photoWGather()
{
  if (not photoDiag) return;

  for (int t=1; t<nthrds; t++) {
    for (auto v : photoW[t]) photoW[0][v.first] += v.second;
    for (auto v : photoN[t]) photoN[0][v.first] += v.second;
    for (auto s : SpList) {
      lQ Q = s.first;
      if (photoStat[t].find(Q) != photoStat[t].end()) {
	for (int k=0; k<3; k++) photoStat[0][Q][k] += photoStat[t][Q][k];
	photoStat[0][Q][3] =
	  std::max<double>(photoStat[0][Q][3], photoStat[t][Q][3]);
      }
    }
				// Clear all but zero thread
    photoW   [t].clear();
    photoN   [t].clear();
    photoStat[t].clear();
  }

  // Collect species weights for diagnostic histogram
  //
  if (aType == Trace) {
    std::map<speciesKey, std::vector<double>> data;

    // Sanity check tolerance
    //
    const double tolW = 1.0e-10;

    // Iterate through all cells
    //
    pHOT_iterator itree(*tree);
  
    while (itree.nextCell()) {
      
      pCell *c = itree.Cell();

      // Iterate through bodies
      //
      for (auto b : c->bods) {
	Particle *p = tree->Body(b);
	for (auto s : SpList) {
	  data[s.first].push_back(p->dattrib[s.second]);
	  if (p->dattrib[s.second]<-tolW or p->dattrib[s.second]>1.0+tolW) {
	    std::cout << "Crazy value n=" << p->indx << ": ("
		      << s.first.first << ", " << s.first.second
		      << ") = " << p->dattrib[s.second]
		      << ", "   << 1.0 - p->dattrib[s.second] << std::endl;
	  }
	}
      }
    }

    // Accumulate histogram from all processes and send to root
    //
    for (int n=1; n<numprocs; n++) {
      if (myid==n) {
	for (auto s : SpList) {
	  unsigned sz = data[s.first].size();
	  MPI_Send(&sz, 1, MPI_UNSIGNED, 0, 794, MPI_COMM_WORLD);
	  MPI_Send(&data[s.first][0], sz, MPI_DOUBLE, 0, 795, MPI_COMM_WORLD);
	}

	// Send number of entries
	unsigned ns = photoStat[0].size();
	MPI_Send(&ns, 1, MPI_UNSIGNED, 0, 796, MPI_COMM_WORLD);

	// Now send the entries
	for (auto v : photoStat[0]) {
	  lQ Q = v.first;
	  MPI_Send(&Q.first,     1, MPI_UNSIGNED_SHORT, 0, 797, MPI_COMM_WORLD);
	  MPI_Send(&Q.second,    1, MPI_UNSIGNED_SHORT, 0, 798, MPI_COMM_WORLD);
	  MPI_Send(&v.second[0], 4, MPI_DOUBLE,         0, 799, MPI_COMM_WORLD);
	}
      }
      if (myid==0) {
	std::vector<double> rcv;
	unsigned sz;
	for (auto s : SpList) {
	  MPI_Recv(&sz, 1, MPI_UNSIGNED, n, 794, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  rcv.resize(sz);
	  MPI_Recv(&rcv[0], sz, MPI_DOUBLE, n, 795, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  data[s.first].insert(data[s.first].end(), rcv.begin(), rcv.end());
	}

	// Receive the number of entries
	MPI_Recv(&sz, 1, MPI_UNSIGNED, n, 796, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	// Now receive the data for each entry and add to local copy
	for (unsigned s=0; s<sz; s++) {
	  std::array<double, 4> dd;
	  lQ Q;
	  MPI_Recv(&Q.first,  1, MPI_UNSIGNED_SHORT, n, 797, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  MPI_Recv(&Q.second, 1, MPI_UNSIGNED_SHORT, n, 798, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  MPI_Recv(&dd[0],    4, MPI_DOUBLE,         n, 799, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  for (int k=0; k<3; k++) photoStat[0][Q][k] += dd[k];
	  photoStat[0][Q][3] = std::max<double>(photoStat[0][Q][3], dd[3]);
	}
      }
    }

    // Root process generates the histograms
    //
    if (myid==0) {
      static const bool use_log = true;
      for (auto s : SpList) {
	std::sort(data[s.first].begin(), data[s.first].end());

	unsigned q1 = 0.25*data[s.first].size();
	unsigned q2 = 0.50*data[s.first].size();
	unsigned q3 = 0.75*data[s.first].size();

	if (use_log) {
	  std::vector<double>::iterator ibeg = data[s.first].begin();
	  std::vector<double>::iterator iend = data[s.first].end();
	  std::vector<double>::iterator it;
	  for (it=ibeg; it<iend; it++) { if (*it>0.0) break; }
	  if (it!=ibeg) data[s.first].erase(ibeg, it);
	}

	frcHist[s.first] =
	  ahistoDPtr(new AsciiHisto<double>(data[s.first], diagBins, use_log));

	frcQ1[s.first] = data[s.first][q1];
	frcQ2[s.first] = data[s.first][q2];
	frcQ3[s.first] = data[s.first][q3];
      }
    }
  }

  for (int n=1; n<numprocs; n++) {
    if (myid==n) {
      unsigned numE = photoW[0].size();
      MPI_Send(&numE, 1, MPI_UNSIGNED, 0, 801, MPI_COMM_WORLD);
      for (auto v : photoW[0]) {
	MPI_Send(&v.first.first,  1, MPI_UNSIGNED_SHORT, 0, 802, MPI_COMM_WORLD);
	MPI_Send(&v.first.second, 1, MPI_UNSIGNED_SHORT, 0, 803, MPI_COMM_WORLD);
	MPI_Send(&v.second,       1, MPI_DOUBLE,         0, 804, MPI_COMM_WORLD);
      }
      photoW[0].clear();	// Clear zero thread for all but master node.
				// Master will be cleared in photoWPrint().
      for (auto v : photoN[0]) {
	MPI_Send(&v.first.first,  1, MPI_UNSIGNED_SHORT, 0, 805, MPI_COMM_WORLD);
	MPI_Send(&v.first.second, 1, MPI_UNSIGNED_SHORT, 0, 806, MPI_COMM_WORLD);
	MPI_Send(&v.second,       1, MPI_DOUBLE,         0, 807, MPI_COMM_WORLD);
      }
      photoN[0].clear();	// Clear zero thread for all but master node.
				// Master will be cleared in photoWPrint().
    }
    if (myid==0) {
      unsigned numE;
      speciesKey k;
      double v;

      MPI_Recv(&numE, 1, MPI_UNSIGNED, n, 801, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
      for (unsigned q=0; q<numE; q++) {
	MPI_Recv(&k.first,  1, MPI_UNSIGNED_SHORT, n, 802, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	MPI_Recv(&k.second, 1, MPI_UNSIGNED_SHORT, n, 803, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	MPI_Recv(&v,        1, MPI_DOUBLE,         n, 804, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	photoW[0][k] += v;
      }

      for (unsigned q=0; q<numE; q++) {
	MPI_Recv(&k.first,  1, MPI_UNSIGNED_SHORT, n, 805, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	MPI_Recv(&k.second, 1, MPI_UNSIGNED_SHORT, n, 806, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	MPI_Recv(&v,        1, MPI_DOUBLE,         n, 807, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	photoN[0][k] += v;
      }

    }
  }

}

void CollideIon::photoWPrint()
{
  if (not photoDiag) return;

  if (photoW[0].size() == 0) return;

  std::ostringstream sout;
  sout << runtag << ".photoIB";

  std::ofstream out;
  out.open(sout.str().c_str(), ios::out | ios::app);
  if (out) {
    out << std::setw(70) << std::setfill('-') << left << '-'
	<< std::endl << std::setfill(' ')
	<< "---- Step "  << this_step << " Time=" << tnow << std::endl
	<< std::setw(70) << std::setfill('-') << left << '-'
	<< std::endl << std::setfill(' ')
	<< std::setw(3)  << std::right << "Z"
	<< std::setw(3)  << std::right << "C"
	<< std::setw(18) << std::right << "Sfrac"
	<< std::setw(18) << std::right << "Total"
	<< std::endl;
    for (auto v : photoW[0]) {
      out << std::setw(3)  << std::right << v.first.first
	  << std::setw(3)  << std::right << v.first.second
	  << std::setw(18) << std::right << v.second
	  << std::setw(18) << std::right << photoN[0][v.first]
	  << std::endl;
    }
    out << std::endl;
    for (auto h : frcHist) {
      if (h.second.get()) {
	std::ostringstream sout1, sout2, sout3, sout4, sout5;
	lQ Q = h.first;

	sout1 << "-----Fraction for (Z, C) = ("
	      << Q.first << ", "
	      << Q.second << "), T="
	      << tnow << ' ';
	sout2 << "-----Q1 = " << frcQ1[Q];
	sout3 << "-----Q2 = " << frcQ2[Q];
	sout4 << "-----Q3 = " << frcQ3[Q];

	if (photoStat[0][Q][0]>0) {
	  sout5 << "-----" << std::endl
		<< "-----Stats (" << Q.first << ", " << Q.second << "):"
		<< " tot=" << static_cast<int>(photoStat[0][Q][0])
		<< " max=" << photoStat[0][Q][3];
	  if (photoStat[0][Q][1]>0) // Only print for Pr>1 events
	    sout5 << " oab=" << static_cast<int>(photoStat[0][Q][1])
		  << " rat=" << photoStat[0][Q][1]/photoStat[0][Q][0]
		  << " avg=" << photoStat[0][Q][2]/photoStat[0][Q][1];
	}

				// Print header
	out << std::endl << std::left
	    << std::setfill('-')
	    << std::setw(53) << '-'  << std::endl
	    << std::setw(53) << sout1.str() << std::endl
	    << std::setw(53) << '-'  << std::endl << std::setfill(' ')
	    << std::setw(53) << sout2.str() << std::endl
	    << std::setw(53) << sout3.str() << std::endl
	    << std::setw(53) << sout4.str() << std::endl;
	if (sout5.str().size())
	out << std::setw(53) << sout5.str() << std::endl;
	out << std::setfill('-')
	    << std::setw(53) << '-'  << std::endl << std::setfill(' ');

	(*h.second)(out);
      }
    }

  } else {
    std::cout << "Could not open <" << sout.str() << "> for append"
	      << std::endl;
  }

  // Clear accumulators
  //
  photoW[0]   .clear();
  photoN[0]   .clear();
  photoStat[0].clear();
}

void CollideIon::electronGather()
{
  if (not distDiag) return;

  static bool IDBG = false;

  if (use_elec >= 0) {

    std::vector<double> eEeV, eIeV, eJeV, eVel, iVel;
    std::map<unsigned short, std::vector<double> > eEeVsp, eIeVsp;

    // Interate through all cells
    //
    pHOT_iterator itree(*c0->Tree());

    ee.clear();

    while (itree.nextCell()) {

      for (auto b : itree.Cell()->bods) {
	double cri = 0.0, cre = 0.0, crj = 0.0;
	Particle* p = c0->Tree()->Body(b);
	for (int l=0; l<3; l++) {
	  double ve = p->dattrib[use_elec+l];
	  cre += ve*ve;
	  double vi = p->vel[l];
	  cri += vi*vi;
	  crj += (vi - ve)*(vi - ve);
	}

	unsigned short Z = 0;
	double mi        = 0.0;

	if (aType==Trace) { // Compute molecular weight for Trace-type
	  mi = 0.0;	    // particle
	  for (auto s : SpList)
	    mi += p->dattrib[s.second] / atomic_weights[s.first.first];
	  mi = amu/mi;
	} else {		// For all other types besides Trace
	  Z  = KeyConvert(p->iattrib[use_key]).getKey().first;
	  mi = atomic_weights[Z] * amu;
	}
	  

	double mu = mi*me/(mi + me);

	double Ee        = 0.5*cre*me*TreeDSMC::Vunit*TreeDSMC::Vunit/eV;
	double Ei        = 0.5*cri*mi*TreeDSMC::Vunit*TreeDSMC::Vunit/eV;
	double Ej        = 0.5*crj*mu*TreeDSMC::Vunit*TreeDSMC::Vunit/eV;

	if (aType!=Trace) {
	  eEeVsp[Z].push_back(Ee);
	  eIeVsp[Z].push_back(Ei);
	}

	eEeV.push_back(Ee);
	eIeV.push_back(Ei);
	eJeV.push_back(Ej);
	eVel.push_back(sqrt(cre));
	iVel.push_back(sqrt(cri));
      }

      if (ntcDist) {
	for (auto q : qv) {
	  try {
	    double v = ntcdb[itree.Cell()->mykey].CrsVel(electronKey, elecElec, q);
	    ee[q].push_back(v);
	  }
	  catch (NTC::NTCitem::Error &error) {}
	}
      }
    }

    // Accumulate from threads
    //
    std::vector<double> loss, keE, keI, mom, crs;
    unsigned Ovr=0, Acc=0, Tot=0, NumE=0;
    double RatE=0.0;

    CntE = 0;
    RhoE = 0.0;
    Rho2 = 0.0;
    RhoV = 0.0;			// MFP: SUM(rho*sigma)
    RhoN = 0.0;			// MFP: count

    for (int t=0; t<nthrds; t++) {
      loss.insert(loss.end(), velER[t].begin(), velER[t].end());
      velER[t].clear();

      Ovr  += elecOvr[t];
      Acc  += elecAcc[t];
      Tot  += elecTot[t];
      RhoE += elecDen[t];
      Rho2 += elecDn2[t];
      CntE += elecCnt[t];
      
      RhoV += rhoSigV[t];
      RhoN += rhoSigN[t];

      NumE += elecNum[t];
      RatE += elecRat[t];

      elecOvr[t] = elecAcc[t] = elecTot[t] = 0;
      elecDen[t] = elecDn2[t] = 0.0;
      elecCnt[t] = 0;
      rhoSigV[t] = rhoSigN[t] = 0.0;
      elecNum[t] = 0;
      elecRat[t] = 0.0;
    }

    for (int t=1; t<nthrds; t++) {

      for (int k=0; k<4; k++) {
	tauIon[0][k] += tauIon[t][k]; // Ion counters
	tauElc[0][k] += tauElc[t][k]; // Electron counters
	colUps[0][k] += colUps[t][k]; // Upscale counter
      }
    }

    std::ofstream dbg;
    if (IDBG) {
      std::ostringstream sout;
      sout << runtag << ".eGather." << myid;
      dbg.open(sout.str().c_str(), ios::out | ios::app);
      sout.str(""); sout << "---- Step " << this_step
			 << " Time=" << tnow << " ";
      dbg << std::setw(70) << std::setfill('-') << left << sout.str()
	  << std::endl << std::setfill(' ');
    }

    if (elecDist and (aType==Hybrid or aType==Trace)) {
      std::vector<double> eEV, eRC, eEVmin, eEVavg, eEVmax, eEVsub;
      for (int t=0; t<nthrds; t++) {
	eEV.insert(eEV.end(),
		   elecEV[t].begin(), elecEV[t].end());
	eRC.insert(eRC.end(),
		   elecRC[t].begin(), elecRC[t].end());
	eEVmin.insert(eEVmin.end(),
		      elecEVmin[t].begin(), elecEVmin[t].end());
	eEVavg.insert(eEVavg.end(),
		      elecEVavg[t].begin(), elecEVavg[t].end());
	eEVmax.insert(eEVmax.end(),
		      elecEVmax[t].begin(), elecEVmax[t].end());
	eEVsub.insert(eEVsub.end(),
		      elecEVsub[t].begin(), elecEVsub[t].end());
      }

      // All processes send to root
      //
      for (int n=1; n<numprocs; n++) {

	if (myid == n) {
	  MPI_Send(&CntE,          1, MPI_UNSIGNED, 0, 317, MPI_COMM_WORLD);
	  MPI_Send(&RhoE,          1, MPI_DOUBLE,   0, 318, MPI_COMM_WORLD);
	  MPI_Send(&Rho2,          1, MPI_DOUBLE,   0, 319, MPI_COMM_WORLD);

	  unsigned num = eEV.size();
	  MPI_Send(&num,           1, MPI_UNSIGNED, 0, 320, MPI_COMM_WORLD);
	  if (num)
	    MPI_Send(&eEV[0],    num, MPI_DOUBLE,   0, 321, MPI_COMM_WORLD);

	  num = eEVmin.size();
	  MPI_Send(&num,           1, MPI_UNSIGNED, 0, 322, MPI_COMM_WORLD);
	  if (num) {
	    MPI_Send(&eEVmin[0], num, MPI_DOUBLE,   0, 323, MPI_COMM_WORLD);
	    MPI_Send(&eEVavg[0], num, MPI_DOUBLE,   0, 324, MPI_COMM_WORLD);
	    MPI_Send(&eEVmax[0], num, MPI_DOUBLE,   0, 325, MPI_COMM_WORLD);
	  }

	  num = eEVsub.size();
	  MPI_Send(&num,           1, MPI_UNSIGNED, 0, 326, MPI_COMM_WORLD);
	  if (num) {
	    MPI_Send(&eEVsub[0], num, MPI_DOUBLE,   0, 327, MPI_COMM_WORLD);
	  }

	  num = eRC.size();
	  MPI_Send(&num,           1, MPI_UNSIGNED, 0, 328, MPI_COMM_WORLD);
	  if (num)
	    MPI_Send(&eRC[0],    num, MPI_DOUBLE,   0, 329, MPI_COMM_WORLD);

	  MPI_Send(&RhoV,          1, MPI_DOUBLE,   0, 330, MPI_COMM_WORLD);
	  MPI_Send(&RhoN,          1, MPI_DOUBLE,   0, 331, MPI_COMM_WORLD);

	  MPI_Send(&tauIon[0][0],  4, MPI_DOUBLE,   0, 332, MPI_COMM_WORLD);
	  MPI_Send(&tauElc[0][0],  4, MPI_DOUBLE,   0, 335, MPI_COMM_WORLD);
	  MPI_Send(&colUps[0][0],  4, MPI_DOUBLE,   0, 338, MPI_COMM_WORLD);

	} // END: process send to root

	if (myid==0) {

	  std::vector<double> v;
	  unsigned num;
	  AvgRpt tmpR;
	  double dv;


	  MPI_Recv(&num,      1, MPI_UNSIGNED, n, 317, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  CntE += num;

	  MPI_Recv(&dv,       1, MPI_DOUBLE,   n, 318, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  RhoE += dv;

	  MPI_Recv(&dv,       1, MPI_DOUBLE,   n, 319, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  Rho2 += dv;

	  MPI_Recv(&num,      1, MPI_UNSIGNED, n, 320, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

	  if (num) {
	    v.resize(num);
	    MPI_Recv(&v[0], num, MPI_DOUBLE,   n, 321, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

	    eEV.insert(eEV.end(), v.begin(), v.end());
	  }

	  MPI_Recv(&num,      1, MPI_UNSIGNED, n, 322, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

	  if (num) {
	    v.resize(num);
	    MPI_Recv(&v[0], num, MPI_DOUBLE,   n, 323, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	    eEVmin.insert(eEVmin.end(), v.begin(), v.end());

	    MPI_Recv(&v[0], num, MPI_DOUBLE,   n, 324, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	    eEVavg.insert(eEVavg.end(), v.begin(), v.end());

	    MPI_Recv(&v[0], num, MPI_DOUBLE,   n, 325, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	    eEVmax.insert(eEVmax.end(), v.begin(), v.end());
	  }

	  MPI_Recv(&num,      1, MPI_UNSIGNED, n, 326, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

	  if (num) {
	    v.resize(num);
	    MPI_Recv(&v[0], num, MPI_DOUBLE,   n, 327, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	    eEVsub.insert(eEVsub.end(), v.begin(), v.end());
	  }

	  MPI_Recv(&num,      1, MPI_UNSIGNED, n, 328, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

	  if (num) {
	    v.resize(num);
	    MPI_Recv(&v[0], num, MPI_DOUBLE,   n, 329, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	    eRC.insert(eRC.end(), v.begin(), v.end());
	  }

	  MPI_Recv(&dv,       1, MPI_DOUBLE,   n, 330, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  RhoV += dv;

	  MPI_Recv(&dv,       1, MPI_DOUBLE,   n, 331, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  RhoN += dv;

	  MPI_Recv(&tmpR[0],  4, MPI_DOUBLE,   n, 332, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

	  for (int k=0; k<4; k++) tauIon[0][k] += tmpR[k];

	  MPI_Recv(&tmpR[0],  4, MPI_DOUBLE,   n, 335, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

	  for (int k=0; k<4; k++) tauElc[0][k] += tmpR[k];

	  MPI_Recv(&tmpR[0],  4, MPI_DOUBLE,   n, 338, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

	  for (int k=0; k<4; k++) colUps[0][k] += tmpR[k];

	} // Root receive loop

	MPI_Barrier(MPI_COMM_WORLD);

      } // Process loop

      double rcmbTotlSum = rcmbTotlGet();

      if (myid==0) {
	if (eEV.size()) {
	  elecEVH = ahistoDPtr(new AsciiHisto<double>(eEV, 20, 0.01));
	  if (IDBG) dbg << std::setw(16) << "eEV.size() = "
			<< std::setw(10) << eEV.size() << std::endl;
	}
	if (eEVmin.size()) elecEVHmin = ahistoDPtr(new AsciiHisto<double>(eEVmin, 20,  0.01 ));
	if (eEVavg.size()) elecEVHavg = ahistoDPtr(new AsciiHisto<double>(eEVavg, 20,  0.01 ));
	if (eEVmax.size()) elecEVHmax = ahistoDPtr(new AsciiHisto<double>(eEVmax, 20,  0.01 ));
	if (eEVsub.size()) elecEVHsub = ahistoDPtr(new AsciiHisto<double>(eEVsub, 20,  0.01 ));
	if (eRC.size())    elecRCH    = ahistoDPtr(new AsciiHisto<double>(eRC,    100, 0.005));
	if (rcmbTotlSum>0) {
	  std::vector<unsigned> rcmbT;
	  rcmbScale = 1.0e9/rcmbTotlSum;
	  for (auto v : rcmbLH) rcmbT.push_back(std::round(v*rcmbScale));
	  elecRCN = ahistoDPtr(new AsciiHisto<double>(rcmbT, rcmbEVmin, rcmbEVmax));
	}
      }

    } // END: elecDist

    if (ExactE and DebugE) {
      for (int t=0; t<nthrds; t++) {
	mom.insert(mom.end(), momD[t].begin(), momD[t].end());
      }
    }
    
    if (aType==Trace) {
      for (int t=0; t<nthrds; t++) {
	crs.insert(crs.end(), crsD[t].begin(), crsD[t].end());
      }
      energyD = energyA[0];
      for (int t=1; t<nthrds; t++) {
	energyP & x = energyA[t];
	if (std::get<0>(energyD)[0] > std::get<0>(x)[0])
	  std::get<0>(energyD)[0] = std::get<0>(x)[0];
	if (std::get<0>(energyD)[1] < std::get<0>(x)[1]) {
	  std::get<0>(energyD)[1] = std::get<0>(x)[1];
	  std::get<0>(energyD)[2] = std::get<0>(x)[2];
	  std::get<0>(energyD)[3] = std::get<0>(x)[3];
	  std::get<1>(energyD)    = std::get<1>(x);
	}
      }
      for (auto & v : energyA) {
	std::get<0>(v) = {DBL_MAX, 0.0, 0.0, 0.0};
	std::get<1>(v) = 0;
      }
    }

    if (KE_DEBUG) {
      for (int t=0; t<nthrds; t++) {
	keE.insert(keE.end(), keER[t].begin(), keER[t].end());
	keI.insert(keI.end(), keIR[t].begin(), keIR[t].end());
      }
    }

    if ((aType==Hybrid or aType==Trace) and collLim) {

      clampDat clamp1(clamp0);

      for (int t=0; t<nthrds; t++) {
	std::get<0>(clamp1) += std::get<0>(clampdat[t]);
	std::get<1>(clamp1) += std::get<1>(clampdat[t]);
	std::get<2>(clamp1)  = std::max<double>(std::get<2>(clampdat[0]),
						std::get<2>(clampdat[t]));
      }

      MPI_Reduce(&std::get<0>(clamp1), &std::get<0>(clampStat), 1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);
      MPI_Reduce(&std::get<1>(clamp1), &std::get<1>(clampStat), 1, MPI_DOUBLE,   MPI_SUM, 0, MPI_COMM_WORLD);
      MPI_Reduce(&std::get<2>(clamp1), &std::get<2>(clampStat), 1, MPI_DOUBLE,   MPI_MAX, 0, MPI_COMM_WORLD);

      if (std::get<0>(clampStat)) std::get<1>(clampStat) /= std::get<0>(clampStat);

      for (int t=0; t<nthrds; t++) clampdat[t] = clamp0;
    }

    if (aType==Hybrid and IonRecombChk) {
      for (int t=1; t<nthrds; t++) {
	for (auto v : ionCHK[t]) {
	  if (ionCHK[0].find(v.first) == ionCHK[0].end()) ionCHK[0][v.first] = 0.0;
	  ionCHK[0][v.first] += v.second;
	}
	for (auto v: recombCHK[t]) {
	  if (recombCHK[0].find(v.first) == recombCHK[0].end())
	    recombCHK[0][v.first] = 0.0;
	  recombCHK[0][v.first] += v.second;
	}
	ionCHK   [t].clear();
	recombCHK[t].clear();
      }

      unsigned short Z, C;
      unsigned num;
      double V;

      for (int n=1; n<numprocs; n++) {
	if (myid == n) {
	  num = ionCHK[0].size();
	  MPI_Send(&num, 1, MPI_UNSIGNED, 0, 310, MPI_COMM_WORLD);
	  for (auto v : ionCHK[0]) {
	    Z = v.first.first;
	    C = v.first.second;
	    V = v.second;
	    MPI_Send(&Z, 1, MPI_UNSIGNED_SHORT, 0, 311, MPI_COMM_WORLD);
	    MPI_Send(&C, 1, MPI_UNSIGNED_SHORT, 0, 312, MPI_COMM_WORLD);
	    MPI_Send(&V, 1, MPI_DOUBLE,         0, 313, MPI_COMM_WORLD);
	  }

	  num = recombCHK[0].size();
	  MPI_Send(&num, 1, MPI_UNSIGNED, 0, 314, MPI_COMM_WORLD);
	  for (auto v : recombCHK[0]) {
	    Z = v.first.first;
	    C = v.first.second;
	    V = v.second;
	    MPI_Send(&Z, 1, MPI_UNSIGNED_SHORT, 0, 315, MPI_COMM_WORLD);
	    MPI_Send(&C, 1, MPI_UNSIGNED_SHORT, 0, 316, MPI_COMM_WORLD);
	    MPI_Send(&V, 1, MPI_DOUBLE,         0, 317, MPI_COMM_WORLD);
	  }
	}

	if (myid == 0) {
	  MPI_Recv(&num, 1, MPI_UNSIGNED, n, 310, MPI_COMM_WORLD,
		   MPI_STATUS_IGNORE);
	  for (size_t i=0; i<num; i++) {
	    MPI_Recv(&Z, 1, MPI_UNSIGNED_SHORT, n, 311, MPI_COMM_WORLD,
		     MPI_STATUS_IGNORE);
	    MPI_Recv(&C, 1, MPI_UNSIGNED_SHORT, n, 312, MPI_COMM_WORLD,
		     MPI_STATUS_IGNORE);
	    MPI_Recv(&V, 1, MPI_DOUBLE,         n, 313, MPI_COMM_WORLD,
		     MPI_STATUS_IGNORE);

	    speciesKey k(Z, C);
	    if (ionCHK[0].find(k) == ionCHK[0].end()) ionCHK[0][k] = 0.0;
	    ionCHK[0][k] += V;
	  }

	  MPI_Recv(&num, 1, MPI_UNSIGNED, n, 314, MPI_COMM_WORLD,
		   MPI_STATUS_IGNORE);
	  for (size_t i=0; i<num; i++) {
	    MPI_Recv(&Z, 1, MPI_UNSIGNED_SHORT, n, 315, MPI_COMM_WORLD,
		     MPI_STATUS_IGNORE);
	    MPI_Recv(&C, 1, MPI_UNSIGNED_SHORT, n, 316, MPI_COMM_WORLD,
		     MPI_STATUS_IGNORE);
	    MPI_Recv(&V, 1, MPI_DOUBLE,         n, 317, MPI_COMM_WORLD,
		     MPI_STATUS_IGNORE);

	    speciesKey k(Z, C);
	    if (recombCHK[0].find(k) == recombCHK[0].end()) recombCHK[0][k] = 0.0;
	    recombCHK[0][k] += V;
	  }
	}
      }

    }

    if (ntcDist) {

      for (int n=1; n<numprocs; n++) {

	if (myid == n) {

	  int base = 326;

	  for (auto j : ee) {

	    double   val = j.first;
	    unsigned num = j.second.size();

	    MPI_Send(&num, 1, MPI_UNSIGNED, 0, base+0, MPI_COMM_WORLD);
	    MPI_Send(&val, 1, MPI_DOUBLE,   0, base+1, MPI_COMM_WORLD);
	    MPI_Send(&j.second[0], num, MPI_DOUBLE, 0, base+2, MPI_COMM_WORLD);
	    base += 3;
	  }

	  unsigned zero = 0;
	  MPI_Send(&zero, 1, MPI_UNSIGNED, 0, base, MPI_COMM_WORLD);

	} // END: process send to root

	if (myid==0) {

	  std::vector<double> v;
	  unsigned num;
	  double val;

	  int base = 326;

	  while (1) {

	    MPI_Recv(&num, 1,    MPI_UNSIGNED, n, base+0, MPI_COMM_WORLD,
		     MPI_STATUS_IGNORE);

	    if (num==0) break;

	    MPI_Recv(&val, 1,    MPI_DOUBLE,   n, base+1, MPI_COMM_WORLD,
		     MPI_STATUS_IGNORE);

	    v.resize(num);

	    MPI_Recv(&v[0], num, MPI_DOUBLE,   n, base+2, MPI_COMM_WORLD,
		       MPI_STATUS_IGNORE);

	    ee[val].insert( ee[val].end(), v.begin(), v.end() );

	    base += 3;

	  } // Loop over quantiles

	} // Root receive loop

	MPI_Barrier(MPI_COMM_WORLD);

      } // Node loop

      if (myid==0) {
	for (auto &u : ee) {
	  eeHisto[u.first] = ahistoDPtr(new AsciiHisto<double>(u.second, 20, 0.01));
	}
      }

    } // END: ntcDist


    (*barrier)("CollideIon::electronGather: BEFORE Send/Recv loop", __FILE__, __LINE__);

    unsigned eNum;

    for (int i=1; i<numprocs; i++) {

      if (i == myid) {

	MPI_Send(&(eNum=eVel.size()), 1, MPI_UNSIGNED, 0, 433, MPI_COMM_WORLD);

	if (IDBG) dbg << std::setw(16) << "eVel.size() = " << std::setw(10) << eNum;

	if (eNum) {
	  MPI_Send(&eEeV[0], eNum, MPI_DOUBLE, 0, 434, MPI_COMM_WORLD);
	  MPI_Send(&eIeV[0], eNum, MPI_DOUBLE, 0, 435, MPI_COMM_WORLD);
	  MPI_Send(&eJeV[0], eNum, MPI_DOUBLE, 0, 623, MPI_COMM_WORLD);
	  MPI_Send(&eVel[0], eNum, MPI_DOUBLE, 0, 436, MPI_COMM_WORLD);
	  MPI_Send(&iVel[0], eNum, MPI_DOUBLE, 0, 437, MPI_COMM_WORLD);
	}

	if (IDBG) dbg << " ... eEeV, eIeV, eJeV, eVel and iVel sent" << std::endl;

	MPI_Send(&(eNum=loss.size()), 1, MPI_UNSIGNED, 0, 438, MPI_COMM_WORLD);

	if (IDBG) dbg << std::setw(16) << "loss.size() = " << std::setw(10) << eNum;

	if (eNum) MPI_Send(&loss[0], eNum, MPI_DOUBLE, 0, 439, MPI_COMM_WORLD);

	if (IDBG) dbg << " ... loss sent" << std::endl;

	if (KE_DEBUG) {
	  MPI_Send(&(eNum=keE.size()), 1, MPI_UNSIGNED, 0, 440, MPI_COMM_WORLD);
	  if (IDBG) dbg << std::setw(16) << "keE.size() = " << std::setw(10) << eNum;

	  if (eNum) MPI_Send(&keE[0], eNum, MPI_DOUBLE, 0, 441, MPI_COMM_WORLD);
	  if (IDBG) dbg << " ... keE sent" << std::endl;

	  MPI_Send(&(eNum=keI.size()), 1, MPI_UNSIGNED, 0, 442, MPI_COMM_WORLD);
	  if (IDBG) dbg << std::setw(16) << "keI.size() = " << std::setw(10) << eNum;

	  if (eNum) MPI_Send(&keI[0], eNum, MPI_DOUBLE, 0, 443, MPI_COMM_WORLD);
	  if (IDBG) dbg << " ... keI sent" << std::endl;
	}

	if (ExactE and DebugE) {
	  MPI_Send(&(eNum=mom.size()), 1, MPI_UNSIGNED, 0, 444, MPI_COMM_WORLD);
	  if (IDBG) dbg << std::setw(16) << "mom.size() = " << std::setw(10) << eNum;

	  if (eNum) MPI_Send(&mom[0], eNum, MPI_DOUBLE, 0, 445, MPI_COMM_WORLD);
	  if (IDBG) dbg << " ... mom sent" << std::endl;
	}

	if (aType==Trace) {
	  MPI_Send(&(eNum=crs.size()), 1, MPI_UNSIGNED, 0, 446, MPI_COMM_WORLD);
	  if (IDBG) dbg << std::setw(16) << "crs.size() = " << std::setw(10) << eNum;

	  if (eNum) MPI_Send(&crs[0], eNum, MPI_DOUBLE, 0, 447, MPI_COMM_WORLD);
	  if (IDBG) dbg << " ... crs sent" << std::endl;
	  if (IDBG) dbg << std::setw(16) << "energyD.size() = "
			<< std::setw(10) << std::get<0>(energyD).size() + 1;
	  MPI_Send(&std::get<0>(energyD)[0], 4, MPI_DOUBLE, 0, 621, MPI_COMM_WORLD);
	  MPI_Send(&std::get<1>(energyD),    1, MPI_INT,    0, 622, MPI_COMM_WORLD);
	  if (IDBG) dbg << " ... energyD sent" << std::endl;
	}

	unsigned Nspc = eIeVsp.size();
	MPI_Send(&Nspc, 1, MPI_UNSIGNED, 0, 448, MPI_COMM_WORLD);

	if (Nspc) {
	  int base = 449;
	  unsigned short Z;
	  unsigned Enum;

	  for (auto z : eEeVsp) {
	    Z = z.first;
	    Enum = z.second.size();
	    MPI_Send(&Z,           1,    MPI_UNSIGNED_SHORT, 0, base++, MPI_COMM_WORLD);
	    MPI_Send(&Enum,        1,    MPI_UNSIGNED,       0, base++, MPI_COMM_WORLD);
	    MPI_Send(&z.second[0], Enum, MPI_DOUBLE,         0, base++, MPI_COMM_WORLD);
	  }

	  for (auto z : eIeVsp) {
	    Z = z.first;
	    Enum = z.second.size();
	    MPI_Send(&Z,           1,    MPI_UNSIGNED_SHORT, 0, base++, MPI_COMM_WORLD);
	    MPI_Send(&Enum,        1,    MPI_UNSIGNED,       0, base++, MPI_COMM_WORLD);
	    MPI_Send(&z.second[0], Enum, MPI_DOUBLE,         0, base++, MPI_COMM_WORLD);
	  }
	}

	if (IDBG) dbg << " ... eIeVsp sent [size=" << Nspc << "]" << std::endl;

      }

				// Root receives from Node i
      if (0 == myid) {

	std::vector<double> vTmp;

	MPI_Recv(&eNum, 1, MPI_UNSIGNED, i, 433, MPI_COMM_WORLD,
		 MPI_STATUS_IGNORE);

	if (IDBG) dbg << "recvd from " << std::setw(4) << i
		      << std::setw(16) << " eVel.size() = "
		      << std::setw(10) << eNum;

	if (eNum) {
	  vTmp.resize(eNum);

	  MPI_Recv(&vTmp[0], eNum, MPI_DOUBLE, i, 434, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  eEeV.insert(eEeV.begin(), vTmp.begin(), vTmp.end());

	  if (IDBG) dbg << " ... eEeV recvd";

	  MPI_Recv(&vTmp[0], eNum, MPI_DOUBLE, i, 435, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  eIeV.insert(eIeV.begin(), vTmp.begin(), vTmp.end());

	  if (IDBG) dbg << " ... eIeV recvd";

	  MPI_Recv(&vTmp[0], eNum, MPI_DOUBLE, i, 623, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  eJeV.insert(eJeV.begin(), vTmp.begin(), vTmp.end());

	  if (IDBG) dbg << " ... eJeV recvd";

	  MPI_Recv(&vTmp[0], eNum, MPI_DOUBLE, i, 436, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  eVel.insert(eVel.begin(), vTmp.begin(), vTmp.end());

	  if (IDBG) dbg << " ... eVel recvd";

	  MPI_Recv(&vTmp[0], eNum, MPI_DOUBLE, i, 437, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  iVel.insert(iVel.begin(), vTmp.begin(), vTmp.end());

	  if (IDBG) dbg << ", iVel recvd" << std::endl;
	} else {
	  if (IDBG) dbg << std::endl;
	}

	MPI_Recv(&eNum, 1, MPI_UNSIGNED, i, 438, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

	if (IDBG) dbg << "recvd from " << std::setw(4) << i
		      << std::setw(16) << " loss.size() = "
		      << std::setw(10) << eNum;

	if (eNum) {
	  vTmp.resize(eNum);

	  MPI_Recv(&vTmp[0], eNum, MPI_DOUBLE, i, 439, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  loss.insert(loss.end(), vTmp.begin(), vTmp.end());

	  if (IDBG) dbg << " ... loss recvd" << std::endl;
	} else {
	  if (IDBG) dbg << std::endl;
	}

	if (KE_DEBUG) {
	  MPI_Recv(&eNum, 1, MPI_UNSIGNED, i, 440, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  if (IDBG) dbg << "recvd from " << std::setw(4) << i
			<< std::setw(16) << " keE.size() = "
			<< std::setw(10) << eNum;

	  if (eNum) {
	    vTmp.resize(eNum);

	    MPI_Recv(&vTmp[0], eNum, MPI_DOUBLE, i, 441, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	    keE.insert(keE.end(), vTmp.begin(), vTmp.end());

	    if (IDBG) dbg << " ... keE recvd" << std::endl;
	  } else {
	    if (IDBG) dbg << std::endl;
	  }

	  MPI_Recv(&eNum, 1, MPI_UNSIGNED, i, 442, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

	  if (IDBG) dbg << "recvd from " << std::setw(4) << i
			<< std::setw(16) << " keI.size() = "
			<< std::setw(10) << eNum;

	  if (eNum) {
	    vTmp.resize(eNum);

	    MPI_Recv(&vTmp[0], eNum, MPI_DOUBLE, i, 443, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	    keI.insert(keI.end(), vTmp.begin(), vTmp.end());

	    if (IDBG) dbg << " ... keI recvd" << std::endl;
	  } else {
	    if (IDBG) dbg << std::endl;
	  }
	}

	if (ExactE and DebugE) {
	  MPI_Recv(&eNum, 1, MPI_UNSIGNED, i, 444, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

	  if (IDBG) dbg << "recvd from " << std::setw(4) << i
			<< std::setw(16) << " mom.size() = "
			<< std::setw(10) << eNum;

	  if (eNum) {
	    vTmp.resize(eNum);

	    MPI_Recv(&vTmp[0], eNum, MPI_DOUBLE, i, 445, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	    mom.insert(mom.end(), vTmp.begin(), vTmp.end());

	    if (IDBG) dbg << " ... mom recvd" << std::endl;
	  } else {
	    if (IDBG) dbg << std::endl;
	  }
	}

	if (aType==Trace) {
	  if (IDBG) dbg << "root in crs stanza" << std::endl;
	  MPI_Recv(&eNum, 1, MPI_UNSIGNED, i, 446, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

	  if (IDBG) dbg << "recvd from " << std::setw(4) << i
			<< std::setw(16) << " crs.size() = "
			<< std::setw(10) << eNum;

	  if (eNum) {
	    vTmp.resize(eNum);

	    MPI_Recv(&vTmp[0], eNum, MPI_DOUBLE, i, 447, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	    crs.insert(crs.end(), vTmp.begin(), vTmp.end());

	    if (IDBG) dbg << " ... crs recvd" << std::endl;
	  } else {
	    if (IDBG) dbg << std::endl;
	  }

	  energyP tmpE;
	  MPI_Recv(&std::get<0>(tmpE)[0], 4,  MPI_DOUBLE, i, 621, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  MPI_Recv(&std::get<1>(tmpE),    1,  MPI_INT,    i, 622, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	  if (IDBG) dbg << " ... energyD size 4+1 recvd" << std::endl;
	  if (std::get<0>(tmpE)[0] < std::get<0>(energyD)[0])
	    std::get<0>(energyD)[0] = std::get<0>(tmpE)[0];
	  if (std::get<0>(tmpE)[1] > std::get<0>(energyD)[1]) {
	    std::get<0>(energyD)[1] = std::get<0>(tmpE)[1];
	    std::get<0>(energyD)[2] = std::get<0>(tmpE)[2];
	    std::get<0>(energyD)[3] = std::get<0>(tmpE)[3];	    
	    std::get<1>(energyD)    = std::get<1>(tmpE);
	  }
	}

	unsigned Nspc;
	MPI_Recv(&Nspc, 1, MPI_UNSIGNED, i, 448, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

	if (Nspc) {
	  int base = 449;
	  unsigned short Z;
	  unsigned Enum;

	  for (unsigned q=0; q<Nspc; q++) {
	    MPI_Recv(&Z,    1, MPI_UNSIGNED_SHORT, i, base++, MPI_COMM_WORLD,
		     MPI_STATUS_IGNORE);
	    MPI_Recv(&Enum, 1, MPI_UNSIGNED,       i, base++, MPI_COMM_WORLD,
		     MPI_STATUS_IGNORE);
	    vTmp.resize(Enum);
	    MPI_Recv(&vTmp[0], Enum, MPI_DOUBLE,   i, base++, MPI_COMM_WORLD,
		     MPI_STATUS_IGNORE);

	    eEeVsp[Z].insert(eEeVsp[Z].begin(), vTmp.begin(), vTmp.end());
	  }

	  for (unsigned q=0; q<Nspc; q++) {
	    MPI_Recv(&Z,    1, MPI_UNSIGNED_SHORT, i, base++, MPI_COMM_WORLD,
		     MPI_STATUS_IGNORE);
	    MPI_Recv(&Enum, 1, MPI_UNSIGNED,       i, base++, MPI_COMM_WORLD,
		     MPI_STATUS_IGNORE);
	    vTmp.resize(Enum);
	    MPI_Recv(&vTmp[0], Enum, MPI_DOUBLE,   i, base++, MPI_COMM_WORLD,
		     MPI_STATUS_IGNORE);

	    eIeVsp[Z].insert(eIeVsp[Z].begin(), vTmp.begin(), vTmp.end());
	  }
	}

	if (IDBG) dbg << " ... eEeVsp, eIeVsp recvd [size=" << Nspc << "]" << std::endl;

      } // end: myid=0

    } // end: process loop

    (*barrier)("CollideIon::electronGather: AFTER Send/Recv loop", __FILE__, __LINE__);

    MPI_Reduce(&Ovr,  &Ovr_s, 1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&Acc,  &Acc_s, 1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&Tot,  &Tot_s, 1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&NumE, &Num_s, 1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&RatE, &Rat_s, 1, MPI_DOUBLE,   MPI_SUM, 0, MPI_COMM_WORLD);

    (*barrier)("CollideIon::electronGather: AFTER REDUCE loop", __FILE__, __LINE__);

    if (myid==0) {

      if (eVel.size()) {
	// Sort the lists
	std::sort(eVel.begin(), eVel.end());
	std::sort(iVel.begin(), iVel.end());

	// Make the histograms
	elecT = ahistoDPtr(new AsciiHisto<double>(eEeV, 20, 0.01));
	ionsT = ahistoDPtr(new AsciiHisto<double>(eIeV, 20, 0.01));
	ionET = ahistoDPtr(new AsciiHisto<double>(eJeV, 20, 0.01));
	elecH = ahistoDPtr(new AsciiHisto<double>(eVel, 20, 0.01));
	ionH  = ahistoDPtr(new AsciiHisto<double>(iVel, 20, 0.01));

	for (auto v : eEeVsp) {
	  elecZH[v.first] = ahistoDPtr(new AsciiHisto<double>(v.second, 20, 0.01));
	}

	for (auto v : eIeVsp) {
	  ionZH[v.first] = ahistoDPtr(new AsciiHisto<double>(v.second, 20, 0.01));
	}

	// Make the quantiles
	size_t qnt_s = qnt.size(), ev_s = eVel.size();
	elecV.resize(qnt_s);
	ionV .resize(qnt_s);
	for (size_t i=0; i<qnt_s; i++) {
	  elecV[i] = eVel[floor(ev_s*qnt[i])];
	  ionV [i] = iVel[floor(ev_s*qnt[i])];
	}
      }

      if (loss.size()) {
	lossH = ahistoDPtr(new AsciiHisto<double>(loss, 20, 0.01));
      }

      if (keE.size()) {
	keEH = ahistoDPtr(new AsciiHisto<double>(keE, 20, 0.01));
      }

      if (keI.size()) {
	keIH = ahistoDPtr(new AsciiHisto<double>(keI, 20, 0.01));
      }

      if (mom.size()) {
	momH = ahistoDPtr(new AsciiHisto<double>(mom, 20, 0.01));
      }

      if (crs.size()) {
	crsH = ahistoDPtr(new AsciiHisto<double>(crs, 20, 0.01));
      }

    }

  }

  (*barrier)("CollideIon::electronGather complete", __FILE__, __LINE__);
}


void CollideIon::electronPrint(std::ostream& out)
{
  if (not distDiag) return;

  // Mean electron density per cell n #/cm^3
  //
  if (CntE) {
    RhoE /= CntE;
    Rho2 /= CntE;
    double disp2 = std::max<double>(Rho2 - RhoE*RhoE, 0.0);
    out << std::string(53, '-') << std::endl
	<< "-----Mean electron density (cm^{-3}): " << RhoE
	<< " +/- " << sqrt(disp2) << std::endl
	<< std::string(53, '-') << std::endl;
  }
  
  if (RhoN>0.0) {
    out << std::string(53, '-') << std::endl
	<< "-----Mean electron MFP (cm): "
	<< std::setw(14) << std::scientific << RhoN/RhoV
	<< "  (pc): " << std::fixed << RhoN/RhoV/pc << std::endl
	<< std::string(53, '-') << std::endl;
  }

  // Print the header for electron quantiles
  //
  if (elecV.size()) {
    out << std::endl << std::string(53, '-')  << std::endl
	<< "-----Electron velocity quantiles---------------------" << std::endl
	<< std::string(53, '-') << std::endl << std::left
	<< std::setw(12) << "Quantile"
	<< std::setw(16) << "V_electron"
	<< std::setw(16) << "V_ion"      << std::endl
	<< std::setw(12) << "--------"
	<< std::setw(16) << "----------"
	<< std::setw(16) << "----------" << std::endl;
    for (size_t i=0; i<qnt.size(); i++)
      out << std::setw(12) << qnt[i]
	  << std::setw(16) << elecV[i]
	  << std::setw(16) << ionV [i] << std::endl;
  }

  if (elecEVH.get()) {
    out << std::endl
	<< std::string(53, '-')  << std::endl
	<< "-----Electron interaction energy distribution--------" << std::endl
	<< std::string(53, '-')  << std::endl;
    (*elecEVH)(out);
  }

  if (elecEVHmin.get()) {
    out << std::endl
	<< std::string(53, '-')  << std::endl
	<< "-----Cell min interaction energy distribution--------" << std::endl
	<< std::string(53, '-')  << std::endl;
    (*elecEVHmin)(out);
  }

  if (elecEVHavg.get()) {
    out << std::endl
	<< std::string(53, '-')  << std::endl
	<< "-----Cell avg interaction energy distribution--------" << std::endl
	<< std::string(53, '-')  << std::endl;
    (*elecEVHavg)(out);
  }

  if (elecEVHmax.get()) {
    out << std::endl
	<< std::string(53, '-')  << std::endl
	<< "-----Cell max interaction energy distribution--------" << std::endl
	<< std::string(53, '-')  << std::endl;
    (*elecEVHmax)(out);
  }

  if (elecEVHsub.get()) {
    out << std::endl
	<< std::string(53, '-')  << std::endl;
    if (aType==Trace)
      out << "-----Selected electron-ion collision energies--------" << std::endl;
    else
      out << "-----Subspecies electron energy distribution---------" << std::endl;
    out << std::string(53, '-')  << std::endl;
    (*elecEVHsub)(out);
  }

  if (elecRCH.get()) {
    out << std::endl
	<< std::string(53, '-')  << std::endl
	<< "-----Electron recombination energy distribution--------" << std::endl
	<< std::string(53, '-')  << std::endl;
    (*elecRCH)(out);
  }

  if (elecRCN.get()) {
    out << std::endl
	<< std::string(53, '-')  << std::endl
	<< "-----Electron recombination counts * "
	<< std::setprecision(6) << std::scientific << rcmbScale
	<< std::fixed << std::endl
	<< std::string(53, '-')  << std::endl;
    (*elecRCN)(out);
  }

  if (elecH.get()) {
    out << std::endl
	<< std::string(53, '-')  << std::endl
	<< "-----Electron velocity distribution------------------" << std::endl
	<< std::string(53, '-')  << std::endl;
    (*elecH)(out);
  }

  if (elecT.get()) {
    out << std::endl
	<< std::string(53, '-')  << std::endl
	<< "-----Electron energy (in eV) distribution------------" << std::endl
	<< std::string(53, '-')  << std::endl;
    (*elecT)(out);
  }

  if (ionsT.get()) {
    out << std::endl
	<< std::string(53, '-')  << std::endl
	<< "-----Ion energy (in eV) distribution-----------------" << std::endl
	<< std::string(53, '-')  << std::endl;
    (*ionsT)(out);
  }

  if (ionET.get()) {
    out << std::endl
	<< std::string(53, '-')  << std::endl
	<< "-----Ion-electron energy (in eV) distribution--------" << std::endl
	<< std::string(53, '-')  << std::endl;
    (*ionET)(out);
  }

  for (auto v : elecZH) {
    out << std::endl
	<< std::string(53, '-')  << std::endl
	<< "-----Electron (Z=" << v.first
	<< ") energy (in eV) distribution-----------" << std::endl
	<< std::string(53, '-')  << std::endl;
    (*v.second)(out);
  }

  for (auto v : ionZH) {
    out << std::endl
	<< std::string(53, '-')  << std::endl
	<< "-----Ion (Z=" << v.first
	<< ") energy (in eV) distribution-----------" << std::endl
	<< std::string(53, '-')  << std::endl;
    (*v.second)(out);
  }

  if (ionH.get()) {
    out << std::endl
	<< std::string(53, '-')  << std::endl
	<< "-----Ion velocity distribution-----------------------" << std::endl
	<< std::string(53, '-')  << std::endl;
    (*ionH)(out);
  }
  if (lossH.get()) {
    out << std::endl
	<< std::string(53, '-')  << std::endl
	<< "-----Electron energy gain/loss distribution----------" << std::endl
	<< std::string(53, '-')  << std::endl;
    (*lossH)(out);
  }
  if (keEH.get()) {
    out << std::endl
	<< std::string(53, '-')  << std::endl
	<< "-----Relative electron energy gain/loss -------------" << std::endl
	<< std::string(53, '-')  << std::endl;
    (*keEH)(out);
  }
  if (keIH.get()) {
    out << std::endl
	<< std::string(53, '-')  << std::endl
	<< "-----Relative ion energy gain/loss ------------------" << std::endl
	<< std::string(53, '-')  << std::endl;
    (*keIH)(out);
  }

  if (momH.get()) {
    out << std::endl
	<< std::string(53, '-')  << std::endl
	<< "-----Electron momentum difference ratio -------------" << std::endl
	<< std::string(53, '-')  << std::endl;
    (*momH)(out);
  }

  if (crsH.get()) {
    out << std::endl
	<< std::string(53, '-')  << std::endl
	<< "-----Trace Kinetic/Inelastic loss ratio -------------" << std::endl
	<< std::string(53, '-')  << std::endl;
    (*crsH)(out);
    int ilab = std::get<1>(energyD);
    out << std::setw(14) << " min(E)"     << std::setw(16) << std::get<0>(energyD)[0] << std::endl
	<< std::setw(14) << " max(E)"     << std::setw(16) << std::get<0>(energyD)[1] << std::endl
	<< std::setw(14) << " max(KE)"    << std::setw(16) << std::get<0>(energyD)[2] << std::endl
	<< std::setw(14) << " max(consE)" << std::setw(16) << std::get<0>(energyD)[3] << std::endl
	<< std::setw(14) << " max(type)"  << std::setw(16) << interLabels[ilab % 100] << std::setw(6) << std::right << ilab << std::endl;
  }


  if ((aType==Hybrid or aType==Trace) and collLim) {
    out << std::endl << std::string(53, '-') << std::endl
	<< "-----Collisions per cell over limit------------------" << std::endl
	<< std::string(53, '-') << std::endl << std::left
	<< std::setw(14) << " Over"      << std::setw(16) << std::get<0>(clampStat)    << std::endl
	<< std::setw(14) << " Mean"      << std::setw(16) << std::get<1>(clampStat)    << std::endl
	<< std::setw(14) << " Max"       << std::setw(16) << std::get<2>(clampStat)    << std::endl
	<< std::setw(14) << " Total"     << std::setw(16) << c0->Tree()->TotalNumber() << std::endl;
  }

  out << std::endl << std::string(53, '-') << std::endl
      << "-----Electron NTC diagnostics------------------------" << std::endl
      << std::string(53, '-') << std::endl << std::left
      << std::setw(14) << " Over"      << std::setw(16) << Ovr_s << std::endl
      << std::setw(14) << " Accepted"  << std::setw(16) << Acc_s << std::endl
      << std::setw(14) << " Total"     << std::setw(16) << Tot_s << std::endl
      << std::fixed;

  if (Num_s>0) {
    Rat_s /= Num_s;
    out << std::setw(14) << " TargR"   << std::setw(16) << Rat_s << std::endl;
  }

  if (Tot_s>0)
    out << std::setw(14) << " Ratio"     << std::setw(16) << static_cast<double>(Acc_s)/Tot_s << std::endl
	<< std::setw(14) << " Fail"      << std::setw(16) << static_cast<double>(Ovr_s)/Tot_s << std::endl;

  out << std::string(53, '-') << std::endl << std::right;

  out << std::endl;

  if (eeHisto.size() > 0) {

    for (auto j : eeHisto) {

      if (j.second.get()) {
	out << std::endl << std::string(53, '-') << std::endl
	    << std::left << std::fixed
	    << " Quantile: " << j.first << std::endl
	    << std::string(53, '-') << std::endl
	    << std::left << std::scientific;
	(*j.second)(out);
	out << std::endl;
      }
    }
  }

  if (aType==Hybrid and IonRecombChk) {
    out << std::endl << std::string(53, '-') << std::endl
	<< std::left << std::fixed
	<< " Electron ionization/recombination tally" << std::endl
	<< std::string(53, '-') << std::endl
	<< std::left << std::scientific;

    out << std::endl << "===> Ionization" << std::endl;

    for (auto v : ionCHK[0])
      out << std::setw( 4) << v.first.first
	  << std::setw( 4) << v.first.second
	  << std::setw(18) << v.second
	  << std::endl;

    out << std::endl << "===> Recombination" << std::endl;

    for (auto v : recombCHK[0])
      out << std::setw( 4) << v.first.first
	  << std::setw( 4) << v.first.second
	  << std::setw(18) << v.second
	  << std::endl;

    ionCHK   [0].clear();
    recombCHK[0].clear();
  }

  // Print out Coulomb statistics from the generalized Bobylev-Nanbu
  // algorithm
  //
  if (aType==Trace) {

    out << std::endl << std::string(53, '-') << std::endl
	<< std::left << std::fixed
	<< "-----Coulomb scattering statistics-------------------" << std::endl
	<< std::string(53, '-') << std::endl
	<< std::left << std::scientific;
    
    out << std::setw(12) << "Species" << std::setw(12) << "Count"
	<< std::setw(18) << "Tau"     << std::setw(18) << "S(Tau)"
	<< std::setw(18) << "Frac"
	<< std::endl << std::endl;

    // Counts
    unsigned nion = tauIon[0][0];
    unsigned nelc = tauElc[0][0];
    unsigned nups = colUps[0][0];

    out << std::setw(12) << "Ions"<< std::setw(12) << nion;
    if (nion)
      out << std::setw(18) << tauIon[0][1]/nion
	  << std::setw(18) << sqrt(fabs(tauIon[0][2] - tauIon[0][1]*tauIon[0][1]/nion)/nion)
	  << std::setw(18) << tauIon[0][3]/nion
	  << std::endl;
    else
      out << std::setw(18) << "*****"
	  << std::setw(18) << "*****"
	  << std::setw(18) << "*****"
	  << std::endl;

    out << std::setw(12) << "Electrons" << std::setw(12) << nelc;
    if (nelc)
      out << std::setw(18) << tauElc[0][1]/nelc
	  << std::setw(18) << sqrt(fabs(tauElc[0][2] - tauElc[0][1]*tauElc[0][1]/nelc)/nelc)
	  << std::setw(18) << tauElc[0][3]/nelc
	  << std::endl;
    else
      out << std::setw(18) << "*****"
	  << std::setw(18) << "*****"
	  << std::setw(18) << "*****"
	  << std::endl;

    out << std::setw(12) << "Upscale" << std::setw(12) << nups;
    if (nups)
      out << std::setw(18) << colUps[0][1]/nups
	  << std::setw(18) << sqrt(fabs(colUps[0][2] - colUps[0][1]*colUps[0][1]/nups)/nups)
	  << std::setw(18) << colUps[0][3]/nups
	  << std::endl;
    else
      out << std::setw(18) << "*****"
	  << std::setw(18) << "*****"
	  << std::setw(18) << "*****"
	  << std::endl;

    out << std::string(53, '-') << std::endl;

    for (auto & v : tauIon) { for(auto & u : v) u = 0.0; }
    for (auto & v : tauElc) { for(auto & u : v) u = 0.0; }
    for (auto & v : colUps) { for(auto & u : v) u = 0.0; }
  }
}

void CollideIon::electronReset()
{
  // Zero out counters
  for (auto & v : tauIon) { for(auto & u : v) u = 0.0; }
  for (auto & v : tauElc) { for(auto & u : v) u = 0.0; }
  for (auto & v : colUps) { for(auto & u : v) u = 0.0; }
}

const std::string clabl(unsigned c)
{
  std::ostringstream sout;
  sout << "[" << c << "]  ";
  return sout.str();
}

// Print out species counts (Trace version)
//
void CollideIon::printSpeciesTrace()
{
  std::ofstream dout;

  // Generate the file name, if it does not exist
  //
  if (species_file_debug.size()==0) {
    std::ostringstream sout;
    sout << outdir << runtag << ".species";
    species_file_debug = sout.str();

    // Check for existence of file
    //
    std::ifstream in (species_file_debug.c_str());

    // Write a new file?
    //
    if (in.fail()) {

      // Open the file for the first time
      //
      dout.open(species_file_debug.c_str());

      // Print the header
      //
      int nhead = 2;
      if (use_elec>=0) {
	dout << "# "
	     << std::setw(12) << std::right << "Time  "
	     << std::setw(12) << std::right << "Temp_i"
	     << std::setw(12) << std::right << "KE_i"
	     << std::setw(12) << std::right << "Temp_e"
	     << std::setw(12) << std::right << "KE_e";
	nhead = 4;
      } else {
	dout << "# "
	     << std::setw(12) << std::right << "Time  "
	     << std::setw(12) << std::right << "Temp  "
	     << std::setw(12) << std::right << "Disp  ";
      }
      for (spDItr it=specM.begin(); it != specM.end(); it++) {
	std::ostringstream sout;
	sout << "(" << it->first.first << "," << it->first.second << ") ";
	dout << std::setw(12) << right << sout.str();
      }
      dout << std::endl;

      unsigned cnt = 0;
      dout << "# "
	   << std::setw(12) << std::right << clabl(++cnt);
      for (int n=0; n<nhead; n++)
	dout << std::setw(12) << std::right << clabl(++cnt);
      for (spDItr it=specM.begin(); it != specM.end(); it++)
	dout << std::setw(12) << right << clabl(++cnt);
      dout << std::endl;

      dout << "# "
	   << std::setw(12) << std::right << "--------";
      for (int n=0; n<nhead; n++)
	dout << std::setw(12) << std::right << "--------";
      for (spDItr it=specM.begin(); it != specM.end(); it++)
	dout << std::setw(12) << std::right << "--------";
      dout << std::endl;
    }
  }

  const double Tfac = 2.0*TreeDSMC::Eunit/3.0 * amu  /
    TreeDSMC::Munit/boltz;
  
  double Ti = 0.0, Te = 0.0;
  if (tM[1]>0.0) Ti = Tfac*tM[0]/tM[1];
  if (tM[3]>0.0) Te = Tfac*tM[2]/tM[3];
  
  // Open for append
  //
  if (!dout.is_open())
    dout.open(species_file_debug.c_str(), ios::out | ios::app);

  dout << std::setprecision(5);
  dout << "  "
       << std::setw(12) << std::right << tnow
       << std::setw(12) << std::right << Ti
       << std::setw(12) << std::right << tM[0];
  if (use_elec>=0)
    dout << std::setw(12) << std::right << Te
	 << std::setw(12) << std::right << tM[2];
  for (spDItr it=specM.begin(); it != specM.end(); it++)
    dout << std::setw(12) << std::right << it->second;
  dout << std::endl;
}


// Compute the mean molecular weight in atomic mass units
//
double CollideIon::molWeight(pCell *cell)
{
  double mol_weight = 1.0;

  if (aType==Direct or aType==Weight or aType==Hybrid) {
    double numbC = 0.0, massC = 0.0;
    for (auto it : cell->count) {
      speciesKey i = it.first;
      double M = cell->Mass(i);
      numbC += M / atomic_weights[i.first];
      massC += M;
    }

    mol_weight = massC/numbC;
  }

  if (aType==Trace) {
    double numbC = 0.0, massC = 0.0;

    for (auto b : cell->bods) {
      
      try {
	Particle *p = tree->Body(b);
	double m = p->mass;

	for (auto s : SpList) {
	  speciesKey i = s.first;
	  numbC += m * p->dattrib[s.second] / atomic_weights[i.first];
	  massC += m * p->dattrib[s.second] ;
	}
      }
      catch(const std::string & errstr) {
	std::cout << errstr << ", body=" << b << std::endl;
      }
    }

    mol_weight = massC/numbC;
  }

  if (0) {
    double numbC = 0.0, massC = 0.0;
    for (auto it : cell->count) {
      speciesKey i = it.first;
      double M = cell->Mass(i);
      numbC += M * ZWList[i.first];
      massC += M;
    }

    mol_weight = massC/numbC;
  }

  return mol_weight;
}


void CollideIon::printSpeciesElectrons
(std::map<speciesKey, unsigned long>& spec, const std::array<double, 4>& T)
{
  if (myid) return;

  typedef std::map<speciesKey, unsigned long> spCountMap;
  typedef spCountMap::iterator spCountMapItr;

				// Field width
  const unsigned short wid = 16;

  std::ofstream dout;

				// Generate the file name
  if (species_file_debug.size()==0) {
    std::ostringstream sout;
    sout << outdir << runtag << ".species";
    species_file_debug = sout.str();

    // Make species list
    //
    for (auto k : spec) specZ.insert(k.first.first);

    // Check for existence of file
    //
    std::ifstream in (species_file_debug.c_str());

    // Write a new file?
    //
    if (in.fail()) {

      // Open the file for the first time
      //
      dout.open(species_file_debug.c_str());

      // Print the header
      //
      dout << "# "
	   << std::setw(wid) << std::right << "Time "
	   << std::setw(wid) << std::right << "Temp "
	   << std::setw(wid) << std::right << "KE ";
      if (aType == Hybrid) {
	for (spDMap::iterator it=specM.begin(); it != specM.end(); it++) {
	  std::ostringstream sout;
	  sout << "(" << it->first.first << "," << it->first.second << ") ";
	  dout << setw(wid) << right << sout.str();
	}
      } else {
	for (spCountMapItr it=spec.begin(); it != spec.end(); it++) {
	  std::ostringstream sout;
	  sout << "(" << it->first.first << "," << it->first.second << ") ";
	  dout << setw(wid) << right << sout.str();
	}
      }
      dout << std::setw(wid) << std::right << "Cons_E"
	   << std::setw(wid) << std::right << "Ions_E"
	   << std::setw(wid) << std::right << "Comb_E";
      if (use_elec>=0) {
	dout << std::setw(wid) << std::right << "Temp_E"
	     << std::setw(wid) << std::right << "Elec_E"
	     << std::setw(wid) << std::right << "Cons_G"
	     << std::setw(wid) << std::right << "Totl_E";
	for (auto Z : specZ) {
	  std::ostringstream sout1, sout2, sout3, sout4, sout5;
	  std::ostringstream sout6, sout7, sout8, sout9, sout0;
	  sout1 << "Eion(" << Z << ")";
	  sout2 << "Nion(" << Z << ")";
	  sout3 << "Tion(" << Z << ")";
	  sout4 << "Sion(" << Z << ")";
	  sout5 << "Rion(" << Z << ")";
	  sout6 << "Eelc(" << Z << ")";
	  sout7 << "Nelc(" << Z << ")";
	  sout8 << "Telc(" << Z << ")";
	  sout9 << "Selc(" << Z << ")";
	  sout0 << "Relc(" << Z << ")";
	  dout << std::setw(wid) << std::right << sout1.str()
	       << std::setw(wid) << std::right << sout2.str()
	       << std::setw(wid) << std::right << sout3.str()
	       << std::setw(wid) << std::right << sout4.str()
	       << std::setw(wid) << std::right << sout5.str();
	  for (int j=0; j<3; j++) {
	    std::ostringstream sout;
	    sout << "Vi[" << j << "](" << Z << ")";
	    dout << std::setw(wid) << std::right << sout.str();
	  }
	  dout << std::setw(wid) << std::right << sout6.str()
	       << std::setw(wid) << std::right << sout7.str()
	       << std::setw(wid) << std::right << sout8.str()
	       << std::setw(wid) << std::right << sout9.str()
	       << std::setw(wid) << std::right << sout0.str();
	  for (int j=0; j<3; j++) {
	    std::ostringstream sout;
	    sout << "Ve[" << j << "](" << Z << ")";
	    dout << std::setw(wid) << std::right << sout.str();
	  }
	}
      }
      dout << std::endl;

      dout << "# "
	   << std::setw(wid) << std::right << "--------"
	   << std::setw(wid) << std::right << "--------"
	   << std::setw(wid) << std::right << "--------";
      if (aType == Hybrid) {
	for (spDMap::iterator it=specM.begin(); it != specM.end(); it++)
	  dout << setw(wid) << std::right << "--------";
      } else {
	for (spCountMapItr it=spec.begin(); it != spec.end(); it++)
	  dout << setw(wid) << std::right << "--------";
      }
      for (int j=0; j<3; j++)
	dout << std::setw(wid) << std::right << "--------";
      if (use_elec>=0) {
	for (int j=0; j<4; j++)
	  dout << std::setw(wid) << std::right << "--------";
	for (size_t z=0; z<specZ.size(); z++) {
	  for (int j=0; j<10+6; j++)
	    dout << std::setw(wid) << std::right << "--------";
	}
      }
      dout << std::endl;

    }
  }

  // Open for append
  //
  if (!dout.is_open())
    dout.open(species_file_debug.c_str(), ios::out | ios::app);


  double tmass = 0.0;
  if (aType != Hybrid) {
    for (spCountMapItr it=spec.begin(); it != spec.end(); it++)
      tmass += ZMList[it->first.first] * it->second;
  }

				// Use total mass to print mass
				// fraction
  dout << "  "
       << std::setw(wid) << std::right << tnow
       << std::setw(wid) << std::right << T[0]
       << std::setw(wid) << std::right << T[1];


  if (aType == Hybrid) {

    for (spDMap::iterator it=specM.begin(); it != specM.end(); it++) {
      dout << std::setw(wid) << std::right << it->second;
    }

  } else {

    for (spCountMapItr it=spec.begin(); it != spec.end(); it++) {
      if (tmass > 0.0)
	dout << std::setw(wid) << std::right
	     << ZMList[it->first.first] * it->second / tmass;
      else
	dout << std::setw(wid) << std::right << 0.0;
    }
  }

  const double Tfac = 2.0*TreeDSMC::Eunit/3.0 * amu  /
    TreeDSMC::Munit/boltz;

  double totlE = tM[0] + tM[2], numbE = tM[3], tempE = tM[2];
  if (numbE>0.0) tempE = Tfac*totlE/numbE;

  dout << std::setw(wid) << std::right << consE
       << std::setw(wid) << std::right << totlE
       << std::setw(wid) << std::right << totlE + consE;
  
  if (use_elec>=0)
    dout << std::setw(wid) << std::right << tempE
	 << std::setw(wid) << std::right << totlE
	 << std::setw(wid) << std::right << consG
	 << std::setw(wid) << std::right << totlE + consE + consG;
  for (auto Z : specZ) {

    if (specI.find(Z) != specI.end()) {
      double E = 0.0, S = 0.0, T = 0.0, R = 0.0, N = std::get<1>(specI[Z]);
      std::array<double, 3> V;
      if (N > 0.0) {
	for (int j=0; j<3; j++) {
	  double v1 = std::get<0>(std::get<0>(specI[Z])[j])/N;
	  double v2 = std::get<1>(std::get<0>(specI[Z])[j]);
	  E += 0.5*v2;
	  S += 0.5*(v2 - v1*v1*N);
	  V[j] = v1;
	}
	T = E * Tfac * atomic_weights[Z] / N;
	R = S * Tfac * atomic_weights[Z] / N;
      }

      dout << std::setw(wid) << std::right << E
	   << std::setw(wid) << std::right << N
	   << std::setw(wid) << std::right << T
	   << std::setw(wid) << std::right << S
	   << std::setw(wid) << std::right << R;
      for (int j=0; j<3; j++)
	dout << std::setw(wid) << std::right << V[j];
    } else {
      for (int j=0; j<8; j++)
	dout << std::setw(wid) << std::right << 0.0;
    }

    if (specE.find(Z) != specE.end()) {

      double E = 0.0, S = 0.0, T = 0.0, R = 0.0, N = std::get<1>(specE[Z]);
      std::array<double, 3> V;
      if (N > 0.0) {
	for (int j=0; j<3; j++) {
	  double v1 = std::get<0>(std::get<0>(specE[Z])[j])/N;
	  double v2 = std::get<1>(std::get<0>(specE[Z])[j]);
	  E += 0.5*v2;
	  S += 0.5*(v2 - v1*v1*N);
	  V[j] = v1;
	}
	T = E * Tfac * atomic_weights[0] / N;
	R = S * Tfac * atomic_weights[0] / N;
      }


      dout << std::setw(wid) << std::right << E
	   << std::setw(wid) << std::right << N
	   << std::setw(wid) << std::right << T
	   << std::setw(wid) << std::right << S
	   << std::setw(wid) << std::right << R;
      for (int j=0; j<3; j++)
	dout << std::setw(wid) << std::right << V[j];
    } else {
      for (int j=0; j<8; j++)
	dout << std::setw(wid) << std::right << 0.0;
    }
  }
  dout << std::endl;
}

void CollideIon::processConfig()
{
  // Parse test algorithm features
  //
  Configuration cfg;
  std::string config(config0);

  // Ensure that the original config is used, unless explicited edited
  // by the user
  //
  if (restart) config = runtag + ".CollideIon.config.json";

  try {

    if ( !boost::filesystem::exists(config) ) {
      if (myid==0) std::cout << "CollideIon: can't find config file <"
			     << config << ">, using defaults" << std::endl;
    } else {
      cfg.load(config, "JSON");
    }

    if (!cfg.property_tree().count("_description")) {
      // Write description as the first node
      //
      ptree::value_type val("_description",
			    ptree("CollideIon configuration, tag=" + runtag));
      cfg.property_tree().insert(cfg.property_tree().begin(), val);
    }

    // Write or rewrite date string
    {
      time_t t = time(0);   // get current time
      struct tm * now = localtime( & t );
      std::ostringstream sout;
      sout << (now->tm_year + 1900) << '-'
	   << (now->tm_mon + 1) << '-'
	   <<  now->tm_mday << ' '
	   << std::setfill('0') << std::setw(2) << now->tm_hour << ':'
	   << std::setfill('0') << std::setw(2) << now->tm_min  << ':'
	   << std::setfill('0') << std::setw(2) << now->tm_sec;

      if (cfg.property_tree().count("_date")) {
	std::string orig = cfg.property_tree().get<std::string>("_date");
	cfg.property_tree().put("_date", sout.str() + " [" + orig + "] ");
      } else {
	// Write description as the second node, after "_description"
	//
	ptree::assoc_iterator ait = cfg.property_tree().find("_description");
	ptree::iterator it = cfg.property_tree().to_iterator(ait);
	ptree::value_type val("_date", ptree(sout.str()));
	cfg.property_tree().insert(++it, val);
      }
    }

    NoExact =
      cfg.entry<bool>("NO_EXACT", "Enable equal electron-ion interactions in Hybrid method", false);

    IPS =
      cfg.entry<bool>("USE_IPS", "Use mean interparticle spacing as minimum Coulombic impact parameter", false);

    ExactE =
      cfg.entry<bool>("ENERGY_ES", "Enable the explicit energy conservation algorithm", false);

    DebugE =
      cfg.entry<bool>("ENERGY_ES_DBG", "Enable explicit energy conservation checking", true);

    MeanMass =
      cfg.entry<bool>("MEAN_MASS", "Mean mass, energy and momentum conserving algorithm", false);
    
    AlgOrth =
      cfg.entry<bool>("ENERGY_ORTHO", "Add energy in orthogonal direction", false);

    AlgWght =
      cfg.entry<bool>("ENERGY_WEIGHT", "Energy conservation weighted by superparticle number count", false);

    TRACE_ELEC =
      cfg.entry<bool>("TRACE_ELEC", "Add excess energy directly to the electrons", false);

    ALWAYS_APPLY =
      cfg.entry<bool>("ALWAYS_APPLY", "Attempt to remove excess energy from all interactions", false);

    if (!ExactE and ALWAYS_APPLY) {
      ALWAYS_APPLY = false;
      if (myid==0) {
	std::cout << "ALWAYS_APPLY should only be used with ENERGY_ES"
		  << "; I am disabling this option.  If you are sure"
		  << std::endl
		  << "that you want this, change the code at line "
		  << __LINE__ << " in file " << __FILE__ << std::endl;
      }
    }

    COLL_SPECIES =
      cfg.entry<bool>("COLL_SPECIES", "Print collision count by species for debugging", false);

    SECONDARY_SCATTER =
      cfg.entry<unsigned>("SECONDARY_SCATTER", "Scatter electron with its donor ion n times (0 value for no scattering)", 0);

    TRACE_FRAC =
      cfg.entry<double>("TRACE_FRAC", "Add this fraction to electrons and rest to ions", 1.0f);

    SAME_ELEC_SCAT =
      cfg.entry<bool>("SAME_ELEC_SCAT", "Only scatter electrons with the same donor-ion mass", false);

    DIFF_ELEC_SCAT =
      cfg.entry<bool>("DIFF_ELEC_SCAT", "Only scatter electrons with different donor-ion mass", false);

    SAME_IONS_SCAT =
      cfg.entry<bool>("SAME_IONS_SCAT", "Only scatter ions with the same mass", false);

    SAME_INTERACT =
      cfg.entry<bool>("SAME_INTERACT", "Only perform interactions with equal-mass particles", false);

    DIFF_INTERACT =
      cfg.entry<bool>("DIFF_INTERACT", "Only perform interactions with different species particles", false);

    Fwght =
      cfg.entry<double>("WEIGHT_RATIO", "Weighting ratio for spreading excess energy to components", 0.5);

    TRACE_OVERRIDE =
      cfg.entry<bool>("TRACE_OVERRIDE", "Distribute energy equally to trace species", false);

    NOCOOL_ELEC =
      cfg.entry<bool>("NOCOOL_ELEC", "Suppress distribution of energy to electrons when using NOCOOL", false);

    NOSHARE_ELEC =
      cfg.entry<bool>("NOSHARE_ELEC", "Suppress distribution of ionization energy between electrons", false);

    CLONE_ELEC =
      cfg.entry<bool>("CLONE_ELEC", "Clone energy of ionizing electron to newly created free electron", false);

    frost_warning =
      cfg.entry<bool>("frost_warning", "Warn if energy lost is smaller than available energy", false);

    DEBUG_SL =
      cfg.entry<bool>("DEBUG_SL", "Enable verbose interaction selection diagnostics", false);

    DEBUG_CR =
      cfg.entry<bool>("DEBUG_CR", "Enable printing of relative cross sections and probabilities for interaction selection", false);

    DEBUG_NQ =
      cfg.entry<bool>("DEBUG_NQ", "Printing of cross section debug info for unequal species only", false);

    NO_DOF =
      cfg.entry<bool>("NO_DOF", "Suppress adjustment of electron speed based on degrees of freedom", true);

    NO_VEL =
      cfg.entry<bool>("NO_VEL", "Suppress adjustment of electron speed for equipartition equilibrium", false);

    NO_ION_E =
      cfg.entry<bool>("NO_ION_E", "Suppress energy loss from ionization", false);

    NO_FF =
      cfg.entry<bool>("NO_FF", "Ignore free-free interaction", false);

    NO_FF_E =
      cfg.entry<bool>("NO_FF_E", "Suppress energy loss from free-free", false);

    KE_DEBUG =
      cfg.entry<bool>("KE_DEBUG", "Check energy bookkeeping for debugging", true);

    NO_HSCAT =
      cfg.entry<bool>("NO_HSCAT", "Artificially suppress scattering for debugging Trace method", false);

    DBG_HSCAT =
      cfg.entry<bool>("DBG_HSCAT", "Cache and check constancy of masses and weights with scattering for only debugging Trace method", false);

    tolE =
      cfg.entry<double>("tolE", "Threshold for reporting energy conservation bookkeeping", 1.0e-5);

    tolCS =
      cfg.entry<double>("tolCS", "Threshold for cross-section sanity using NTCdb", 1.0);

    qCrit =
      cfg.entry<double>("qCrit", "Critical weighting threshold for energy conserving electron interactions", -1.0);

    RECOMB_IP =
      cfg.entry<bool>("RECOMB_IP", "Electronic binding energy is lost in recombination", false);

    CROSS_DBG =
      cfg.entry<bool>("CROSS_DBG", "Enable verbose cross-section value diagnostics", false);

    EXCESS_DBG =
      cfg.entry<bool>("EXCESS_DBG", "Enable check for excess weight counter in trace algorithm", false);

    DEBUG_CNT =
      cfg.entry<int>("DEBUG_CNT", "Count collisions in each particle for debugging", -1);

    collLim =
      cfg.entry<bool>("COLL_LIMIT", "Limit number of collisions per particle", false);

    collCor =
      cfg.entry<bool>("COLL_CORR", "Correct collisions per particle for limit", false);

    maxSel =
      cfg.entry<unsigned>("COLL_LIMIT_ABS", "Limiting number of collisions per cell", 5000);

    energy_scale =
      cfg.entry<double>("COOL_SCALE", "If positive, reduce the inelastic energy by this fraction", -1.0);

    TSESUM =
      cfg.entry<bool>("TSESUM", "Use sum per cell of TRUE, use max per cell if FALSE for setting time step", true);

    TSCOOL =
      cfg.entry<double>("TSCOOL", "Multiplicative factor for choosing cooling time step", 0.05);

    TSFLOOR =
      cfg.entry<double>("TSFLOOR", "Floor KE/deltaE for choosing cooling time step", 0.001);

    scatFac1 =
      cfg.entry<double>("scatFac1", "Energy split in favor of dominant species (Hybrid)", 1.0);

    scatFac2 =
      cfg.entry<double>("scatFac2", "Energy split in favor of trace species (Hybrid)", 1.0);

    E_split =
      cfg.entry<bool>("E_split", "Apply energy loss to ions and electrons", false);

    KE_DEBUG =
      cfg.entry<bool>("KE_DEBUG", "Check energy bookkeeping for debugging", false);

    FloorEv =
      cfg.entry<double>("FloorEv", "Minimum energy for Coulombic elastic scattering cross section", 0.05f);

    minCollFrac =
      cfg.entry<double>("minCollFrac", "Minimum relative fraction for collisional excitation", -1.0f);

    maxCoul = 
      cfg.entry<unsigned>("maxCoul", "Maximum number of elastic Coulombic collisions per step", UINT_MAX);

    logL = 
      cfg.entry<double>("logL", "Coulombic log(Lambda) value", 24.0);

    coulInter =
      cfg.entry<bool>("coulInter", "Compute maximum cross section based Chandrasekhar interference", true);

    Collide::collTnum = 
      cfg.entry<unsigned>("collTnum", "Target number of accepted collisions per cell for assigning time step", UINT_MAX);

    distDiag =
      cfg.entry<bool>("distDiag", "Report binned histogram for particle energies", false);

    elecDist =
      cfg.entry<bool>("elecDist", "Additional detailed histograms for electron velocities", false);

    rcmbDist =
      cfg.entry<bool>("recombDist", "Histograms for electron recombination energy", false);

    rcmbDlog =
      cfg.entry<bool>("recombDistLog", "Logscale histogram electron recombination energy", true);

    ntcDist =
      cfg.entry<bool>("ntcDist", "Enable NTC full distribution for electrons", false);

    photoDiag =
      cfg.entry<bool>("photoDiag", "Enable photoionization diagnostics", false);

    enforceMOM =
      cfg.entry<bool>("enforceMOM", "Enforce momentum conservation per cell (for ExactE and Hybrid)", false);

    coulScale =
      cfg.entry<bool>("coulScale", "Use 'effective' ion-electron scattering cross section", false);

    coulPow =
      cfg.entry<double>("coulPow", "Energy power scaling for 'effective' ion-electron scattering cross section", false);

    elc_cons =
      cfg.entry<bool>("ElcCons", "Use separate electron conservation of energy collection", true);

    debugFC =
      cfg.entry<bool>("debugFC", "Enable finalize-cell electron scattering diagnostics", false);

    newRecombAlg =
      cfg.entry<bool>("newRecombAlg", "Compute recombination cross section based on ion's electron", false);

    HybridWeightSwitch =
      cfg.entry<bool>("HybridWeightSwitch", "Use full trace algorithm for interaction fractions below threshold", false);

    DBG_NewTest =
      cfg.entry<bool>("DBG_TEST", "Verbose debugging of energy conservation", false);

    scatter_check =
      cfg.entry<bool>("scatterCheck", "Print interaction channel diagnostics", false);

    recomb_check =
      cfg.entry<bool>("recombCheck", "Print recombination coefficient for all active species", false);

    NO_ION_ION =
      cfg.entry<bool>("NO_ION_ION", "Artificially suppress the ion-ion scattering in the Hybrid method", false);

    NO_ION_ELECTRON =
      cfg.entry<bool>("NO_ION_ELECTRON", "Artificially suppress the ion-electron scattering in the Hybrid method", false);

    use_spectrum =
      cfg.entry<bool>("Spectrum", "Tabulate emission spectrum.  Use log scale if min > 0.0 and wvlSpect is false", false);

    wvlSpect =
      cfg.entry<bool>("wvlSpectrum", "Tabulate emission spectrum using in constant wavelength bins", true);

    minSpect =
      cfg.entry<double>("minSpect", "Minimum energy (eV) or wavelength (in angstrom) for tabulated emission spectrum", 100.0);

    maxSpect =
      cfg.entry<double>("maxSpect", "Maximum energy (eV) or wavelength (in angstrom) for tabulated emission spectrum", 20000.0);

    delSpect =
      cfg.entry<double>("delEvSpect", "Energy or wavelength bin width for tabulated emission spectrum (eV)", 100.0);

    ESthresh =
      cfg.entry<double>("ESthresh", "Ionization threshold for electron-electron scattering", 1.0e-10);

    photoIB =
      cfg.entry<std::string>("photoIB", "Photo ionization background model (none, uvIGM)", "none");

    use_photon = 
      cfg.entry<int>("use_photon", "Attribute position for photon interaction time", -1);

    {
      std::string phType =
	cfg.entry<std::string>("phType", "Photo ionization background type (Particle, Collision)", "Particle");

      photoIBType = phMap[phType];

      if (photoIBType == perCollision and use_photon<0) {
	if (myid==0)
	  std::cout << "You requested the photoionization <perCollision> algorithm but no attribute position" << std::endl
		    << "Switching to <perParticle> algorithm" << std::endl;
	photoIBType = perParticle;
      }
    }

    IonElecRate = 
      cfg.entry<bool>("ION_ELEC_RATE", "Use ion-ion relative speed to compute electron-electron interaction rate", false);

    reverse_apply = 
      cfg.entry<bool>("REVERSE_APPLY", "Add energy excess from momentum conservation ion only", false);

    elec_balance = 
      cfg.entry<bool>("ELEC_BALANCE", "Add energy excess from momentum conservation to electron and ion free pool", true);

    ke_weight = 
      cfg.entry<bool>("KE_WEIGHT", "Add energy excess from momentum conservation to electron and weighted by KE", true);

    Collide::DEBUG_NTC =
      cfg.entry<bool>("DEBUG_NTC", "Enable verbose NTC diagnostics", false);

    Collide::NTC_DIST =
      cfg.entry<bool>("NTC_DIST", "Enable full NTC distribution output", false);

    Collide::numSanityStop =
      cfg.entry<bool>("collStop", "Stop simulation if collisions per step are over threshold", false);

    Collide::numSanityMax =
      cfg.entry<unsigned>("maxStop", "Threshold for simulation stop", 100000000u);

    Collide::numSanityMsg =
      cfg.entry<bool>("collMsg", "Report collisions over threshold value", false);

    Collide::numSanityVal =
      cfg.entry<unsigned>("collMin", "Minimum threshold for reporting", 10000000u);

    Collide::numSanityFreq =
      cfg.entry<unsigned>("collFreq", "Stride for collision reporting", 2000000u);

    Collide::use_ntcdb =
      cfg.entry<bool>("ntcDB", "Use the NTC data base for max CrsVel values", true);

    Collide::ntcThresh =
      cfg.entry<double>("ntcThresh", "Quantile for NTC CrsVel", 0.95);

    Collide::ntcFactor =
      cfg.entry<double>("ntcFactor", "Scaling factor NTC CrsVel", 1.0);

    Ion::HandM_coef = 
      cfg.entry<double>("HandMcoef", "Coefficient for Haard & Madau UV spectral flux", 1.5e-22);

    Ion::HandM_expon = 
      cfg.entry<double>("HandMexpon", "Frequency exponent for Haard & Madau UV spectral flux", -0.5);

    Ion::gs_only =
      cfg.entry<bool>("radRecombGS", "Cross section for recombination into the ground state only", false);

    Ion::useFreeFreeGrid =
      cfg.entry<bool>("freeFreeCache", "Use cache for free-free cross section", true);

    Ion::useRadRecombGrid =
      cfg.entry<bool>("radRecombCache", "Use cache for radiative recombination cross section", true);

    Ion::useExciteGrid =
      cfg.entry<bool>("exciteCache", "Use cache for collisional excitation cross section", true);

    Ion::useIonizeGrid =
      cfg.entry<bool>("ionizeCache", "Use cache for collisional ionization cross section", true);


    // Enter cross-section scale factors into PT if specified
    //
    boost::optional<ptree&> vt =
      cfg.property_tree().get_child_optional("CrossSectionScale");

    if (vt) {			// Parse stanza ONLY IF it exists

      for (auto & v : vt.get()) {

	if (PT[v.first]) {
	  PT[v.first]->set(vt->get<double>(v.first));
	} else {
	  if (myid==0) {
	    std::cout << "Element <" << v.first << "> is not in my "
		      << "periodic table.  Continuing WITHOUT "
		      << "setting <" << v.first << "> = "
		      << vt->get<double>(v.first) << std::endl;
	  }
	}
      }
    }

    if (cfg.property_tree().find("MFP") !=
	cfg.property_tree().not_found())
      {
	std::string name = cfg.property_tree().get<std::string>("MFP.value");
	try {
	  mfptype = MFP_s.right.at(name);
	}
	catch ( std::out_of_range & e ) {
	  if (myid==0) {
	    std::cout << "Caught standard error: " << e.what() << std::endl
		      << "CollideIon::processConfig: "
		      << "MFP algorithm <" << name << "> is not implemented"
		      << ", using <Direct> instead" << std::endl;
	  }
	  mfptype = MFP_t::Direct;
	  cfg.property_tree().put("MFP.value", "Direct");
	}
      }
    else {
      if (myid==0) {
	    std::cout << "CollideIon::processConfig: "
		      << "MFP algorithm not specified"
		      << ", using <Direct> instead" << std::endl;
      }
      cfg.property_tree().put("MFP.desc", "Name of MFP estimation algorithm for time step selection");
      cfg.property_tree().put("MFP.value", "Direct");
    }

    // Update atomic weight databases IF ElctronMass is specified
    // using direct call to underlying boost::property_tree
    //
    if (cfg.property_tree().find("ElectronMass") !=
	cfg.property_tree().not_found())
      {
	double mass = cfg.property_tree().get<double>("ElectronMass.value");
	TreeDSMC::atomic_weights[0] = atomic_weights[0] = mass;
      }

    if (myid==0) {
      // cfg.display();
      cfg.save(runtag +".CollideIon.config", "JSON");
    }
  }
  catch (const boost::property_tree::ptree_error &e) {
    if (myid==0)
      std::cerr << "Error parsing CollideIon config info" << std::endl
		<< e.what() << std::endl;
    MPI_Abort(MPI_COMM_WORLD, 54);
  }
  catch (...) {
    if (myid==0)
      std::cerr << "Error parsing CollideIon config info" << std::endl
		<< "unknown error" << std::endl;
    MPI_Abort(MPI_COMM_WORLD, 55);
  }
}

void CollideIon::spectrumSetup()
{
  if (not use_spectrum) return;

  if (aType != Hybrid) {
    if (myid==0) {
      std::cout << std::endl
		<< "********************************************" << std::endl
		<< "*** Warning: spectrum requested but only ***" << std::endl
		<< "*** implemented for HYBRID method        ***" << std::endl
		<< "********************************************" << std::endl;
    }
    use_spectrum = false;
    return;
  }

  for (auto v : {free_free, colexcite, ionize, recomb}) spectTypes.insert(v);

  // Use log scale?
  logSpect = false;

  // Cache for range check
  flrSpect = minSpect;

  // Use log scaling or wavelength scaling?
  if (wvlSpect) {
    minSpect = std::max<double>(10.0, minSpect);
  } else if (minSpect>0.0) {
    logSpect = true;
    minSpect = log(minSpect);
    maxSpect = log(maxSpect);
  }

  // Deduce number of bins
  numSpect = std::floor( (maxSpect - minSpect)/delSpect );

  // Must have at least one bin
  numSpect = max<int>(numSpect, 1);

  // Reset outer bin edge value
  maxSpect = minSpect + delSpect * numSpect;

  // Initialize spectral histogram
  dSpect.resize(nthrds);
  eSpect.resize(nthrds);
  nSpect.resize(nthrds);
  for (auto i : spectTypes) {
    for (auto & v : dSpect) v[i].resize(numSpect, 0);
    for (auto & v : eSpect) v[i].resize(numSpect, 0);
    for (auto & v : nSpect) v[i].resize(numSpect, 0);
  }
}

void CollideIon::spectrumAdd(int id, int type, double energy, double weight)
{
  if (not use_spectrum) return;
  
  double E = energy;

  if (wvlSpect) {
    energy = eVtoAng/energy;
  } else if (logSpect) {
    if (energy < flrSpect) return;
    energy = log(energy);
  }

  // Sanity check
  if (spectTypes.find(type) == spectTypes.end()) {
    std::cout << "spectrumAdd: requested illegal type: " << type << std::endl;
    return;
  }

  if (energy >= minSpect and energy < maxSpect) {
    int indx = (energy - minSpect)/delSpect;
    indx = std::max<double>(indx, 0);
    indx = std::min<double>(indx, numSpect-1);
    dSpect[id][type][indx] += weight;
    eSpect[id][type][indx] += weight*E;
    nSpect[id][type][indx] ++;
  }
}

void CollideIon::spectrumGather()
{
  if (not use_spectrum) return;

  // Sum up threads
  for (int n=1; n<nthrds; n++) {
    for (auto i : spectTypes) {
      for (int j=0; j<numSpect; j++) dSpect[0][i][j] += dSpect[n][i][j];
      for (int j=0; j<numSpect; j++) eSpect[0][i][j] += eSpect[n][i][j];
      for (int j=0; j<numSpect; j++) nSpect[0][i][j] += nSpect[n][i][j];
    }
  }

  for (auto i : spectTypes) {
    // Collect number data
    if (myid==0) tSpect[i].resize(numSpect);
    MPI_Reduce(&dSpect[0][i][0], &tSpect[i][0], numSpect, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

    // Collect flux data
    if (myid==0) fSpect[i].resize(numSpect);
    MPI_Reduce(&eSpect[0][i][0], &fSpect[i][0], numSpect, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

    // Collect count data
    if (myid==0) mSpect[i].resize(numSpect);
    MPI_Reduce(&nSpect[0][i][0], &mSpect[i][0], numSpect, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);
  }

  // Zero all data
  for (auto i : spectTypes) {
    for (int n=0; n<nthrds; n++) {
      for (auto & v : dSpect[n][i]) v = 0;
      for (auto & v : eSpect[n][i]) v = 0;
      for (auto & v : nSpect[n][i]) v = 0;
    }
  }
}


void CollideIon::spectrumPrint()
{
  if (not use_spectrum or myid>0) return;

  double normN = 0.0, normF = 0.0;
  for (auto i : spectTypes) {
    for (auto v : tSpect[i]) normN += v;
    for (auto v : fSpect[i]) normF += v;
  }
  
  ostringstream ostr;
  ostr << outdir << runtag << ".spectrum." << this_step;
  std::ofstream out(ostr.str());
  out << "# T=" << tnow << "  m/M=" << mstep << "/" << Mstep << std::endl
      << "# Norm(number)=" << normN << std::endl
      << "# Norm(flux)="   << normF
      << std::endl     << "#" << std::endl
      << std::right    << "#"
      << std::setw( 8) << "bin";
  if (wvlSpect)
    out << std::setw(18) << "Min lambda"
	<< std::setw(18) << "Max lambda";
  else {
    out << std::setw(18) << "Min eV"
	<< std::setw(18) << "Max eV";
  }
  for (auto i : spectTypes) {
    std::ostringstream nout; nout << "N(" << labels[i] << ")";
    std::ostringstream fout; fout << "F(" << labels[i] << ")";
    std::ostringstream sout; sout << "C(" << labels[i] << ")";
    out << std::setw(18) << nout.str()
	<< std::setw(18) << fout.str()
	<< std::setw(18) << sout.str();
  }
  out << std::setw(18) << "N(total)"
      << std::setw(18) << "F(total)"
      << std::setw(18) << "C(total)"
      << std::endl     << "#"
      << std::setw( 8) << "-------"
      << std::setw(18) << "-------"
      << std::setw(18) << "-------";
  for (size_t i=0; i<spectTypes.size(); i++)
    out << std::setw(18) << "-------"
	<< std::setw(18) << "-------"
	<< std::setw(18) << "-------";
  out << std::setw(18) << "-------"
      << std::setw(18) << "-------"
      << std::setw(18) << "-------" << std::endl;
  
  for (int j=0; j<numSpect; j++) {
    double inner = minSpect + delSpect*j;
    double outer = minSpect + delSpect*(j+1);
    if (logSpect) {
      inner = exp(inner);
      outer = exp(outer);
    }

    out << std::right
	<< std::setw( 9) << j + 1
	<< std::setw(18) << inner
	<< std::setw(18) << outer;

    // Print the flux and counts
    //
    double  sumN = 0.0;
    double  sumF = 0.0;
    unsigned num = 0;
    for (auto i : spectTypes) {
      out << std::setw(18) << tSpect[i][j]/normN
	  << std::setw(18) << fSpect[i][j]/normF
	  << std::setw(18) << mSpect[i][j];
      sumN += tSpect[i][j]/normN; // Accumulate the flux
      sumF += fSpect[i][j]/normF; // Accumulate the flux
      num  += mSpect[i][j];	  // Accumulate the counts
    }
    out << std::setw(18) << sumN
	<< std::setw(18) << sumF
	<< std::setw(18) << num
	<< std::endl;
  }
}

void CollideIon::post_cell_loop(int id)
{
  if (aType == Hybrid and NOCOOL and KE_DEBUG) {
    unsigned good = 0;
    unsigned bad  = 0;
    double   dEgd = 0.0;
    double   dEbd = 0.0;

    for (auto & v : cellEg)  { good += v; v = 0; }
    for (auto & v : cellEb)  { bad  += v; v = 0; }
    for (auto & v : dEratg)  { dEgd += v; v = 0; }
    for (auto & v : dEratb)  { dEbd += v; v = 0; }

    unsigned nC = 0;
    unsigned nP = 0;
    double   KE = 0.0;
    for (auto c : cellist[id]) {
      nC++;
      for (auto b : c->bods) {
	Particle *p = tree->Body(b);
	nP++;
	unsigned short Z = KeyConvert(p->iattrib[use_key]).getKey().first;
	for (size_t j=0; j<3; j++) {
	  KE += 0.5 * p->mass * p->vel[j] * p->vel[j];
	  if (use_elec>=0) {
	    KE += 0.5 * p->mass * atomic_weights[0]/atomic_weights[Z] *
	      p->dattrib[use_elec+j] * p->dattrib[use_elec+j];
	  }
	}
      }
    }

    MPI_Barrier(MPI_COMM_WORLD);

    unsigned u;
    double   d;
    MPI_Reduce(&(u=good), &good, 1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&(u=bad),  &bad,  1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&(u=nC),   &nC,   1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&(u=nP),   &nP,   1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&(d=dEgd), &dEgd, 1, MPI_DOUBLE,   MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&(d=dEbd), &dEbd, 1, MPI_DOUBLE,   MPI_SUM, 0, MPI_COMM_WORLD);

    double z;
    MPI_Reduce(&(z=KE), &KE, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

    if (myid==0 and mlev==0 and nP>0) {
      if (bad) {
	std::cout << std::endl << "KE cell conservation check, T="
		  << std::left << std::setw(10) << tnow
		  << " good="  << std::setw(8)  << good
		  << " bad="   << std::setw(8)  << bad
		  << " KE="    << std::setprecision(10) << KE
		  << " dE(g)=" << std::setprecision(10) << (good ? dEgd/good : 0.0)
		  << " dE(b)=" << std::setprecision(10) << dEbd/bad
		  << std::endl;
      }
    }
  } // END: Hybrid and NOCOOL
}


CollideIon::Pord::Pord(CollideIon* c, Particle *P1, Particle *P2,
		       double WW1, double WW2, pType p, double T) :
  caller(c), p1(P1), p2(P2), w1(WW1), w2(WW2), P(p),
  thresh(T), swap(false), wght(false), E1({0, 0}), E2({0, 0})
{
  // Assign weights (proportional to number of true ion/atom particles)
  //
  W1 = w1;
  W2 = w2;

  if (c->aType == Trace) {

    k1 = k2 = speciesKey(0, 0);
    Z1 = Z2 = 0;

    // Get kinetic energies
    //
    for (size_t k=0; k<3; k++) {
      KE1[0] += p1->vel[k] * p1->vel[k];
      KE2[0] += p2->vel[k] * p2->vel[k];
      KE1[1] += p1->dattrib[c->use_elec+k] * p1->dattrib[c->use_elec+k];
      KE2[1] += p2->dattrib[c->use_elec+k] * p2->dattrib[c->use_elec+k];
    }

    // Get molecular weights and electron fractions
    //
    double mol1 = 0.0, mol2 = 0.0;
    eta1 = eta2 = 0.0;
    for (auto s : c->SpList) {
				// Molecular weight
      mol1 += p1->dattrib[s.second]/atomic_weights[s.first.first];
      mol2 += p2->dattrib[s.second]/atomic_weights[s.first.first];
				// Electron fraction
      unsigned P =  s.first.second - 1;
      eta1 += p1->dattrib[s.second]*P/atomic_weights[s.first.first];
      eta2 += p2->dattrib[s.second]*P/atomic_weights[s.first.first];

    }
    eta1 /= mol1;		// Electrons per particle
    eta2 /= mol2;

    m1 = m10 = 1.0/mol1;	// Molecular weight
    m2 = m20 = 1.0/mol2;
    
    // Cache ionization fractions
    //
    size_t sz = c->SpList.size();
    f1.resize(sz);
    f2.resize(sz);
    
    auto sp = c->SpList.begin();
    for (size_t n=0; n<sz; n++) {
      f1[n] = p1->dattrib[sp->second];
      f2[n] = p2->dattrib[sp->second];
      sp++;
    }

    // Get kinetic energies
    //
    for (size_t k=0; k<3; k++) {
      KE1[0] += p1->vel[k] * p1->vel[k];
      KE2[0] += p2->vel[k] * p2->vel[k];
      KE1[1] += p1->dattrib[c->use_elec+k] * p1->dattrib[c->use_elec+k];
      KE2[1] += p2->dattrib[c->use_elec+k] * p2->dattrib[c->use_elec+k];
    }

    KE1[0] *= 0.5*p1->mass;
    KE2[0] *= 0.5*p2->mass;
    KE1[1] *= 0.5*p1->mass * eta1 * atomic_weights[0]/m1;
    KE2[1] *= 0.5*p2->mass * eta2 * atomic_weights[0]/m2;

  } // END: Trace method
  else {

    // Cache species keys
    //
    k1 = KeyConvert(p1->iattrib[key]).getKey();
    k2 = KeyConvert(p2->iattrib[key]).getKey();
  
    // Get atomic numbers
    //
    Z1 = k1.first;
    Z2 = k2.first;
  
    // Get atomic masses
    //
    m1 = m10 = atomic_weights[Z1];
    m2 = m20 = atomic_weights[Z2];
    
    // Compute electron fractions
    //
    eta1 = eta2 = 0.0;
    if (caller->use_elec>=0) {
      for (unsigned short C=1; C<=Z1; C++)
	eta1 += p1->dattrib[caller->spc_pos+C]*C;
      for (unsigned short C=1; C<=Z2; C++)
	eta2 += p2->dattrib[caller->spc_pos+C]*C;
    }
    
    // Cache ionization fractions
    //
    f1.resize(Z1+1);
    f2.resize(Z2+1);
    
    for (unsigned short C=0; C<=Z1; C++)
      f1[C] = p1->dattrib[caller->spc_pos+C];

    for (unsigned short C=0; C<=Z2; C++)
      f2[C] = p2->dattrib[caller->spc_pos+C];
  }

  switch (P) {
  case ion_ion:
    if (w2/w1 < thresh) {
      wght = true;
    }
    break;
  case ion_electron:
    m2 = atomic_weights[0];
    if (w2*eta2/w1 < thresh) {
      W2 *= eta2;
      wght = true;
    }
    break;
  case electron_ion:
    m1  = atomic_weights[0];
    if (w1*eta1/w2 < thresh) {
      W1 *= eta1;
      wght = true;
    }
    break;
  }
  
  // Swap needed?
  //
  if (W1 < W2) swapPs();

  // Trace ratio (in possibly swapped state)
  //
  q = W2/W1;

  // Compute initial KE
  //
  eInitial();

}

void CollideIon::Pord::swapPs()
{
  // Swap the particle pointers
  //
  zswap(p1, p2);
    
  // Swap electron number fraction
  //
  zswap(eta1, eta2);
    
  // Swap true particle numbers
  //
  zswap(w1, w2);
  zswap(W1, W2);
    
  // Swap ionization fractions
  //
  zswap(f1, f2);
  
  // Swap particle masses
  //
  zswap(m1,  m2 );
  zswap(m10, m20);
    
  // Swap species and masses
  //
  zswap(k1, k2);
  zswap(Z1, Z2);
  
  // Toggle swap flag
  //
  swap = not swap;
}

void CollideIon::Pord::update()
{
  if (caller->use_elec<0) return;

  double sum1 = 1.0, sum2 = 1.0;
  if (norm_enforce) {
    sum1 = sum2 = 0.0;
    for (auto v : f1) sum1 += v;
    for (auto v : f2) sum2 += v;
    
    // Sanity check
    //
    if (sum1 > 0.0) {
      if (fabs(sum1-1.0) > 1.0e-6) {
	std::cout << "**ERROR [" << myid << "] Pord:"
		  << " Unexpected f1 sum=" << sum1
		  << ", T=" << tnow << ", ";
	if (caller->aType!= Trace) std::cout << "Z=" << Z1 << ", ";
	for (auto v : f1) std::cout << std::setw(18) << v;
	std::cout << std::endl;
      }
    }

    // Sanity check
    //
    if (sum2 > 0.0) {
      if (fabs(sum2-1.0) > 1.0e-6) {
	std::cout << "**ERROR [" << myid << "] Pord:"
		  << " Unexpected f2 sum=" << sum2
		  << ", T=" << tnow << ", ";
	if (caller->aType != Trace) std::cout << "Z=" << Z2 << ", ";
	for (auto v : f2 ) std::cout << std::setw(18) << v;
	std::cout << std::endl;
      }
    }
  }

  if (caller->aType == Trace) {
    // Normalization check
    //
    double tot1 = 0.0, tot2 = 0.0;
    size_t C = 0;
    for (auto s : caller->SpList) {
      p1->dattrib[s.second] = f1[C]/sum1;
      p2->dattrib[s.second] = f2[C]/sum2;
      tot1 += p1->dattrib[s.second];
      tot2 += p2->dattrib[s.second];
      C++;
    }

    // Sanity check
    //
    if (tot1 > 0.0) {
      if (fabs(tot1-1.0) > 1.0e-6) {
	std::cout << "**ERROR [" << myid << "] Pord:"
		  << " Unexpected p1 norm in update=" << tot1
		  << ", T=" << tnow
		  << ", index=" << p1->indx << ", ";
	for (auto s : caller->SpList)
	  std::cout << std::setw(18) << p1->dattrib[s.second];
	std::cout << std::endl;
      }
    }
    
    // Sanity check
    //
    if (tot2 > 0.0) {
      if (fabs(tot2-1.0) > 1.0e-6) {
	std::cout << "**ERROR [" << myid << "] Pord:"
		  << " Unexpected p1 norm in update=" << tot2
		  << ", T=" << tnow
		  << ", index=" << p2->indx << ", ";
	for (auto s : caller->SpList)
	  std::cout << std::setw(18) << p2->dattrib[s.second];
	std::cout << std::endl;
      }
    }

  } else {

    double tot1 = 0.0, tot2 = 0.0;
    for (unsigned short C=0; C<=Z1; C++) {
      p1->dattrib[caller->spc_pos+C] = f1[C]/sum1;
      tot1 += p1->dattrib[caller->spc_pos+C];
    }

    for (unsigned short C=0; C<=Z2; C++) {
      p2->dattrib[caller->spc_pos+C] = f2[C]/sum2;
      tot2 += p2->dattrib[caller->spc_pos+C];
    }

    if (tot1 > 0.0) {
      if (fabs(tot1-1.0) > 1.0e-6) {
	std::cout << "**ERROR [" << myid << "] Pord:"
		  << " Unexpected p1 norm in update=" << tot1
		  << ", T=" << tnow
		  << ", index=" << p1->indx
		  << ", Z=" << Z1 << ", ";
	for (unsigned short C=0; C<=Z1; C++)
	  std::cout << std::setw(18) << p1->dattrib[caller->spc_pos+C];
	std::cout << std::endl;
      }
    }
    
    if (tot2 > 0.0) {
      if (fabs(tot2-1.0) > 1.0e-6) {
	std::cout << "**ERROR [" << myid << "] Pord:"
		  << " Unexpected p2 norm in update=" << tot2
		  << ", T=" << tnow
		  << ", index=" << p2->indx
		  << ", Z=" << Z2 << ", ";
	for (unsigned short C=0; C<=Z2; C++)
	  std::cout << std::setw(18) << p2->dattrib[caller->spc_pos+C];
	std::cout << std::endl;
      }
    }
  }

}

void CollideIon::Pord::scheme(bool W)
{
  if (wght == W) return;	// Okay as is

  W1 = w1;			// For unweighted (e.g. was weighted
  W2 = w2;			// previously)

  if (W) {			// Was unweighted (switch to weighted)
    switch (P) {
    case ion_ion:
      break;
    case ion_electron:
      if (swap)	W1 *= eta1;
      else	W2 *= eta2;
      break;
    case electron_ion:
      if (swap) W2 *= eta2;
      else      W1 *= eta1;
      break;
    }
  }

  wght = W;
}

// Recompute electron fraction and energies from current state
//
CollideIon::Pord::Epair CollideIon::Pord::compE()
{
  Epair ret;			// zero initialized structure

				// Compute electron fractions
  if (caller->aType == Trace) {
    double mol1 = 0.0, mol2 = 0.0;
    eta1 = eta2 = 0.0;
    for (auto s : caller->SpList) {
				// Molecular weight
      mol1 += p1->dattrib[s.second]/atomic_weights[s.first.first];
      mol2 += p2->dattrib[s.second]/atomic_weights[s.first.first];
				// Electron fraction
      unsigned P = s.first.second - 1;
      eta1 += p1->dattrib[s.second]*P/atomic_weights[s.first.first];
      eta2 += p2->dattrib[s.second]*P/atomic_weights[s.first.first];
    }

    eta1 /= mol1;
    eta2 /= mol2;

  } // END: Trace method
  else {

    eta1 = 0.0;
    for (unsigned short C=1; C<=Z1; C++)
      eta1 += p1->dattrib[caller->spc_pos+C]*C;

    eta2 = 0.0;
    for (unsigned short C=1; C<=Z2; C++)
      eta2 += p2->dattrib[caller->spc_pos+C]*C;
  }


  // Using weighted interaction?
  //
  if (wght) {
    W1 = w1;			// Reset to unweighted
    W2 = w2;
				// Now update weights if necessary
    switch (P) {
    case ion_ion:
      break;
    case ion_electron:
      if (swap)	W1 *= eta1;
      else	W2 *= eta2;
      break;
    case electron_ion:
      if (swap) W2 *= eta2;
      else      W1 *= eta1;
      break;
    }

    // Swap needed?
    //
    if (W1 < W2) {
      swapPs();
    }
  }
				// Compute KE
  for (size_t k=0; k<3; k++) {
    ret[0].KEi += p1->vel[k] * p1->vel[k];
    ret[1].KEi += p2->vel[k] * p2->vel[k];
    if (caller->use_elec>=0) {
      ret[0].KEe += p1->dattrib[caller->use_elec+k] * p1->dattrib[caller->use_elec+k];
      ret[1].KEe += p2->dattrib[caller->use_elec+k] * p2->dattrib[caller->use_elec+k];
    }
  }
 
  if (false and DBG_NewTest)
    std::cout << std::setprecision(14) << "Pord: "
	      << " v1i2=" << std::setw(22) << ret[0].KEi
	      << " v1e2=" << std::setw(22) << ret[0].KEe
	      << " v2i2=" << std::setw(22) << ret[1].KEi
	      << " v2e2=" << std::setw(22) << ret[1].KEe
	      << std::endl;

  // Particle 1
  //
  ret[0].KEi *= 0.5 * w1 * m10;
  ret[0].KEe *= 0.5 * w1 * atomic_weights[0];

  ret[0].KEw = ret[0].KEe * eta1;
  ret[0].ke0 = ret[0].KEi + ret[0].KEe;
  ret[0].ke1 = ret[0].KEi + ret[0].KEw;
  ret[0].eta = eta1;

  // Particle 2
  //
  ret[1].KEi *= 0.5 * w2 * m20;
  ret[1].KEe *= 0.5 * w2 * atomic_weights[0];

  ret[1].KEw = ret[1].KEe * eta2;
  ret[1].ke0 = ret[1].KEi + ret[1].KEe;
  ret[1].ke1 = ret[1].KEi + ret[1].KEw;
  ret[1].eta = eta2;

  if (DBG_NewTest)
    std::cout << std::setprecision(14)
	      << " KE1i=" << std::setw(22) << ret[0].KEi
	      << " KE1e=" << std::setw(22) << ret[0].KEw
	      << " KE2i=" << std::setw(22) << ret[1].KEi
	      << " KE2e=" << std::setw(22) << ret[1].KEw
	      << " Eta1=" << std::setw(22) << eta1
	      << " Eta2=" << std::setw(22) << eta2 << std::endl;
  
  return ret;
}


void CollideIon::Pord::normTest(unsigned short n, const std::string& lab)
{
  if (swap) n++;

  std::vector<double> *f;
  unsigned short Z;
  Particle *p;

  if (n%2==1) {
    Z = Z1;
    f = &f1;
    p = p1;
  } else {
    Z = Z2;
    f = &f2;
    p = p2;
  }

  double tot = 0.0;
  bool posdef = true;

  for (auto v : *f) {
    tot += v;
    if (v < 0.0) posdef = false;
  }

  if (!posdef) {
    std::cout << "**ERROR [" << myid << "] Pord:"
	      << " Values not posdef, norm" << tot
	      << " for " << lab
	      << ", T=" << tnow
	      << ", index=" << p->indx << ", ";
    if (caller->aType != Trace) std::cout << "Z=" << Z << ", ";
    for (auto v : *f)
      std::cout << std::setw(18) << std::setprecision(8) << v;
    std::cout << std::endl;
  }

  if (tot > 0.0) {
    if (fabs(tot-1.0) > 1.0e-6) {
      std::cout << "**ERROR [" << myid << "] Pord:"
		<< " Unexpected norm=" << tot
		<< " for " << lab
		<< ", T=" << tnow
		<< ", index=" << p->indx << ", ";
      if (caller->aType != Trace) std::cout << ", Z=" << Z;
      if (DEBUG_CNT>=0) std::cout << ", Count=" << p->iattrib[DEBUG_CNT];
      std::cout << ", ";
      for (auto v : *f)
	std::cout << std::setw(18) << std::setprecision(8) << v;
      std::cout << std::endl;
    }
    for (auto & v : *f) v /= tot;
  } else {
    std::cout << "**ERROR [" << myid << "] Pord:"
	      << " Invalid zero norm for " << lab
	      << ", T=" << tnow
	      << ", index=" << p->indx << ", ";
    if (caller->aType != Trace) std::cout << "Z=" << Z;
    if (DEBUG_CNT>=0) std::cout << ", Count=" << p->iattrib[DEBUG_CNT];
    std::cout << std::endl;
  }

  tot = 0.0;
  if (caller->aType == Trace) {
    for (auto s : caller->SpList) tot += p1->dattrib[s.second];
  } else {
    for (unsigned short C=0; C<=Z1; C++) tot += p1->dattrib[caller->spc_pos+C];
  }
  
  if (tot > 0.0) {
    if (fabs(tot-1.0) > 1.0e-6) {
      std::cout << "**ERROR [" << myid << "] Pord:"
		<< " Unexpected p1 norm=" << tot
		<< " for " << lab
		<< ", T=" << tnow
		<< ", index=" << p1->indx << ", ";
      if (caller->aType != Trace) {
	std::cout << "Z=" << Z1 << ", ";
	for (auto s : caller->SpList) 
	  std::cout << std::setw(18) << std::setprecision(8)
		    << p1->dattrib[s.second];
      } else {
	for (unsigned short C=0; C<=Z1; C++)
	  std::cout << std::setw(18) << std::setprecision(8)
		    << p1->dattrib[caller->spc_pos+C];
      }
      std::cout << std::endl;
    }
  }

  tot = 0.0;
  if (caller->aType == Trace) {
    for (auto s : caller->SpList) tot += p2->dattrib[s.second];
  } else {
    for (unsigned short C=0; C<=Z2; C++) tot += p2->dattrib[caller->spc_pos+C];
  }
  
  if (tot > 0.0) {
    if (fabs(tot-1.0) > 1.0e-6) {
      std::cout << "**ERROR [" << myid << "] Pord:"
		<< " Unexpected p2 norm=" << tot
		<< " for " << lab
		<< ", T=" << tnow
		<< ", index=" << p2->indx << ", ";
      if (caller->aType != Trace) {
	std::cout << "Z=" << Z2 << ", ";
	for (auto s : caller->SpList) 
	  std::cout << std::setw(18) << std::setprecision(8)
		    << p2->dattrib[s.second];
      } else {
	for (unsigned short C=0; C<=Z2; C++)
	  std::cout << std::setw(18) << std::setprecision(8)
		    << p2->dattrib[caller->spc_pos+C];
      }
      std::cout << std::endl;
    }
  }

}


// Return 3d Colombic scattering vector
//
std::vector<double>& CollideIon::coulomb_vector(std::vector<double>& rel,
						double W1, double W2, double Tau)
{
  if (not coulombSel.get()) coulombSel = boost::make_shared<coulombSelect>();

				// Normalize
  double rel2 = 0.0;
  for (auto v : rel) rel2 += v*v;
  double vfac = sqrt(rel2);
  if (vfac>0.0) for (auto & v : rel) v /= vfac;

  double fac    = std::max<double>(W1, W2)/std::min<double>(W1, W2);
  double tau    = fac * Tau;
  double cosx   = (*coulombSel)(tau, (*unit)());
  double sinx   = sqrt(fabs(1.0 - cosx*cosx));
  double phi    = 2.0*M_PI*(*unit)();
  double cosp   = cos(phi);
  double sinp   = sin(phi);
  double g_perp = sqrt(rel[1]*rel[1] + rel[2]*rel[2]);

				// Compute randomly-oriented
				// perpendicular vector
  std::vector<double> h(3);
  if (g_perp>0.0) {
    h[0] = g_perp * cosp;
    h[1] = -(rel[1]*rel[0]*cosp + rel[2]*sinp)/g_perp;
    h[2] = -(rel[2]*rel[0]*cosp - rel[1]*sinp)/g_perp;
  } else {
    h[0] = 0.0;
    h[1] = cosp;
    h[2] = sinp;
  }
  
  if (false) {
    double test = 0.0;
    for (auto v : h) test += v*v;

    if (fabs(test - 1.0) > 1.0e-6) {
      std::cout << "Norm error [1]: " << test << " expected 1" << std::endl;
    }
  }

  for (int i=0; i<3; i++) rel[i] = rel[i]*cosx - h[i]*sinx;

  if (false) {
    double test = 0.0;
    for (auto v : rel) test += v*v;

    if (fabs(test - 1.0) > 1.0e-6) {
      std::cout << "Norm error [2]: " << test << " expected 1" << std::endl;
    }
  }

  return rel;
}
