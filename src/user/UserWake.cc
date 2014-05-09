#include <cmath>
#include <sstream>

#include <expand.h>
#include <localmpi.h>

#include <ExternalCollection.H>
#include <Basis.H>
#include <UserWake.H>

#include <pthread.h>  

using namespace std;

UserWake::UserWake(string &line) : ExternalForce(line)
{
  first = true;
  filename = "wake";

				// Output surface
  NUMX = 100;
  NUMY = 100;
  XMIN = -1.8;
  XMAX =  1.8;
  YMIN = -1.8;
  YMAX =  1.8;
				// Rotation of surface from equitorial plane
  PHI = 0.0;
  PSI = 0.0;
  THETA = 0.0;
				// Steps between frames
  NSTEP = 10;

  initialize();

  if (numComp==0) {
    if (myid==0) cerr << "You must specify component targets!" << endl;
    MPI_Abort(MPI_COMM_WORLD, 120);
  }

				// Search for component by name
  for (int i=0; i<numComp; i++) {
    bool found = false;
    list<Component*>::iterator cc;
    Component *c;
    for (cc=comp.components.begin(); cc != comp.components.end(); cc++) {
      c = *cc;
      if ( !C[i].compare(c->name) ) {
	// Check to see that the force can return field values
	if (dynamic_cast <Basis*> (c->force)) {
	  c0.push_back(c);
	  found = true;
	  break;
	} else {
	  cerr << "Process " << myid << ": desired component <"
	       << C[i] << "> is not a basis type!" << endl;

	  MPI_Abort(MPI_COMM_WORLD, 121);
	}
      }
    }

    if (!found) {
      cerr << "Process " << myid << ": can't find desired component <"
	   << C[i] << ">" << endl;
    }
  }

  userinfo();

				// Names of output file
  names.push_back("dens0");
  names.push_back("dens1");
  names.push_back("dens");
  names.push_back("densR");

  names.push_back("potl0");
  names.push_back("potl1");
  names.push_back("potl");
  names.push_back("potlR");

				// Data storage
  npix = NUMX*NUMY;
  for (unsigned i=0; i<names.size(); i++) {
    data0.push_back(vector<float>(npix));
    data1.push_back(vector<float>(npix));
  }

  nbeg = npix*myid/numprocs;
  nend = npix*(myid+1)/numprocs;

  double onedeg = M_PI/180.0;
  Matrix rotate = return_euler_slater(PHI*onedeg, 
				      THETA*onedeg, 
				      PSI*onedeg, 1);
  Three_Vector P0, P1;
  P0.zero();
    

  double dX = (XMAX - XMIN)/(NUMX-1);
  double dY = (YMAX - YMIN)/(NUMY-1);
  double R;

  for (int j=0; j<NUMY; j++) {

    P0[2] = YMIN + dY*j;

    for (int i=0; i<NUMX; i++) {

      P0[1] = XMIN + dX*i;

      P1 = rotate * P0;

      R = sqrt(P0[1]*P1[1] + P1[2]*P1[2] + P1[3]*P1[3]);

				// Location of the rotated plane
      r.push_back(R);
      theta.push_back(acos(P1[3]/R));
      phi.push_back(atan2(P1[2], P1[1]));
    }
  }
    
}

UserWake::~UserWake()
{
  // Nothing so far
}

void UserWake::userinfo()
{
  if (myid) return;		// Return if node master node

  print_divider();

  cout << "** User routine WAKE initialized"
       << " with Components <" << C[0];
  for (int i=1; i<numComp; i++) cout << " " << C[i];
  cout << ">";
  
  cout << ", NUMX=" << NUMX
       << ", NUMY=" << NUMY
       << ", XMIN=" << XMIN
       << ", XMAX=" << XMAX
       << ", YMIN=" << YMIN
       << ", YMAX=" << YMAX
       << ", PHI=" << PHI
       << ", THETA=" << THETA
       << ", PSI=" << PSI
       << ", NSTEP=" << NSTEP
       << ", filename=" << filename
       << endl;
  
  print_divider();
}

void UserWake::initialize()
{
  string val;


  for (numComp=0; numComp<1000; numComp++) {
    ostringstream count;
    count << "C(" << numComp+1 << ")";
    if (get_value(count.str(), val))
      C.push_back(val.c_str());
    else break;
  }

  if (numComp != (int)C.size()) {
    cerr << "UserWake: error parsing component names, "
	 << "  Size(C)=" << C.size()
	 << "  numRes=" << numComp << endl;
    MPI_Abort(MPI_COMM_WORLD, 122);
  }

  if (get_value("filename", val)) filename = val;
  if (get_value("NUMX", val))     NUMX = atoi(val.c_str());
  if (get_value("NUMY", val))     NUMY = atoi(val.c_str());
  if (get_value("XMIN", val))     XMIN = atof(val.c_str());
  if (get_value("XMAX", val))     XMAX = atof(val.c_str());
  if (get_value("YMIN", val))     YMIN = atof(val.c_str());
  if (get_value("YMAX", val))     YMAX = atof(val.c_str());
  if (get_value("PHI", val))      PHI = atof(val.c_str());
  if (get_value("THETA", val))    THETA = atof(val.c_str());
  if (get_value("PSI", val))      PSI = atof(val.c_str());
  if (get_value("NSTEP", val))      NSTEP = atoi(val.c_str());
}


