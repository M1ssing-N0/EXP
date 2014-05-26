#include <values.h>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>

using namespace std;

#include "Timer.h"
#include "global.H"
#include "pHOT.H"
#include "UserTreeDSMC.H"
#include "Collide.H"

#ifdef USE_GPTL
#include <gptl.h>
#endif

static bool DEBUG      = false;	// Thread diagnostics, false for
				// production

				// Use the original Pullin velocity 
				// selection algorithm
bool Collide::PULLIN   = false;

// Use the explicit energy solution
bool Collide::ESOL     = false;

// Print out sorted cell parameters
bool Collide::SORTED   = false;

// Print out T-rho plane for cells 
// with mass weighting
bool Collide::PHASE    = false;

// Extra debugging output
bool Collide::EXTRA    = false;

// Turn off collisions for testing
bool Collide::DRYRUN   = false;

// Turn off cooling for testing
bool Collide::NOCOOL   = false;

// Ensemble-based excess cooling
bool Collide::ENSEXES  = true;

// Time step diagnostics
bool Collide::TSDIAG   = false;

// Cell-volume diagnostics
bool Collide::VOLDIAG  = false;

// Mean free path diagnostics
bool Collide::MFPDIAG  = false;

// Sample based on maximum (true) or estimate
// from variance (false);
bool Collide::NTC      = false;

// Use cpu work to augment per particle effort
bool Collide::EFFORT   = true;	

// Verbose timing
bool Collide::TIMING   = true;

// Temperature floor in EPSM
double Collide::TFLOOR = 1000.0;

double 				// Enhance (or suppress) fiducial cooling rate
Collide::ENHANCE       = 1.0;

// Power of two interval for KE/cool histogram
int Collide::TSPOW     = 4;

// Proton mass (g)
const double mp        = 1.67262158e-24;

// Boltzmann constant (cgs)
const double boltz     = 1.3810e-16;


extern "C"
void *
collide_thread_call(void *atp)
{
  thrd_pass_Collide *tp = (thrd_pass_Collide *)atp;
  Collide *p = (Collide *)tp->p;
  p -> collide_thread((void*)&tp->arg);
  return NULL;
}

void Collide::collide_thread_fork(pHOT* tree, sKeyDmap* Fn, double tau)
{
  int errcode;
  void *retval;
  
  if (nthrds==1) {
    thrd_pass_Collide td;
    
    td.p        = this;
    td.arg.tree = tree;
    td.arg.fn   = Fn;
    td.arg.tau  = tau;
    td.arg.id   = 0;
    
    collide_thread_call(&td);
    
    return;
  }
  
  td = new thrd_pass_Collide [nthrds];
  t = new pthread_t [nthrds];
  
  if (!td) {
    cerr << "Process " << myid 
	 << ": collide_thread_fork: error allocating memory for thread counters\n";
    exit(18);
  }
  if (!t) {
    cerr << "Process " << myid
	 << ": collide_thread_fork: error allocating memory for thread\n";
    exit(18);
  }
  
  // Make the <nthrds> threads
  for (int i=0; i<nthrds; i++) {
    td[i].p        = this;
    td[i].arg.tree = tree;
    td[i].arg.fn   = Fn;
    td[i].arg.tau  = tau;
    td[i].arg.id   = i;
    
    errcode =  pthread_create(&t[i], 0, collide_thread_call, &td[i]);
    if (errcode) {
      cerr << "Process " << myid;
      cerr << " collide: cannot make thread " << i
	   << ", errcode=" << errcode << endl;
      exit(19);
    }
  }
  
  if (DEBUG)
    cerr << "Process " << myid << ": " << nthrds << " threads created"
	 << std::endl;
  
  
  waitTime.start();
  
  // Collapse the threads
  for (int i=0; i<nthrds; i++) {
    if ((errcode=pthread_join(t[i], &retval))) {
      cerr << "Process " << myid;
      cerr << " collide: thread join " << i
	   << " failed, errcode=" << errcode << endl;
      exit(20);
    }
    if (i==0) {
      waitSoFar = waitTime.stop();
      joinTime.start();
    }
  }
  
  if (DEBUG)
    cerr << "Process " << myid << ": " << nthrds << " threads joined"
	 << std::endl;
  
  
  joinSoFar = joinTime.stop();
  
  delete [] td;
  delete [] t;
}


double   Collide::EPSMratio = -1.0;
unsigned Collide::EPSMmin   = 0;

Collide::Collide(ExternalForce *force, Component *comp,
		 double hDiam, double sDiam, int nth)
{
  caller = force;
  c0     = comp;
  nthrds = nth;
  
  // Counts the total number of collisions
  colcntT = vector< vector<unsigned> > (nthrds);
  
  // Total number of particles processsed
  numcntT = vector< vector<unsigned> > (nthrds);
  
  // Total velocity dispersion (i.e. mean temperature)
  tdispT  = vector< vector<double> >   (nthrds);
  
  // Number of collisions with inconsistencies (only meaningful for LTE)
  error1T = vector<unsigned> (nthrds, 0);
  
  // Number of particles selected for collision
  sel1T   = vector<unsigned> (nthrds, 0);
  
  // Number of particles actually collided
  col1T   = vector<unsigned> (nthrds, 0);
  
  // Number of particles processed by the EPSM algorithm
  epsm1T  = vector<unsigned> (nthrds, 0);
  
  // Number of cells processed by the EPSM algorithm
  Nepsm1T = vector<unsigned> (nthrds, 0);
  
  // Total mass of processed particles
  tmassT  = vector<double>   (nthrds, 0);
  
  // True energy lost to dissipation (i.e. radiation)
  decolT  = vector<double>   (nthrds, 0);
  
  // Full energy lost to dissipation (i.e. radiation) 
  decelT  = vector<double>   (nthrds, 0);
  
  // Energy excess (true energy)
  exesCT  = vector<double>   (nthrds, 0);
  
  // Energy excess (full energy)
  exesET  = vector<double>   (nthrds, 0);
  
  if (MFPDIAG) {
    // List of ratios of free-flight length to cell size
    tsratT  = vector< vector<double> >  (nthrds);
    
    // List of fractional changes in KE per cell
    keratT  = vector< vector<double> >  (nthrds);
    
    // List of cooling excess to KE per cell
    deratT  = vector< vector<double> >  (nthrds);
    
    // List of densities in each cell
    tdensT  = vector< vector<double> >  (nthrds);
    
    // List of cell volumes
    tvolcT  = vector< vector<double> >  (nthrds);
    
    // Temperature per cell; assigned in derived class instance
    ttempT  = vector< vector<double> >  (nthrds);
    
    // List of change in energy per cell due to cooling (for LTE only)
    tdeltT  = vector< vector<double> >  (nthrds);
    
    // List of collision selections per particle
    tselnT  = vector< vector<double> >  (nthrds);
    
    // List of cell diagnostic info per cell
    tphaseT = vector< vector<Precord> > (nthrds);
    
    // List of mean-free path info per cell
    tmfpstT = vector< vector<Precord> > (nthrds);
  }
  
  cellist = vector< vector<pCell*> > (nthrds);
  
  hsdiam    = hDiam;
  diamfac   = sDiam;
  
  seltot    = 0;	      // Count estimated collision targets
  coltot    = 0;	      // Count total collisions
  errtot    = 0;	      // Count errors in inelastic computation
  epsmcells = 0;	      // Count cells in EPSM regime
  epsmtot   = 0;	      // Count particles in EPSM regime
  
  // EPSM diagnostics
  lostSoFar_EPSM = vector<double>(nthrds, 0.0);
  
  if (EPSMratio> 0) use_epsm = true;
  else              use_epsm = false;
  
  // 
  // TIMERS
  //
  
  diagTime.Microseconds();
  snglTime.Microseconds();
  forkTime.Microseconds();
  waitTime.Microseconds();
  joinTime.Microseconds();
  
  stepcount = 0;
  bodycount = 0;
  
  listTime   = vector<Timer>(nthrds);
  initTime   = vector<Timer>(nthrds);
  collTime   = vector<Timer>(nthrds);
  elasTime   = vector<Timer>(nthrds);
  stat1Time  = vector<Timer>(nthrds);
  stat2Time  = vector<Timer>(nthrds);
  stat3Time  = vector<Timer>(nthrds);
  coolTime   = vector<Timer>(nthrds);
  cellTime   = vector<Timer>(nthrds);
  curcTime   = vector<Timer>(nthrds);
  epsmTime   = vector<Timer>(nthrds);
  listSoFar  = vector<TimeElapsed>(nthrds);
  initSoFar  = vector<TimeElapsed>(nthrds);
  collSoFar  = vector<TimeElapsed>(nthrds);
  elasSoFar  = vector<TimeElapsed>(nthrds);
  cellSoFar  = vector<TimeElapsed>(nthrds);
  curcSoFar  = vector<TimeElapsed>(nthrds);
  epsmSoFar  = vector<TimeElapsed>(nthrds);
  stat1SoFar = vector<TimeElapsed>(nthrds);
  stat2SoFar = vector<TimeElapsed>(nthrds);
  stat3SoFar = vector<TimeElapsed>(nthrds);
  coolSoFar  = vector<TimeElapsed>(nthrds);
  collCnt    = vector<int>(nthrds, 0);
  
  EPSMT      = vector< vector<Timer> >(nthrds);
  EPSMTSoFar = vector< vector<TimeElapsed> >(nthrds);
  for (int n=0; n<nthrds; n++) {
    EPSMT[n] = vector<Timer>(nEPSMT);
    for (int i=0; i<nEPSMT; i++) EPSMT[n][i].Microseconds();
    EPSMTSoFar[n] = vector<TimeElapsed>(nEPSMT);
  }  
  
  for (int n=0; n<nthrds; n++) {
    listTime [n].Microseconds();
    initTime [n].Microseconds();
    collTime [n].Microseconds();
    elasTime [n].Microseconds();
    stat1Time[n].Microseconds();
    stat2Time[n].Microseconds();
    stat3Time[n].Microseconds();
    coolTime [n].Microseconds();
    cellTime [n].Microseconds();
    curcTime [n].Microseconds();
    epsmTime [n].Microseconds();
  }
  
  if (TSDIAG) {
    // Accumulate distribution log ratio of flight time to time step
    tdiag  = vector<unsigned>(numdiag, 0);
    tdiag1 = vector<unsigned>(numdiag, 0);
    tdiag0 = vector<unsigned>(numdiag, 0);
    tdiagT = vector< vector<unsigned> > (nthrds);
    
    // Accumulate distribution log energy overruns
    Eover  = vector<double>(numdiag, 0);
    Eover1 = vector<double>(numdiag, 0);
    Eover0 = vector<double>(numdiag, 0);
    EoverT = vector< vector<double> > (nthrds);
  }
  
  // Accumulate the ratio cooling time to time step each cell
  tcool  = vector<unsigned>(numdiag, 0);
  tcool1 = vector<unsigned>(numdiag, 0);
  tcool0 = vector<unsigned>(numdiag, 0);
  tcoolT = vector< vector<unsigned> > (nthrds);
  
  if (VOLDIAG) {
    Vcnt  = vector<unsigned>(nbits, 0);
    Vcnt1 = vector<unsigned>(nbits, 0);
    Vcnt0 = vector<unsigned>(nbits, 0);
    VcntT = vector< vector<unsigned> > (nthrds);
    Vdbl  = vector<double  >(nbits*nvold, 0.0);
    Vdbl1 = vector<double  >(nbits*nvold, 0.0);
    Vdbl0 = vector<double  >(nbits*nvold, 0.0);
    VdblT = vector< vector<double> >(nthrds);
  }
  
  for (int n=0; n<nthrds; n++) {
    if (TSDIAG) {
      tdiagT[n] = vector<unsigned>(numdiag, 0);
      EoverT[n] = vector<double>(numdiag, 0);
    }
    if (VOLDIAG) {
      VcntT[n] = vector<unsigned>(nbits, 0);
      VdblT[n] = vector<double>(nbits*nvold, 0.0);
    }
    tcoolT[n] = vector<unsigned>(numdiag, 0);
    tdispT[n] = vector<double>(3, 0);
  }
  
  disptot = vector<double>(3, 0);
  masstot = 0.0;
  
  use_Eint = -1;
  use_temp = -1;
  use_dens = -1;
  use_delt = -1;
  use_exes = -1;
  use_Kn   = -1;
  use_St   = -1;
  
  gen  = new ACG     (11+myid);
  unit = new Uniform (0.0, 1.0, gen);
  norm = new Normal  (0.0, 1.0, gen);
  
  if (MFPDIAG) {
    prec = vector<Precord>(nthrds);
    for (int n=0; n<nthrds; n++)
      prec[n].second = vector<double>(Nmfp, 0);
  }
  
  if (VERBOSE>5) {
    tv_list    = vector<struct timeval>(nthrds);
    timer_list = vector<double>(2*nthrds);
  }
  
  forkSum  = vector<double>(3);
  snglSum  = vector<double>(3);
  waitSum  = vector<double>(3);
  diagSum  = vector<double>(3);
  joinSum  = vector<double>(3);
  
  if (TIMING) {
    listSum  = vector<double>(3);
    initSum  = vector<double>(3);
    collSum  = vector<double>(3);
    elasSum  = vector<double>(3);
    cellSum  = vector<double>(3);
    epsmSum  = vector<double>(3);
    stat1Sum = vector<double>(3);
    stat2Sum = vector<double>(3);
    stat3Sum = vector<double>(3);
    coolSum  = vector<double>(3);
    numbSum  = vector<int   >(3);
  }
  
  EPSMtime = vector<long>(nEPSMT);
  CPUH = vector<long>(12);
  
  // Debug maximum work per cell
  //
  minUsage = vector<long>(nthrds*2, MAXLONG);
  maxUsage = vector<long>(nthrds*2, 0);
  minPart  = vector<long>(nthrds*2, -1);
  maxPart  = vector<long>(nthrds*2, -1);
  minCollP = vector<long>(nthrds*2, -1);
  maxCollP = vector<long>(nthrds*2, -1);
  
  effortAccum  = false;
  effortNumber = vector< list< pair<long, unsigned> > >(nthrds);

}

