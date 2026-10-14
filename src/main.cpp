#include <cstdio>
#include <memory>
#include <string>

#include "Application.h"
#include "Forum.h"
#include "Terminal.h"

static void usage(const char* prog)
{
	std::printf("Usage: %s [--data DIR]\n"
		"  --data DIR   where forum.db is kept (default: data)\n"
		"The terminal client and the web server (forum-web) share one data directory,\n"
		"but only one of them can use it at a time.\n",
		prog);
}

int main(int argc, char** argv)
{
	std::string dataDir = "data";

	for (int i = 1; i < argc; i++)
	{
		std::string arg = argv[i];
		if (arg == "--help" || arg == "-h")
		{
			usage(argv[0]);
			return 0;
		}
		else if (arg == "--data" && i + 1 < argc)
			dataDir = argv[++i];
		else
		{
			usage(argv[0]);
			return 2;
		}
	}

	std::string error;
	std::unique_ptr<Forum> forum = Forum::open(dataDir, error);
	if (!forum)
	{
		std::fprintf(stderr, "error: %s\n", error.c_str());
		return 1;
	}

	Terminal terminal;
	Application app(*forum, terminal);
	return app.run();
}
