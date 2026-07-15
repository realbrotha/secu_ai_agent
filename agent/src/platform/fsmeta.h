#pragma once
// 파일 메타데이터 백엔드 (OS 분기 격리). op 는 이 인터페이스만 본다.
//  - POSIX: lstat/stat + getpwuid/getgrgid → owner/group, setuid/setgid/sticky 포함 mode.
//  - Windows: std::filesystem 로 존재/타입/크기, 소유자는 best-effort, POSIX mode 없음(modeValid=false).
#include <string>

namespace wu::platform {

struct FileMeta {
    bool        exists     = false;
    bool        isDir      = false;
    bool        isRegular  = false;
    bool        isSymlink  = false;
    bool        isSocket   = false;
    std::string type;                 // file|dir|symlink|char|block|fifo|socket|unknown
    long long   size       = 0;
    unsigned    mode       = 0;       // 하위 12비트: setuid/setgid/sticky + rwxrwxrwx (POSIX)
    bool        modeValid  = false;   // Windows 는 false
    int         uid        = -1;
    int         gid        = -1;
    std::string owner;                // 이름 해석 실패 시 빈 문자열
    std::string group;
    std::string linkTarget;           // isSymlink 일 때 원본 링크 대상(raw)
};

// follow_symlinks=false 면 심링크 자체(lstat), true 면 대상(stat).
FileMeta stat_file(const std::string& path, bool follow_symlinks);

} // namespace wu::platform
