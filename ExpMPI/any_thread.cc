#include <iostream>
#include "any_thread.h"

extern "C"
void *
call_any_threads_thread_call(void *atp)
{
  thrd_pass *tp = (thrd_pass *)atp;
  any_thread *p = (any_thread *)tp->t;
  p -> thread_call((void*)&tp->id);
  return NULL;
}
                   
any_thread::~any_thread(void)
{
  delete [] td;
  delete [] t;
}

void any_thread::start_threads(int N)
{
  if (td != NULL) throw "Some threads still running!\n";

  int errcode;
  nthreads = N;
  td = new thrd_pass [nthreads];
  t = new pthread_t [nthreads];
  for (int n=0; n<nthreads; n++) {
    td[n].t = this;
    td[n].id = n;

    errcode =  pthread_create(&t[n], 0, call_any_threads_thread_call, &td[n]);
    if (errcode) cerr << "[" << n << "] errcode=" << errcode << '\n';
  }
}

void any_thread::join_threads(void)
{
  if (!t) throw "No running threads!\n";
  
  int errcode;
  void *retval;

  for (int n=0; n<nthreads; n++) {
    cerr << "Trying to join thread #" << n << '\n';
    if ((errcode=pthread_join(t[n], &retval))) {
      cerr << "Thread join " << n << " failed, errcode=" << errcode << '\n';
      exit(20);      
    }
    cerr << "Thread #" << n << " joined\n";

  }

  delete [] td;
  delete [] t;

  td = NULL;
  t = NULL;
}

