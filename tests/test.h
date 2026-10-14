#pragma once

/* A tiny test harness: CHECK records a failure and carries on, and main()
   returns testReport() so the exit code reflects whether anything failed. */

#include <cstdio>

inline int& testFailures()
{
	static int failures = 0;
	return failures;
}

#define CHECK(cond)                                                              \
	do                                                                           \
	{                                                                            \
		if (!(cond))                                                             \
		{                                                                        \
			std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
			++testFailures();                                                    \
		}                                                                        \
	} while (0)

inline int testReport()
{
	if (testFailures() == 0)
	{
		std::puts("OK");
		return 0;
	}
	std::fprintf(stderr, "%d check(s) failed\n", testFailures());
	return 1;
}
