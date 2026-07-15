#pragma once
// 최소 HTTP/1.1 클라이언트 백엔드 (native socket, 셸/외부 lib 미사용).
//  - TCP: http.probe (루프백 헤더 점검)
//  - AF_UNIX: docker.api_get (/var/run/docker.sock)
//  - TLS(https) 미지원(v1) → op 에서 na 처리. Windows npipe 미지원(v1).
#include <string>
#include <map>

namespace wu::platform {

struct HttpResponse {
    bool        ok = false;        // 소켓 왕복 성공 여부(HTTP status 와 무관)
    int         status = 0;        // HTTP status code
    std::string statusLine;
    std::map<std::string, std::string> headers;  // 소문자 키
    std::string body;
    std::string error;             // ok=false 사유
};

// TCP: host:port 로 method path 요청. host 는 Host 헤더에도 사용.
HttpResponse http_request_tcp(const std::string& host, int port,
                              const std::string& method, const std::string& path,
                              int timeoutMs, size_t capBytes);

// AF_UNIX: socketPath 로 GET path 요청. hostHeader 는 Host 헤더값(도커는 임의값 허용).
HttpResponse http_request_unix(const std::string& socketPath, const std::string& hostHeader,
                               const std::string& method, const std::string& path,
                               int timeoutMs, size_t capBytes);

} // namespace wu::platform
