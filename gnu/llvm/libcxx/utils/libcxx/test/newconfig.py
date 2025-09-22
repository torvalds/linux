#===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
#===----------------------------------------------------------------------===##

import os


def _getSubstitution(substitution, config):
  for (orig, replacement) in config.substitutions:
    if orig == substitution:
      return replacement
  raise ValueError('Substitution {} is not in the config.'.format(substitution))

def configure(parameters, features, config, lit_config):
  note = lambda s: lit_config.note("({}) {}".format(config.name, s))
  config.environment = dict(os.environ)

  # Apply the actions supplied by parameters to the configuration first, since
  # parameters are things that we request explicitly and which might influence
  # what features are implicitly made available next.
  for param in parameters:
    actions = param.getActions(config, lit_config.params)
    for action in actions:
      action.applyTo(config)
      if lit_config.debug:
        note("Applied '{}' as a result of parameter '{}'".format(
          action.pretty(config, lit_config.params),
          param.pretty(config, lit_config.params)))

  # Then, apply the automatically-detected features.
  for feature in features:
    actions = feature.getActions(config)
    for action in actions:
      action.applyTo(config)
      if lit_config.debug:
        note("Applied '{}' as a result of implicitly detected feature '{}'".format(
          action.pretty(config, lit_config.params),
          feature.pretty(config)))

  # Print the basic substitutions
  for sub in ('%{cxx}', '%{flags}', '%{compile_flags}', '%{link_flags}', '%{exec}'):
    note("Using {} substitution: '{}'".format(sub, _getSubstitution(sub, config)))

  # Print all available features
  note("All available features: {}".format(', '.join(config.available_features)))
