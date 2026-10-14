#include "StringSearch.h"
#include "test.h"

int main()
{
	CHECK(containsIgnoreCase("Hello, World", "world"));
	CHECK(containsIgnoreCase("Hello, World", "HELLO"));
	CHECK(containsIgnoreCase("Hello, World", "o, w"));
	CHECK(!containsIgnoreCase("Hello, World", "worlds"));
	CHECK(!containsIgnoreCase("Hello", "xyz"));

	/* Empty pattern matches everything, even an empty text */
	CHECK(containsIgnoreCase("anything", ""));
	CHECK(containsIgnoreCase("", ""));
	CHECK(!containsIgnoreCase("", "a"));

	/* Pattern longer than the text */
	CHECK(!containsIgnoreCase("ab", "abc"));

	/* Whole-string and boundary matches */
	CHECK(containsIgnoreCase("abc", "abc"));
	CHECK(containsIgnoreCase("abcdef", "abc"));
	CHECK(containsIgnoreCase("abcdef", "def"));

	/* Overlapping partial matches are the case KMP exists for */
	CHECK(containsIgnoreCase("aaaaab", "aaab"));
	CHECK(containsIgnoreCase("abababc", "ababc"));
	CHECK(containsIgnoreCase("ABABABC", "abababc"));
	CHECK(!containsIgnoreCase("aaaaa", "aaab"));
	CHECK(containsIgnoreCase("aabaaabaaac", "aabaaac"));

	/* Punctuation, digits and non-ASCII bytes compare exactly */
	CHECK(containsIgnoreCase("C++ Forum #1", "c++"));
	CHECK(containsIgnoreCase("caf\xC3\xA9 au lait", "caf\xC3\xA9"));
	CHECK(!containsIgnoreCase("caf\xC3\xA9", "CAF\xC3\x89"));

	return testReport();
}
