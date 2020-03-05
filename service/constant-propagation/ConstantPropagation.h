/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "ConstantPropagationTransform.h"
#include "IRCode.h"

namespace constant_propagation {

struct Config {
  Transform::Config transform;
};

class ConstantPropagation final {
 public:
  explicit ConstantPropagation(const Config& config) : m_config(config) {}

  Transform::Stats run(DexMethod* method);
  Transform::Stats run(const Scope& scope);

 private:
  const Config& m_config;
};
} // namespace constant_propagation