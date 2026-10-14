#include "StringSearch.h"

#include "Vector.h"

static char lowerAscii(char c)
{
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

/* table[i] is the length of the longest proper prefix of pattern[0..i] that is
   also a suffix of it */
static Vector<int> buildPrefixTable(const std::string& pattern)
{
	Vector<int> table;
	for (std::size_t i = 0; i < pattern.size(); i++)
		table.push(0);

	int k = 0;
	for (int i = 1; i < table.count(); i++)
	{
		while (k > 0 && lowerAscii(pattern[i]) != lowerAscii(pattern[k]))
			k = table[k - 1];
		if (lowerAscii(pattern[i]) == lowerAscii(pattern[k]))
			k++;
		table[i] = k;
	}
	return table;
}

bool containsIgnoreCase(const std::string& text, const std::string& pattern)
{
	if (pattern.empty())
		return true;

	Vector<int> table = buildPrefixTable(pattern);
	int m = static_cast<int>(pattern.size());
	int k = 0;

	for (char c : text)
	{
		while (k > 0 && lowerAscii(c) != lowerAscii(pattern[k]))
			k = table[k - 1];
		if (lowerAscii(c) == lowerAscii(pattern[k]))
			k++;
		if (k == m)
			return true;
	}
	return false;
}
