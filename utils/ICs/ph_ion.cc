#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>
#include <cmath>
#include <array>

//
// BOOST stuff
//
#include <boost/program_options.hpp>

namespace po = boost::program_options;

int main(int argc, char**argv)
{
  double n0, tol;
  unsigned T;
  int niter;
  std::string outf;

  po::options_description desc("Allowed options");
  desc.add_options()
    ("help,h", "produce this help message")
    ("density,D", po::value<double>(&n0)->default_value(1.0e-4), 
     "Density in amu/cc. Good for n0<8.5e-2")
    ("temp,T", po::value<double>(&n0)->default_value(25000), 
     "Density in amu/cc. Good for n0<8.5e-2")
    ("tol,e",     po::value<double>(&tol)->default_value(1.0e-10), 
     "error tolerance")
    ("iter,n",    po::value<int>(&niter)->default_value(1000), 
     "maximum number of iterations")
    ("outfile,o", po::value<std::string>(&outf)->default_value("IonRecombFrac.data"),
     "data file for makeIon input")
    ;
  
  po::variables_map vm;

  try {
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);    
  } catch (po::error& e) {
    std::cout << "Option error: " << e.what() << std::endl;
    exit(-1);
  }

  if (vm.count("help")) {
    std::cout << desc << "\n";
    return 1;
  }

  std::array<double, 3> alpha {4.9771e-13, 5.9671e-13, 6.2216e-14};

  std::map<unsigned, std::array<double, 3> > beta
  {
    { 25000, {1.8989e-13, 2.0745e-13, 9.4349e-13} },
    { 30000, {1.6449e-13, 1.8229e-13, 8.3322e-13} }
  };

  std::array<double, 3> gamma;
  std::array<double, 3> init  {0.001, 0.001, 0.001};
  std::array<double, 3> curr, last;

  for (int i=0; i<3; i++) gamma[i] = beta[T][i]/alpha[i];
  double X = 0.76, Y = 0.24, mX = 1.0, mY = 4.0;
  double mu = X/mX + Y/mY;

  curr = init;

  double err = 0.0;

  for (int n=0; n<niter; n++) {

    double ne = n0*(X/mX*(1.0 - curr[0]) +
		    Y/mY*(curr[2] + 2.0*(1.0 - curr[0] - curr[2])));
    last = curr;

    curr[0] = ne*(1.0 - last[0])*gamma[0];
    curr[1] = ne*last[2]*gamma[1];
    curr[2] = ne*(1.0 - last[1] - last[2])*gamma[2];

    std::cout << std::setw(8) << n;
    for (int j=0; j<3; j++) std::cout << std::setw(14) << curr[j];
    std::cout << std::endl;

    err = 0.0;
    for (int j=0; j<3; j++)
      err += (curr[j] - last[j]) * (curr[j] - last[j]);
    err = sqrt(err);
    if (err < tol) break;
  }

  std::cout << std::endl << std::left
	    << std::setw(24) << "Convergence error"   << err << std::endl
	    << std::setw(24) << "Requested tolerance" << tol << std::endl
	    << std::endl;

  if (err < tol) {
    std::ofstream out(outf);
    if (out) {
      for (int j=0; j<3; j++) out << std::setw(14) << curr[j];
      out << std::endl;
      std::cout << "SUCCESS: "
		<< "file <" << outf << "> written" << std::endl;
    } else {
      std::cout << "FAILURE: "
		<< "error opening <" << outf << "> for output" << std::endl;
    }
  } else {
    std::cout << "FAILURE: no convergence" << std::endl;
  }
  std::cout << std::endl;

  return 0;
}