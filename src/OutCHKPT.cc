#include <cstdio>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "expand.h"
#include <global.H>

#include <AxisymmetricBasis.H>
#include <OutCHKPT.H>

OutCHKPT::OutCHKPT(string& line) : Output(line)
{
  initialize();
}

void OutCHKPT::initialize()
{
  string tmp;
				// Get file name
  if (!Output::get_value(string("filename"), filename)) {
    filename.erase();
    filename = outdir + "OUT." + runtag + ".chkpt";
  }

  if (Output::get_value(string("nint"), tmp))
    nint = atoi(tmp.c_str());
  else
    nint = 100;

}


void OutCHKPT::Run(int n, bool last)
{
  if (n % nint && !last) return;
  if (VERBOSE>5 && myid==0) {
    cout << " OutCHKPT::Run(): n=" << n << " psdump=" << psdump << endl;
  }
  if (n == psdump) {       
    if (myid==0) {
      string backfile = filename + ".bak";
      if (unlink(backfile.c_str())) {
	perror("OutCHKPT::Run()");
	cout << "OutCHKPT::Run(): error unlinking old backup file <" 
	     << backfile << ">" << endl;
      } else {
	if (VERBOSE>5) {
	  cout << "OutCHKPT::Run(): successfully unlinked <"
	       << backfile << ">" << endl;
	}
      }
      if (rename(filename.c_str(), backfile.c_str())) {
	perror("OutCHKPT::Run()");
	cout << "OutCHKPT: error creating backup file <" 
	     << backfile << ">" << endl;
      } else {
	if (VERBOSE>5) {
	  cout << "OutCHKPT::Run(): successfully renamed <"
	       << filename << "> to <" << backfile << ">" << endl;
	}
      }
      if (symlink(lastPS.c_str(), filename.c_str())) {
	perror("OutCHKPT::Run()");
	cout << "OutCHKPT::Run(): error symlinking new backup file <" 
	     << filename << ">" << endl;
      } else {
	if (VERBOSE>5) {
	  cout << "OutCHKPT::Run(): successfully linked <"
	       << lastPS << "> to new backup file <" 
	       << filename << ">" << endl;
	}
      }
    }
    return;
  }

  ofstream *out;
  list<Component*>::iterator cc;
  Component* c;

  if (myid==0) {
    
				// Attempt to move file to backup
    string backfile = filename + ".bak";
    if (rename(filename.c_str(), backfile.c_str())) {
      perror("OutCHKPT::Run()");
      cout << "OutCHKPT::Run(): error creating backup file <" 
	   << backfile << ">";
    }
	
				// Open file and write master header
    out = new ofstream(filename.c_str());

    if (!*out) {
      cerr << "OutCHKPT: can't open file <" << filename.c_str() 
	   << "> . . . quitting\n";
      MPI_Abort(MPI_COMM_WORLD, 33);
    }
				// Open file and write master header
    
    struct MasterHeader header;
    header.time  = tnow;
    header.ntot  = comp.ntot;
    header.ncomp = comp.ncomp;

    out->write((char *)&header, sizeof(MasterHeader));
#ifdef DEBUG
    cout << "OutCHKPT: header written" << endl;
#endif

  }
  
  for (cc=comp.components.begin(); cc != comp.components.end(); cc++) {
    c = *cc;
#ifdef DEBUG
    cout << "OutCHKPT: process " << myid << " trying to write name=" << c->name
	 << " force=" << c->id << endl;
#endif
    c->write_binary(out);
#ifdef DEBUG
    cout << "OutCHKPT: process " << myid << " write completed on " << c->name << endl;
#endif
  }
  
  if (myid==0) {
    out->close();
    delete out;
  }

  chktimer.mark();

}
