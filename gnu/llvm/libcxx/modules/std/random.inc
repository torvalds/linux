// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std {
  // [rand.req.urng], uniform random bit generator requirements
  using std::uniform_random_bit_generator;

  // [rand.eng.lcong], class template linear_congruential_engine
  using std::linear_congruential_engine;

  // [rand.eng.mers], class template mersenne_twister_engine
  using std::mersenne_twister_engine;

  // [rand.eng.sub], class template subtract_with_carry_engine
  using std::subtract_with_carry_engine;

  // [rand.adapt.disc], class template discard_block_engine
  using std::discard_block_engine;

  // [rand.adapt.ibits], class template independent_bits_engine
  using std::independent_bits_engine;

  // [rand.adapt.shuf], class template shuffle_order_engine
  using std::shuffle_order_engine;

  // [rand.predef], engines and engine adaptors with predefined parameters
  using std::knuth_b;
  using std::minstd_rand;
  using std::minstd_rand0;
  using std::mt19937;
  using std::mt19937_64;
  using std::ranlux24;
  using std::ranlux24_base;
  using std::ranlux48;
  using std::ranlux48_base;

  using std::default_random_engine;

#ifndef _LIBCPP_HAS_NO_RANDOM_DEVICE
  // [rand.device], class random_device
  using std::random_device;
#endif

  // [rand.util.seedseq], class seed_seq
  using std::seed_seq;

  // [rand.util.canonical], function template generate_canonical
  using std::generate_canonical;

  // [rand.dist.uni.int], class template uniform_int_distribution
  using std::uniform_int_distribution;

  // [rand.dist.uni.real], class template uniform_real_distribution
  using std::uniform_real_distribution;

  // [rand.dist.bern.bernoulli], class bernoulli_distribution
  using std::bernoulli_distribution;

  // [rand.dist.bern.bin], class template binomial_distribution
  using std::binomial_distribution;

  // [rand.dist.bern.geo], class template geometric_distribution
  using std::geometric_distribution;

  // [rand.dist.bern.negbin], class template negative_binomial_distribution
  using std::negative_binomial_distribution;

  // [rand.dist.pois.poisson], class template poisson_distribution
  using std::poisson_distribution;

  // [rand.dist.pois.exp], class template exponential_distribution
  using std::exponential_distribution;

  // [rand.dist.pois.gamma], class template gamma_distribution
  using std::gamma_distribution;

  // [rand.dist.pois.weibull], class template weibull_distribution
  using std::weibull_distribution;

  // [rand.dist.pois.extreme], class template extreme_value_distribution
  using std::extreme_value_distribution;

  // [rand.dist.norm.normal], class template normal_distribution
  using std::normal_distribution;

  // [rand.dist.norm.lognormal], class template lognormal_distribution
  using std::lognormal_distribution;

  // [rand.dist.norm.chisq], class template chi_squared_distribution
  using std::chi_squared_distribution;

  // [rand.dist.norm.cauchy], class template cauchy_distribution
  using std::cauchy_distribution;

  // [rand.dist.norm.f], class template fisher_f_distribution
  using std::fisher_f_distribution;

  // [rand.dist.norm.t], class template student_t_distribution
  using std::student_t_distribution;

  // [rand.dist.samp.discrete], class template discrete_distribution
  using std::discrete_distribution;

  // [rand.dist.samp.pconst], class template piecewise_constant_distribution
  using std::piecewise_constant_distribution;

  // [rand.dist.samp.plinear], class template piecewise_linear_distribution
  using std::piecewise_linear_distribution;
} // namespace std
