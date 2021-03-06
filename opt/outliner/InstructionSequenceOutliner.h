/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "Pass.h"

bool is_outlined_class(DexClass* cls);

struct InstructionSequenceOutlinerConfig {
  size_t min_insns_size{3};
  size_t max_insns_size{77};
  bool use_method_to_weight{true};
  bool reuse_outlined_methods_across_dexes{true};
  size_t max_outlined_methods_per_class{100};
  size_t threshold{10};
};

class InstructionSequenceOutliner : public Pass {
 public:
  InstructionSequenceOutliner() : Pass("InstructionSequenceOutlinerPass") {}

  void bind_config() override;
  void run_pass(DexStoresVector& stores,
                ConfigFiles& cfg,
                PassManager& mgr) override;

 private:
  InstructionSequenceOutlinerConfig m_config;
};