Collide::~Collide()
{
  delete gen;
  delete unit;
  delete norm;
}

void Collide::debug_list(pHOT& tree)
{
  return;
  
  unsigned ncells = tree.Number();
  pHOT_iterator c(tree);
  for (int cid=0; cid<numprocs; cid++) {
    if (myid == cid) {
      ostringstream sout;
      sout << "==== Collide " << myid << " ncells=" << ncells;
      cout << setw(70) << setfill('=') << left << sout.str() 
	   << endl << setfill(' ');
      
      for (int n=0; n<nthrds; n++) {
	int nbeg = ncells*(n  )/nthrds;
	int nend = ncells*(n+1)/nthrds;
	for (int j=nbeg; j<nend; j++) {
	  int tnum = c.nextCell();
	  cout << setw(8)  << j
	       << setw(12) << c.Cell()
	       << setw(12) << cellist[n][j-nbeg]
	       << setw(12) << c.Cell()->bods.size()
	       << setw(12) << tnum << endl;
	}
      }
    }
    MPI_Barrier(MPI_COMM_WORLD);
  }
}


unsigned Collide::collide(pHOT& tree, sKeyDmap& Fn, 
			  double tau, int mlevel, bool diag)
{
  snglTime.start();
  // Initialize diagnostic counters
  // 
  if (diag) pre_collide_diag();
  
  // Make cellist
  // 
  for (int n=0; n<nthrds; n++) cellist[n].clear();
  ncells = 0;
  set<pCell*>::iterator ic, icb, ice;
  
  // For debugging
  unsigned nullcell = 0, totalcell = 0;
  
  for (unsigned M=mlevel; M<=multistep; M++) {
    
    // Don't queue null cells
    if (tree.CLevels(M).size()) {
      icb = tree.CLevels(M).begin(); 
      ice = tree.CLevels(M).end(); 
      for (ic=icb; ic!=ice; ic++) {
	if ((*ic)->bods.size()) {
	  cellist[(ncells++)%nthrds].push_back(*ic);
	  bodycount += (*ic)->bods.size();
	} else {
	  nullcell++;
	}
	totalcell++;
      }
    }
  }
  stepcount++;
  
  if (DEBUG) {
    if (nullcell)
      std::cout << "DEBUG: null cells " << nullcell << "/" 
		<< totalcell << std::endl;
    
    debug_list(tree);
  }
  snglTime.stop();
  
  // Needed for meaningful timing results
  waitTime.start();
  MPI_Barrier(MPI_COMM_WORLD);
  waitTime.stop();
  
  // For effort debugging
  if (mlevel==0) effortAccum = true;
  
  forkTime.start();
  if (0) {
    ostringstream sout;
    sout << "before fork, " << __FILE__ << ": " << __LINE__;
    tree.checkBounds(2.0, sout.str().c_str());
  }
  collide_thread_fork(&tree, &Fn, tau);
  if (0) {
    ostringstream sout;
    sout << "after fork, " << __FILE__ << ": " << __LINE__;
    tree.checkBounds(2.0, sout.str().c_str());
  }
  forkSoFar = forkTime.stop();
  
  snglTime.start();
  unsigned col = 0;
  if (diag) col = post_collide_diag();
  snglSoFar = snglTime.stop();
  
  // Effort diagnostics
  //
  if (mlevel==0 && EFFORT && effortAccum) {
    
    // Write to the file in process order
    list< pair<long, unsigned> >::iterator it;
    ostringstream ostr;
    ostr << outdir << runtag << ".collide.effort";
    for (int i=0; i<numprocs; i++) {
      if (myid==i) {
	ofstream out(ostr.str().c_str(), ios::app);
	if (out) {
	  if (myid==0) out << "# Time=" << tnow << endl;
	  for (int n=0; n<nthrds; n++) {
	    for (it=effortNumber[n].begin(); it!=effortNumber[n].end(); it++)
	      out << setw(12) << it->first << setw(12) << it->second << endl;
	  }
	} else {
	  cerr << "Process " << myid 
	       << ": error opening <" << ostr.str() << ">" << endl;
	}
      }
      MPI_Barrier(MPI_COMM_WORLD);
    }
    // Reset the list
    effortAccum = false;
    for (int n=0; n<nthrds; n++) 
      effortNumber[n].erase(effortNumber[n].begin(), effortNumber[n].end());
  }
  
  caller->print_timings("Collide: collision thread timings", timer_list);
  
  return( col );
}

void Collide::dispersion(vector<double>& disp)
{
  disp = disptot;
  if (masstot>0.0) {
    for (unsigned k=0; k<3; k++) disp[k] /= masstot;
  }
  for (unsigned k=0; k<3; k++) disptot[k] = 0.0;
  masstot = 0.0;
}


