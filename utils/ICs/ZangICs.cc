/*
  A tapered Mestel disk IC generator
*/
                                // C++/STL headers
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <random>
#include <string>
#include <vector>
#include <array>

#include <mestel.H>

#include <cxxopts.H>

int 
main(int ac, char **av)
{
  //=====================
  // Begin option parsing
  //=====================

  int          N;		// Number of particles
  double       mu, nu, Ri;	// Taper paramters
  double       Rmin, Rmax;      // Radial range
  double       sigma;		// Velocity dispersion
  std::string  bodyfile;	// Output file
  unsigned     seed;		// Will be inialized by /dev/random if
				// not set on the command line

  cxxopts::Options options(av[0], "Ideal tapered Mestel IC generator");

  options.add_options()
    ("h,help",    "Print this help message")
    ("z,zerovel", "Zero the mean velocity")
    ("N,number",  "Number of particles to generate",
     cxxopts::value<int>(N)->default_value("100000"))
    ("n,nu",      "Inner taper exponent (0 for no taper)",
     cxxopts::value<double>(nu)->default_value("2.0"))
    ("m,mu",      "Outer taper exponent (0 for no taper)",
     cxxopts::value<double>(mu)->default_value("2.0"))
    ("i,Ri",      "Inner radius for taper",
     cxxopts::value<double>(Ri)->default_value("0.1"))
    ("r,Rmin",    "Inner radius for model",
     cxxopts::value<double>(Rmin)->default_value("0.01"))
    ("R,Rmax",    "Outer radius for model",
     cxxopts::value<double>(Rmax)->default_value("10.0"))
    ("S,sigma",   "Radial velocity dispersion",
     cxxopts::value<double>(sigma))
    ("s,seed",    "Random number seed. Default: use /dev/random",
     cxxopts::value<unsigned>(seed))
    ("o,file",    "Output body file",
     cxxopts::value<std::string>(bodyfile)->default_value("cube.bods"))
    ;

  cxxopts::ParseResult vm;

  try {
    vm = options.parse(ac, av);
  } catch (cxxopts::OptionException& e) {
    std::cout << "Option error: " << e.what() << std::endl;
    exit(-1);
  }

  // Print help message and exit
  //
  if (vm.count("help")) {
    std::cout << options.help() << std::endl << std::endl;
    return 1;
  }

  // Set from /dev/random if not specified
  if (vm.count("seed")==0) {
    seed = std::random_device{}();
  }

  // Make sure N>0
  if (N<=0) {
    std::cerr << av[0] << ": you must requiest at least one body"
	      << std::endl;
  }

  // Open the output file
  //
  std::ofstream out(bodyfile);
  if (not out) {
    std::string msg(av[0]);
    msg +=  ": output file <" + bodyfile + "> can not be opened";
    throw std::runtime_error(msg);
  }

  // Create the model
  //
  auto model = std::make_shared<TaperedMestelDisk>(nu, mu, Ri);

  // Create an orbit grid
  //
  SphericalOrbit orb(model);
  
  double Ktol = 0.01;
  double Kmin = Ktol, Kmax = 1.0 - Ktol;

  double Emin = 0.5*Rmin*model->get_dpot(Rmin) + model->get_pot(Emin);
  double Emax = 0.5*Rmax*model->get_dpot(Rmax) + model->get_pot(Rmax);

  // Scan to find the peak df
  //
  const int num = 100;
  double peak = 0.0;
  double dE = (Emax - Emin)/num, dK = (1.0 - 2.0*Ktol)/num;
  for (int i=0; i<=num; i++) {
    double E = Emin + dE*i;
    for (int j=0; j<=num; j++) {
      double K = Kmin + dK*j;
      orb.new_orbit(E, K);
      double F = model->distf(E, orb.get_action(2)) / orb.get_freq(1);
      peak = std::max<double>(peak, F);
    }
  }

  // Header
  //
  out << std::setw(10) << N << std::setw(10) << 0 << std::setw(10) << 0
	<< std::endl << std::setprecision(10);

  std::mt19937 gen(seed);
  std::uniform_real_distribution<> uniform(0.0, 1.0);

  // Save the position and velocity vectors
  std::vector<std::array<double, 3>> pos(N), vel(N);

  int itmax = 10000;
  int over  = 0;

  // Generation loop
  for (int n=0; n<N; n++) {
    
    double E, K, R;
    int j;

    for (j=0; j<itmax; j++) {

      E = Emin + (Emax - Emin)*uniform(gen);
      K = Kmin + (Kmax - Kmin)*uniform(gen);
      R = uniform(gen);

      orb.new_orbit(E, K);
      double F = model->distf(E, orb.get_action(2)) / orb.get_freq(1);
      if (F/peak > R) break;
    }

    if (j==itmax) over++;

    double J   = orb.get_action(2);
    double T   = 2.0*M_PI/orb.get_freq(1)*uniform(gen);
    double r   = orb.get_angle(6, T);
    double w1  = orb.get_angle(1, T);
    double phi = 2.0*M_PI*uniform(gen) + orb.get_angle(7, T);

    double vt  = J/r;
    double vr  = sqrt(fabs(2.0*E - model->get_pot(r)) - J*J/(r*r));

    if (w1 > M_PI) vr *= -1.0;	// Branch of radial motion

    // Convert from polar to Cartesian
    //
    pos[n][0] = r*cos(phi);
    pos[n][1] = r*sin(phi);
    pos[n][2] = 0.0;

    vel[n][0] = vr*cos(phi) - vt*sin(phi);
    vel[n][1] = vr*sin(phi) + vt*cos(phi);
    vel[n][2] = 0.0;
  }

  // Compute the particle mass
  //
  double mass = (model->get_mass(Rmax) - model->get_mass(Rmin))/N;

  std::cout << "** " << over << " particles failed iteration" << std::endl
	    << "** Particle mass=" << mass << std::endl;

  out << std::setw(8) << N << std::setw(8) << 0 << std::setw(8) << 0
      << std::endl;

  for (int n=0; n<N; n++) {
    out << std::setw(18) << mass;
    for (int k=0; k<3; k++) out << std::setw(18) << pos[n][k];
    for (int k=0; k<3; k++) out << std::setw(18) << vel[n][k];
    out << std::endl;
  }

  return 0;
}
