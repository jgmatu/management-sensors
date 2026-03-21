#include <gtest/gtest.h>
#include <jwt/JwtManager.hpp>

#include <botan/base64.h>
#include <boost/json.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#ifndef TEST_CERTS_DIR
#error "TEST_CERTS_DIR must be defined by CMake"
#endif

static const std::string JWT_KEY  = std::string(TEST_CERTS_DIR) + "/jwt.key";
static const std::string JWT_CERT = std::string(TEST_CERTS_DIR) + "/jwt.pem";

namespace {

std::vector<uint8_t> base64url_decode(const std::string& input)
{
    std::string b64 = input;
    std::replace(b64.begin(), b64.end(), '-', '+');
    std::replace(b64.begin(), b64.end(), '_', '/');
    while (b64.size() % 4 != 0)
        b64.push_back('=');
    return Botan::unlock(Botan::base64_decode(b64));
}

std::string decode_to_string(const std::string& input)
{
    auto bytes = base64url_decode(input);
    return {bytes.begin(), bytes.end()};
}

uint64_t now_epoch()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

} // anonymous namespace

class JwtManagerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        jwt_ = std::make_unique<JwtManager>(JWT_KEY, JWT_CERT);
    }

    std::unique_ptr<JwtManager> jwt_;
};

// ── Structure tests ─────────────────────────────────────────────────────────

TEST_F(JwtManagerTest, TokenHasThreeDotSeparatedParts)
{
    auto token = jwt_->generate("alice", "admin");
    int dot_count = 0;
    for (char c : token)
        if (c == '.') ++dot_count;
    EXPECT_EQ(dot_count, 2);
}

TEST_F(JwtManagerTest, HeaderIsES384JWT)
{
    auto token = jwt_->generate("bob", "operator");
    auto header_b64 = token.substr(0, token.find('.'));
    auto header_str = decode_to_string(header_b64);
    auto header = boost::json::parse(header_str).as_object();

    EXPECT_EQ(header.at("alg").as_string(), "ES384");
    EXPECT_EQ(header.at("typ").as_string(), "JWT");
}

TEST_F(JwtManagerTest, PayloadContainsStandardClaims)
{
    const auto before = now_epoch();
    auto token = jwt_->generate("carol", "viewer", 7200);
    const auto after = now_epoch();

    auto dot1 = token.find('.');
    auto dot2 = token.find('.', dot1 + 1);
    auto payload_str = decode_to_string(token.substr(dot1 + 1, dot2 - dot1 - 1));
    auto payload = boost::json::parse(payload_str).as_object();

    EXPECT_EQ(payload.at("iss").as_string(), "management-sensors");
    EXPECT_EQ(payload.at("sub").as_string(), "carol");
    EXPECT_EQ(payload.at("role").as_string(), "viewer");

    auto iat = static_cast<uint64_t>(payload.at("iat").as_int64());
    auto exp = static_cast<uint64_t>(payload.at("exp").as_int64());
    EXPECT_GE(iat, before);
    EXPECT_LE(iat, after);
    EXPECT_EQ(exp, iat + 7200);
}

// ── Signature round-trip ────────────────────────────────────────────────────

TEST_F(JwtManagerTest, GenerateAndVerifyRoundTrip)
{
    auto token = jwt_->generate("dave", "admin", 600);
    auto claims = jwt_->verify(token);

    ASSERT_TRUE(claims.has_value());
    EXPECT_EQ(claims->sub, "dave");
    EXPECT_EQ(claims->role, "admin");
    EXPECT_GT(claims->iat, 0u);
    EXPECT_EQ(claims->exp, claims->iat + 600);
}

TEST_F(JwtManagerTest, DifferentSubjectsProduceDifferentTokens)
{
    auto t1 = jwt_->generate("user-a", "admin");
    auto t2 = jwt_->generate("user-b", "admin");
    EXPECT_NE(t1, t2);
}

// ── Signature tampering ─────────────────────────────────────────────────────

