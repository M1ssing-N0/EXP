/*
  Test cuda cross section implementation
*/

/* Manual compile string:

mpiCC -g -o testCrossCuda testCrossCuda.o Ion.o cudaIon.o TopBase.o spline.o phfit2.o -lexputil -lexpgnu -lboost_program_options -lvtkCommonCore-7.1 -lvtkCommonDataModel-7.1 -lvtkIOXML-7.1 -lmpi -lcuda -lcudart

*/

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <numeric>
#include <tuple>

#include <boost/program_options.hpp>
#include <boost/random/mersenne_twister.hpp>

#include "atomic_constants.H"
#include "Ion.H"
#include "Elastic.H"

namespace po = boost::program_options;

#include <mpi.h>

extern "C" void phfit2_(int* nz, int* ne, int* is, float* e, float* s);

// For sorting tuples
//
template<int M, template<typename> class F = std::less>
struct TupleCompare
{
  template<typename T>
  bool operator()(T const &t1, T const &t2)
  {
    return F<typename std::tuple_element<M, T>::type>()(std::get<M>(t1), std::get<M>(t2));
  }
};

int numprocs, myid;
std::string outdir(".");
std::string runtag("run");
char threading_on = 0;
pthread_mutex_t mem_lock;
boost::mt19937 random_gen;

int main (int ac, char **av)
{
  //===================
  // MPI preliminaries 
  //===================

  MPI_Init(&ac, &av);
  MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
  MPI_Comm_rank(MPI_COMM_WORLD, &myid);

  std::string cmd_line;
  for (int i=0; i<ac; i++) {
    cmd_line += av[i];
    cmd_line += " ";
  }

  int num;
  double emin, emax;
  bool eVout = false;
  std::string scaling;


  po::options_description desc("Allowed options");
  desc.add_options()
    ("help,h",		"produce help message")
    ("vanHoof",         "Use Gaunt factor from van Hoof et al.")
    ("KandM",           "Use original Koch & Motz cross section")
    ("eV",		"print results in eV")
    ("compare,c",       "for comparison with CPU version")
    ("Num,N",		po::value<int>(&num)->default_value(200),
     "number of evaluations")
    ("Emin,e",		po::value<double>(&emin)->default_value(0.001),
     "minimum energy (Rydbergs)")
    ("Emax,E",		po::value<double>(&emax)->default_value(100.0),
     "maximum energy (Rydbergs)")
    ("scaling,S",	po::value<std::string>(&scaling),
     "cross-section scaling (born, mbarn, (null))")
    ;

  po::variables_map vm;

  try {
    po::store(po::parse_command_line(ac, av, desc), vm);
    po::notify(vm);    
  } catch (po::error& e) {
    std::cout << "Option error: " << e.what() << std::endl;
    MPI_Finalize();
    return -1;
  }

  if (vm.count("help")) {
    std::cout << desc << std::endl;
    MPI_Finalize();
    return 1;
  }

  if (vm.count("eV")) {
    eVout = true;
  }

  if (vm.count("vanHoof")) {
    Ion::use_VAN_HOOF = true;
  }

  if (vm.count("KandM")) {
    Ion::use_VAN_HOOF = false;
  }

  std::string prefix("crossCuda");
  std::string cmdFile = prefix + ".cmd_line";
  std::ofstream out(cmdFile.c_str());
  if (!out) {
    std::cerr << "testCrossCuda: error opening <" << cmdFile
	      << "> for writing" << std::endl;
  } else {
    out << cmd_line << std::endl;
  }

  // Initialize CHIANTI
  //
  // std::set<unsigned short> ZList = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 16};

  std::set<unsigned short> ZList = {1, 2};

  atomicData ad;

  // Using CUDA-----------+
  //                      |
  //                      v
  ad.createIonList(ZList, true);

  std::cout << "# Ions = " << ad.IonList.size() << std::endl;
  if (vm.count("compare"))
    ad.testCrossCompare(num, emin, emax, eVout, scaling);
  else
    ad.testCross(num);
  
  MPI_Finalize();

  return 0;
}