void * Collide::collide_thread(void * arg)
{
  pHOT *tree    = static_cast<pHOT*>    (((thrd_pass_arguments*)arg)->tree);
  sKeyDmap *Fn  = static_cast<sKeyDmap*>(((thrd_pass_arguments*)arg)->fn  );
  double tau    = static_cast<double>   (((thrd_pass_arguments*)arg)->tau );
  int id        = static_cast<int>      (((thrd_pass_arguments*)arg)->id  );
  
  thread_timing_beg(id);
  
  cellTime[id].start();
  
  // Loop over cells, processing collisions in each cell
  //
  for (unsigned j=0; j<cellist[id].size(); j++ ) {
    
#ifdef USE_GPTL
    GPTLstart("Collide::bodylist");
#endif
    
    int EPSMused = 0;
    
    // Start the effort time
    //
    curcTime[id].reset();
    curcTime[id].start();
    listTime[id].start();
    
    // Number of particles in this cell
    //
    pCell *c = cellist[id][j];
    unsigned number = c->bods.size();
    numcntT[id].push_back(number);
    
#ifdef USE_GPTL
    GPTLstop("Collide::bodylist");
#endif
    
    // Skip cells with only one particle
    //
    if ( number < 2 ) {
      colcntT[id].push_back(0);
      // Stop timers
      curcTime[id].stop();
      listSoFar[id] = listTime[id].stop();
      // Skip to the next cell
      continue;
    }
    
#ifdef USE_GPTL
    GPTLstart("Collide::prelim");
    GPTLstart("Collide::energy");
#endif
    
    listSoFar[id] = listTime[id].stop();
    stat1Time[id].start();
    
    // Energy lost in this cell
    //
    decolT[id] = 0.0;
    decelT[id] = 0.0;
    
    // Compute 1.5 times the mean relative velocity in each MACRO cell
    //
    sCell *samp = c->sample;
    sCell::dPair ntcF(1, 1);
    //
    // Sanity check
    //
    if (samp == 0x0) {
      cout << "Process "  << myid << " in collide: no sample cell"
	   << ", owner="   << c->owner << hex
	   << ", mykey="   << c->mykey
	   << ", mask="    << c->mask  << dec
	   << ", level="   << c->level    
	   << ", Count="   << c->ctotal
	   << ", maxplev=" << c->maxplev;
      if (tree->onFrontier(c->mykey)) cout << ", ON frontier" << endl;
      else cout << ", NOT on frontier" << endl;
      
    } else {
      ntcF = samp->VelCrsAvg();
    }

    sCell::dPair ntcFmax(1, 1);

    double crm = 0.0;
    
    if (samp) {
      if (samp->stotal[0]>0.0) {
	for (unsigned k=0; k<3; k++) {
	  crm += (samp->stotal[1+k] - 
		  samp->stotal[4+k]*samp->stotal[4+k]/samp->stotal[0])
	    /samp->stotal[0];}
      }
      crm  = sqrt(2.0*crm);

      if (NTC) crm *= ntcF.first;
    }
    
    stat1SoFar[id] = stat1Time[id].stop();
    stat2Time [id].start();
    
#ifdef USE_GPTL
    GPTLstop ("Collide::energy");
    GPTLstart("Collide::mfp");
#endif
    
    // KE in the cell
    //
    double kedsp=0.0;
    if (MFPDIAG) {
      if (c->stotal[0]>0.0) {
	for (unsigned k=0; k<3; k++) 
	  kedsp += 
	    0.5*(c->stotal[1+k] - c->stotal[4+k]*c->stotal[4+k]/c->stotal[0]);
      }
    }
    
    // Volume in the cell
    //
    double volc = c->Volume();
    
    // Mass in the cell
    //
    double mass = c->Mass();
    
    // Mass density in the cell
    double dens = mass/volc;
    
    if (mass <= 0.0) continue;
    
    // Cell length
    //
    double cL = pow(volc, 0.33333333);
    
    
    // Cell initialization (generate cross sections)
    //
    initialize_cell(tree, c, crm, id);


    // Per species quantities
    //
    double              meanLambda, meanCollP, totalNsel;
    sKey2Dmap           crossIJ = totalCrossSections(id);
    sKey2Umap           nselM   = generateSelection(c, Fn, crm, tau, id, 
						    meanLambda, meanCollP, totalNsel);
    

#ifdef USE_GPTL
    GPTLstop ("Collide::mfp");
#endif
    
#ifdef USE_GPTL
    GPTLstart("Collide::mfp_diag");
#endif
    
    if (MFPDIAG) {
      
      // Diagnostics
      //
      tsratT[id].push_back(crm*tau/pow(volc,0.33333333));
      tdensT[id].push_back(dens);
      tvolcT[id].push_back(volc);
      
      double posx, posy, posz;
      c->MeanPos(posx, posy, posz);
      
      // MFP/side = MFP/vol^(1/3)
      
      prec[id].first = meanLambda/pow(volc, 0.33333333);
      prec[id].second[0] = sqrt(posx*posx + posy*posy);
      prec[id].second[1] = posz;
      prec[id].second[2] = sqrt(posx*posx+posy*posy*+posz*posz);
      prec[id].second[3] = mass/volc;
      prec[id].second[4] = volc;
      
      tmfpstT[id].push_back(prec[id]);
    }
    
    // Ratio of cell size to mean free path
    //
    double mfpCL = cL/meanLambda;
    
    if (TSDIAG) {		// Diagnose time step in this cell
      double vmass;
      vector<double> V1, V2;
      c->Vel(vmass, V1, V2);
      double scale = c->Scale();
      double taudiag = 1.0e40;
      for (int k=0; k<3; k++) {	// Time of flight
	taudiag = min<double>
	  (pHOT::sides[k]*scale/(fabs(V1[k]/vmass)+sqrt(V2[k]/vmass)+1.0e-40), 
	   taudiag);
      }
      
      int indx = (int)floor(log(taudiag/tau)/log(4.0) + 5);
      if (indx<0 ) indx = 0;
      if (indx>10) indx = 10;
      tdiagT[id][indx]++;
    }
    
    if (VOLDIAG) {
      if (c->level<nbits) {
	VcntT[id][c->level]++;
	VdblT[id][c->level*nvold+0] += dens;
	VdblT[id][c->level*nvold+1] += 1.0 / mfpCL;
	VdblT[id][c->level*nvold+2] += meanCollP;
	VdblT[id][c->level*nvold+3] += crm*tau / cL;
	VdblT[id][c->level*nvold+4] += number;
	VdblT[id][c->level*nvold+5] += number*number;
      }
    }
    // Selection number per particle
    if (MFPDIAG)
      tselnT[id].push_back(totalNsel/number);
    
    stat2SoFar[id] = stat2Time[id].stop();
    
    // Species map for collisions

    unsigned colc = 0;
    std::map<speciesKey, std::vector<unsigned long> > bmap;

#ifdef USE_GPTL
    GPTLstop ("Collide::mfp_diag");
    GPTLstart("Collide::cell_init");
#endif
    
    
#ifdef USE_GPTL
    GPTLstop("Collide::cell_init");
    GPTLstop("Collide::prelim");
#endif
    // No collisions, primarily for testing . . .
    if (DRYRUN) continue;
    
    collTime[id].start();
    collCnt[id]++;
    // Number of collisions per particle:
    // assume equipartition if large
    if (use_epsm && meanCollP > EPSMratio && number > EPSMmin) {
      
      EPSMused = 1;
      
#ifdef USE_GPTL
      GPTLstart("Collide::cell_init");
#endif
      initTime[id].start();
      initialize_cell_epsm(tree, c, nselM, crm, tau, id);
      initSoFar[id] = initTime[id].stop();
      
#ifdef USE_GPTL
      GPTLstop("Collide::cell_init");
      GPTLstart("Collide::EPSM");
#endif
      epsmTime[id].start();
      EPSM(tree, c, id);
      epsmSoFar[id] = epsmTime[id].stop();
#ifdef USE_GPTL

      GPTLstop ("Collide::EPSM");
#endif
      
    } else {
      
#ifdef USE_GPTL
      GPTLstart("Collide::cell_init");
#endif
      initTime[id].start();
      initialize_cell_dsmc(tree, c, nselM, crm, tau, id);
      initSoFar[id] = initTime[id].stop();
      
#ifdef USE_GPTL
      GPTLstop("Collide::cell_init");
      GPTLstart("Collide::inelastic");
#endif
      for (size_t k=0; k<c->bods.size(); k++) {
	unsigned long kk = c->bods[k];
	Particle* p = tree->Body(kk);

	speciesKey skey = defaultKey;
	if (use_key>=0) skey = KeyConvert(p->iattrib[use_key]).getKey();

	bmap[skey].push_back(kk);
      }
    }
    
    sKeyUmap::iterator  it1, it2;
    int totalCount = 0;
    
    for (it1=c->count.begin(); it1!=c->count.end(); it1++) {
      speciesKey i1 = it1->first;
      size_t num1 = bmap[i1].size();
      if (num1==0) continue;
      
      for (it2=it1; it2!=c->count.end(); it2++) {
	speciesKey i2 = it2->first;
	size_t num2 = bmap[i2].size();

	if (num2==0) continue;
	
	if (i1==i2 && num2==1) continue;

	// Loop over total number of candidate collision pairs
	//
	for (unsigned i=0; i<nselM[i1][i2]; i++ ) {
	  
	  totalCount++;
	  
	  // Pick two particles at random out of this cell. l1 and l2
	  // are indices in the bmap[i1] and bmap[i2] vectors of body
	  // indices

	  size_t l1, l2;

	  l1 = static_cast<size_t>(floor((*unit)()*num1));
	  l1 = std::min<size_t>(l1, num1-1);
	  
	  if (i1 == i2) {
	    l2 = static_cast<size_t>(floor((*unit)()*(num2-1)));
	    l2 = std::min<size_t>(l2, num2-2);
				// Get random l2 != l1
	    l2 = (l2 + l1 + 1) % num2;
	  } else {
	    l2 = static_cast<size_t>(floor((*unit)()*num2));
	    l2 = std::min<size_t>(l2, num2-1);
	  }
	  
	  // Get index from body map for the cell
	  //
	  Particle* p1 = tree->Body(bmap[i1][l1]);
	  Particle* p2 = tree->Body(bmap[i2][l2]);
	  
	  // Calculate pair's relative speed (pre-collision)
	  //
	  vector<double> crel(3);
	  double cr = 0.0;
	  for (int k=0; k<3; k++) {
	    crel[k] = p1->vel[k] - p2->vel[k];
	    cr += crel[k]*crel[k];
	  }
	  cr = sqrt(cr);

	  // No point in inelastic collsion for zero velocity . . . 
	  //
	  if (cr == 0.0) continue;

	  // Accept or reject candidate pair according to relative speed
	  //
	  bool   ok   = false;
	  double cros = crossSection(tree, p1, p2, cr, id);
	  double crat = cros/(ntcF.second*crossIJ[i1][i2]);
	  double vrat = cr/crm;
	  double targ = vrat * crat;

	  if (NTC)
	    ok = ( targ > (*unit)() );
	  else
	    ok = true;
	  

	  // Update v_max and cross_max
	  //
	  if (NTC) {
	    ntcFmax.first  = std::max<double>(ntcFmax.first,  vrat*ntcF.first );
	    ntcFmax.second = std::max<double>(ntcFmax.second, crat*ntcF.second);
	  }

	  if (ok) {

	    elasTime[id].start();
	    
	    // If pair accepted, select post-collision velocities
	    //
	    colc++;			// Collision counter
	    
	    // Do inelastic stuff
	    //
	    error1T[id] += inelastic(tree, p1, p2, &cr, id);
	    
	    // Update the particle velocity
	    //
	    velocityUpdate(p1, p2, cr);

	  } // Inelastic computation

	} // Loop over trial pairs
	
      } // Next species 2 particle

    } // Next species 1 particle
    
    elasSoFar[id] = elasTime[id].stop();
    
#pragma omp critical
    if (NTC) samp->VelCrsAdd(ntcFmax);

    // Count collisions
    //
    colcntT[id].push_back(colc);
    sel1T[id] += totalCount;
    col1T[id] += colc;
      
#ifdef USE_GPTL
    GPTLstop("Collide::inelastic");
#endif
    collSoFar[id] = collTime[id].stop();
  
#ifdef USE_GPTL
    GPTLstart("Collide::diag");
#endif
  
    // Compute dispersion diagnostics
    //
    stat3Time[id].start();
  
    double tmass = 0.0;
    vector<double> velm(3, 0.0), velm2(3, 0.0);
    for (unsigned j=0; j<number; j++) {
      Particle* p = tree->Body(c->bods[j]);
      for (unsigned k=0; k<3; k++) {
	velm[k]  += p->mass*p->vel[k];
	velm2[k] += p->mass*p->vel[k]*p->vel[k];
      }
      tmass += p->mass;
    }
  
    if (tmass>0.0) {
      for (unsigned k=0; k<3; k++) {
	
	velm[k] /= tmass;
	velm2[k] = velm2[k] - velm[k]*velm[k]*tmass;
	if (velm2[k]>0.0) {
	  tdispT[id][k] += velm2[k];
	  tmassT[id]    += tmass;
	}
      }
    }
    
    //
    // General hook for the derived classes for specific diagnostics
    //
  
    finalize_cell(tree, c, kedsp, id);
  
    stat3SoFar[id] = stat3Time[id].stop();
  
    //
    // Compute Knudsen and/or Strouhal number
    //
    if (use_Kn>=0 || use_St>=0) {
      double cL = pow(volc, 0.33333333);
      double Kn = meanLambda/cL;
      double St = cL/fabs(tau*ntcF.first);
      for (unsigned j=0; j<number; j++) {
	Particle* p = tree->Body(c->bods[j]);
	if (use_Kn>=0) p->dattrib[use_Kn] = Kn;
	if (use_St>=0) p->dattrib[use_St] = St;
      }
    }
    
#ifdef USE_GPTL
    GPTLstop("Collide::diag");
#endif
  
    // Record effort per particle in microseconds
    //
    curcSoFar[id] = curcTime[id].stop();
    long tt = curcSoFar[id].getRealTime();
    if (EFFORT) {
      if (effortAccum) 
	effortNumber[id].push_back(pair<long, unsigned>(tt, number));
      double effort = static_cast<double>(tt)/number;
      for (unsigned k=0; k<number; k++) 
	tree->Body(c->bods[k])->effort += effort;
    }
  
    // Usage debuging
    //
    if (tt==0) { 
      cout << "T=0" << ", precision=" 
	   << (curcTime[id].Precision() ? "microseconds" : "seconds")
	   << endl;
    }
    if (minUsage[id*2+EPSMused] > tt) {
      minUsage[id*2+EPSMused] = tt;
      minPart [id*2+EPSMused] = number;
      minCollP[id*2+EPSMused] = meanCollP;
    }
    if (maxUsage[id*2+EPSMused] < tt) {
      maxUsage[id*2+EPSMused] = tt;
      maxPart [id*2+EPSMused] = number;
      maxCollP[id*2+EPSMused] = meanCollP;
    }
    
  } // Loop over cells

  cellSoFar[id] = cellTime[id].stop();

  thread_timing_end(id);
  
  return (NULL);
}


unsigned Collide::medianNumber() 
{
  MPI_Status s;
  
  if (myid==0) {
    unsigned num;
    for (int n=1; n<numprocs; n++) {
      MPI_Recv(&num, 1, MPI_UNSIGNED, n, 39, MPI_COMM_WORLD, &s);
      vector<unsigned> tmp(num);
      MPI_Recv(&tmp[0], num, MPI_UNSIGNED, n, 40, MPI_COMM_WORLD, &s);
      numcnt.insert(numcnt.end(), tmp.begin(), tmp.end());
    }
    
    std::sort(numcnt.begin(), numcnt.end()); 
    
    if (EXTRA) {
      string file = outdir + "tmp.numcnt";
      ofstream out(file.c_str());
      for (unsigned j=0; j<numcnt.size(); j++)
	out << setw(8) << j << setw(18) << numcnt[j] << endl;
    }


    
    if (numcnt.size()) 
      return numcnt[numcnt.size()/2]; 
    else
      return 0;
    
  } else {
    unsigned num = numcnt.size();
    MPI_Send(&num, 1, MPI_UNSIGNED, 0, 39, MPI_COMM_WORLD);
    MPI_Send(&numcnt[0], num, MPI_UNSIGNED, 0, 40, MPI_COMM_WORLD);
    
    return 0;
  }
}

unsigned Collide::medianColl() 
{ 
  MPI_Status s;
  
  if (myid==0) {
    unsigned num;
    vector<unsigned> coltmp(colcnt);
    for (int n=1; n<numprocs; n++) {
      MPI_Recv(&num, 1, MPI_UNSIGNED, n, 39, MPI_COMM_WORLD, &s);
      vector<unsigned> tmp(num);
      MPI_Recv(&tmp[0], num, MPI_UNSIGNED, n, 40, MPI_COMM_WORLD, &s);
      coltmp.insert(coltmp.end(), tmp.begin(), tmp.end());
    }
    
    std::sort(coltmp.begin(), coltmp.end()); 
    
    if (EXTRA) {
      ostringstream ostr;
      ostr << outdir << runtag << ".colcnt";
      ofstream out(ostr.str().c_str());
      for (unsigned j=0; j<coltmp.size(); j++)
	out << setw(8) << j << setw(18) << coltmp[j] << endl;
    }
    
    return coltmp[coltmp.size()/2]; 
    
  } else {
    unsigned num = colcnt.size();
    MPI_Send(&num, 1, MPI_UNSIGNED, 0, 39, MPI_COMM_WORLD);
    MPI_Send(&colcnt[0], num, MPI_UNSIGNED, 0, 40, MPI_COMM_WORLD);
    return 0;
  }
  
}

