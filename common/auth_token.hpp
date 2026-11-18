#pragma once

#include <charconv>
#include <climits>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <sstream>
#include <string>
#include <string_view>

namespace auth_token {

inline bool secret_is_valid(std::string_view secret) {
    return secret.size() >= 32;
}

inline bool valid_subject(std::string_view subject) {
    if (subject.empty() || subject.size() > 128) {
        return false;
    }

    for (unsigned char c : subject) {
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '-' || c == '_')) {
            return false;
        }
    }
    return true;
}

inline bool valid_hex(std::string_view value) {
    for (unsigned char c : value) {
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

inline std::string random_nonce() {
    unsigned char bytes[16];
    if (RAND_bytes(bytes, sizeof(bytes)) != 1) {
        return {};
    }

    std::ostringstream nonce;
    nonce << std::hex << std::setfill('0');
    for (unsigned char byte : bytes) {
        nonce << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return nonce.str();
}

inline bool sign_payload(std::string_view payload,
                         std::string_view secret,
                         unsigned char digest[EVP_MAX_MD_SIZE],
                         unsigned int& digest_len) {
    if (!secret_is_valid(secret) || secret.size() > INT_MAX) {
        return false;
    }

    return HMAC(EVP_sha256(),
                secret.data(),
                static_cast<int>(secret.size()),
                reinterpret_cast<const unsigned char*>(payload.data()),
                payload.size(),
                digest,
                &digest_len) != nullptr;
}

inline std::string issue(std::string_view subject,
                         std::time_t expires_at,
                         std::string_view secret) {
    if (!valid_subject(subject) || expires_at <= 0 ||
        !secret_is_valid(secret)) {
        return {};
    }

    const std::string nonce = random_nonce();
    if (nonce.empty()) {
        return {};
    }

    const std::string payload =
        "v1." + std::string(subject) + "." + std::to_string(expires_at) +
        "." + nonce;
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    if (!sign_payload(payload, secret, digest, digest_len)) {
        return {};
    }

    std::ostringstream signature;
    signature << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < digest_len; ++i) {
        signature << std::setw(2) << static_cast<unsigned int>(digest[i]);
    }
    return payload + "." + signature.str();
}

inline bool verify(std::string_view token,
                   std::string_view secret,
                   std::string& subject_out,
                   std::time_t& expires_at_out) {
    const size_t first = token.find('.');
    const size_t second = first == std::string_view::npos
        ? first : token.find('.', first + 1);
    const size_t third = second == std::string_view::npos
        ? second : token.find('.', second + 1);
    const size_t fourth = third == std::string_view::npos
        ? third : token.find('.', third + 1);

    if (first == std::string_view::npos ||
        second == std::string_view::npos ||
        third == std::string_view::npos ||
        fourth == std::string_view::npos ||
        token.find('.', fourth + 1) != std::string_view::npos ||
        token.substr(0, first) != "v1") {
        return false;
    }

    const auto subject = token.substr(first + 1, second - first - 1);
    const auto expiry_text = token.substr(second + 1, third - second - 1);
    const auto nonce = token.substr(third + 1, fourth - third - 1);
    const auto signature = token.substr(fourth + 1);
    if (!valid_subject(subject) || nonce.size() != 32 ||
        !valid_hex(nonce) || signature.size() != 64 ||
        !valid_hex(signature)) {
        return false;
    }

    std::int64_t expiry = 0;
    const char* begin = expiry_text.data();
    const char* end = begin + expiry_text.size();
    const auto parsed = std::from_chars(begin, end, expiry);
    if (expiry_text.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != end || expiry <= 0 ||
        std::time(nullptr) >= expiry) {
        return false;
    }

    const auto payload = token.substr(0, fourth);
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    if (!sign_payload(payload, secret, digest, digest_len) ||
        digest_len != 32) {
        return false;
    }

    std::ostringstream expected;
    expected << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < digest_len; ++i) {
        expected << std::setw(2) << static_cast<unsigned int>(digest[i]);
    }
    const std::string expected_signature = expected.str();
    if (CRYPTO_memcmp(signature.data(),
                      expected_signature.data(),
                      expected_signature.size()) != 0) {
        return false;
    }

    subject_out.assign(subject);
    expires_at_out = static_cast<std::time_t>(expiry);
    return true;
}

} // namespace auth_token
