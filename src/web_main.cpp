#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "Forum.h"
#include "WebServer.h"

static volatile std::sig_atomic_t g_stop = 0;

static void onSignal(int) { g_stop = 1; }

static void usage(const char* prog)
{
	std::printf("Usage: %s [--data DIR] [--web DIR] [--host ADDR] [--port N]\n"
		"  --data DIR   where forum.db is kept        (default: data)\n"
		"  --web DIR    the web app's static files    (default: web)\n"
		"  --host ADDR  address to listen on          (default: 127.0.0.1)\n"
		"  --port N     port to listen on, 0 = any    (default: 8080)\n",
		prog);
}

int main(int argc, char** argv)
{
	std::string dataDir = "data", webRoot = "web", host = "127.0.0.1";
	int port = 8080;

	for (int i = 1; i < argc; i++)
	{
		std::string arg = argv[i];
		bool hasValue = i + 1 < argc;
		if (arg == "--help" || arg == "-h")
		{
			usage(argv[0]);
			return 0;
		}
		else if (arg == "--data" && hasValue)
			dataDir = argv[++i];
		else if (arg == "--web" && hasValue)
			webRoot = argv[++i];
		else if (arg == "--host" && hasValue)
			host = argv[++i];
		else if (arg == "--port" && hasValue)
		{
			char* end = nullptr;
			long p = std::strtol(argv[++i], &end, 10);
			if (*end != '\0' || p < 0 || p > 65535)
			{
				std::fprintf(stderr, "invalid port '%s'\n", argv[i]);
				return 2;
			}
			port = static_cast<int>(p);
		}
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

	WebServer server(*forum, webRoot);
	if (!server.ready())
	{
		std::fprintf(stderr, "error: web directory '%s' not found (use --web)\n", webRoot.c_str());
		return 1;
	}

	int boundPort = server.bind(host, port);
	if (boundPort < 0)
	{
		std::fprintf(stderr, "error: cannot listen on %s:%d\n", host.c_str(), port);
		return 1;
	}

	if (host != "127.0.0.1" && host != "localhost" && host != "::1")
		std::fprintf(stderr, "warning: listening on %s without HTTPS; passwords and sessions travel in clear text.\n"
			"         Only do this on a network you trust.\n", host.c_str());

	std::signal(SIGINT, onSignal);
	std::signal(SIGTERM, onSignal);
	std::signal(SIGPIPE, SIG_IGN);

	std::printf("Listening on http://%s:%d\n", host.c_str(), boundPort);
	std::fflush(stdout);

	/* Signal handlers may only set a flag; this thread turns the flag into a clean stop */
	std::atomic<bool> finished{ false };
	std::thread watcher([&] {
		while (!g_stop && !finished)
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		if (g_stop)
			server.stop();
	});

	server.run();
	finished = true;
	watcher.join();
	return 0;
}