void Collide::collQuantile(vector<double>& quantiles, vector<double>& coll_)
{
  MPI_Status s;
  
  if (myid==0) {
    unsigned num;
    vector<unsigned> coltmp(colcnt);
    for (int n=1; n<numprocs; n++) {
      MPI_Recv(&num, 1, MPI_UNSIGNED, n, 39, MPI_COMM_WORLD, &s);
      vector<unsigned> tmp(num);
      MPI_Recv(&tmp[0], num, MPI_UNSIGNED, n, 40, MPI_COMM_WORLD, &s);
      coltmp.insert(coltmp.end(), tmp.begin(), tmp.end());
    }
    
    std::sort(coltmp.begin(), coltmp.end()); 
    
    coll_ = vector<double>(quantiles.size(), 0);
    if (coltmp.size()) {
      for (unsigned j=0; j<quantiles.size(); j++)
	coll_[j] = coltmp[Qi(quantiles[j],coltmp.size())];
    }
    
    ostringstream ostr;
    ostr << outdir << runtag << ".coll_counts";
    ifstream in(ostr.str().c_str());
    in.close();
    if (in.fail()) {
      ofstream out(ostr.str().c_str());
      out << left
	  << setw(14) << "# Time" 
	  << setw(10) << "Quantiles"
	  << setw(10) << "Counts"
	  << endl;
    }
    
    ofstream out(ostr.str().c_str(), ios::app);
    for (unsigned j=0; j<quantiles.size(); j++) {
      out << setw(14) << tnow
	  << setw(10) << quantiles[j] 
	  << setw(10) << coll_[j] << endl;
    }
    out << endl;
    
  } else {
    unsigned num = colcnt.size();
    MPI_Send(&num, 1, MPI_UNSIGNED, 0, 39, MPI_COMM_WORLD);
    MPI_Send(&colcnt[0], num, MPI_UNSIGNED, 0, 40, MPI_COMM_WORLD);
  }
}

void Collide::mfpsizeQuantile(vector<double>& quantiles, 
			      vector<double>& mfp_, 
			      vector<double>& ts_,
			      vector<double>& coll_,
			      vector<double>& cool_,
			      vector<double>& rate_,
			      unsigned &collnum, unsigned &coolnum) 
{
  if (!MFPDIAG) return;
  
  MPI_Status s;
  
  if (myid==0) {
    unsigned nmt, nmb, num;
    
    // Temporaries so we don't touch the
    // root node's counters
    
    vector<Precord> phsI(tphase), mfpI(tmfpst);
    vector<double>  ratI(tsrat), denI(tdens), volI(tvolc);
    vector<double>  temI(ttemp), selI(tseln), kerI(kerat);
    vector<double>  delI(tdelt), derI(derat);
    
    
    for (int n=1; n<numprocs; n++) {
      MPI_Recv(&nmt, 1, MPI_UNSIGNED, n, 37, MPI_COMM_WORLD, &s);
      MPI_Recv(&nmb, 1, MPI_UNSIGNED, n, 38, MPI_COMM_WORLD, &s);
      MPI_Recv(&num, 1, MPI_UNSIGNED, n, 39, MPI_COMM_WORLD, &s);
      
      vector<double> tmb(nmb), tmt(nmt), tmp(num);
      
      MPI_Recv(&tmp[0], num, MPI_DOUBLE, n, 40, MPI_COMM_WORLD, &s);
      ratI.insert(ratI.end(), tmp.begin(), tmp.end());
      
      MPI_Recv(&tmp[0], num, MPI_DOUBLE, n, 41, MPI_COMM_WORLD, &s);
      denI.insert(denI.end(), tmp.begin(), tmp.end());
      
      MPI_Recv(&tmp[0], num, MPI_DOUBLE, n, 42, MPI_COMM_WORLD, &s);
      volI.insert(volI.end(), tmp.begin(), tmp.end());
      
      MPI_Recv(&tmp[0], nmt, MPI_DOUBLE, n, 43, MPI_COMM_WORLD, &s);
      temI.insert(temI.end(), tmp.begin(), tmp.end());
      
      MPI_Recv(&tmp[0], nmt, MPI_DOUBLE, n, 44, MPI_COMM_WORLD, &s);
      delI.insert(delI.end(), tmp.begin(), tmp.end());
      
      MPI_Recv(&tmp[0], num, MPI_DOUBLE, n, 45, MPI_COMM_WORLD, &s);
      selI.insert(selI.end(), tmp.begin(), tmp.end());
      
      MPI_Recv(&tmb[0], nmb, MPI_DOUBLE, n, 46, MPI_COMM_WORLD, &s);
      kerI.insert(kerI.end(), tmb.begin(), tmb.end());
      
      MPI_Recv(&tmb[0], nmb, MPI_DOUBLE, n, 47, MPI_COMM_WORLD, &s);
      derI.insert(derI.end(), tmb.begin(), tmb.end());
      
      vector<Precord> tmp2(nmt), tmp3(num), phsI(tphase);
      
      MPI_Recv(&tmt[0], nmt, MPI_DOUBLE, n, 48, MPI_COMM_WORLD, &s);
      for (unsigned k=0; k<nmt; k++) {
	// Load density
	tmp2[k].first = tmt[k];
	// Initialize record
	tmp2[k].second = vector<double>(Nphase, 0);
      }
      for (unsigned l=0; l<Nphase; l++) {
	MPI_Recv(&tmp[0], nmt, MPI_DOUBLE, n, 49+l, MPI_COMM_WORLD, &s);
	for (unsigned k=0; k<nmt; k++) tmp2[k].second[l] = tmp[k];
      }
      phsI.insert(phsI.end(), tmp2.begin(), tmp2.end());
      
      MPI_Recv(&tmp[0], num, MPI_DOUBLE, n, 49+Nphase, MPI_COMM_WORLD, &s);
      for (unsigned k=0; k<num; k++) {
	// Load mfp
	tmp3[k].first = tmp[k];
	// Initialize record
	tmp3[k].second = vector<double>(Nmfp, 0);
      }
      for (unsigned l=0; l<Nmfp; l++) {
	MPI_Recv(&tmp[0], num, MPI_DOUBLE, n, 50+Nphase+l, MPI_COMM_WORLD, &s);
	for (unsigned k=0; k<num; k++) tmp3[k].second[l] = tmp[k];
      }
      mfpI.insert(mfpI.end(), tmp3.begin(), tmp3.end());
    }
    
    // Sort the counters in prep for quantiles
    // 
    std::sort(ratI.begin(),  ratI.end()); 
    std::sort(denI.begin(),  denI.end()); 
    std::sort(volI.begin(),  volI.end()); 
    std::sort(temI.begin(),  temI.end()); 
    std::sort(delI.begin(),  delI.end()); 
    std::sort(selI.begin(),  selI.end()); 
    std::sort(phsI.begin(),  phsI.end());
    std::sort(mfpI.begin(),  mfpI.end());
    std::sort(kerI.begin(),  kerI.end());
    std::sort(derI.begin(),  derI.end());
    
    collnum = selI.size();
    coolnum = kerI.size();
    
    mfp_  = vector<double>(quantiles.size());
    ts_   = vector<double>(quantiles.size());
    coll_ = vector<double>(quantiles.size());
    cool_ = vector<double>(quantiles.size());
    rate_ = vector<double>(quantiles.size());
    for (unsigned j=0; j<quantiles.size(); j++) {
      if (mfpI.size())
	mfp_[j]  = mfpI [Qi(quantiles[j],mfpI.size())].first;
      else
	mfp_[j] = 0;
      if (ratI.size())
	ts_[j]   = ratI [Qi(quantiles[j],ratI.size()) ];
      else
	ts_[j]   = 0;
      if (selI.size())
	coll_[j] = selI [Qi(quantiles[j],selI.size()) ];
      else
	coll_[j] = 0;
      if (kerI.size())
	cool_[j] = ratI [Qi(quantiles[j],kerI.size()) ];
      else
	cool_[j] = 0;
      if (derI.size())
	rate_[j] = derI [Qi(quantiles[j],derI.size()) ];
      else
	rate_[j] = 0;
    }
    
    if (SORTED) {
      ostringstream ostr;
      ostr << outdir << runtag << ".collide." << this_step;
      ofstream out(ostr.str().c_str());
      out << left << setw(8) << "# N" // Header
	  << setw(18) << "| MFP/L"
	  << setw(18) << "| Cyl radius (MFP)"
	  << setw(18) << "| Vertical (MFP)"
	  << setw(18) << "| Sph radius (MFP)"
	  << setw(18) << "| Density(MFP)"
	  << setw(18) << "| Volume(MFP)"
	  << setw(18) << "| TOF/TS"
	  << setw(18) << "| Density"
	  << setw(18) << "| Cell vol"
	  << setw(18) << "| Cell temp"
	  << setw(18) << "| Cool/part"
	  << setw(18) << "| Number/Nsel"
	  << endl;
      out << "# " << setw(6) << 1;
      for (unsigned k=2; k<13; k++) out << "| " << setw(16) << k;
      out << endl;
      cout << "SORTED: " << mfpI.size() << " cells" << endl;
      for (unsigned j=0; j<mfpI.size(); j++)
	out << setw(8) << j 
	    << setw(18) << mfpI[j].first
	    << setw(18) << mfpI[j].second[0]
	    << setw(18) << mfpI[j].second[1]
	    << setw(18) << mfpI[j].second[2]
	    << setw(18) << mfpI[j].second[3]
	    << setw(18) << mfpI[j].second[4]
	    << setw(18) << ratI[j] 
	    << setw(18) << denI[j] 
	    << setw(18) << volI[j] 
	    << setw(18) << temI[j] 
	    << setw(18) << delI[j] 
	    << setw(18) << selI[j] 
	    << endl;
      
      out << flush;
      out.close();
    }
    
    
    if (PHASE) {
      ostringstream ostr;
      ostr << outdir << runtag << ".phase." << this_step;
      ofstream out(ostr.str().c_str());
      out << left << setw(8) << "# N" // Header
	  << setw(18) << "| Density"
	  << setw(18) << "| Temp"
	  << setw(18) << "| Number"
	  << setw(18) << "| Mass"
	  << setw(18) << "| Volume"
	  << endl;
      out << "# " << setw(6) << 1;
      for (unsigned k=2; k<7; k++) out << "| " << setw(16) << k;
      out << endl;
      for (unsigned j=0; j<phsI.size(); j++) {
	out << setw(8) << j << setw(18) << phsI[j].first;
	for (unsigned k=0; k<Nphase; k++) 
	  out << setw(18) << phsI[j].second[k];
	out << endl;
      }
    }
    
  } else {
    unsigned num = tmfpst.size();
    unsigned nmt = tdelt.size();
    unsigned nmb = derat.size();
    
    MPI_Send(&nmt, 1, MPI_UNSIGNED, 0, 37, MPI_COMM_WORLD);
    MPI_Send(&nmb, 1, MPI_UNSIGNED, 0, 38, MPI_COMM_WORLD);
    MPI_Send(&num, 1, MPI_UNSIGNED, 0, 39, MPI_COMM_WORLD);
    MPI_Send(&tsrat[0],  num, MPI_DOUBLE, 0, 40, MPI_COMM_WORLD);
    MPI_Send(&tdens[0],  num, MPI_DOUBLE, 0, 41, MPI_COMM_WORLD);
    MPI_Send(&tvolc[0],  num, MPI_DOUBLE, 0, 42, MPI_COMM_WORLD);
    MPI_Send(&ttemp[0],  nmt, MPI_DOUBLE, 0, 43, MPI_COMM_WORLD);
    MPI_Send(&tdelt[0],  nmt, MPI_DOUBLE, 0, 44, MPI_COMM_WORLD);
    MPI_Send(&tseln[0],  num, MPI_DOUBLE, 0, 45, MPI_COMM_WORLD);
    MPI_Send(&kerat[0],  nmb, MPI_DOUBLE, 0, 46, MPI_COMM_WORLD);
    MPI_Send(&derat[0],  nmb, MPI_DOUBLE, 0, 47, MPI_COMM_WORLD);
    
    vector<double> tmt(nmt), tmp(num);
    
    for (unsigned k=0; k<nmt; k++) tmt[k] = tphase[k].first;
    MPI_Send(&tmt[0], nmt, MPI_DOUBLE, 0, 48, MPI_COMM_WORLD);
    for (unsigned l=0; l<Nphase; l++) {
      for (unsigned k=0; k<nmt; k++) tmt[k] = tphase[k].second[l];
      MPI_Send(&tmt[0], nmt, MPI_DOUBLE, 0, 49+l, MPI_COMM_WORLD);
    }
    
    for (unsigned k=0; k<num; k++) tmp[k] = tmfpst[k].first;
    MPI_Send(&tmp[0], num, MPI_DOUBLE, 0, 49+Nphase, MPI_COMM_WORLD);
    for (unsigned l=0; l<Nmfp; l++) {
      for (unsigned k=0; k<num; k++) tmp[k] = tmfpst[k].second[l];
      MPI_Send(&tmp[0], num, MPI_DOUBLE, 0, 50+Nphase+l, MPI_COMM_WORLD);
    }
  }
}

