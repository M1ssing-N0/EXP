/*
  Compute initial conditions for a uniform density gas
*/

#include <iostream>
#include <iomanip>

#include <boost/random.hpp>
#include <boost/random/uniform_real.hpp>
#include <boost/random/normal_distribution.hpp>
#include <boost/shared_ptr.hpp>

#include <boost/program_options.hpp>

namespace po = boost::program_options;

#include "Particle.H"
#include "globalInit.H"

//
// Boost random types
//
typedef boost::shared_ptr<boost::mt19937> gen_ptr;
typedef boost::shared_ptr<boost::uniform_real<> > uniform_ptr;
typedef boost::shared_ptr<boost::normal_distribution<> > normal_ptr;
typedef boost::variate_generator<boost::mt19937&, boost::uniform_real<> > unif_var;
typedef boost::shared_ptr<unif_var> unit_ptr;
typedef boost::variate_generator<boost::mt19937&, boost::normal_distribution<> > norm_var;
typedef boost::shared_ptr<norm_var> norm_ptr;

//
// Global variable for Particle
//
unsigned multistep = 0;
string runtag, outdir;
char threading_on = 0;
int myid = 0;
pthread_mutex_t mem_lock;

//
// Physical constants
//
double a0    = 5.2917721092e-9;	    // cm (2xBohr radius)
double boltz = 1.3806488e-16;	    // cgs
double mp    = 1.67262178e-24;	    // g
double amu   = 1.660011e-24;	    // atomic mass unit
double pc    = 3.08567758e18;	    // Parsec (in cm)
double msun  = 1.9891e33;	    // Solar mass (in g)
double year  = 365.242*24.0*3600.0; // seconds per year

double atomic_masses[2] = {1.00794, 4.002602};

double Lunit;
double Tunit;
double Vunit;
double Munit;

//
// boost RNGs
//
gen_ptr     gen;
uniform_ptr uniform;
normal_ptr  normal;
unit_ptr    Unit;
norm_ptr    Norm;

/**
   Make Uniform temperature box of gas
*/
void InitializeUniform(std::vector<Particle>& p, double mass,
                       double T, vector<double> &L, int nd=6)
{
  unsigned npart = p.size();
  double   rho   = mass/(L[0]*L[1]*L[2]);

  std::cout << std::string(60, '-')                 << std::endl;
  std::cout << "Temperature: "  << T     << " K "   << std::endl;
  std::cout << "Number:      "  << npart            << std::endl;
  std::cout << "Length unit: "  << Lunit << " cm"   << std::endl;
  std::cout << "Time unit:   "  << Tunit << " s"    << std::endl;
  std::cout << "Vel unit:    "  << Vunit << " cm/s" << std::endl;
  std::cout << "Mass unit:   "  << Munit << " g"    << std::endl;
  std::cout << std::string(60, '-')                 << std::endl;

  double varH  = sqrt((boltz*T)/(atomic_masses[0]*amu));
  double varHe = sqrt((boltz*T)/(atomic_masses[1]*amu));

  for (unsigned i=0; i<npart; i++) {
    for (unsigned k=0; k<3; k++) {
      p[i].pos[k] = L[k]*(*Unit)();
      if (p[i].Z == 1) {
	p[i].vel[k] = varH*(*Norm)();
      }
      if (p[i].Z == 2) p[i].vel[k] = varHe*(*Norm)();
      p[i].vel[k] /= Vunit;
    }
    p[i].dattrib.push_back(T);
    p[i].dattrib.push_back(rho);

    for (int n=0; n<nd-2; n++) p[i].dattrib.push_back(0.0);
  }
}

