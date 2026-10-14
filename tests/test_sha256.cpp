#include <set>
#include <string>

#include "Sha256.h"
#include "test.h"

int main()
{
	/* FIPS 180-4 / NIST example vectors */
	CHECK(crypto::sha256Hex("") ==
		"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	CHECK(crypto::sha256Hex("abc") ==
		"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	CHECK(crypto::sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
		"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
	CHECK(crypto::sha256Hex(std::string(1000000, 'a')) ==
		"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

	/* Lengths around the padding boundaries (55, 56, 63, 64 bytes) must all work and differ */
	std::set<std::string> seen;
	for (int n : { 54, 55, 56, 57, 63, 64, 65 })
		seen.insert(crypto::sha256Hex(std::string(n, 'x')));
	CHECK(seen.size() == 7);

	/* Password hashing is deterministic, salted, and sensitive to both inputs */
	std::string a = crypto::hashPassword("hunter2", "salt1");
	CHECK(a.size() == 64);
	CHECK(a == crypto::hashPassword("hunter2", "salt1"));
	CHECK(a != crypto::hashPassword("hunter2", "salt2"));
	CHECK(a != crypto::hashPassword("hunter3", "salt1"));
	CHECK(a != crypto::sha256Hex("salt1hunter2"));

	/* Random hex has the requested length, only hex digits, and does not repeat */
	std::string r1 = crypto::randomHex(16), r2 = crypto::randomHex(16);
	CHECK(r1.size() == 32 && r2.size() == 32);
	CHECK(r1.find_first_not_of("0123456789abcdef") == std::string::npos);
	CHECK(r1 != r2);
	CHECK(crypto::randomHex(0).empty());

	CHECK(crypto::constantTimeEquals("abc", "abc"));
	CHECK(!crypto::constantTimeEquals("abc", "abd"));
	CHECK(!crypto::constantTimeEquals("abc", "abcd"));
	CHECK(crypto::constantTimeEquals("", ""));

	return testReport();
}
