// This may look like C code, but it is really -*- C++ -*-

// ======================================================================
// Class to compute orbit of a satellite in spherical halo
//
//	o Orientation of orbit specified by Euler angles
//
// 	o Returns position and force in halo frame
//
//      o Compute tidal force in satellite frame 
//
//	o Arbitrary orientation of satellite body specified by Euler angles
//
// ======================================================================

#ifndef _SatelliteOrbit_h
#ifdef __GNUG__
#pragma interface
#endif
#define _SatelliteOrbit_h

#include <string>

#include <Vector.h>
#include <massmodel.h>
#include <model3d.h>

#include <FindOrb.H>
#include <ParamDatabase.H>

//! Computes an satellite orbits and tidal forces in fixed halo
class SatelliteOrbit
{
private:

  SphericalModelTable *m;
  AxiSymModel *halo_model;
  FindOrb *orb;
  Matrix rotate, rotateI;
  Three_Vector v, v0, u, u0, non;
  Matrix tidalRot, tidalRotI;
  double omega, domega;
  double rsat, vsat, Omega;
  bool circ;

  int halo_type;

				// Keep current satellte position
  double currentTime;
  Three_Vector currentR, currentF;

				// Private members

  void parse_args(void);	// Set parameters from parmFile
  void set_parm(string& word, string& alu);

				// General function for double and Vector input
  Vector get_tidal_force();
  Vector get_tidal_force_non_inertial();

  ParamDatabase *config;

public:

  //! Constructor
  SatelliteOrbit(const string &file);

  //! Destructor
  ~SatelliteOrbit(void);

				// Members

  //! Get satellite position in halo frame
  Vector get_satellite_orbit(double T);

  //! Get satellite position in halo frame
  void get_satellite_orbit(double T, double *v);

  //! Get force on satellite in halo frame
  Vector get_satellite_force(double T);


				// Member functions
				// for TIDAL calculation

  //! Call once to set satelliet body orientation
  void setTidalOrientation(double phi, double theta, double psi);

  //! Call to set satellite position
  void setTidalPosition(double T, int NI=0);

  //! Retrieve satellite time
  double Time(void) { return currentTime; }

  //! Get tidal force
  Vector tidalForce(const Vector p);

  //! Get tidal force
  Vector tidalForce(const double x, const double y, const double z);

  //! Get tidal force
  Vector tidalForce(const Vector p, const Vector q);

  //! Get tidal force
  Vector tidalForce(const double x, const double y, const double z,
		    const double u, const double v, const double w);

};

#endif
