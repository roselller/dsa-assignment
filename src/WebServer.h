#pragma once

#include <memory>
#include <string>

#include "Forum.h"

/* HTTP front-end for a Forum: a JSON API under /api plus the static web app.

   Requests are handled on a thread pool, but a single mutex serialises everything that
   touches the Forum or the sessions, and each handler builds its whole response while
   holding it.

   Sign-in uses a random session token in an HttpOnly, SameSite=Strict cookie. Every POST
   must carry the header "X-Forum: 1", which a cross-site page cannot add without CORS
   (and CORS is never enabled), so forged cross-site requests are rejected. */
class WebServer
{
public:
	/* webRoot is the directory with index.html, app.js and app.css */
	WebServer(Forum& forum, const std::string& webRoot);
	~WebServer();

	/* False if webRoot is not a directory */
	bool ready() const;

	/* Binds host:port (port 0 picks any free port). Returns the port, or -1 on failure. */
	int bind(const std::string& host, int port);

	/* Serves until stop() is called. Blocks. */
	void run();
	void stop();

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