void Collide::EPSM(pHOT* tree, pCell* cell, int id)
{
  if (cell->bods.size()<2) return;
  
#ifdef USE_GPTL
  GPTLstart("Collide::EPSM");
#endif
  // Compute mean and variance in each dimension
  // 
  EPSMT[id][0].start();
  
  vector<double> mvel(3, 0.0), disp(3, 0.0);
  double mass = 0.0;
  double Exes = 0.0;
  unsigned nbods = cell->bods.size();
  double coolheat = getCoolingRate(id);
  
  for (vector<unsigned long>::iterator
	 ib=cell->bods.begin(); ib!=cell->bods.end(); ib++) {
    
    Particle* p = tree->Body(*ib);
    if (p->mass<=0.0 || isnan(p->mass)) {
      cout << "[crazy mass]";
    }
    for (unsigned k=0; k<3; k++) {
      mvel[k] += p->mass*p->vel[k];
      disp[k] += p->mass*p->vel[k]*p->vel[k];
      if (fabs(p->pos[k])>1.0e6 || isnan(p->pos[k])) {
	cout << "[crazy pos]";
      }
      if (fabs(p->vel[k])>1.0e6 || isnan(p->vel[k])) {
	cout << "[crazy vel]";
      }
    }
    mass += p->mass;
    
    // Compute the total  undercooled (-) or overcooled (+) energy.
    // That is Exes must be added to the internal energy before cooling
    // at this step.  If use_exes<0, Exes will remain zero (0).
    if (use_exes>=0) {
      Exes += p->dattrib[use_exes];
      p->dattrib[use_exes] = 0;
    }
  }
  EPSMTSoFar[id][0] = EPSMT[id][0].stop();
  
  //
  // Can't do anything if the gas has no mass
  //
  if (mass<=0.0) return;
  
  //
  // Compute the thermal (e.g. internal) energy
  //
  EPSMT[id][1].start();
  
  double Einternal = 0.0, Enew;
  for (unsigned k=0; k<3; k++) {
    mvel[k] /= mass;
    // Disp is variance here
    disp[k] = (disp[k] - mvel[k]*mvel[k]*mass)/mass;
    
    // Crazy value?
    if (disp[k]<0.0) disp[k] = 0.0;
    
    // Total kinetic energy in COV frame
    Einternal += 0.5*mass*disp[k];
  }
  
  EPSMTSoFar[id][1] = EPSMT[id][1].stop();
  
  //
  // Can't collide if with no internal energy
  //
  if (Einternal<=0.0) return;
  
  //
  // Correct 1d vel. disp. after cooling
  // 
  EPSMT[id][2].start();
  
  double Emin = 1.5*boltz*TFLOOR * mass/mp * 
    UserTreeDSMC::Munit/UserTreeDSMC::Eunit;
  
  // Einternal+Exes is the amount that
  // is *really* available for cooling
  // after excess or deficit is included
  //
  // Exes will be - if more energy still
  // needs to be removed and + if too much
  // energy was removed by cooling last step
  // 
  
  // Again, this should be moved to the
  // derived class
  
  if (Einternal + Exes - Emin > coolheat) {
    Enew = Einternal + Exes - coolheat;
  } else {
    Enew = min<double>(Emin, Einternal);
    
    decelT[id] += Einternal - Enew + Exes - coolheat;
    
    if (TSDIAG) {
      if (coolheat-Exes>0.0) {
	
	int indx = (int)floor(log(Einternal/coolheat) /
			      (log(2.0)*TSPOW) + 5);
	if (indx<0 ) indx = 0;
	if (indx>10) indx = 10;
	
	EoverT[id][indx] += mass;
      }
    }
  }
  // Compute the mean 1d vel.disp. from the
  // new internal energy value
  double mdisp = sqrt(Enew/mass/3.0);
  
  EPSMTSoFar[id][2] = EPSMT[id][2].stop();
  
  // Sanity check
  // 
  if (mdisp<=0.0 || isnan(mdisp) || isinf(mdisp)) {
    cout << "Process " << myid  << " id " << id 
	 << ": crazy values, mdisp=" << mdisp << " Enew=" << Enew
	 << " Eint=" << Einternal << " nbods=" << nbods << endl;
    return;
  }
  // Realize new velocities for all particles
  // 
  if (PULLIN) {
    EPSMT[id][3].start();
    
    double R=0.0, T=0.0;	// [Shuts up the compile-time warnings]
    const double sqrt3 = sqrt(3.0);
    
    if (nbods==2) {
      Particle* p1 = tree->Body(cell->bods[0]);
      Particle* p2 = tree->Body(cell->bods[1]);
      for (unsigned k=0; k<3; k++) {
	R = (*unit)();
	if ((*unit)()>0.5)
	  p1->vel[k] = mvel[k] + mdisp;
	else 
	  p1->vel[k] = mvel[k] - mdisp;
	p2->vel[k] = 2.0*mvel[k] - p1->vel[k];
      }
      
    } else if (nbods==3) {
      Particle* p1 = tree->Body(cell->bods[0]);
      Particle* p2 = tree->Body(cell->bods[1]);
      Particle* p3 = tree->Body(cell->bods[2]);
      double v2, v3;
      for (unsigned k=0; k<3; k++) {
	T = 2.0*M_PI*(*unit)();
	v2 = M_SQRT2*mdisp*cos(T);
	v3 = M_SQRT2*mdisp*sin(T);
	p1->vel[k] = mvel[k] - M_SQRT2*v2/sqrt3;
	p2->vel[k] = p1->vel[k] + (sqrt3*v2 - v3)/M_SQRT2;
	p3->vel[k] = p2->vel[k] + M_SQRT2*v3;
      }
    } else if (nbods==4) {
      Particle* p1 = tree->Body(cell->bods[0]);
      Particle* p2 = tree->Body(cell->bods[1]);
      Particle* p3 = tree->Body(cell->bods[2]);
      Particle* p4 = tree->Body(cell->bods[3]);
      double v2, v3, e2, e4, v4;
      for (unsigned k=0; k<3; k++) {
	R = (*unit)();
	e2 = mdisp*mdisp*(1.0 - R*R);
	T = 2.0*M_PI*(*unit)();
	v2 = sqrt(2.0*e2)*cos(T);
	v3 = sqrt(2.0*e2)*sin(T);
	p1->vel[k] = mvel[k] - sqrt3*v2/2.0;
	p2->vel[k] = p1->vel[k] + (2.0*v2 - M_SQRT2*v3)/sqrt3;
	e4 = mdisp*mdisp*R*R;
	if ((*unit)()>0.5) v4 =  sqrt(2.0*e4);
	else               v4 = -sqrt(2.0*e4);
	p3->vel[k] = p2->vel[k] + (sqrt3*v3 - v4)/M_SQRT2;
	p4->vel[k] = p3->vel[k] + M_SQRT2*v4;
      }
      
    } else {
      
      Particle *Pm1, *P00, *Pp1;
      vector<double> Tk, v(nbods), e(nbods);
      int kmax, dim, jj;
      bool Even = (nbods/2*2 == nbods);
      
      for (int k=0; k<3; k++) {

	if (Even) { 
	  // Even
	  kmax = nbods;
	  dim = kmax/2-1;
	  Tk = vector<double>(dim);
	  for (int m=0; m<dim; m++) 
	    Tk[m] = pow((*unit)(), 1.0/(kmax/2 - m - 1.5));
	} else {			
	  // Odd
	  kmax = nbods-1;
	  dim = kmax/2-1;
	  Tk = vector<double>(dim);
	  for (int m=0; m<dim; m++) 
	    Tk[m] = pow((*unit)(), 1.0/(kmax/2 - m - 1.0));
	}
	
	e[1] = mdisp*mdisp*(1.0 - Tk[0]);
	T = 2.0*M_PI*(*unit)();
	v[1] = sqrt(2.0*e[1])*cos(T);
	v[2] = sqrt(2.0*e[1])*sin(T);
	
	P00 = tree->Body(cell->bods[0]);
	Pp1 = tree->Body(cell->bods[1]);
	
	P00->vel[k] = mvel[k] - sqrt(nbods-1)*v[1]/sqrt(nbods);
	Pp1->vel[k] = P00->vel[k] + (sqrt(nbods)*v[1] - sqrt(nbods-2)*v[2])/sqrt(nbods-1);
	
	double prod = 1.0;
	for (int j=4; j<kmax-1; j+=2) {
	  jj = j-1;
	  
	  Pm1 = tree->Body(cell->bods[jj-2]);
	  P00 = tree->Body(cell->bods[jj-1]);
	  Pp1 = tree->Body(cell->bods[jj  ]);
	  
	  prod *= Tk[j/2-2];
	  e[jj] = mdisp*mdisp*(1.0 - Tk[j/2-1])*prod;
	  T = 2.0*M_PI*(*unit)();
	  v[jj]   = sqrt(2.0*e[jj])*cos(T);
	  v[jj+1] = sqrt(2.0*e[jj])*sin(T);
	  
	  P00->vel[k] = Pm1->vel[k] + 
	    (sqrt(3.0+nbods-j)*v[jj-1] - sqrt(1.0+nbods-j)*v[jj]  )/sqrt(2.0+nbods-j);
	  Pp1->vel[k] = P00->vel[k] +
	    (sqrt(2.0+nbods-j)*v[jj  ] - sqrt(    nbods-j)*v[jj+1])/sqrt(1.0+nbods-j);
	}
	
	prod *= Tk[kmax/2-2];
	e[kmax-1] = mdisp*mdisp*prod;
	
	if (Even) {
	  if ((*unit)()>0.5) v[nbods-1] =  sqrt(2.0*e[kmax-1]);
	  else               v[nbods-1] = -sqrt(2.0*e[kmax-1]);
	} else {
	  T = 2.0*M_PI*(*unit)();
	  v[nbods-2] = sqrt(2.0*e[kmax-1])*cos(T);
	  v[nbods-1] = sqrt(2.0*e[kmax-1])*sin(T);
	  
	  Pm1 = tree->Body(cell->bods[nbods-4]);
	  P00 = tree->Body(cell->bods[nbods-3]);
	  
	  P00->vel[k] = Pm1->vel[k] + (2.0*v[nbods-3] - M_SQRT2*v[nbods-2])/sqrt3;
	}
	
	Pm1 = tree->Body(cell->bods[nbods-3]);
	P00 = tree->Body(cell->bods[nbods-2]);
	Pp1 = tree->Body(cell->bods[nbods-1]);
	
	P00->vel[k] = Pm1->vel[k] + (sqrt3*v[nbods-2] - v[nbods-1])/M_SQRT2;
	Pp1->vel[k] = P00->vel[k] + M_SQRT2*v[nbods-1];
      }
    }
    
    // End Pullin algorithm
    
    EPSMTSoFar[id][1] = EPSMT[id][3].stop();
    
  } else {
    
    EPSMT[id][4].start();
    
    //
    // Realize a distribution with internal dispersion only
    //
    vector<double> Tmvel(3, 0.0);
    vector<double> Tdisp(3, 0.0);
    
    for (unsigned j=0; j<nbods; j++) {
      Particle* p = tree->Body(cell->bods[j]);
      
      for (unsigned k=0; k<3; k++) {
	p->vel[k] = mdisp*(*norm)();
	Tmvel[k] += p->mass*p->vel[k];
	Tdisp[k] += p->mass*p->vel[k]*p->vel[k];
	if (fabs(p->vel[k])>1e6 || isnan(p->vel[k])) {
	  cout << "[Collide crazy vel indx=" << p->indx 
	       << hex << ", key=" << p->key << dec << "]";
	}
      }
    }
    
    //
    // Compute mean and variance
    // 
    double Tmdisp = 0.0;
    for (unsigned k=0; k<3; k++) {
      Tmvel[k] /= mass;
      Tdisp[k] = (Tdisp[k] - Tmvel[k]*Tmvel[k]*mass)/mass;
      Tmdisp += Tdisp[k];
    }
    Tmdisp = sqrt(0.5*Tmdisp/3.0);
    
    //
    // Sanity check
    // 
    if (Tmdisp<=0.0 || isnan(Tmdisp) || isinf(Tmdisp)) {
      cout << "Process " << myid  << " id " << id 
	   << ": crazy values, Tmdisp=" << Tmdisp << " mdisp=" << mdisp 
	   << " nbods=" << nbods << endl;
      return;
    }
    
    //
    // Enforce energy and momentum conservation
    // 
    for (unsigned j=0; j<nbods; j++) {
      Particle* p = tree->Body(cell->bods[j]);
      for (unsigned k=0; k<3; k++)
	p->vel[k] = mvel[k] + (p->vel[k]-Tmvel[k])*mdisp/Tmdisp;
    }
    
    EPSMTSoFar[id][4] = EPSMT[id][4].stop();
  }
  
  EPSMT[id][5].start();
  
  //
  // Debugging sanity check
  // 
  if (0) {
    vector<double> mvel1(3, 0.0), disp1(3, 0.0);
    double dvel = 0.0;
    double mass1 = 0.0;
    double Efinal = 0.0;
    double mdisp1 = 0.0;
    
    for (unsigned j=0; j<nbods; j++) {
      Particle* p = tree->Body(cell->bods[j]);
      for (unsigned k=0; k<3; k++) {
	mvel1[k] += p->mass*p->vel[k];
	disp1[k] += p->mass*p->vel[k]*p->vel[k];
      }
      mass1 += p->mass;
    }
    
    for (unsigned k=0; k<3; k++) {
      mvel1[k] /= mass1;
      // Disp is variance here
      disp1[k] = (disp1[k] - mvel1[k]*mvel1[k]*mass1)/mass1;
      mdisp1 += disp1[k];
      
      // Crazy value?
      if (disp1[k]<0.0) disp1[k] = 0.0;
      
      // Total kinetic energy in COV frame
      Efinal += 0.5*mass1*disp1[k];
      dvel += (mvel[k] - mvel1[k])*(mvel[k] - mvel1[k]);
    }
    
    if (fabs(Efinal - Einternal)>1.0e-8*Einternal) {
      cerr << "Process " << myid << ": Collide::EPSM: energy boo-boo,"
	   << "  nbods="    << nbods
	   << "  Efinal="   << Efinal
	   << "  Einter="   << Einternal
	   << "  mass="     << mass1
	   << "  delta D="  << (mdisp - sqrt(mdisp1/3.0))/mdisp
	   << "  delta E="  << Enew - Einternal
	   << "  delta F="  << (Efinal - Einternal)/Einternal
	   << "  delta V="  << sqrt(dvel)
	   << ")"
	   << endl;
    }
  }
  
  //
  // Record diagnostics
  // 
  lostSoFar_EPSM[id] += Einternal - Enew;
  epsm1T[id] += nbods;
  Nepsm1T[id]++;
  
#ifdef USE_GPTL
  GPTLstop("Collide::EPSM");
#endif
  
  EPSMTSoFar[id][5] = EPSMT[id][5].stop();
  
}


