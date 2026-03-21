#include <jwt/JwtManager.hpp>

#include <botan/auto_rng.h>
#include <botan/base64.h>
#include <botan/data_src.h>
#include <botan/pk_keys.h>
#include <botan/pkcs8.h>
#include <botan/pubkey.h>
#include <botan/x509cert.h>

#include <boost/json.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <stdexcept>

namespace {

constexpr auto JWT_ALGO    = "ES384";
constexpr auto BOTAN_HASH  = "SHA-384";
constexpr auto JWT_HEADER  = R"({"alg":"ES384","typ":"JWT"})";

uint64_t now_epoch()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

} // anonymous namespace

// ── Base64URL helpers (RFC 7515 §2) ─────────────────────────────────────────

std::string JwtManager::base64url_encode(const std::vector<uint8_t>& data)
{
    std::string b64 = Botan::base64_encode(data);
    std::replace(b64.begin(), b64.end(), '+', '-');
    std::replace(b64.begin(), b64.end(), '/', '_');
    b64.erase(std::remove(b64.begin(), b64.end(), '='), b64.end());
    return b64;
}

std::string JwtManager::base64url_encode(const std::string& data)
{
    return base64url_encode(
        std::vector<uint8_t>(data.begin(), data.end()));
}

std::vector<uint8_t> JwtManager::base64url_decode(const std::string& input)
{
    std::string b64 = input;
    std::replace(b64.begin(), b64.end(), '-', '+');
    std::replace(b64.begin(), b64.end(), '_', '/');
    while (b64.size() % 4 != 0)
        b64.push_back('=');
    return Botan::unlock(Botan::base64_decode(b64));
}

// ── Constructor / Destructor ────────────────────────────────────────────────

JwtManager::JwtManager(const std::string& private_key_path,
                       const std::string& certificate_path)
{
    std::ifstream key_stream(private_key_path);
    if (!key_stream)
        throw std::runtime_error("JwtManager: cannot open " + private_key_path);
    Botan::DataSource_Stream key_src(key_stream);
    private_key_ = Botan::PKCS8::load_key(key_src);
    if (!private_key_)
        throw std::runtime_error("JwtManager: failed to load private key from " + private_key_path);

    Botan::X509_Certificate cert(certificate_path);
    public_key_ = cert.subject_public_key();
    if (!public_key_)
        throw std::runtime_error("JwtManager: failed to extract public key from " + certificate_path);
}

JwtManager::~JwtManager() = default;

// ── Token generation ────────────────────────────────────────────────────────

std::string JwtManager::generate(const std::string& subject,
                                 const std::string& role,
                                 uint64_t ttl_seconds) const
{
    const auto now = now_epoch();

    boost::json::object payload;
    payload["iss"] = "management-sensors";
    payload["sub"] = subject;
    payload["role"] = role;
    payload["iat"] = now;
    payload["exp"] = now + ttl_seconds;

    const std::string header_b64  = base64url_encode(std::string(JWT_HEADER));
    const std::string payload_b64 = base64url_encode(boost::json::serialize(payload));
    const std::string signing_input = header_b64 + "." + payload_b64;

    Botan::AutoSeeded_RNG rng;
    Botan::PK_Signer signer(*private_key_, rng, BOTAN_HASH,
                             Botan::Signature_Format::Standard);

    const auto input_bytes = reinterpret_cast<const uint8_t*>(signing_input.data());
    auto signature = signer.sign_message(input_bytes, signing_input.size(), rng);

    return signing_input + "." + base64url_encode(signature);
}

// ── Token verification ──────────────────────────────────────────────────────

std::optional<JwtClaims> JwtManager::verify(const std::string& token) const
{
    try
    {
        const auto dot1 = token.find('.');
        if (dot1 == std::string::npos) return std::nullopt;

        const auto dot2 = token.find('.', dot1 + 1);
        if (dot2 == std::string::npos) return std::nullopt;

        const std::string header_b64  = token.substr(0, dot1);
        const std::string payload_b64 = token.substr(dot1 + 1, dot2 - dot1 - 1);
        const std::string sig_b64     = token.substr(dot2 + 1);

        // 1. Verify header algorithm
        auto header_raw = base64url_decode(header_b64);
        auto header_json = boost::json::parse(
            std::string(header_raw.begin(), header_raw.end()));
        if (header_json.as_object().at("alg").as_string() != JWT_ALGO)
            return std::nullopt;

        // 2. Verify cryptographic signature (Botan ECDSA P-384)
        const std::string signing_input = header_b64 + "." + payload_b64;
        auto sig_bytes = base64url_decode(sig_b64);

        Botan::PK_Verifier verifier(*public_key_, BOTAN_HASH,
                                    Botan::Signature_Format::Standard);

        const auto input_ptr = reinterpret_cast<const uint8_t*>(signing_input.data());
        if (!verifier.verify_message(input_ptr, signing_input.size(),
                                     sig_bytes.data(), sig_bytes.size()))
            return std::nullopt;

        // 3. Parse payload and validate expiration (RFC 7519 §4.1.4)
        auto payload_raw = base64url_decode(payload_b64);
        auto payload_json = boost::json::parse(
            std::string(payload_raw.begin(), payload_raw.end()));
        const auto& obj = payload_json.as_object();

        JwtClaims claims;
        claims.sub  = boost::json::value_to<std::string>(obj.at("sub"));
        claims.role = boost::json::value_to<std::string>(obj.at("role"));
        claims.iat  = static_cast<uint64_t>(obj.at("iat").as_int64());
        claims.exp  = static_cast<uint64_t>(obj.at("exp").as_int64());

        if (now_epoch() >= claims.exp)
            return std::nullopt;

        return claims;
    }
    catch (...)
    {
        return std::nullopt;
    }
}
