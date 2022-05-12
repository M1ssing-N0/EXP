/**
   Globals used by EXP runtime environment supplied here for
   standalone utilities
 */

#include <libvars.H>

namespace __EXP__
{
  //@{
  //! POSIX thread support
  char             threading_on     = 0;
  pthread_mutex_t  mem_lock;
  //@}

  //! Location for output
  std::string      outdir           = "./";

  //! Run name for file labeling
  std::string      runtag           = "newrun";

  //! Number of POSIX threads (minimum: 1)
  int              nthrds           = 1;

  //@{
  //! Multistep indices
  int              this_step        = 0;
  unsigned         multistep        = 0;
  unsigned         maxlev           = 100;
  int              mstep            = 1;
  int              Mstep            = 1;
  //@}

  //! MPI node id
  int              myid             = 0;

  //! Random number generator instance
  std::mt19937     random_gen;
};


