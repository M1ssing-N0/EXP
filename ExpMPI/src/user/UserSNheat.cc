#include <sys/timeb.h>
#include <math.h>
#include <sstream>

#include <expand.h>
#include <global.H>

#include <UserSNheat.H>

//
// Physical units
//

static double pc = 3.086e18;		// cm
// static double a0 = 2.0*0.054e-7;	// cm (2xBohr radius)
// static double boltz = 1.381e-16;	// cgs
static double year = 365.25*24*3600;	// seconds per year
// static double mp = 1.67e-24;		// g
static double msun = 1.989e33;		// g

double UserSNheat::Lunit = 3.0e5*pc;
double UserSNheat::Munit = 1.0e12*msun;
double UserSNheat::Tunit = sqrt(Lunit*Lunit*Lunit/(Munit*6.673e-08));
double UserSNheat::Vunit = Lunit/Tunit;
double UserSNheat::Eunit = Munit*Vunit*Vunit;

UserSNheat::UserSNheat(string &line) : ExternalForce(line)
{

  id = "SupernovaHeating";	// ID

				// Location heat source
  origin = vector<double>(3, 0.0);	
				// Radius of spherical region for heating
				// 0.0001 is 30 pc
  radius = 0.0001;
				// Delay in system units
  delay = 0.0;
				// Spacing in years
  dT = 1.0e+04;
				// Energy per SN in erg
  dE = 1.0e+51;
				// Number of SN
  N = 100;
				// Default component (must be specified)
  comp_name = "";
				// Debugging output switch
  verbose = false;

  initialize();

				// Look for the fiducial component
  bool found = false;
  list<Component*>::iterator cc;
  Component *c;
  for (cc=comp.components.begin(); cc != comp.components.end(); cc++) {
    c = *cc;
    if ( !comp_name.compare(c->name) ) {
      c0 = c;
      found = true;
      break;
    }
  }

  if (!found) {
    cerr << "UserSNheat: process " << myid 
	 << " can't find fiducial component <" << comp_name << ">" << endl;
    MPI_Abort(MPI_COMM_WORLD, 35);
  }
  
  userinfo();

  gen = new ACG(7+myid);
  unit = new Uniform(0.0, 1.0, gen);
  norm = new Normal(0.0, 1.0, gen);

  Vunit = Lunit/Tunit;
  Eunit = Munit*Vunit*Vunit;

  dT *= year/Tunit;
  dE /= Eunit;

  // DEBUGGING
  if (myid==0 && verbose) {
    cout << "UserSNheat: dT=" << dT << " dE=" << dE << endl;
  }
  //

  firstime = true;
  ncount   = 0;
  mm0      = vector<double>(nthrds);
  ke0      = vector<double>(nthrds);
  ke1      = vector<double>(nthrds);
  mom      = vector<double>(3);
  pp0      = vector< vector<double> >(nthrds);

  for (int k=0; k<nthrds; k++) pp0[k] = vector<double>(3, 0.0);

  threading_init();
}

UserSNheat::~UserSNheat()
{
  delete unit;
  delete norm;
  delete gen;
}


int UserSNheat::arrivalTime(double dt)
{
  double L = exp(-dt/dT);
  double p = 1.0;
  int    k = 0;
  do {
    k++;
    p *= (*unit)();
  } while (p>L);

  return k - 1;
}

void UserSNheat::userinfo()
{
  if (myid) return;		// Return if node master node

  print_divider();

  cout << "** User routine stochastic Supernova heating initialized"
       << " using component <" << comp_name << ">"
       << " with delay time=" << delay << ", time interval dT=" << dT 
       << ", SN energy dE=" << dE << ", number SN=" << N 
       << ", bubble radius=" << radius 
       << endl
       << "   Lunit=" << Lunit << ", Tunit=" << Tunit << ", Munit=" << Munit
       << endl
       << "   Origin (x , y , z) = (" 
       << origin[0] << " , " 
       << origin[1] << " , " 
       << origin[2] << " ) " << endl; 

  print_divider();
}

void UserSNheat::initialize()
{
  string val;

  if (get_value("compname", val))	comp_name  = val;
  if (get_value("verbose",  val))	verbose = atoi(val.c_str()) ? true : false;

  if (get_value("X", val))	        origin[0]  = atof(val.c_str());
  if (get_value("Y", val))	        origin[1]  = atof(val.c_str());
  if (get_value("Z", val))	        origin[2]  = atof(val.c_str());

  if (get_value("dT", val))	        dT         = atof(val.c_str());
  if (get_value("dE", val))	        dE         = atof(val.c_str());
  if (get_value("radius", val))	        radius     = atof(val.c_str());
  if (get_value("delay", val))	        delay      = atof(val.c_str());
  if (get_value("number", val))	        N          = atoi(val.c_str());

  if (get_value("Lunit", val))		Lunit      = atof(val.c_str());
  if (get_value("Tunit", val))		Tunit      = atof(val.c_str());
  if (get_value("Munit", val))		Munit      = atof(val.c_str());
}


