// op: sys.accounts — /etc/passwd·group·shadow 모델 (네이티브 getent, NSS 손실 감수).
//   observe 모드: users/groups/uid0Users/duplicateUids 반환.
//   rule 모드(pass.rule:"op_verdict"): uid0_unique|duplicate_uid|empty_password|login_shell|password_algo.
#include "op/op.h"
#include "op/registry.h"
#include "op/context.h"
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>

namespace wu {

namespace {

std::vector<std::string> split(const std::string& s, char d) {
    std::vector<std::string> out; std::string cur; std::istringstream ss(s);
    while (std::getline(ss, cur, d)) out.push_back(cur);
    if (!s.empty() && s.back() == d) out.push_back("");
    return out;
}

std::string pw_algo(const std::string& hash) {
    if (hash.empty() || hash == "*" || hash == "!" || hash == "!!") return "locked/none";
    if (hash[0] != '$') return "des/legacy";
    if (hash.rfind("$1$", 0) == 0) return "md5";
    if (hash.rfind("$2", 0) == 0)  return "bcrypt";
    if (hash.rfind("$5$", 0) == 0) return "sha256";
    if (hash.rfind("$6$", 0) == 0) return "sha512";
    if (hash.rfind("$y$", 0) == 0) return "yescrypt";
    if (hash.rfind("$7$", 0) == 0) return "scrypt";
    return "unknown";
}

bool is_login_shell(const std::string& sh) {
    if (sh.empty()) return true;   // 빈 shell = 기본 shell 로그인 가능
    std::string l = sh; std::transform(l.begin(), l.end(), l.begin(), ::tolower);
    return l.find("nologin") == std::string::npos && l.find("false") == std::string::npos
        && l != "/dev/null" && l.find("/sync") == std::string::npos;
}

} // namespace

class SysAccountsOp : public IOp {
    static OpDescriptor make() {
        OpDescriptor d;
        d.name = "sys.accounts"; d.summary = "passwd/group/shadow 계정 모델 + 계정 규칙 판정";
        d.safety = Safety::read_only_local; d.os = {"linux","macos"}; d.impl = "native";
        d.params = {
            { "passwd", "path", false, "/etc/passwd", {}, "passwd 파일" },
            { "group",  "path", false, "/etc/group",  {}, "group 파일" },
            { "shadow", "path", false, "/etc/shadow", {}, "shadow 파일(권한 필요)" },
            { "rule",   "enum", false, nullptr,
              { {}, {}, {"uid0_unique","duplicate_uid","empty_password","login_shell","password_algo"}, "" },
              "판정 규칙(지정 시 verdict 반환)" },
            { "expect_algo", "string", false, "sha512", {}, "password_algo 규칙의 기대 알고리즘" },
        };
        d.returns = { {"users","array<object>",""}, {"groups","array<object>",""},
                      {"uid0Users","array<string>",""}, {"duplicateUids","array<int>",""},
                      {"verdict","string","rule 모드"}, {"detail","string",""} };
        return d;
    }
public:
    const OpDescriptor& descriptor() const override { static OpDescriptor d = make(); return d; }

