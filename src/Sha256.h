#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

/* Hand-written SHA-256 (FIPS 180-4), kept for learning. */
namespace crypto
{
	using Digest = std::array<std::uint8_t, 32>;

	Digest sha256(const std::string& data);

	/* Lower-case hex of the SHA-256 digest of data */
	std::string sha256Hex(const std::string& data);

	/* nBytes random bytes from the OS entropy source, as 2*nBytes hex characters.
	   Used for password salts and session tokens. */
	std::string randomHex(std::size_t nBytes);

	/* Salted, iterated password hash:
	   h0 = SHA256(salt + password), h(i+1) = SHA256(h(i) + salt + password), 10,000 rounds.
	   Slows down guessing, but real systems should use argon2, scrypt or bcrypt. */
	std::string hashPassword(const std::string& password, const std::string& salt);

	/* Compares two strings without bailing out at the first difference */
	bool constantTimeEquals(const std::string& a, const std::string& b);
}
