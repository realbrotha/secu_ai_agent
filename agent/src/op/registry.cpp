#include "op/registry.h"

namespace wu {

OpRegistry& OpRegistry::instance() {
    static OpRegistry inst;
    return inst;
}

void OpRegistry::add(std::unique_ptr<IOp> op) {
    const std::string name = op->descriptor().name;
    ops_[name] = std::move(op);
}

IOp* OpRegistry::find(const std::string& name) const {
    auto it = ops_.find(name);
    return it == ops_.end() ? nullptr : it->second.get();
}

std::vector<IOp*> OpRegistry::all() const {
    std::vector<IOp*> out;
    out.reserve(ops_.size());
    for (const auto& [k, v] : ops_) out.push_back(v.get());
    return out;
}

} // namespace wu