void Collide::EPSMtimingGather()
{
  for (int i=0; i<nEPSMT; i++) EPSMtime[i] = 0;
  
  for (int n=0; n<nthrds; n++) {
    for (int i=0; i<nEPSMT; i++) {
      EPSMtime[i] += EPSMTSoFar[n][i]();
      EPSMT[n][i].reset();
    }
  }
  
  if (myid==0) {
    MPI_Reduce(MPI_IN_PLACE, &EPSMtime[0], nEPSMT, MPI_LONG, MPI_SUM, 0, 
	       MPI_COMM_WORLD);
  } else {
    MPI_Reduce(&EPSMtime[0], MPI_IN_PLACE, nEPSMT, MPI_LONG, MPI_SUM, 0,
	       MPI_COMM_WORLD);
  }
}

void Collide::EPSMtiming(ostream& out)
{
  if (EPSMratio<=0.0) return;
  
  if (myid==0) {
    
    const char labels[nEPSMT][20] = {"Mean/Var",
				     "Energy",
				     "1-d vel",
				     "Pullin",
				     "Standard",
				     "Final"};
    long sum = 0;
    for (int i=0; i<nEPSMT; i++) sum += EPSMtime[i];
    
    if (sum>0.0) {
      out << setfill('-') << setw(40) << '-' << setfill(' ') << endl
	  << "EPSM timing" << endl
	  << "-----------" << endl;
      for (int i=0; i<nEPSMT; i++)
	out << left << setw(20) << labels[i] << setw(10) << EPSMtime[i] 
	    << setw(10) << fixed << 100.0*EPSMtime[i]/sum << "%" << endl;
      out << setfill('-') << setw(40) << '-' << setfill(' ') << endl;
    }      
  }
}

void Collide::list_sizes()
{
  string sname = outdir + runtag + ".collide_storage";
  for (int n=0; n<numprocs; n++) {
    if (myid==n) {
      ofstream out(sname.c_str(), ios::app);
      if (out) {
	out << setw(18) << tnow
	    << setw(6)  << myid;
	list_sizes_proc(&out);
	out << endl;
	if (myid==numprocs-1) out << endl;
	out.close();
      }
    }
    MPI_Barrier(MPI_COMM_WORLD);
  }
}


void Collide::list_sizes_proc(ostream* out)
{
  *out << setw(12) << numcnt.size()
       << setw(12) << colcnt.size()
       << setw(12) << tsrat.size()
       << setw(12) << derat.size()
       << setw(12) << tdens.size()
       << setw(12) << tvolc.size()
       << setw(12) << ttemp.size()
       << setw(12) << tdelt.size()
       << setw(12) << tseln.size()
       << setw(12) << tphase.size()
       << setw(12) << (tphaseT.size() ? tphaseT[0].size() : (size_t)0)
       << setw(12) << tmfpst.size()
       << setw(12) << (tmfpstT.size() ? tmfpstT[0].size() : (size_t)0)
       << setw(12) << (numcntT.size() ? numcntT[0].size() : (size_t)0)
       << setw(12) << (colcntT.size() ? colcntT[0].size() : (size_t)0)
       << setw(12) << error1T.size()
       << setw(12) << col1T.size()
       << setw(12) << epsm1T.size()
       << setw(12) << Nepsm1T.size()
       << setw(12) << KEtotT.size()
       << setw(12) << KElostT.size()
       << setw(12) << tmassT.size()
       << setw(12) << decelT.size()
       << setw(12) << (mfpratT.size() ? mfpratT[0].size() : (size_t)0)
       << setw(12) << (tsratT.size() ? tsratT[0].size() : (size_t)0)
       << setw(12) << (tdensT.size() ? tdensT[0].size() : (size_t)0)
       << setw(12) << (tvolcT.size() ? tvolcT[0].size() : (size_t)0)
       << setw(12) << (ttempT.size() ? ttempT[0].size() : (size_t)0)
       << setw(12) << (tselnT.size() ? tselnT[0].size() : (size_t)0)
       << setw(12) << (deratT.size() ? deratT[0].size() : (size_t)0)
       << setw(12) << (tdeltT.size() ? tdeltT[0].size() : (size_t)0)
       << setw(12) << (tdispT.size() ? tdispT[0].size() : (size_t)0)
       << setw(12) << tdiag.size()
       << setw(12) << tdiag1.size()
       << setw(12) << tdiag0.size()
       << setw(12) << tcool.size()
       << setw(12) << tcool1.size()
       << setw(12) << tcool0.size()
       << setw(12) << (cellist.size() ? cellist[0].size() : (size_t)0)
       << setw(12) << disptot.size()
       << setw(12) << lostSoFar_EPSM.size();
}


