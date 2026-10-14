#pragma once

#include <string>

/* True if pattern occurs anywhere in text, ignoring ASCII case. An empty pattern
   matches everything. Uses Knuth-Morris-Pratt, so it runs in O(n + m). */
bool containsIgnoreCase(const std::string& text, const std::string& pattern);

/* Copy of s with ASCII letters lower-cased (other bytes untouched) */
std::string toLowerAscii(const std::string& s);