void UserSNheat::determine_acceleration_and_potential(void)
{
  if (cC != c0)     return;
  if (tnow < delay) return;
  if (ncount > N)   return;

  if (!firstime) {

    if (myid==0) {
      nSN = arrivalTime(tnow - tlast);
      // DEBUGGING
      if (verbose && nSN)
	cout << "UserSNheat: T=" << setw(12) << tnow 
	     << " [" << setw(12) << tnow*Tunit/year << " years]"
	     << "     SN=" << setw(4) << nSN 
	     << "     so far=" << setw(4) << ncount << endl;
    }
    MPI_Bcast(&nSN, 1, MPI_INT, 0, MPI_COMM_WORLD);
    
    if (nSN) {
      plist = vector< set<int> >(nthrds);
      exp_thread_fork(false);
      ncount += nSN;
    }
    print_timings("UserSNheat: thread timings");

  }

  tlast    = tnow;
  firstime = false;

}

void * UserSNheat::determine_acceleration_and_potential_thread(void * arg) 
{
  unsigned nbodies = cC->Number();
  int id = *((int*)arg);
  int nbeg = nbodies*id/nthrds;
  int nend = nbodies*(id+1)/nthrds;
  
  thread_timing_beg(id);

  PartMapItr it = cC->Particles().begin();

  mm0[id] = ke0[id] = ke1[id] = 0.0;
  for (int k=0; k<3; k++) pp0[id][k] = 0.0;
  
  for (int q=0   ; q<nbeg; q++) it++;
  for (int q=nbeg; q<nend; q++) {
    
				// Index for the current particle
    unsigned long i = (it++)->first;
    
    Particle *p = cC->Part(i);
    
    double dist = 0.0;
    for (int k=0; k<3; k++) {
      double pos = p->pos[k] - origin[k];
      dist += pos*pos;
    }
    if (dist < radius*radius) {
      plist[id].insert(i);
      mm0[id] += p->mass;
      for (int k=0; k<3; k++) pp0[id][k] += p->mass*p->vel[k];
    }
  }
  
  Pbarrier(id, 37);

  if (id==0) {			// Thread 0 add up contributions to
				// mass and momentum from remaining
				// threads
    for (int i=1; i<nthrds; i++) {
      mm0[0] += mm0[i];
      for (int k=0; k<3; k++) pp0[0][k] += pp0[i][k];
    }

    MPI_Allreduce(&mm0[0],    &mass0,  1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    MPI_Allreduce(&pp0[0][0], &mom[0], 3, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
  }

  Pbarrier(id, 38);

  if (mass0 > 0.0) {
				// Compute the center of mass velocity
    for (int k=0; k<3; k++) mom[k] /= mass0;

    for (set<int>::iterator s=plist[id].begin(); s!=plist[id].end(); s++) {
      Particle *p = cC->Part(*s);
      for (int k=0; k<3; k++) {
	double vel = p->vel[k] - mom[k];
	ke0[id] += 0.5*p->mass * vel*vel;
      }
    }

    Pbarrier(id, 39);

    if (id==0) {
      for (int i=1; i<nthrds; i++) ke0[0] += ke0[i];
      MPI_Allreduce(&ke0[0], &ketot0, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    }
    
    Pbarrier(id, 40);
    
    double disp = sqrt(2.0/3.0*(dE*nSN+ketot0)/mass0);
    for (set<int>::iterator s=plist[id].begin(); s!=plist[id].end(); s++) {
      Particle *p = cC->Part(*s);
      for (int k=0; k<3; k++) {
	double vel = disp*(*norm)();
	ke1[id] += 0.5*p->mass * vel*vel;
	p->vel[k] = mom[k] + vel;
      }
    }

    Pbarrier(id, 41);
    
    if (id == 0) {
      for (int j=1; j<nthrds; j++) ke1[0] += ke1[j];
      MPI_Allreduce(&ke1[0], &ketot1, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
      factor = sqrt((dE*nSN+ketot0)/ketot1);
    }

    Pbarrier(id, 42);
    
    for (set<int>::iterator s=plist[id].begin(); s!=plist[id].end(); s++) {
      Particle *p = cC->Part(*s);
      for (int k=0; k<3; k++) p->vel[k] *= factor;
    }
    
    if (id==0 && myid==0 && verbose)
      cout << "UserSNheat: " << "mass=" << mass0 
	   << ", factor=" << factor << ", snE=" << dE*nSN
	   << ", ke0=" << ketot0 << ", ke1=" << ketot1
	   << endl;

  } else {
    if (id==0 && myid==0)
      cout << "UserSNheat: "
	   << "No points in heating sphere of radius " << radius 
	   << " at time " << tnow << endl;
  }

  thread_timing_end(id);

  return (NULL);
}


extern "C" {
  ExternalForce *makerSNheat(string& line)
  {
    return new UserSNheat(line);
  }
}

class proxysnheat { 
public:
  proxysnheat()
  {
    factory["usersnheat"] = makerSNheat;
  }
};

proxysnheat p;