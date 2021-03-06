/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "CallGraph.h"

#include <utility>

#include "MethodOverrideGraph.h"
#include "Walkers.h"

namespace mog = method_override_graph;

namespace {

using namespace call_graph;

class SingleCalleeStrategy final : public BuildStrategy {
 public:
  explicit SingleCalleeStrategy(const Scope& scope) : m_scope(scope) {
    auto non_virtual_vec = mog::get_non_true_virtuals(scope);
    m_non_virtual.insert(non_virtual_vec.begin(), non_virtual_vec.end());
  }

  CallSites get_callsites(const DexMethod* method) const override {
    CallSites callsites;
    auto* code = const_cast<IRCode*>(method->get_code());
    if (code == nullptr) {
      return callsites;
    }
    for (auto& mie : InstructionIterable(code)) {
      auto insn = mie.insn;
      if (is_invoke(insn->opcode())) {
        auto callee = resolve_method(insn->get_method(),
                                     opcode_to_search(insn),
                                     m_resolved_refs,
                                     method);
        if (callee == nullptr || is_definitely_virtual(callee)) {
          continue;
        }
        if (callee->is_concrete()) {
          callsites.emplace_back(callee, code->iterator_to(mie));
        }
      }
    }
    return callsites;
  }

  std::vector<const DexMethod*> get_roots() const override {
    std::vector<const DexMethod*> roots;

    walk::code(m_scope, [&](DexMethod* method, IRCode& code) {
      if (is_definitely_virtual(method) || root(method) ||
          method::is_clinit(method)) {
        roots.emplace_back(method);
      }
    });
    return roots;
  }

 private:
  bool is_definitely_virtual(DexMethod* method) const {
    return method->is_virtual() && m_non_virtual.count(method) == 0;
  }

  const Scope& m_scope;
  std::unordered_set<DexMethod*> m_non_virtual;
  mutable MethodRefCache m_resolved_refs;
};

class CompleteCallGraphStrategy final : public BuildStrategy {
 public:
  explicit CompleteCallGraphStrategy(const Scope& scope)
      : m_scope(scope), m_method_override_graph(mog::build_graph(scope)) {}

  CallSites get_callsites(const DexMethod* method) const override {
    CallSites callsites;
    auto* code = const_cast<IRCode*>(method->get_code());
    if (code == nullptr) {
      return callsites;
    }
    for (auto& mie : InstructionIterable(code)) {
      auto insn = mie.insn;
      if (is_invoke(insn->opcode())) {
        auto callee = resolve_method(insn->get_method(),
                                     opcode_to_search(insn),
                                     m_resolved_refs,
                                     method);
        if (callee == nullptr) {
          continue;
        }
        if (callee->is_concrete()) {
          callsites.emplace_back(callee, code->iterator_to(mie));
        }
        auto overriding =
            mog::get_overriding_methods(*m_method_override_graph, callee);

        for (auto m : overriding) {
          callsites.emplace_back(m, code->iterator_to(mie));
        }
      }
    }
    return callsites;
  }

  std::vector<const DexMethod*> get_roots() const override {
    std::vector<const DexMethod*> roots;

    walk::methods(m_scope, [&](DexMethod* method) {
      if (root(method) || method::is_clinit(method)) {
        roots.emplace_back(method);
      }
    });
    return roots;
  }

 private:
  const Scope& m_scope;
  mutable MethodRefCache m_resolved_refs;
  std::unique_ptr<const mog::Graph> m_method_override_graph;
};
} // namespace

namespace call_graph {

Graph single_callee_graph(const Scope& scope) {
  return Graph(SingleCalleeStrategy(scope));
}

Graph complete_call_graph(const Scope& scope) {
  return Graph(CompleteCallGraphStrategy(scope));
}

Edge::Edge(NodeId caller, NodeId callee, const IRList::iterator& invoke_it)
    : m_caller(std::move(caller)),
      m_callee(std::move(callee)),
      m_invoke_it(invoke_it) {}

Graph::Graph(const BuildStrategy& strat)
    : m_entry(std::make_shared<Node>(Node::GHOST_ENTRY)),
      m_exit(std::make_shared<Node>(Node::GHOST_EXIT)) {
  // Add edges from the single "ghost" entry node to all the "real" entry
  // nodes in the graph.
  auto roots = strat.get_roots();
  for (const DexMethod* root : roots) {
    auto edge =
        std::make_shared<Edge>(m_entry, make_node(root), IRList::iterator());
    m_entry->m_successors.emplace_back(edge);
    make_node(root)->m_predecessors.emplace_back(edge);
  }

  // Obtain the callsites of each method recursively, building the graph in the
  // process.
  std::unordered_set<const DexMethod*> visited;
  auto visit = [&](const auto* caller) {
    auto visit_impl = [&](const auto* caller, auto& visit_fn) {
      if (visited.count(caller) != 0) {
        return;
      }
      visited.emplace(caller);
      auto callsites = strat.get_callsites(caller);
      if (callsites.empty()) {
        this->add_edge(make_node(caller), m_exit, IRList::iterator());
      }
      for (const auto& callsite : callsites) {
        this->add_edge(
            make_node(caller), make_node(callsite.callee), callsite.invoke);
        visit_fn(callsite.callee, visit_fn);
      }
    };
    visit_impl(caller, visit_impl);
  };

  for (const DexMethod* root : roots) {
    visit(root);
  }
}

NodeId Graph::make_node(const DexMethod* m) {
  auto it = m_nodes.find(m);
  if (it != m_nodes.end()) {
    return it->second;
  }
  m_nodes.emplace(m, std::make_shared<Node>(m));
  return m_nodes.at(m);
}

void Graph::add_edge(const NodeId& caller,
                     const NodeId& callee,
                     const IRList::iterator& invoke_it) {
  auto edge = std::make_shared<Edge>(caller, callee, invoke_it);
  caller->m_successors.emplace_back(edge);
  callee->m_predecessors.emplace_back(edge);
}

} // namespace call_graph