TEST_F(JwtManagerTest, TamperedPayloadIsRejected)
{
    auto token = jwt_->generate("eve", "admin");

    auto dot1 = token.find('.');
    auto dot2 = token.find('.', dot1 + 1);

    // Replace the payload with a different one, keep original signature
    std::string fake_payload = R"({"iss":"evil","sub":"root","role":"admin","iat":0,"exp":9999999999})";
    std::vector<uint8_t> fake_bytes(fake_payload.begin(), fake_payload.end());
    std::string fake_b64 = Botan::base64_encode(fake_bytes);
    std::replace(fake_b64.begin(), fake_b64.end(), '+', '-');
    std::replace(fake_b64.begin(), fake_b64.end(), '/', '_');
    fake_b64.erase(std::remove(fake_b64.begin(), fake_b64.end(), '='), fake_b64.end());

    std::string tampered = token.substr(0, dot1 + 1) + fake_b64 + token.substr(dot2);
    EXPECT_FALSE(jwt_->verify(tampered).has_value());
}

TEST_F(JwtManagerTest, TamperedSignatureIsRejected)
{
    auto token = jwt_->generate("frank", "operator");
    auto dot2 = token.rfind('.');

    // Flip a character in the signature
    std::string tampered = token;
    tampered[dot2 + 1] = (tampered[dot2 + 1] == 'A') ? 'B' : 'A';
    EXPECT_FALSE(jwt_->verify(tampered).has_value());
}

TEST_F(JwtManagerTest, TruncatedSignatureIsRejected)
{
    auto token = jwt_->generate("grace", "viewer");
    auto dot2 = token.rfind('.');
    std::string truncated = token.substr(0, dot2 + 5);
    EXPECT_FALSE(jwt_->verify(truncated).has_value());
}

// ── Malformed input ─────────────────────────────────────────────────────────

TEST_F(JwtManagerTest, EmptyStringIsRejected)
{
    EXPECT_FALSE(jwt_->verify("").has_value());
}

TEST_F(JwtManagerTest, NoDotIsRejected)
{
    EXPECT_FALSE(jwt_->verify("nodots").has_value());
}

TEST_F(JwtManagerTest, SingleDotIsRejected)
{
    EXPECT_FALSE(jwt_->verify("one.dot").has_value());
}

TEST_F(JwtManagerTest, GarbageTokenIsRejected)
{
    EXPECT_FALSE(jwt_->verify("aaa.bbb.ccc").has_value());
}

// ── Expiration ──────────────────────────────────────────────────────────────

TEST_F(JwtManagerTest, ExpiredTokenIsRejected)
{
    // Generate a token with 0-second TTL — already expired
    auto token = jwt_->generate("hank", "admin", 0);
    EXPECT_FALSE(jwt_->verify(token).has_value());
}

// ── Algorithm enforcement ───────────────────────────────────────────────────

TEST_F(JwtManagerTest, WrongAlgorithmInHeaderIsRejected)
{
    auto token = jwt_->generate("ivan", "admin");
    auto dot1 = token.find('.');

    // Forge a header with HS256 instead of ES384
    std::string fake_header = R"({"alg":"HS256","typ":"JWT"})";
    std::vector<uint8_t> hdr_bytes(fake_header.begin(), fake_header.end());
    std::string hdr_b64 = Botan::base64_encode(hdr_bytes);
    std::replace(hdr_b64.begin(), hdr_b64.end(), '+', '-');
    std::replace(hdr_b64.begin(), hdr_b64.end(), '/', '_');
    hdr_b64.erase(std::remove(hdr_b64.begin(), hdr_b64.end(), '='), hdr_b64.end());

    std::string forged = hdr_b64 + token.substr(dot1);
    EXPECT_FALSE(jwt_->verify(forged).has_value());
}

// ── Constructor robustness ──────────────────────────────────────────────────

TEST(JwtManagerConstructor, InvalidKeyPathThrows)
{
    EXPECT_THROW(
        JwtManager("/nonexistent/path/jwt.key", JWT_CERT),
        std::runtime_error);
}

TEST(JwtManagerConstructor, InvalidCertPathThrows)
{
    EXPECT_THROW(
        JwtManager(JWT_KEY, "/nonexistent/path/jwt.pem"),
        std::exception);
}