void writeParticles(std::vector<Particle>& particles, const string& file)
{
  // For tabulating mass fractions . . . 
  typedef std::map<unsigned short, double> Frac;
  Frac frac;

  std::ofstream out(file.c_str());

  out.precision(10);

  out << setw(15) << particles.size()
      << setw(10) << particles[0].iattrib.size()
      << setw(10) << particles[0].dattrib.size()
      << std::endl;

  for (int n=0; n<particles.size(); n++) {

    out << setw(18) << particles[n].mass;

    for (int k=0; k<3; k++) 
      out << setw(18) << particles[n].pos[k];

    for (int k=0; k<3; k++) 
      out << setw(18) << particles[n].vel[k];

    out << setw(18) << static_cast<unsigned>(particles[n].Z);
    out << setw(18) << static_cast<unsigned>(particles[n].C);

    Frac::iterator it = frac.find(particles[n].Z);
    if (it==frac.end()) frac[particles[n].Z] = 0.0;
    frac[particles[n].Z] += particles[n].mass;

    for (unsigned k=0; k<particles[n].iattrib.size(); k++)
      out << setw(12) << particles[n].iattrib[k];

    for (unsigned k=0; k<particles[n].dattrib.size(); k++)
      out << setw(18) << particles[n].dattrib[k];

    out << std::endl;
  }

  double Mtot = 0.0;
  for (Frac::iterator it=frac.begin(); it!=frac.end(); it++) 
    Mtot += it->second;

  std::cout << std::setw( 3) << "Z"
	    << std::setw(18) << "Mass"
	    << std::setw(18) << "Fraction"
	    << std::endl
	    << std::setw( 3) << "-"
	    << std::setw(18) << "--------"
	    << std::setw(18) << "--------"
	    << std::endl;
  for (Frac::iterator it=frac.begin(); it!=frac.end(); it++) 
    std::cout << std::setw( 3) << it->first
	      << std::setw(18) << it->second
	      << std::setw(18) << it->second/Mtot
	      << std::endl;
}

void InitializeSpecies(std::vector<Particle> & particles, 
		       std::vector<unsigned char>& sZ, 
		       std::vector<double>& sF, double M, double T)

{
  std::vector< std::vector<double> > frac, cuml;

  //
  // Generate the ionization-fraction input file
  //
  for (std::vector<unsigned char>::iterator 
	 n=sZ.begin(); n!=sZ.end(); n++) {

    const std::string ioneq("makeIonIC.ioneq");
    std::ostringstream sout;
    sout << "./genIonization"
	 << " -1 " << static_cast<unsigned>(*n)
	 << " -2 " << static_cast<unsigned>(*n)
	 << " -T " << T << " -o " << ioneq;

    int ret = system(sout.str().c_str());

    if (ret) {
      std::cout << "System command  = " << sout.str() << std::endl;
      std::cout << "System ret code = " << ret << std::endl;
    }

    typedef std::vector<std::string> vString;

    std::string inLine;
    std::ifstream sFile(ioneq.c_str());
    if (sFile.is_open()) {

      std::getline(sFile, inLine); // Read and discard the headers
      std::getline(sFile, inLine);
      
      {
	vString s;
	std::getline(sFile, inLine);
	
	std::istringstream iss(inLine);
	std::copy(std::istream_iterator<std::string>(iss), 
		  std::istream_iterator<std::string>(), 
		  std::back_inserter<vString>(s));
	
	std::vector<double> v;
	for (vString::iterator i=s.begin(); i!=s.end(); i++)
	  v.push_back(::atof(i->c_str()));
	frac.push_back(v);
      }
      
      {
	vString s;
	std::getline(sFile, inLine);
	
	std::istringstream iss(inLine);
	std::copy(std::istream_iterator<std::string>(iss), 
		  std::istream_iterator<std::string>(), 
		  std::back_inserter<vString>(s));
	
	std::vector<double> v;
	for (vString::iterator i=s.begin(); i!=s.end(); i++)
	  v.push_back(::atof(i->c_str()));
	cuml.push_back(v);
      }
    }
  }

  int N = particles.size();

				// Compute cumulative species
				// distribution
  const size_t NZ = sF.size();

				// Normalize species map, just in case
  double norm = 0.0;
  for (size_t i=1; i<NZ; i++) norm += sF[i];

  std::vector<double> frcS(NZ), cumS(NZ);
  for (size_t i=0; i<NZ; i++) {
    frcS[i]= sF[i]/atomic_masses[sZ[i]]/norm;
    cumS[i] = frcS[i] + (i ? cumS[i-1] : 0);
  }

  for (size_t i=0; i<N; i++) {

    double rz = (*Unit)();
    double rc = (*Unit)();

    unsigned char Ci = 1, Zi;
    size_t indx;
				// Get the species
    for (indx=0; indx<NZ; indx++) { 
      if (rz < cumS[indx]) {
	Zi = sZ[indx]; 
	break;
      }
    }
				// Get the ionization state
    for (size_t j=0; j<Zi+1; j++) {
      if (rc < cuml[indx][j]) {
	Ci = j+1;
	break;
      }
    }

    particles[i].Z     = Zi;
    particles[i].C     = Ci;
    particles[i].mass  = M/N * frcS[indx];
  }
  
}

