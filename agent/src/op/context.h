#pragma once
// PathGuard — fs 계열 op 의 경로 안전 강제 (final_plan.md §3, §6.8)
//  - allow 루트 이하만 허용, deny 접두 차단
//  - 경로 경계 인식(/opt/tomcat 이 /opt/tomcat-evil 을 매칭하지 않음)
//  - 심링크 이탈 방지: weakly_canonical 로 실제 경로 해석 후 비교
#include <string>
#include <vector>
#include <filesystem>

namespace wu {

class PathGuard {
public:
    PathGuard(std::vector<std::string> allow, std::vector<std::string> deny) {
        for (auto& a : allow) allow_.push_back(canon(a));
        for (auto& d : deny)  deny_.push_back(canon(d));
    }

    bool allowed(const std::string& path) const {
        const std::string p = canon(path);
        for (const auto& d : deny_)  if (under(p, d)) return false;
        if (allow_.empty()) return true;               // allowlist 미설정 시 deny 만 적용
        for (const auto& a : allow_) if (under(p, a)) return true;
        return false;
    }

private:
    static std::string canon(const std::string& s) {
        std::error_code ec;
        auto c = std::filesystem::weakly_canonical(s, ec);
        std::string r = ec ? s : c.string();
        // 표준화: 후행 구분자 제거
        while (r.size() > 1 && (r.back() == '/' || r.back() == '\\')) r.pop_back();
        return r;
    }
    // p 가 root 이하(또는 동일)인가 — 경로 경계 존중
    static bool under(const std::string& p, const std::string& root) {
        if (p == root) return true;
        if (p.size() <= root.size()) return false;
        if (p.compare(0, root.size(), root) != 0) return false;
        const char sep = p[root.size()];
        return sep == '/' || sep == '\\';
    }
    std::vector<std::string> allow_, deny_;
};

} // namespace wu
