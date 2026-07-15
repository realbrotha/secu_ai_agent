#pragma once
// 정규식 컴파일 공용 헬퍼. std::regex(ECMAScript)는 인라인 (?i) 미지원 →
// 선행 "(?i)" 를 벗겨 icase 플래그로 처리(assert.matches 와 동일 관례).
#include <regex>
#include <string>

namespace wu {

inline std::regex compile_regex(std::string pat) {
    auto flags = std::regex::ECMAScript;
    if (pat.rfind("(?i)", 0) == 0) { flags |= std::regex::icase; pat = pat.substr(4); }
    return std::regex(pat, flags);
}

} // namespace wu
