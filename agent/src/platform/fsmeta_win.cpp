// 파일 메타데이터 — Windows 백엔드. POSIX mode 없음(modeValid=false), 소유자 best-effort.
#if defined(_WIN32)
#include "platform/fsmeta.h"

#include <windows.h>
#include <aclapi.h>
#include <filesystem>
#pragma comment(lib, "advapi32.lib")

namespace fs = std::filesystem;

namespace wu::platform {

static std::string owner_of(const std::string& path) {
    PSECURITY_DESCRIPTOR sd = nullptr;
    PSID ownerSid = nullptr;
    if (GetNamedSecurityInfoA(path.c_str(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
                              &ownerSid, nullptr, nullptr, nullptr, &sd) != ERROR_SUCCESS)
        return std::string();
    std::string result;
    char name[256], domain[256];
    DWORD nlen = sizeof(name), dlen = sizeof(domain);
    SID_NAME_USE use;
    if (ownerSid && LookupAccountSidA(nullptr, ownerSid, name, &nlen, domain, &dlen, &use))
        result = name;
    if (sd) LocalFree(sd);
    return result;
}

FileMeta stat_file(const std::string& path, bool follow_symlinks) {
    FileMeta m;
    std::error_code ec;
    auto st = follow_symlinks ? fs::status(path, ec) : fs::symlink_status(path, ec);
    if (ec || !fs::exists(st)) return m;

    m.exists    = true;
    m.modeValid = false;   // Windows: POSIX mode 없음
    switch (st.type()) {
        case fs::file_type::directory: m.isDir = true;     m.type = "dir";     break;
        case fs::file_type::regular:   m.isRegular = true; m.type = "file";    break;
        case fs::file_type::symlink:   m.isSymlink = true; m.type = "symlink"; break;
        case fs::file_type::character: m.type = "char";  break;
        case fs::file_type::block:     m.type = "block"; break;
        case fs::file_type::fifo:      m.type = "fifo";  break;
        case fs::file_type::socket:    m.isSocket = true; m.type = "socket"; break;
        default:                        m.type = "unknown"; break;
    }
    if (m.isRegular) { auto sz = fs::file_size(path, ec); if (!ec) m.size = (long long)sz; }
    if (m.isSymlink) { auto tgt = fs::read_symlink(path, ec); if (!ec) m.linkTarget = tgt.string(); }
    m.owner = owner_of(path);
    return m;
}

} // namespace wu::platform
#endif // _WIN32
