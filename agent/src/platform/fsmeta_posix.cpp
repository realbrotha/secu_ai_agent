// 파일 메타데이터 — POSIX(lstat/stat + passwd/group) 백엔드. 셸 미사용.
#if !defined(_WIN32)
#include "platform/fsmeta.h"

#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <cerrno>
#include <vector>

namespace wu::platform {

static std::string uid_to_name(uid_t uid) {
    struct passwd pw{};
    struct passwd* res = nullptr;
    std::vector<char> buf(1024);
    while (true) {
        int rc = getpwuid_r(uid, &pw, buf.data(), buf.size(), &res);
        if (rc == ERANGE && buf.size() < (1 << 20)) { buf.resize(buf.size() * 2); continue; }
        break;
    }
    return res && res->pw_name ? std::string(res->pw_name) : std::string();
}

static std::string gid_to_name(gid_t gid) {
    struct group gr{};
    struct group* res = nullptr;
    std::vector<char> buf(1024);
    while (true) {
        int rc = getgrgid_r(gid, &gr, buf.data(), buf.size(), &res);
        if (rc == ERANGE && buf.size() < (1 << 20)) { buf.resize(buf.size() * 2); continue; }
        break;
    }
    return res && res->gr_name ? std::string(res->gr_name) : std::string();
}

FileMeta stat_file(const std::string& path, bool follow_symlinks) {
    FileMeta m;
    struct stat st{};
    int rc = follow_symlinks ? ::stat(path.c_str(), &st) : ::lstat(path.c_str(), &st);
    if (rc != 0) return m;   // exists=false

    m.exists     = true;
    m.modeValid  = true;
    m.mode       = st.st_mode & 07777;   // setuid/setgid/sticky + rwx
    m.size       = (long long)st.st_size;
    m.uid        = (int)st.st_uid;
    m.gid        = (int)st.st_gid;
    m.owner      = uid_to_name(st.st_uid);
    m.group      = gid_to_name(st.st_gid);

    if (S_ISDIR(st.st_mode))       { m.isDir = true;     m.type = "dir"; }
    else if (S_ISREG(st.st_mode))  { m.isRegular = true; m.type = "file"; }
    else if (S_ISLNK(st.st_mode))  { m.isSymlink = true; m.type = "symlink"; }
    else if (S_ISSOCK(st.st_mode)) { m.isSocket = true;  m.type = "socket"; }
    else if (S_ISCHR(st.st_mode))  { m.type = "char"; }
    else if (S_ISBLK(st.st_mode))  { m.type = "block"; }
    else if (S_ISFIFO(st.st_mode)) { m.type = "fifo"; }
    else                            { m.type = "unknown"; }

    if (m.isSymlink) {
        char buf[4096];
        ssize_t n = ::readlink(path.c_str(), buf, sizeof(buf) - 1);
        if (n > 0) { buf[n] = '\0'; m.linkTarget = buf; }
    }
    return m;
}

} // namespace wu::platform
#endif // !_WIN32