    OpResult run(const json& p, OpContext& ctx) override {
        const std::string passwdPath = p.value("passwd", std::string("/etc/passwd"));
        if (ctx.guard && !ctx.guard->allowed(passwdPath)) return OpResult::err("path_guard 거부: " + passwdPath);
        std::ifstream pf(passwdPath);
        if (!pf) return OpResult::na("passwd 없음: " + passwdPath);

        // shadow (선택)
        std::map<std::string, std::vector<std::string>> shadow;
        {
            const std::string shPath = p.value("shadow", std::string("/etc/shadow"));
            if (!ctx.guard || ctx.guard->allowed(shPath)) {
                std::ifstream sf(shPath); std::string ln;
                while (std::getline(sf, ln)) {
                    if (ln.empty()) continue;
                    auto f = split(ln, ':');
                    if (!f.empty()) shadow[f[0]] = f;
                }
            }
        }

        json users = json::array(), uid0 = json::array();
        std::map<int,int> uidCount; std::map<std::string,int> nameCount;
        std::string line;
        while (std::getline(pf, line)) {
            if (line.empty() || line[0] == '#') continue;
            auto f = split(line, ':');
            if (f.size() < 7) continue;
            const std::string name = f[0];
            int uid = std::atoi(f[2].c_str());
            json u = { {"name", name}, {"uid", uid}, {"gid", std::atoi(f[3].c_str())},
                       {"home", f[5]}, {"shell", f[6]}, {"loginShell", is_login_shell(f[6])} };
            auto it = shadow.find(name);
            if (it != shadow.end() && it->second.size() >= 8) {
                const auto& s = it->second;
                u["pwAlgo"]  = pw_algo(s[1]);
                u["pwEmpty"] = s[1].empty();
                if (!s[4].empty()) u["maxDays"] = std::atoi(s[4].c_str());
                if (!s[3].empty()) u["minDays"] = std::atoi(s[3].c_str());
            }
            if (uid == 0) uid0.push_back(name);
            uidCount[uid]++; nameCount[name]++;
            users.push_back(std::move(u));
        }

        json dupUids = json::array();
        for (auto& [uid, c] : uidCount) if (c > 1) dupUids.push_back(uid);

        // groups
        json groups = json::array();
        {
            std::ifstream gf(p.value("group", std::string("/etc/group"))); std::string ln;
            while (std::getline(gf, ln)) {
                if (ln.empty() || ln[0] == '#') continue;
                auto f = split(ln, ':');
                if (f.size() < 4) continue;
                json members = json::array();
                for (auto& m : split(f[3], ',')) if (!m.empty()) members.push_back(m);
                groups.push_back({ {"name", f[0]}, {"gid", std::atoi(f[2].c_str())}, {"members", members} });
            }
        }

        // ── rule 모드: verdict 산출 ──
        if (p.contains("rule") && p["rule"].is_string()) {
            const std::string rule = p["rule"].get<std::string>();
            json out = { {"users", users}, {"groups", groups}, {"uid0Users", uid0}, {"duplicateUids", dupUids} };
            if (rule == "uid0_unique") {
                bool ok = uid0.size() == 1 && uid0[0] == "root";
                out["verdict"] = ok ? "pass" : "fail";
                out["detail"]  = ok ? "UID 0 계정은 root 뿐" : ("UID 0 계정: " + uid0.dump());
            } else if (rule == "duplicate_uid") {
                bool ok = dupUids.empty();
                out["verdict"] = ok ? "pass" : "fail";
                out["detail"]  = ok ? "중복 UID 없음" : ("중복 UID: " + dupUids.dump());
            } else if (rule == "empty_password") {
                if (shadow.empty()) { out["verdict"] = "na"; out["detail"] = "shadow 읽기 불가"; }
                else {
                    json bad = json::array();
                    for (auto& u : users) if (u.value("pwEmpty", false)) bad.push_back(u["name"]);
                    out["verdict"] = bad.empty() ? "pass" : "fail";
                    out["detail"]  = bad.empty() ? "빈 패스워드 계정 없음" : ("빈 패스워드: " + bad.dump());
                }
            } else if (rule == "login_shell") {
                // UID<1000 시스템 계정(및 root 제외) 중 로그인 shell 을 가진 것 = 취약
                json bad = json::array();
                for (auto& u : users) {
                    int uid = u.value("uid", -1);
                    if (u.value("name","") == "root") continue;
                    if (uid > 0 && uid < 1000 && u.value("loginShell", false)) bad.push_back(u["name"]);
                }
                out["verdict"] = bad.empty() ? "pass" : "fail";
                out["detail"]  = bad.empty() ? "시스템 계정 로그인 shell 제한됨" : ("로그인 shell 시스템 계정: " + bad.dump());
            } else if (rule == "password_algo") {
                if (shadow.empty()) { out["verdict"] = "na"; out["detail"] = "shadow 읽기 불가"; }
                else {
                    const std::string want = p.value("expect_algo", std::string("sha512"));
                    json bad = json::array();
                    for (auto& u : users) {
                        if (!u.contains("pwAlgo")) continue;
                        std::string a = u["pwAlgo"].get<std::string>();
                        if (a == "locked/none") continue;
                        if (a != want && !(want == "sha512" && (a == "yescrypt" || a == "bcrypt")))
                            bad.push_back(u["name"]);
                    }
                    out["verdict"] = bad.empty() ? "pass" : "fail";
                    out["detail"]  = bad.empty() ? ("패스워드 해시 " + want + " 이상") : ("취약 해시 계정: " + bad.dump());
                }
            } else {
                out["verdict"] = "na"; out["detail"] = "알 수 없는 rule: " + rule;
            }
            return OpResult::ok_(out);
        }

        return OpResult::ok_({ {"users", users}, {"groups", groups},
                               {"uid0Users", uid0}, {"duplicateUids", dupUids} });
    }
};
REGISTER_OP(SysAccountsOp)

} // namespace wu
