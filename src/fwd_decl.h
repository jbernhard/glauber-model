// Glauber model
// Copyright 2015 Jonah E. Bernhard, J. Scott Moreland
// MIT License

#ifndef FWD_DECL_H
#define FWD_DECL_H

// forward declarations

namespace boost {

namespace filesystem {}

namespace math {}

namespace program_options {
class variables_map;
}

}  // namespace boost

namespace fs = boost::filesystem;
namespace po = boost::program_options;
namespace math = boost::math;

using VarMap = po::variables_map;

namespace glauber {

class Nucleus;

}  // namespace glauber

#endif  // FWD_DECL_H
