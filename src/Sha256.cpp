#include "Sha256.h"

#include <random>

namespace crypto
{
	static const std::uint32_t K[64] = {
		0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
		0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
		0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
		0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
		0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
		0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
		0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
		0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };

	static std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

	/* Processes one 64-byte block into the running state h */
	static void compress(std::uint32_t h[8], const std::uint8_t block[64])
	{
		std::uint32_t w[64];
		for (int i = 0; i < 16; i++)
			w[i] = (std::uint32_t(block[i * 4]) << 24) | (std::uint32_t(block[i * 4 + 1]) << 16) |
				(std::uint32_t(block[i * 4 + 2]) << 8) | std::uint32_t(block[i * 4 + 3]);
		for (int i = 16; i < 64; i++)
		{
			std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
			std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
			w[i] = w[i - 16] + s0 + w[i - 7] + s1;
		}

		std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
		for (int i = 0; i < 64; i++)
		{
			std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
			std::uint32_t ch = (e & f) ^ (~e & g);
			std::uint32_t t1 = hh + S1 + ch + K[i] + w[i];
			std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
			std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
			std::uint32_t t2 = S0 + maj;
			hh = g; g = f; f = e; e = d + t1;
			d = c; c = b; b = a; a = t1 + t2;
		}
		h[0] += a; h[1] += b; h[2] += c; h[3] += d;
		h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
	}

	Digest sha256(const std::string& data)
	{
		std::uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
			0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };

		/* Pad with 0x80, zeros, then the bit length as a 64-bit big-endian number,
		   so the total is a multiple of 64 bytes */
		std::string msg = data;
		msg.push_back(static_cast<char>(0x80));
		while (msg.size() % 64 != 56)
			msg.push_back('\0');
		std::uint64_t bitLen = static_cast<std::uint64_t>(data.size()) * 8;
		for (int i = 7; i >= 0; i--)
			msg.push_back(static_cast<char>((bitLen >> (i * 8)) & 0xff));

		for (std::size_t off = 0; off < msg.size(); off += 64)
			compress(h, reinterpret_cast<const std::uint8_t*>(msg.data()) + off);

		Digest out;
		for (int i = 0; i < 8; i++)
		{
			out[i * 4] = static_cast<std::uint8_t>(h[i] >> 24);
			out[i * 4 + 1] = static_cast<std::uint8_t>(h[i] >> 16);
			out[i * 4 + 2] = static_cast<std::uint8_t>(h[i] >> 8);
			out[i * 4 + 3] = static_cast<std::uint8_t>(h[i]);
		}
		return out;
	}

	static std::string toHex(const std::uint8_t* bytes, std::size_t n)
	{
		static const char* digits = "0123456789abcdef";
		std::string s;
		s.reserve(n * 2);
		for (std::size_t i = 0; i < n; i++)
		{
			s.push_back(digits[bytes[i] >> 4]);
			s.push_back(digits[bytes[i] & 0xf]);
		}
		return s;
	}

	std::string sha256Hex(const std::string& data)
	{
		Digest d = sha256(data);
		return toHex(d.data(), d.size());
	}

	std::string randomHex(std::size_t nBytes)
	{
		std::random_device rd;
		std::string bytes;
		for (std::size_t i = 0; i < nBytes; i++)
			bytes.push_back(static_cast<char>(rd() & 0xff));
		return toHex(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
	}

	std::string hashPassword(const std::string& password, const std::string& salt)
	{
		const std::string tail = salt + password;
		Digest d = sha256(tail);
		for (int i = 0; i < 10000; i++)
			d = sha256(std::string(d.begin(), d.end()) + tail);
		return toHex(d.data(), d.size());
	}

	bool constantTimeEquals(const std::string& a, const std::string& b)
	{
		if (a.size() != b.size())
			return false;
		unsigned char diff = 0;
		for (std::size_t i = 0; i < a.size(); i++)
			diff |= static_cast<unsigned char>(a[i] ^ b[i]);
		return diff == 0;
	}
}
