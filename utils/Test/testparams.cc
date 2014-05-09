// This may look like C code, but it is really -*- C++ -*-

/*****************************************************************************
 *  Description:
 *  -----------
 *
 *  Test parameter parsing
 *
 *
 *  Call sequence:
 *  -------------
 *
 *  Parameters:
 *  ----------
 *
 *
 *  Returns:
 *  -------
 *
 *
 *  Notes:
 *  -----
 *
 *
 *  By:
 *  --
 *
 *  MDW 02/05/04
 *
 ***************************************************************************/

#define IS_MAIN

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <cstdlib>
#include <cmath>

#include <ParamParse.H>

int myid=0;
char threading_on = 0;
pthread_mutex_t mem_lock;
string outdir, runtag;

using namespace std;

int parse_args(int argc, char **argv);
void print_parm(ostream &, const char *);


				// Parameters
int NICE=15;
bool DENS=true;
double RMAX=2.0;
string PARMFILE="test.param";

int main(int argc, char **argv)
{

  /*============================*/
  /* Parse command line:        */
  /*============================*/

  int iret = parse_args(argc, argv);

  print_parm(cout, " ");

  return 0;
}

//----------------------------------------------------------------------
//----------------------------------------------------------------------
//----------------------------------------------------------------------

void usage(char *);
void set_parm(string&, string&);
void write_parm(void);
void print_default(void);

#define WBUFSIZE 80
#define VBUFSIZE 80

int parse_args(int argc, char **argv)
{
  char *prog=argv[0];
  int c, iparmf=0,iret,i;
  string file;

  ParamParse prse("=");

  while (1) {

    c = getopt(argc, argv, "f:dh");
    if (c==-1) break;

    switch (c) {
    case 'f':
      iparmf=1;
      file = optarg;
      cout << "File=" << file << endl;
      break;
    case 'd':
      print_default();
      break;
    case '?':
    case 'h':
      usage(prog);
      break;
    }
  }

  argc -= optind;
  if (iparmf)
    prse.parse_file(file);
  else {
    iret = prse.parse_argv(argc, &argv[optind]);
    argc -= iret;
  }
  
  if (argc != 0)
    usage(prog);
  
				// Set parameters
  spair ret;
  while (prse.get_next(ret)) set_parm(ret.first, ret.second);
}

double atof(string& s)
{
  return atof(s.c_str());
}

int atoi(string& s)
{
  return atoi(s.c_str());
}

bool atol(string& s)
{
  return atoi(s.c_str()) ? true : false;
}

void set_parm(string& word, string& valu)
{
  if (word == "NICE")		NICE = atoi(valu);

  else if (word == "DENS")	DENS = atol(valu);

  else if (word == "RMAX")	RMAX = atof(valu);

  else if (word == "PARMFILE")	PARMFILE = valu;

  else {
      cerr << "No such parameter: " << word << endl;
      exit(-1);
  }
}

void write_parm()
{
  ofstream fout(PARMFILE.c_str());			
  if ( !fout ) {
    cerr << "Couldn't open parameter file:" << PARMFILE << ". . . quitting\n" << endl;
    exit(-1);
  }

  print_parm(fout,"\0");
}

void print_default()
{
  cerr << "\nDefaults:\n";
  cerr << "----------------------------\n";
  print_parm(cerr,"\0");
  exit(0);
}


void print_parm(ostream& stream, const char *comment)
{
  stream.setf(ios::left);

  stream << comment << setw(20) <<  "NICE" << " = " << NICE << endl;
  stream << comment << setw(20) <<  "RMAX" << " = " << RMAX << endl;
  stream << comment << setw(20) <<  "DENS" << " = " << DENS << endl;
  stream << comment << setw(20) <<  "PARMFILE" << " = " << PARMFILE << endl;

  stream.unsetf(ios::left);
}


void usage(char *prog)
{
  char usage_head[] = 
    "[-f file -d] [keyword=value [keyword=value] .. ]";

  char usage_data[][80] = {
    "     -f file",      "keyword/value parameter file",
    "     -d",           "print default parameters",
    "\nKeywords:",       " DFFILE,OUTFILE",
    "\0"                 };


  cerr.setf(ios::left);

  cerr << "Usage: " << prog << " " << usage_head << endl << endl;

  int j = 0;
  while (*(usage_data[j]) != '\0') {
    cerr << setw(25) << usage_data[j] << usage_data[j+1] << endl;
    j+=2;
  }
  cerr << endl;
}