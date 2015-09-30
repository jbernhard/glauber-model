// Glauber model
// Copyright 2015 Jonah E. Bernhard, J. Scott Moreland
// MIT License

#include "event.h"

#include <algorithm>
#include <cmath>

#include <boost/program_options/variables_map.hpp>

#include "nucleus.h"

namespace glauber {

namespace {

constexpr double TINY = 1e-12;

}  // unnamed namespace

// Determine the grid parameters like so:
//   1. Read and set step size from the configuration.
//   2. Read grid max from the config, then set the number of steps as
//      nsteps = ceil(2*max/step).
//   3. Set the actual grid max as max = nsteps*step/2.  Hence if the step size
//      does not evenly divide the config max, the actual max will be marginally
//      larger (by at most one step size).
Event::Event(const VarMap& var_map)
    : norm_(var_map["normalization"].as<double>()),
      dxy_(var_map["grid-step"].as<double>()),
      nsteps_(std::ceil(2.*var_map["grid-max"].as<double>()/dxy_)),
      xymax_(.5*nsteps_*dxy_),
      TA_(boost::extents[nsteps_][nsteps_]),
      TB_(boost::extents[nsteps_][nsteps_]),
      TR_(boost::extents[nsteps_][nsteps_]) {
}

void Event::compute(const Nucleus& nucleusA, const Nucleus& nucleusB,
                    NucleonProfile& profile) {
  // Reset npart; compute_nuclear_thickness() increments it.
  npart_ = 0;
  compute_nuclear_thickness(nucleusA, profile, TA_);
  compute_nuclear_thickness(nucleusB, profile, TB_);
  compute_reduced_thickness();
  compute_observables();
}

namespace {

// Limit a value to a range.
// Used below to constrain grid indices.
template <typename T>
inline const T& clip(const T& value, const T& min, const T& max) {
  if (value < min)
    return min;
  if (value > max)
    return max;
  return value;
}

}  // unnamed namespace

void Event::compute_nuclear_thickness(
    const Nucleus& nucleus, NucleonProfile& profile, Grid& TX) {
  // Construct the thickness grid by looping over participants and adding each
  // to a small subgrid within its radius.  Compared to the other possibility
  // (grid cells as the outer loop and participants as the inner loop), this
  // reduces the number of required distance-squared calculations by a factor of
  // ~20 (depending on the nucleon size).  The Event unit test verifies that the
  // two methods agree.

  // Wipe grid with zeros.
  std::fill(TX.origin(), TX.origin() + TX.num_elements(), 0.);

  const double r = profile.radius();

  // Deposit each participant onto the grid.
  for (const auto& nucleon : nucleus) {
    if (!nucleon.is_participant())
      continue;

    ++npart_;

    // Work in coordinates relative to (-width/2, -width/2).
    double x = nucleon.x() + xymax_;
    double y = nucleon.y() + xymax_;

    // Determine min & max indices of nucleon subgrid.
    int ixmin = clip(static_cast<int>((x-r)/dxy_), 0, nsteps_-1);
    int iymin = clip(static_cast<int>((y-r)/dxy_), 0, nsteps_-1);
    int ixmax = clip(static_cast<int>((x+r)/dxy_), 0, nsteps_-1);
    int iymax = clip(static_cast<int>((y+r)/dxy_), 0, nsteps_-1);

    // Prepare profile for new nucleon.
    profile.fluctuate(nucleon);

    // Add profile to grid.
    for (auto iy = iymin; iy <= iymax; ++iy) {
      double dysq = std::pow(y - (static_cast<double>(iy)+.5)*dxy_, 2);
      for (auto ix = ixmin; ix <= ixmax; ++ix) {
        double dxsq = std::pow(x - (static_cast<double>(ix)+.5)*dxy_, 2);
        TX[iy][ix] += profile.thickness(dxsq + dysq);
      }
    }
  }
}

void Event::compute_reduced_thickness() {
  double sum = 0.;
  double ixcm = 0.;
  double iycm = 0.;

  for (int iy = 0; iy < nsteps_; ++iy) {
    for (int ix = 0; ix < nsteps_; ++ix) {
      auto t = norm_ * (TA_[iy][ix] + TB_[iy][ix]);
      TR_[iy][ix] = t;
      sum += t;
      // Center of mass grid indices.
      // No need to multiply by dxy since it would be canceled later.
      ixcm += t * static_cast<double>(ix);
      iycm += t * static_cast<double>(iy);
    }
  }

  multiplicity_ = dxy_ * dxy_ * sum;
  ixcm_ = ixcm / sum;
  iycm_ = iycm / sum;
}

void Event::compute_observables() {
  // Compute eccentricity.

  // Simple helper class for use in the following loop.
  struct EccentricityAccumulator {
    double re = 0.;  // real part
    double im = 0.;  // imaginary part
    double wt = 0.;  // weight
    double finish() const  // compute final eccentricity
    { return std::sqrt(re*re + im*im) / std::fmax(wt, TINY); }
  } e2, e3, e4, e5;

  for (int iy = 0; iy < nsteps_; ++iy) {
    for (int ix = 0; ix < nsteps_; ++ix) {
      const auto& t = TR_[iy][ix];
      if (t < TINY)
        continue;

      // Compute (x, y) relative to the CM and cache powers of x, y, r.
      auto x = static_cast<double>(ix) - ixcm_;
      auto x2 = x*x;
      auto x3 = x2*x;
      auto x4 = x2*x2;

      auto y = static_cast<double>(iy) - iycm_;
      auto y2 = y*y;
      auto y3 = y2*y;
      auto y4 = y2*y2;

      auto r2 = x2 + y2;
      auto r = std::sqrt(r2);
      auto r4 = r2*r2;

      auto xy = x*y;
      auto x2y2 = x2*y2;

      // The eccentricity harmonics are weighted averages of r^n*exp(i*n*phi)
      // over the entropy profile (reduced thickness).  The naive way to compute
      // exp(i*n*phi) at a given (x, y) point is essentially:
      //
      //   phi = arctan2(y, x)
      //   real = cos(n*phi)
      //   imag = sin(n*phi)
      //
      // However this implementation uses three unnecessary trig functions; a
      // much faster method is to express the cos and sin directly in terms of x
      // and y.  For example, it is trivial to show (by drawing a triangle and
      // using rudimentary trig) that
      //
      //   cos(arctan2(y, x)) = x/r = x/sqrt(x^2 + y^2)
      //   sin(arctan2(y, x)) = y/r = x/sqrt(x^2 + y^2)
      //
      // This is easily generalized to cos and sin of (n*phi) by invoking the
      // multiple angle formula, e.g. sin(2x) = 2sin(x)cos(x), and hence
      //
      //   sin(2*arctan2(y, x)) = 2*sin(arctan2(y, x))*cos(arctan2(y, x))
      //                        = 2*x*y / r^2
      //
      // Which not only eliminates the trig functions, but also naturally
      // cancels the r^2 weight.  This cancellation occurs for all n.
      //
      // The Event unit test verifies that the two methods agree.
      e2.re += t * (y2 - x2);
      e2.im += t * 2.*xy;
      e2.wt += t * r2;

      e3.re += t * (y3 - 3.*y*x2);
      e3.im += t * (3.*x*y2 - x3);
      e3.wt += t * r2*r;

      e4.re += t * (x4 + y4 - 6.*x2y2);
      e4.im += t * 4.*xy*(y2 - x2);
      e4.wt += t * r4;

      e5.re += t * y*(5.*x4 - 10.*x2y2 + y4);
      e5.im += t * x*(x4 - 10.*x2y2 + 5.*y4);
      e5.wt += t * r4*r;
    }
  }

  eccentricity_[2] = e2.finish();
  eccentricity_[3] = e3.finish();
  eccentricity_[4] = e4.finish();
  eccentricity_[5] = e5.finish();
}

}  // namespace glauber
