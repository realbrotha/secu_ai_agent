#pragma once
// 최소 로거 (Phase 0). stdlib 만 사용. 이후 util/log 로 확장(레벨/파일/로테이션).
#include <iostream>
#include <string>

namespace wu::log {

inline void info(const std::string& m)  { std::cout << "[INFO]  " << m << "\n"; }
inline void warn(const std::string& m)  { std::cerr << "[WARN]  " << m << "\n"; }
inline void error(const std::string& m) { std::cerr << "[ERROR] " << m << "\n"; }

} // namespace wu::log
