#include <execinfo.h>
#include <cxxabi.h>
#include <malloc.h>

// For debugging . . . unwinds stack and writes to output stream with
// symbols if available

#include <mpi.h>

#include <cstring>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>

//! Directory for output
extern std::string outdir;

//! Used for labeling report files
extern std::string runtag;

void print_trace(std::ostream& out, const char *file, int line)
{
  const size_t max_depth = 100;
  size_t       stack_depth;
  void        *stack_addrs[max_depth];
  char       **stack_strings;

  //
  // These arge GNU C extensions and therefore are not going to be
  // portable, alas.
  //
  stack_depth   = backtrace        (stack_addrs, max_depth  );
  stack_strings = backtrace_symbols(stack_addrs, stack_depth);
  
  out << std::setfill('-') << std::setw(80) << '-' << std::endl;
  out << std::setfill(' ');

  if (file) out << "Call stack from " << file << ":" << line << std::endl;
  
  if (0) {
    for (size_t i = 1; i < stack_depth; i++) {
      out << "    " << stack_strings[i] << std::endl;
    }
  }
  
  for (size_t i = 1; i < stack_depth; i++) {
    //
    // 4 x 80 character lines worth.  I suppose some template names
    // may be larger . . .
    //
    size_t sz = 320;
				// We need to use malloc/free for
				// these functions, sigh . . .
    char *function = static_cast<char *>(malloc(sz));
    char *begin = 0, *end = 0;
    //
    // Find the parentheses and address offset surrounding the mangled
    // name
    //
    for (char *j = stack_strings[i]; *j; ++j) {
      if (*j == '(') {
	begin = j;
      }
      else if (*j == '+') {
	end = j;
      }
    }
    if (begin && end) {
      *begin++ = '\0';
      *end     = '\0';
      //
      // Found the mangled name, now in [begin, end)
      //	
      int status;
      char *ret = abi::__cxa_demangle(begin, function, &sz, &status);
      if (ret) {
	//
	// Return value may be a realloc() of the input
	//
	function = ret;
      }
      else {
	//
	// Demangling failed, format it as a C function with no args
	//
	std::strncpy(function, begin, sz);
	std::strncat(function, "()",  sz);
				// Null termination
	function[sz-1] = '\0';
      }
      out << "    " << stack_strings[i] << ":" << function << std::endl;
    } else {
				// Didn't find the mangled name, just
				// print the whole line
      out << "    " << stack_strings[i] << std::endl;
    }
    free(function);		// malloc()ed above
  }

  free(stack_strings);		// malloc()ed by backtrace_symbols
  
  out << std::setfill('-') << std::setw(80) << '-' << std::endl
      << std::setfill(' ') << std::flush;
}


void mpi_print_trace(const std::string& routine, const std::string& msg,
		     const char *file, int line)
{
  //
  // Look for active MPI environment
  //
  int numprocs, myid;
  MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
  MPI_Comm_rank(MPI_COMM_WORLD, &myid);

  std::cerr << routine;
  if (numprocs>1) std::cerr << " [mpi_id=" << myid << "]";
  std::cerr << ": "<< msg << std::endl;
  
  std::ostringstream ostr;
  ostr << outdir << runtag << "." << "traceback.";
  if (numprocs>1) ostr << myid;
  else            ostr << "info";
  
  std::ofstream tb(ostr.str().c_str());

  //
  // Print out all the frames
  //
  if (tb.good()) {
    std::cerr << routine;
    if (numprocs>1) std::cerr << " [mpi_id=" << myid << "]";
    std::cerr << ": see <" << ostr.str() << "> for more info" << std::endl;
    
    print_trace(tb,        0, 0);
  } else {
    print_trace(std::cerr, 0, 0);
  }
}