int main (int ac, char **av)
{
  double T, D, L;
  std::string oname;
  unsigned seed;
  int npart;

  po::options_description desc("Allowed options");
  desc.add_options()
    ("help,h",		"produce help message")
    ("temp,T",		po::value<double>(&T)->default_value(25000.0),
     "set the temperature in K")
    ("dens,D",		po::value<double>(&D)->default_value(1.0),
     "density in particles per cc")
    ("length,L",	po::value<double>(&L)->default_value(1.0),
     "length in system units")
    ("number,N",	po::value<int>(&npart)->default_value(250000),
     "set the temperature in K")
    ("seed,s",		po::value<unsigned>(&seed)->default_value(11),
     "random number seed")
    ("Lunit,l",		po::value<double>(&Lunit)->default_value(1.0),
     "length scale of the system in pc")
    ("Tunit,t",		po::value<double>(&Tunit)->default_value(1.0e5),
     "time scale in years")
    ("Munit,m",		po::value<double>(&Munit)->default_value(0.1),
     "mass scale in solar masses")
    ("output,o",	po::value<std::string>(&oname)->default_value("out.bods"),
     "body output file")
    ;


  po::variables_map vm;

  try {
    po::store(po::parse_command_line(ac, av, desc), vm);
    po::notify(vm);    
  } catch (po::error& e) {
    std::cout << "Option error: " << e.what() << std::endl;
    exit(-1);
  }

  if (vm.count("help")) {
    std::cout << desc << std::endl;
    std::cout << "Example: " << std::endl;
    std::cout << "\t" << av[0]
	      << " --temp=25000 --number=250000 --output=out.bod" << std::endl;
    return 1;
  }

  Lunit  *= pc;
  Tunit  *= year;
  Munit  *= msun;
  Vunit   = Lunit/Tunit;

  gen     = gen_ptr    (new boost::mt19937(seed));
  uniform = uniform_ptr(new boost::uniform_real<>(0.0, 1.0));
  normal  = normal_ptr (new boost::normal_distribution<>(0.0, 1.0));
  Unit    = unit_ptr   (new unif_var(*gen, *uniform));
  Norm    = norm_ptr   (new norm_var(*gen, *normal));


  //
  // Define the atomic species statistics
  //
  const size_t Nspec = 2;
  std::vector<double>        sF(Nspec);
  std::vector<unsigned char> sZ(Nspec);

  // Species fraction, atomic number
  sF[0] = 0.76; sZ[0] = 1;
  sF[1] = 0.24; sZ[1] = 2;

  /* Additional species, e.g.
     sF[2] = 0.0; sZ[2] = 3;  // Li
     sF[3] = 0.0; sZ[3] = 6;  // C
     sF[4] = 0.0; sZ[4] = 7;  // N
     sF[5] = 0.0  sZ[5] = 8;  // O
     sF[6] = 0.0; sZ[6] = 12; // Mg
  */


  // Cube axes
  std::vector<double> LL(3, L);

  // Mass in box in m_p
  double Mass = mp*D*(LL[0]*LL[1]*LL[2])*pc*pc*pc/Munit;
  
  vector<Particle> particles(npart);

  // Initialize the Z, C's	
  //
  InitializeSpecies(particles, sZ, sF, Mass, T);
  
  // Initialize the phase space vector
  //
  InitializeUniform(particles, Mass, T, LL);
  
  // Output file
  //
  writeParticles(particles, oname);

  return 0;
}
