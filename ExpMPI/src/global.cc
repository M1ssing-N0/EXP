#include <mpi.h>
#include <string>

#include <coef.H>
#include <ComponentContainer.H>
#include <ExternalForce.H>
#include <ExternalCollection.H>
#include <OutputContainer.H>
#include <ParamParseMPI.H>

				// Numerical parameters

int nbodmax = 20000;		// Maximum number of bodies; this is not
				// an intrinsic limitation just a sanity
				// value

int nsteps = 500;		// Number of steps to execute
int nscale = 20;		// Number of steps between rescaling
int nthrds = 2;			// Number of POSIX threads
int nbalance = 0;		// Steps between load balancing
double dbthresh = 0.05;		// Load balancing threshold (5% by default)
double dtime = 0.1;		// Default time step size

bool use_cwd = true;
bool restart = false;
int NICE = 10;

				// Files
string homedir = "./";
string infile = "restart.in";
string parmfile = "PARAM.FILE";
string ratefile = "processor.rates";
string runtag = "newrun";
string ldlibdir = ".";

double tpos, tvel, tnow;	// Per step variables
int this_step;

				// Global center of mass
double mtot;
double *gcom = new double [3];
double *gcov = new double [3];
bool global_cov = false;
bool eqmotion = true;

				// MPI variables
int is_init=1;
int numprocs, slaves, myid, proc_namelen;
char processor_name[MPI_MAX_PROCESSOR_NAME];

MPI_Comm MPI_COMM_SLAVE;

char threading_on = 0;
pthread_mutex_t mem_lock;

CoefHeader coefheader;
CoefHeader2 coefheader2;

ComponentContainer comp;
ExternalCollection external;
OutputContainer output;
ParamParseMPI *parse;

map<string, maker_t *, less<string> > factory;
map<string, maker_t *, less<string> >::iterator fitr;