void UserWake::determine_acceleration_and_potential(void)
{

  if (first) {

    count = 0;
    nlast = this_step;
    nnext = this_step;

    if (restart) {

      if (myid == 0) {

	for (count=0; count<10000; count++) {
	  ostringstream ostr;
	  ostr << outdir << runtag << "." << filename << "." 
	       << names[0] << "." << count;
	  
	  // Try to open stream for writing
	  ifstream in(ostr.str().c_str());
	  if (!in) break;
	}
      }

      if (myid==0) 
	cout << "UserWake: beginning at frame=" << count << endl;

      MPI_Bcast(&count, 1, MPI_INT, 0, MPI_COMM_WORLD);
	
    }

    first = false;
  }

  if (this_step == nnext) {


    // -----------------------------------------------------------
    // Clean the data store before the first component in the list
    // -----------------------------------------------------------

    if (cC == c0.front()) {
      for (unsigned i=0; i<names.size(); i++) {
	for (int j=0; j<npix; j++) data0[i][j] = data1[i][j] = 0.0;
      }
    }

    // -----------------------------------------------------------
    // Compute the images
    // -----------------------------------------------------------

    // exp_thread_fork(false);

    double dens0, potl0, dens, potl, potr, pott, potp;

    for (int i=nbeg; i<nend; i++) {

      ((Basis *)cC->force)->
	determine_fields_at_point_sph(r[i], theta[i], phi[i], 
				      &dens0, &potl0, 
				      &dens, &potl,
				      &potr, &pott, &potp);
    
      data1[0][i] += dens0;
      data1[1][i] += dens - dens0;
      data1[2][i] += dens;
      
      data1[4][i] += potl0;
      data1[5][i] += potl - potl0;
      data1[6][i] += potl;
    }


    // -----------------------------------------------------------
    // Print the images after the last component in the list
    // -----------------------------------------------------------
    
    if (cC == c0.back()) {

      for (unsigned i=0; i<names.size(); i++) {
      
	MPI_Reduce(&data1[i][0], &data0[i][0], npix, 
		   MPI_FLOAT, MPI_SUM, 0, MPI_COMM_WORLD);
      
	if (myid==0) {		// Print the file
	  
	  float f;

	  for (unsigned i=0; i<names.size(); i++) {

	    ostringstream ostr;
	    ostr << outdir << runtag << "." << filename 
		 << "." << names[i] << "." << count;
	  
	    // Try to open stream for writing
	    ofstream out(ostr.str().c_str());
	    if (out) {
	      out.write((const char *)&NUMX, sizeof(int));
	      out.write((const char *)&NUMY, sizeof(int));
	      out.write((const char *)&(f=XMIN), sizeof(float));
	      out.write((const char *)&(f=XMAX), sizeof(float));
	      out.write((const char *)&(f=YMIN), sizeof(float));
	      out.write((const char *)&(f=YMAX), sizeof(float));

	      for (int j=0; j<npix; j++) {

		if (i==3) {	// Relative density
		  data0[i][j] = data0[1][j];
		  if (data0[0][j]>0.0) data0[i][j] /= fabs(data0[0][j]);
		}
		if (i==7) {	// Relative potential
		  data0[i][j] = data0[5][j];
		  if (data0[4][j]>0.0) data0[i][j] /= fabs(data0[4][j]);
		}
		out.write((const char *)&data0[i][j], sizeof(float));
	      }

	    } else {
	      cerr << "UserWake: error opening <" << ostr.str() << ">" << endl;
	    }
	  }
      
	}

      }

    }

    // -----------------------------------------------------------


    count++;
    nlast = this_step;
    nnext = this_step + NSTEP;
  }

}


void * UserWake::determine_acceleration_and_potential_thread(void * arg) 
{
  int id = *((int*)arg);
  int nbeg1 = nbeg + (nend - nbeg)*(id  )/nthrds;
  int nend1 = nbeg + (nend - nbeg)*(id+1)/nthrds;

  double dens0, potl0, dens, potl, potr, pott, potp;

  for (int i=nbeg1; i<nend1; i++) {

    ((Basis *)cC->force)->
      determine_fields_at_point_sph(r[i], theta[i], phi[i], 
				    &dens0, &potl0, 
				    &dens, &potl,
				    &potr, &pott, &potp);
    
    data1[0][i] += dens0;
    data1[1][i] += dens - dens0;
    data1[2][i] += dens;

    data1[4][i] += potl0;
    data1[5][i] += potl - potl0;
    data1[6][i] += potl;
  }


  return (NULL);
}


extern "C" {
  ExternalForce *makerWake(string& line)
  {
    return new UserWake(line);
  }
}

class proxywake { 
public:
  proxywake()
  {
    factory["userwake"] = makerWake;
  }
};

proxywake p;