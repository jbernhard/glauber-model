// Glauber model
// Copyright 2015 Jonah E. Bernhard, J. Scott Moreland
// MIT License

#include "random.h"

namespace glauber { namespace random {

// Seed random number generator from hardware device.
Engine engine{std::random_device{}()};

}}  // namespace glauber::random
