// 최소 HTTP/1.1 클라이언트 — POSIX(BSD sockets, AF_INET/AF_UNIX). 셸/외부 lib 미사용.
#if !defined(_WIN32)
#include "platform/netclient.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sstream>
#include <algorithm>

namespace wu::platform {

namespace {

std::string lower(std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; }
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"); size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

bool set_timeout(int fd, int timeoutMs) {
    struct timeval tv; tv.tv_sec = timeoutMs / 1000; tv.tv_usec = (timeoutMs % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    return true;
}

bool send_all(int fd, const std::string& data) {
    size_t off = 0;
    while (off < data.size()) {
        ssize_t n = ::send(fd, data.data() + off, data.size() - off, 0);
        if (n <= 0) return false;
        off += (size_t)n;
    }
    return true;
}

std::string recv_all(int fd, size_t capBytes) {
    std::string out;
    char buf[8192];
    while (out.size() < capBytes) {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        out.append(buf, (size_t)n);
    }
    return out;
}

// raw HTTP 응답 파싱(헤더 + content-length/chunked 바디)
void parse_response(const std::string& raw, HttpResponse& r) {
    auto hdrEnd = raw.find("\r\n\r\n");
    if (hdrEnd == std::string::npos) { r.error = "잘못된 HTTP 응답"; return; }
    std::string head = raw.substr(0, hdrEnd);
    std::string body = raw.substr(hdrEnd + 4);

    std::istringstream hs(head);
    std::string line;
    std::getline(hs, r.statusLine);
    if (!r.statusLine.empty() && r.statusLine.back() == '\r') r.statusLine.pop_back();
    {
        std::istringstream sl(r.statusLine); std::string ver; sl >> ver >> r.status;
    }
    while (std::getline(hs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto c = line.find(':');
        if (c == std::string::npos) continue;
        r.headers[lower(trim(line.substr(0, c)))] = trim(line.substr(c + 1));
    }

    // chunked transfer-encoding 디코드
    auto te = r.headers.find("transfer-encoding");
    if (te != r.headers.end() && lower(te->second).find("chunked") != std::string::npos) {
        std::string decoded; size_t pos = 0;
        while (pos < body.size()) {
            auto nl = body.find("\r\n", pos);
            if (nl == std::string::npos) break;
            long sz = std::strtol(body.substr(pos, nl - pos).c_str(), nullptr, 16);
            if (sz <= 0) break;
            pos = nl + 2;
            if (pos + (size_t)sz > body.size()) { decoded.append(body, pos, body.size() - pos); break; }
            decoded.append(body, pos, (size_t)sz);
            pos += (size_t)sz + 2;
        }
        r.body = decoded;
    } else {
        r.body = body;
    }
    r.ok = true;
}

std::string build_request(const std::string& method, const std::string& path, const std::string& host) {
    std::string m = method.empty() ? "GET" : method;
    return m + " " + path + " HTTP/1.1\r\n"
           "Host: " + host + "\r\n"
           "User-Agent: wu-agent\r\n"
           "Accept: */*\r\n"
           "Connection: close\r\n\r\n";
}

} // namespace

HttpResponse http_request_tcp(const std::string& host, int port,
                              const std::string& method, const std::string& path,
                              int timeoutMs, size_t capBytes) {
    HttpResponse r;
    char portstr[16]; std::snprintf(portstr, sizeof(portstr), "%d", port);
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), portstr, &hints, &res) != 0 || !res) {
        r.error = "주소 해석 실패: " + host; return r;
    }
    int fd = -1;
    for (auto* p = res; p; p = p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        set_timeout(fd, timeoutMs);
        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        ::close(fd); fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) { r.error = "연결 실패: " + host + ":" + portstr; return r; }

    if (!send_all(fd, build_request(method, path, host))) { r.error = "전송 실패"; ::close(fd); return r; }
    std::string raw = recv_all(fd, capBytes);
    ::close(fd);
    if (raw.empty()) { r.error = "응답 없음"; return r; }
    parse_response(raw, r);
    return r;
}

HttpResponse http_request_unix(const std::string& socketPath, const std::string& hostHeader,
                               const std::string& method, const std::string& path,
                               int timeoutMs, size_t capBytes) {
    HttpResponse r;
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { r.error = "소켓 생성 실패"; return r; }
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (socketPath.size() >= sizeof(addr.sun_path)) { r.error = "소켓 경로 과다"; ::close(fd); return r; }
    std::strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);
    set_timeout(fd, timeoutMs);
    if (::connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        r.error = std::string("소켓 연결 실패: ") + std::strerror(errno); ::close(fd); return r;
    }
    if (!send_all(fd, build_request(method, path, hostHeader.empty() ? "localhost" : hostHeader))) {
        r.error = "전송 실패"; ::close(fd); return r;
    }
    std::string raw = recv_all(fd, capBytes);
    ::close(fd);
    if (raw.empty()) { r.error = "응답 없음"; return r; }
    parse_response(raw, r);
    return r;
}

} // namespace wu::platform
#endif // !_WIN32
