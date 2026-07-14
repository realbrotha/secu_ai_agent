#pragma once
// op 레지스트리 + 자기등록 (final_plan.md §6.3)
#include "op/op.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace wu {

class OpRegistry {
public:
    static OpRegistry& instance();

    void  add(std::unique_ptr<IOp> op);   // 자기등록(REGISTER_OP)이 호출
    IOp*  find(const std::string& name) const;
    std::vector<IOp*> all() const;        // 프롬프트 도구스키마/문서 생성용

private:
    std::map<std::string, std::unique_ptr<IOp>> ops_;
};

} // namespace wu

// op 파일 맨 아래 한 줄이면 등록 끝. core 무수정.
#define REGISTER_OP(C)                                                    \
    namespace {                                                           \
        const bool _wu_reg_##C = [] {                                     \
            ::wu::OpRegistry::instance().add(std::make_unique<C>());      \
            return true;                                                  \
        }();                                                             \
    }