void Collide::CollectTiming()
{
  int nf = 5, c;
  if (TIMING) nf += 11;
  
  vector<double> in(nf, 0.0);
  vector< vector<double> > out(3);
  for (int i=0; i<3; i++) out[i] = vector<double>(nf);
  
  in[0] += forkSoFar();
  in[1] += snglSoFar();
  in[2] += waitSoFar();
  in[3] += diagSoFar();
  in[4] += joinSoFar();
  
  for (int n=0; n<nthrds; n++) {
    c = 5;
    in[c++] += listSoFar[n]();
    in[c++] += initSoFar[n]();
    in[c++] += collSoFar[n]();
    in[c++] += elasSoFar[n]();
    in[c++] += cellSoFar[n]();
    in[c++] += epsmSoFar[n]();
    in[c++] += coolSoFar[n]();
    in[c++] += stat1SoFar[n]();
    in[c++] += stat2SoFar[n]();
    in[c++] += stat3SoFar[n]();
    in[c++] += collCnt[n];
  }
  
  // Minimum
  MPI_Reduce(&in[0], &out[0][0], nf, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
  
  // Sum
  MPI_Reduce(&in[0], &out[1][0], nf, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
  
  // Maximum
  MPI_Reduce(&in[0], &out[2][0], nf, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
  
  
  c = 0;
  forkSum[0]  = out[0][c];
  forkSum[1]  = out[1][c]/numprocs;
  forkSum[2]  = out[2][c];
  
  c++;
  snglSum[0]  = out[0][c];
  snglSum[1]  = out[1][c]/numprocs;
  snglSum[2]  = out[2][c];
  
  c++;
  waitSum[0]  = out[0][c];
  waitSum[1]  = out[1][c]/numprocs;
  waitSum[2]  = out[2][c];
  
  c++;
  diagSum[0]  = out[0][c];
  diagSum[1]  = out[1][c]/numprocs;
  diagSum[2]  = out[2][c];
  
  c++;
  joinSum[0]  = out[0][c];
  joinSum[1]  = out[1][c]/numprocs;
  joinSum[2]  = out[2][c];
  
  if (TIMING) {
    
    c++;
    listSum[0]  = out[0][c];
    listSum[1]  = out[1][c]/numprocs;
    listSum[2]  = out[2][c];
    
    c++;
    initSum[0]  = out[0][c];
    initSum[1]  = out[1][c]/numprocs;
    initSum[2]  = out[2][c];
    
    c++;
    collSum[0]  = out[0][c];
    collSum[1]  = out[1][c]/numprocs;
    collSum[2]  = out[2][c];
    
    c++;
    elasSum[0]  = out[0][c];
    elasSum[1]  = out[1][c]/numprocs;
    elasSum[2]  = out[2][c];
    
    c++;
    cellSum[0]  = out[0][c];
    cellSum[1]  = out[1][c]/numprocs;
    cellSum[2]  = out[2][c];
    
    c++;
    epsmSum[0]  = out[0][c];
    epsmSum[1]  = out[1][c]/numprocs;
    epsmSum[2]  = out[2][c];
    
    c++;
    coolSum[0]  = out[0][c];
    coolSum[1]  = out[1][c]/numprocs;
    coolSum[2]  = out[2][c];
    
    c++;
    stat1Sum[0] = out[0][c];
    stat1Sum[1] = out[1][c]/numprocs;
    stat1Sum[2] = out[2][c];
    
    c++;
    stat2Sum[0] = out[0][c];
    stat2Sum[1] = out[1][c]/numprocs;
    stat2Sum[2] = out[2][c];
    
    c++;
    stat3Sum[0] = out[0][c];
    stat3Sum[1] = out[1][c]/numprocs;
    stat3Sum[2] = out[2][c];
    
    c++;
    numbSum[0]  = out[0][c];
    numbSum[1]  = floor(out[1][c]/numprocs+0.5);
    numbSum[2]  = out[2][c];
  }
  
  // Reset the timers
  
  forkTime.reset();
  snglTime.reset();
  waitTime.reset();
  joinTime.reset();
  diagTime.reset();
  
  for (int n=0; n<nthrds; n++) {
    listTime [n].reset(); 
    initTime [n].reset(); 
    collTime [n].reset(); 
    stat1Time[n].reset();
    stat2Time[n].reset();
    stat3Time[n].reset();
    coolTime [n].reset(); 
    elasTime [n].reset(); 
    cellTime [n].reset(); 
    epsmTime [n].reset(); 
    collCnt  [n] = 0;
  }
  
}


template<typename T>
void Collide::colldeHelper(ostream& out, const char* lab, vector<T>& v)
{
  out << left << fixed << setw(18) << lab 
      << right << "[" << setprecision(6)
      << setw(10) << v[0] << ", " 
      << setw(10) << v[1] << ", "
      << setw(10) << v[2] << "]" << endl << left;
}

void Collide::colldeTime(ostream& out) 
{ 
  out << "-----------------------------------------------------" << endl;
  out << "-----Collide timing----------------------------------" << endl;
  out << "-----------------------------------------------------" << endl;
  
  colldeHelper<double>(out, "Thread time",  forkSum); 
  colldeHelper<double>(out, "Joined time",  snglSum);
  colldeHelper<double>(out, "Waiting time", waitSum);
  colldeHelper<double>(out, "Join time",    joinSum);
  colldeHelper<double>(out, "Diag time",    diagSum);
  
  out << left 
      << setw(18) << "Body count"   << bodycount    
      << " [" << bodycount/stepcount << "]" << endl
      << setw(18) << "Step count"   << stepcount    << endl
      << endl;
  
  if (TIMING) {
    colldeHelper<double>(out, "All cells",  cellSum); 
    colldeHelper<double>(out, "List bods",  listSum);
    colldeHelper<double>(out, "Init",       initSum);
    colldeHelper<double>(out, "Collide",    collSum); 
    colldeHelper<double>(out, "Inelastic",  elasSum); 
    colldeHelper<double>(out, "EPSM",       epsmSum); 
    colldeHelper<double>(out, "Stat#1",     stat1Sum); 
    colldeHelper<double>(out, "Stat#2",     stat2Sum); 
    colldeHelper<double>(out, "Stat#3",     stat3Sum); 
    colldeHelper<int   >(out, "Cell count", numbSum); 
    out << endl;
  }
  
  stepcount = 0;
  bodycount = 0;
  
  out << "-----------------------------------------------------" << endl;
}

void Collide::tsdiag(ostream& out) 
{
  if (!TSDIAG) return;
  
  if (tdiag.size()==numdiag) {
    
    out << "-----------------------------------------------------" << endl;
    out << "-----Time step diagnostics---------------------------" << endl;
    out << "-----------------------------------------------------" << endl;
    out << right << setw(8) << "2^n" << setw(15) << "TS ratio"
	<< setw(15) << "Size/Vel";
    if (use_delt>=0) out << setw(15) << "Kinetic/Cool";
    out << endl << setprecision(3);
    out << "-----------------------------------------------------" << endl;
    for (unsigned k=0; k<numdiag; k++) {
      double rat = pow(4.0, -5.0+k);
      out << setw(8)  << -10+2*static_cast<int>(k)
	  << (((rat<1.0e-02 || rat>1.0e+06) && rat>0.0) ? scientific : fixed)
	  << setw(15) << rat
	  << setw(15) << tdiag[k];
      if (use_delt>=0) out << setw(15) << tcool[k];
      out << endl;
      tdiag[k] = 0;
      if (use_delt>=0) tcool[k] = 0;
    }
    out << "-----------------------------------------------------" << endl;
    out << left;
  }
  
  
  if (Eover.size()==numdiag) {
    
    double emass = 0.0;
    for (unsigned k=0; k<numdiag; k++) emass += Eover[k];
    if (emass>0.0) {
      out << "-----Cooling rate diagnostics------------------------" << endl;
      out << "-----------------------------------------------------" << endl;
      out << right << setw(8) << "2^n" << setw(15) << "Ratio"
	  << setw(15) << "KE/Cool(%)" << endl;
      out << "-----------------------------------------------------" << endl;
      
      for (unsigned k=0; k<numdiag; k++) {
	double rat = pow(pow(2.0, TSPOW), -5.0+k);
	double val = Eover[k]*100.0/emass;
	out << setw(8)  << TSPOW*(-5 + static_cast<int>(k))
	    << (((rat<1.0e-02 || rat>1.0e+06) && rat>0.0) ? scientific : fixed)
	    << setw(15) << rat
	    << (((val<1.0e-02 || val>1.0e+06) && val>0.0) ? scientific : fixed)
	    << setw(15) << val << endl;
	Eover[k] = 0;
      }
      out << "-----------------------------------------------------" << endl;
    }
    out << left;
  }
  
  if (Cover.size()==numdiag) {
    
    double cmass = 0.0;
    for (unsigned k=0; k<numdiag; k++) cmass += Cover[k];
    if (cmass>0.0) {
      out << "-----CBA scale diagnostics--------------------------" << endl;
      out << "-----------------------------------------------------" << endl;
      out << right << setw(8) << "2^n" << setw(15) << "Ratio"
	  << setw(15) << "Diam/Side(%)" << endl;
      out << "-----------------------------------------------------" << endl;
      
      for (unsigned k=0; k<numdiag; k++) {
	double rat = pow(4.0, -5.0+k);
	double val = Cover[k]*100.0/cmass;
	out << setw(8)  << -10+2*static_cast<int>(k)
	    << (((rat<1.0e-02 || rat>1.0e+06) && rat>0.0) ? scientific : fixed)
	    << setw(15) << rat
	    << (((val<1.0e-02 || val>1.0e+06) && val>0.0) ? scientific : fixed)
	    << setw(15) << val << endl;
	Cover[k] = 0;
      }
      out << "-----------------------------------------------------" << endl;
    }
    out << left;
  }
  
}


void Collide::voldiag(ostream& out) 
{
  if (!VOLDIAG) return;
  
  if (Vcnt.size()==nbits) {
    // Find the smallest cell size
    unsigned nlast;
    for (nlast=nbits; nlast>0; nlast--)
      if (Vcnt[nlast-1]) break;
    
    out << "-----------------------------------------------------"
	<< "-----------------------------------------------------" << endl;
    out << "-----Volume cell diagnostics-------------------------"
	<< "-----------------------------------------------------" << endl;
    out << "-----------------------------------------------------"
      	<< "-----------------------------------------------------" << endl;
    out << right << setw(8) << "n" << setw(8) << "#"
	<< setw(12) << "Factor"
	<< setw(12) << "Density"   << setw(12) << "MFP/L"
	<< setw(12) << "Coll prob" << setw(12) << "Flight/L"
	<< setw(12) << "Particles" << setw(12) << "Root var";
    out << endl << setprecision(3) << scientific;
    out << "-----------------------------------------------------"
      	<< "-----------------------------------------------------" << endl;
    
    for (unsigned k=0; k<nlast; k++) {
      unsigned n  = k*nvold;
      unsigned nn = n + nvold - 1;
      double rat  = pow(2.0, -3.0*k);
      double nrm  = Vcnt[k] ? 1.0/Vcnt[k] : 0.0;
      out << setw(8) << k << setw(8) << Vcnt[k] << setw(12) << rat;
      for (unsigned l=0; l<nvold-1; l++) out << setw(12) << Vdbl[n+l]*nrm;
      // Variance term
      Vdbl[nn] = fabs(Vdbl[nn] - Vdbl[nn-1]*Vdbl[nn-1]*nrm);
      out << setw(12) << sqrt(Vdbl[nn]*nrm) << endl;
    }
    out << "-----------------------------------------------------"
	<< "-----------------------------------------------------" << endl;
    out << left;
  }
  
}


// For timestep computation

extern "C"
void *
tstep_thread_call(void *atp)
{
  thrd_pass_tstep *tp = (thrd_pass_tstep *)atp;
  Collide *p = (Collide *)tp->p;
  p->timestep_thread((void*)&tp->arg);
  return NULL;
}

void Collide::compute_timestep(pHOT* tree, double coolfrac)
{
  int errcode;
  void *retval;
  
  if (nthrds==1) {
    thrd_pass_tstep td;
    
    td.p = this;
    td.arg.tree = tree;
    td.arg.coolfrac = coolfrac;
    td.arg.id = 0;
    
    tstep_thread_call(&td);
    
    return;
  }
  
  tdT = new thrd_pass_tstep [nthrds];
  t = new pthread_t [nthrds];
  
  if (!tdT) {
    cerr << "Process " << myid 
         << ": Collide::tstep_thread_call: error allocating memory for thread counters\n";
    exit(18);
  }
  if (!t) {
    cerr << "Process " << myid
         << ": Collide::tstep_thread_call: error allocating memory for thread\n";
    exit(18);
  }
  
  // Make the <nthrds> threads
  for (int i=0; i<nthrds; i++) {
    tdT[i].p = this;
    tdT[i].arg.tree = tree;
    tdT[i].arg.coolfrac = coolfrac;
    tdT[i].arg.id = i;
    
    errcode =  pthread_create(&t[i], 0, tstep_thread_call, &tdT[i]);
    if (errcode) {
      cerr << "Process " << myid;
      cerr << " Collide: cannot make thread " << i
	   << ", errcode=" << errcode << endl;
      exit(19);
    }
  }
  
  // Collapse the threads
  for (int i=0; i<nthrds; i++) {
    if ((errcode=pthread_join(t[i], &retval))) {
      cerr << "Process " << myid;
      cerr << " Collide::tstep_thread_call: thread join " << i
           << " failed, errcode=" << errcode << endl;
      exit(20);
    }
  }
  
  delete [] tdT;
  delete [] t;
  
  caller->print_timings("Collide: timestep thread timings", timer_list);
}


void * Collide::timestep_thread(void * arg)
{
  pHOT* tree = (pHOT* )((tstep_pass_arguments*)arg)->tree;
  double coolfrac = (double)((tstep_pass_arguments*)arg)->coolfrac;
  int id = (int)((tstep_pass_arguments*)arg)->id;
  
  thread_timing_beg(id);
  
  // Loop over cells, cell time-of-flight time
  // for each particle
  
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
    
    for (vector<unsigned long>::iterator 
	   i=c->bods.begin(); i!=c->bods.end(); i++) {
      // Current particle
      p = tree->Body(*i);
      // Compute time of flight criterion
      DT = 1.0e40;
      mscale = 1.0e40;
      for (unsigned k=0; k<3; k++) {
	DT = min<double>
	  (pHOT::sides[k]*L/(fabs(p->vel[k])+1.0e-40), DT);
	mscale = min<double>(pHOT::sides[k]*L, mscale);
      }
      // Size scale for multistep timestep calc.
      p->scale = mscale;
      // Compute cooling criterion timestep
      if (use_delt>=0) {
	double v = p->dattrib[use_delt];
	if (v>0.0) DT = min<double>(DT, v);
      }
      
      p->dtreq = coolfrac*DT;
    }
  }
  
  thread_timing_end(id);
  
  return (NULL);
}

void Collide::energyExcess(double& ExesColl, double& ExesEPSM)
{
  // Sum up from all the threads
  // 
  for (int n=1; n<nthrds; n++) {
    exesCT[0] += exesCT[n];
    exesET[0] += exesET[n];
  }
  // Sum reduce result to root node
  // 
  MPI_Reduce(&exesCT[0], &ExesColl, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&exesET[0], &ExesEPSM, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
  
  // Zero out the thread accumulators
  // 
  for (int n=0; n<nthrds; n++) exesCT[0] = exesET[n] = 0.0;
}


void Collide::pre_collide_diag()
{
  // Clean thread variables
  diagTime.start();
  for (int n=0; n<nthrds; n++) {
    error1T[n] = 0;
    sel1T[n]   = 0;
    col1T[n]   = 0;
    epsm1T[n]  = 0;
    Nepsm1T[n] = 0;
    tmassT[n]  = 0;
    decolT[n]  = 0;
    decelT[n]  = 0;
    
    // For computing cell occupation #
    colcntT[n].clear();	// and collision counts
    numcntT[n].clear();
    
    // For computing MFP to cell size ratio 
    // and drift ratio
    if (MFPDIAG) {
      tsratT[n].clear();
      keratT[n].clear();
      deratT[n].clear();
      tdensT[n].clear();
      tvolcT[n].clear();
      ttempT[n].clear();
      tdeltT[n].clear();
      tselnT[n].clear();
      tphaseT[n].clear();
      tmfpstT[n].clear();
    }
    
    if (VOLDIAG) {
      for (unsigned k=0; k<nbits; k++) {
	VcntT[n][k] = 0;
	for (unsigned l=0; l<nvold; l++) VdblT[n][k*nvold+l] = 0.0;
      }
    }
    
    for (unsigned k=0; k<numdiag; k++) {
      if (TSDIAG) {
	tdiagT[n][k] = 0;
	EoverT[n][k] = 0;
      }
      if (use_delt>=0) tcoolT[n][k] = 0;
    }
    
    for (unsigned k=0; k<3; k++) tdispT[n][k] = 0;
  }
  if (TSDIAG) {
    for (unsigned k=0; k<numdiag; k++) tdiag1[k] = tdiag0[k] = 0;
    for (unsigned k=0; k<numdiag; k++) Eover1[k] = Eover0[k] = 0;
  }
  if (VOLDIAG) {
    for (unsigned k=0; k<nbits; k++) {
      Vcnt1[k] = Vcnt0[k] = 0;
      for (unsigned l=0; l<nvold; l++) 
	Vdbl1[k*nvold+l] = Vdbl0[k*nvold+l] = 0.0;
    }
  }
  
  if (use_delt>=0) 
    for (unsigned k=0; k<numdiag; k++) tcool1[k] = tcool0[k] = 0;
  
  diagTime.stop();
  
  if (DEBUG) list_sizes();
}


unsigned Collide::post_collide_diag()
{
  unsigned sel=0, col=0;
  

  diagTime.start();
  // Diagnostics

  unsigned error1=0, error=0;
  
  unsigned sel1=0, col1=0;	// Count number of collisions
  unsigned epsm1=0, epsm=0, Nepsm1=0, Nepsm=0;
  
  // Dispersion test
  double mass1 = 0, mass0 = 0;
  vector<double> disp1(3, 0), disp0(3, 0);
  
  numcnt.clear();
  colcnt.clear();
  
  for (int n=0; n<nthrds; n++) {
    error1 += error1T[n];
    sel1   += sel1T[n];
    col1   += col1T[n];
    epsm1  += epsm1T[n];
    Nepsm1 += Nepsm1T[n];
    numcnt.insert(numcnt.end(), numcntT[n].begin(), numcntT[n].end());
    colcnt.insert(colcnt.end(), colcntT[n].begin(), colcntT[n].end());
    if (TSDIAG) {
      for (unsigned k=0; k<numdiag; k++) tdiag1[k] += tdiagT[n][k];
      for (unsigned k=0; k<numdiag; k++) Eover1[k] += EoverT[n][k];
    }
    if (VOLDIAG) {
      for (unsigned k=0; k<nbits; k++) {
	Vcnt1[k] += VcntT[n][k];
	for (unsigned l=0; l<nvold; l++)
	  Vdbl1[k*nvold+l] += VdblT[n][k*nvold+l];
      }
    }
    if (use_delt>=0) 
      for (unsigned k=0; k<numdiag; k++) tcool1[k] += tcoolT[n][k];
  }
  
  // For computing MFP to cell size ratio 
  // and drift ratio (diagnostic only)
  if (MFPDIAG) {
    tsrat.clear();
    kerat.clear();
    derat.clear();
    tdens.clear();
    tvolc.clear();
    ttemp.clear();
    tdelt.clear();
    tseln.clear();
    tphase.clear();
    tmfpst.clear();
    
    for (int n=0; n<nthrds; n++) {
      tsrat. insert(tsrat.end(),   tsratT[n].begin(),  tsratT[n].end());
      kerat. insert(kerat.end(),   keratT[n].begin(),  keratT[n].end());
      derat. insert(derat.end(),   deratT[n].begin(),  deratT[n].end());
      tdens. insert(tdens.end(),   tdensT[n].begin(),  tdensT[n].end());
      tvolc. insert(tvolc.end(),   tvolcT[n].begin(),  tvolcT[n].end());
      ttemp. insert(ttemp.end(),   ttempT[n].begin(),  ttempT[n].end());
      tdelt. insert(tdelt.end(),   tdeltT[n].begin(),  tdeltT[n].end());
      tseln. insert(tseln.end(),   tselnT[n].begin(),  tselnT[n].end());
      tphase.insert(tphase.end(), tphaseT[n].begin(), tphaseT[n].end());
      tmfpst.insert(tmfpst.end(), tmfpstT[n].begin(), tmfpstT[n].end());
    }
  }
  
  for (int n=0; n<nthrds; n++) {
    for (unsigned k=0; k<3; k++) disp1[k] += tdispT[n][k];
    mass1 += tmassT[n];
  }
  
  MPI_Reduce(&sel1, &sel, 1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&col1, &col, 1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&epsm1, &epsm, 1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&Nepsm1, &Nepsm, 1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&error1, &error, 1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&ncells, &numtot, 1, MPI_UNSIGNED, MPI_SUM, 0, MPI_COMM_WORLD);
  if (TSDIAG) {
    MPI_Reduce(&tdiag1[0], &tdiag0[0], numdiag, MPI_UNSIGNED, MPI_SUM, 0, 
	       MPI_COMM_WORLD);
    MPI_Reduce(&Eover1[0], &Eover0[0], numdiag, MPI_DOUBLE,   MPI_SUM, 0, 
	       MPI_COMM_WORLD);
  }
  if (VOLDIAG) {
    MPI_Reduce(&Vcnt1[0], &Vcnt0[0], nbits, MPI_UNSIGNED, MPI_SUM, 0, 
	       MPI_COMM_WORLD);
    MPI_Reduce(&Vdbl1[0], &Vdbl0[0], nbits*nvold, MPI_DOUBLE, MPI_SUM, 0, 
	       MPI_COMM_WORLD);
  }
  if (use_delt>=0)
    MPI_Reduce(&tcool1[0], &tcool0[0], numdiag, MPI_UNSIGNED, MPI_SUM, 0, 
	       MPI_COMM_WORLD);
  MPI_Reduce(&disp1[0], &disp0[0], 3, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
  MPI_Reduce(&mass1, &mass0, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
  
  seltot    += sel;
  coltot    += col;
  epsmtot   += epsm;
  epsmcells += Nepsm;
  errtot    += error;
  if (TSDIAG) {
    for (unsigned k=0; k<numdiag; k++) {
      tdiag[k] += tdiag0[k];
      Eover[k] += Eover0[k];
    }
  }
  if (VOLDIAG) {
    for (unsigned k=0; k<nbits; k++) {
      Vcnt[k] += Vcnt0[k];
      for (unsigned l=0; l<nvold; l++)
	Vdbl[k*nvold+l] += Vdbl0[k*nvold+l];
    }
  }
  if (use_delt>=0)
    for (unsigned k=0; k<numdiag; k++) tcool[k] += tcool0[k];
  for (unsigned k=0; k<3; k++) disptot[k] += disp0[k];
  masstot   += mass0;
  diagSoFar = diagTime.stop();
  
  return col;
}

void Collide::CPUHogGather()
{
  for (int i=0; i<2; i++) {
    CPUH[i]   = MAXLONG;
    CPUH[6+i] = 0;
    CPUH[2+i] = CPUH[4+i] = CPUH[8+i] = CPUH[10+i] = -1;
  }
  
  for (int n=0; n<nthrds; n++) {
    for (int i=0; i<2; i++) {
      if (CPUH[i] > minUsage[n*2+i]) {
	CPUH[i]    = minUsage[n*2+i];
	CPUH[2+i]  = minPart [n*2+i];
	CPUH[4+i]  = minCollP[n*2+i];
      }
      if (CPUH[6+i] < maxUsage[n*2+i]) {
	CPUH[6+i]  = maxUsage[n*2+i];
	CPUH[8+i]  = maxPart [n*2+i];
	CPUH[10+i] = maxCollP[n*2+i];
      }
      // Clear values for next call
      minUsage[n*2+i] = MAXLONG;
      maxUsage[n*2+i] = 0;
      minPart [n*2+i] = -1;
      maxPart [n*2+i] = -1;
      minCollP[n*2+i] = -1;
      maxCollP[n*2+i] = -1;
    }
  }
  
  vector<long> U(12*numprocs);
  
  MPI_Gather(&CPUH[0], 12, MPI_LONG, &U[0], 12, MPI_LONG, 0, MPI_COMM_WORLD);
  
  if (myid==0) {
    
    for (int i=0; i<2; i++) {
      CPUH[i]   = MAXLONG;
      CPUH[6+i] = 0;
      CPUH[2+i] = CPUH[4+i] = CPUH[8+i] = CPUH[10+i] = -1;
    }
    
    for (int n=0; n<numprocs; n++) {
      for (int i=0; i<2; i++) {
	if (CPUH[i] > U[n*12+i]) {
	  CPUH[i]   = U[n*12+i];
	  CPUH[2+i] = U[n*12+2+i];
	  CPUH[4+i] = U[n*12+4+i];
	}
      }
      for (int i=6; i<8; i++) {
	if (CPUH[i] < U[n*12+i]) {
	  CPUH[i]   = U[n*12+i];
	  CPUH[2+i] = U[n*12+2+i];
	  CPUH[4+i] = U[n*12+4+i];
	}
      }
    }
  }
  
}

void Collide::CPUHog(ostream& out)
{
  if (myid==0) {
    const unsigned f = 8;
    out << "Extremal cell timing" << endl
	<< "--------------------" << endl
	<< "T=" << tnow << ",  mstep=" << mstep << endl << right;
    if (EPSMratio>0) {
      out << "                 "
	  << setw(f) << "DSMC"   << "  " << setw(f) << "EPSM"
	  << "      " 
	  << setw(f) << "DSMC"   << "  " << setw(f) << "EPSM"
	  << "      " 
	  << setw(f) << "DSMC"   << ", " << setw(f) << "EPSM" << endl
	  << "  Minimum usage=(" 
	  << setw(f) << CPUH[ 0] << ", " << setw(f) << CPUH[ 1]
	  << ")  N=(" 
	  << setw(f) << CPUH[ 2] << ", " << setw(f) << CPUH[ 3]
	  << ")  C=(" 
	  << setw(f) << CPUH[ 4] << ", " << setw(f) << CPUH[ 5] << ")" << endl
	  << "  Maximum usage=(" 
	  << setw(f) << CPUH[ 6] << ", " << setw(f) << CPUH[ 7]
	  << ")  N=("
	  << setw(f) << CPUH[ 8] << ", " << setw(f) << CPUH[ 9]
	  << ")  C=(" 
	  << setw(f) << CPUH[10] << ", " << setw(f) << CPUH[11] << ")" << endl;
    } else {
      out << endl
	  << "  Minimum usage=(" 
	  << setw(f) << CPUH[ 0] << ")  N=(" 
	  << setw(f) << CPUH[ 2] << ")  C=(" 
	  << setw(f) << CPUH[ 4] << ")" << endl
	  << "  Maximum usage=(" 
	  << setw(f) << CPUH[ 6] << ")  N=("
	  << setw(f) << CPUH[ 8] << ")  C=(" 
	  << setw(f) << CPUH[10] << ")" << endl;
    }
    out << endl;
  }
}

double Collide::hsDiameter()
{
  const double Bohr = 5.2917721092e-09;
  return hsdiam*Bohr*diamfac/UserTreeDSMC::Lunit;
}

void Collide::printSpecies(std::map<speciesKey, unsigned long>& spec)
{
  if (myid) return;

  typedef std::map<speciesKey, unsigned long> spCountMap;
  typedef spCountMap::iterator spCountMapItr;

  std::ofstream dout;

				// Generate the file name
  if (species_file_debug.size()==0) {
    std::ostringstream sout;
    sout << outdir << runtag << ".species";
    species_file_debug = sout.str();

				// Open the file for the first time
    dout.open(species_file_debug.c_str());

				// Print the header
    dout << "# " << std::setw(12) << std::right << "Time ";
    for (spCountMapItr it=spec.begin(); it != spec.end(); it++) {
      std::ostringstream sout;
      sout << "(" << it->first.first << "," << it->first.second << ") ";
      dout << setw(12) << right << sout.str();
    }
    dout << std::endl;

    dout << "# " << std::setw(12) << std::right << "--------";
    for (spCountMapItr it=spec.begin(); it != spec.end(); it++)
      dout << setw(12) << std::right << "--------";
    dout << std::endl;

  } else {
				// Open for append
    dout.open(species_file_debug.c_str(), ios::out | ios::app);
  }

  dout << "  " << std::setw(12) << std::right << tnow;
  for (spCountMapItr it=spec.begin(); it != spec.end(); it++)
    dout << std::setw(12) << std::right << it->second;
  dout << std::endl;
}

void Collide::velocityUpdate(Particle* p1, Particle* p2, double cr)
{
  vector<double> vcm(3), vrel(3);

  // Center of mass velocity
  //
  double tmass = p1->mass + p2->mass;
  for(unsigned k=0; k<3; k++)
    vcm[k] = (p1->mass*p1->vel[k] + p2->mass*p2->vel[k]) / tmass;
	    
  double cos_th = 1.0 - 2.0*(*unit)();       // Cosine and sine of
  double sin_th = sqrt(1.0 - cos_th*cos_th); // Collision angle theta
  double phi    = 2.0*M_PI*(*unit)();	       // Collision angle phi
  
  vrel[0] = cr*cos_th;	  // Compute post-collision
  vrel[1] = cr*sin_th*cos(phi); // relative velocity
  vrel[2] = cr*sin_th*sin(phi);
  
  // Update post-collision velocities
  // 
  for(unsigned k=0; k<3; k++ ) {
    p1->vel[k] = vcm[k] + p2->mass/tmass*vrel[k];
    p2->vel[k] = vcm[k] - p1->mass/tmass*vrel[k];
  }
}
