//
// Created by Vasiliy Ershov on 08/11/2016.
//

#include "gamma_poisson_model.hpp"

using namespace n_gamma_poisson_model;

std::array<double, 100000> PoissonGammaDistribution::LogGammaIntegerCache =
    []() -> std::array<double, 100000> {
  std::array<double, 100000> cache;
  for (int i = 0; i < cache.size(); ++i) {
    cache[i] = boost::math::lgamma(i + 1);
  }
  return cache;
}();
