#pragma once

/* A tiny test harness: CHECK records a failure and carries on, and main()
   returns testReport() so the exit code reflects whether anything failed. */

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include <stdlib.h>  /* mkdtemp (POSIX) */
#include <unistd.h>

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

/* A fresh directory that is removed again when the object goes out of scope */
class TempDir
{
	std::string path_;

public:
	TempDir()
	{
		std::string base = (std::filesystem::temp_directory_path() / "forum-test-XXXXXX").string();
		if (!mkdtemp(base.data()))
		{
			std::perror("mkdtemp");
			std::exit(2);
		}
		path_ = base;
	}
	TempDir(const TempDir&) = delete;
	TempDir& operator=(const TempDir&) = delete;
	~TempDir()
	{
		std::error_code ec;
		std::filesystem::remove_all(path_, ec);
	}

	const std::string& path() const { return path_; }
};
