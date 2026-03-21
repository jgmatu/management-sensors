#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Botan {
class Private_Key;
class Public_Key;
}

struct JwtClaims
{
    std::string sub;
    std::string role;
    uint64_t iat{0};
    uint64_t exp{0};
};

class JwtManager
{
public:
    JwtManager(const std::string& private_key_path,
               const std::string& certificate_path);
    ~JwtManager();

    std::string generate(const std::string& subject,
                         const std::string& role,
                         uint64_t ttl_seconds = 3600) const;

    std::optional<JwtClaims> verify(const std::string& token) const;

private:
    static std::string base64url_encode(const std::vector<uint8_t>& data);
    static std::string base64url_encode(const std::string& data);
    static std::vector<uint8_t> base64url_decode(const std::string& input);

    std::unique_ptr<Botan::Private_Key> private_key_;
    std::unique_ptr<Botan::Public_Key> public_key_;
};
