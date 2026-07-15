// 최소 HTTP/1.1 클라이언트 — Windows(winsock). TCP 지원, AF_UNIX(docker)는 v1 미지원(na).
#if defined(_WIN32)
#include "platform/netclient.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <sstream>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#pragma comment(lib, "ws2_32.lib")

namespace wu::platform {

namespace {

struct WsaInit { WsaInit() { WSADATA d; WSAStartup(MAKEWORD(2,2), &d); } ~WsaInit() { WSACleanup(); } };

std::string lower(std::string s) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; }
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"); size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

void parse_response(const std::string& raw, HttpResponse& r) {
    auto hdrEnd = raw.find("\r\n\r\n");
    if (hdrEnd == std::string::npos) { r.error = "잘못된 HTTP 응답"; return; }
    std::string head = raw.substr(0, hdrEnd);
    std::string body = raw.substr(hdrEnd + 4);
    std::istringstream hs(head); std::string line;
    std::getline(hs, r.statusLine);
    if (!r.statusLine.empty() && r.statusLine.back() == '\r') r.statusLine.pop_back();
    { std::istringstream sl(r.statusLine); std::string ver; sl >> ver >> r.status; }
    while (std::getline(hs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto c = line.find(':'); if (c == std::string::npos) continue;
        r.headers[lower(trim(line.substr(0, c)))] = trim(line.substr(c + 1));
    }
    auto te = r.headers.find("transfer-encoding");
    if (te != r.headers.end() && lower(te->second).find("chunked") != std::string::npos) {
        std::string decoded; size_t pos = 0;
        while (pos < body.size()) {
            auto nl = body.find("\r\n", pos); if (nl == std::string::npos) break;
            long sz = std::strtol(body.substr(pos, nl - pos).c_str(), nullptr, 16);
            if (sz <= 0) break; pos = nl + 2;
            if (pos + (size_t)sz > body.size()) { decoded.append(body, pos, body.size() - pos); break; }
            decoded.append(body, pos, (size_t)sz); pos += (size_t)sz + 2;
        }
        r.body = decoded;
    } else r.body = body;
    r.ok = true;
}

std::string build_request(const std::string& method, const std::string& path, const std::string& host) {
    std::string m = method.empty() ? "GET" : method;
    return m + " " + path + " HTTP/1.1\r\nHost: " + host +
           "\r\nUser-Agent: wu-agent\r\nAccept: */*\r\nConnection: close\r\n\r\n";
}

} // namespace

HttpResponse http_request_tcp(const std::string& host, int port,
                              const std::string& method, const std::string& path,
                              int timeoutMs, size_t capBytes) {
    HttpResponse r;
    static WsaInit wsa;
    char portstr[16]; std::snprintf(portstr, sizeof(portstr), "%d", port);
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), portstr, &hints, &res) != 0 || !res) { r.error = "주소 해석 실패"; return r; }
    SOCKET fd = INVALID_SOCKET;
    for (auto* p = res; p; p = p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd == INVALID_SOCKET) continue;
        DWORD tv = (DWORD)timeoutMs;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
        if (::connect(fd, p->ai_addr, (int)p->ai_addrlen) == 0) break;
        closesocket(fd); fd = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    if (fd == INVALID_SOCKET) { r.error = "연결 실패"; return r; }
    std::string req = build_request(method, path, host);
    if (::send(fd, req.data(), (int)req.size(), 0) == SOCKET_ERROR) { r.error = "전송 실패"; closesocket(fd); return r; }
    std::string raw; char buf[8192]; int n;
    while (raw.size() < capBytes && (n = ::recv(fd, buf, sizeof(buf), 0)) > 0) raw.append(buf, (size_t)n);
    closesocket(fd);
    if (raw.empty()) { r.error = "응답 없음"; return r; }
    parse_response(raw, r);
    return r;
}

HttpResponse http_request_unix(const std::string&, const std::string&,
                               const std::string&, const std::string&, int, size_t) {
    HttpResponse r;
    r.error = "Windows 에서 unix 소켓(docker) 미지원(v1)";
    return r;
}

} // namespace wu::platform
#endif // _WIN32
