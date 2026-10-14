#include "WebServer.h"

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <functional>
#include <mutex>

#include <httplib.h>

#include "HashMap.h"
#include "Json.h"
#include "Sha256.h"
#include "StringSearch.h"

namespace
{
	constexpr std::time_t kSessionIdle = 24 * 60 * 60;  /* sessions expire after a day without use */
	constexpr int kMaxFailures = 5;                      /* failed logins allowed ... */
	constexpr std::time_t kFailureWindow = 60;           /* ... within this many seconds ... */
	constexpr std::time_t kLockout = 30;                 /* ... before logins are refused this long */
	constexpr std::size_t kSnippetBytes = 160;

	struct Session
	{
		std::string user;
		std::time_t lastSeen = 0;
	};

	struct Throttle
	{
		int failures = 0;
		std::time_t windowStart = 0;
		std::time_t lockedUntil = 0;
	};

	std::time_t nowSec() { return std::time(nullptr); }

	/* A positive decimal id that fills the whole string */
	bool parseId(const std::string& s, int& out)
	{
		if (s.empty() || s.size() > 9 || s.find_first_not_of("0123456789") != std::string::npos)
			return false;
		out = std::atoi(s.c_str());
		return out > 0;
	}

	int httpCode(Status s)
	{
		switch (s)
		{
		case Status::Ok: return 200;
		case Status::Invalid: return 400;
		case Status::Unauthorized: return 401;
		case Status::Forbidden: return 403;
		case Status::NotFound: return 404;
		case Status::Conflict: return 409;
		case Status::Internal: return 500;
		}
		return 500;
	}

	const char* defaultMessage(Status s)
	{
		switch (s)
		{
		case Status::Invalid: return "That input is not valid.";
		case Status::NotFound: return "Not found.";
		case Status::Forbidden: return "You are not allowed to do that.";
		case Status::Conflict: return "That already exists.";
		case Status::Unauthorized: return "Wrong credentials.";
		case Status::Internal: return "The change could not be saved.";
		case Status::Ok: break;
		}
		return "";
	}

	void sendJson(httplib::Response& res, int status, const JsonWriter& json)
	{
		res.status = status;
		res.set_content(json.str(), "application/json; charset=utf-8");
	}

	void sendError(httplib::Response& res, int status, const std::string& message)
	{
		JsonWriter j;
		j.beginObject().field("error", message).endObject();
		sendJson(res, status, j);
	}

	JsonWriter okBody()
	{
		JsonWriter j;
		j.beginObject().field("ok", true).endObject();
		return j;
	}

	JsonWriter idBody(int id)
	{
		JsonWriter j;
		j.beginObject().field("id", id).endObject();
		return j;
	}

	/* Optional per-operation wording for the error cases */
	struct Msgs
	{
		const char* invalid;
		const char* conflict;
		const char* forbidden;
		const char* unauthorized;
		const char* notFound;

		Msgs(const char* invalid = nullptr, const char* conflict = nullptr, const char* forbidden = nullptr,
			const char* unauthorized = nullptr, const char* notFound = nullptr) :
			invalid(invalid), conflict(conflict), forbidden(forbidden), unauthorized(unauthorized), notFound(notFound)
		{ }
	};

	/* Sends body on success, or a JSON error for any other status */
	void respond(httplib::Response& res, Status s, const Msgs& m, const JsonWriter& body)
	{
		if (s == Status::Ok)
		{
			sendJson(res, 200, body);
			return;
		}
		const char* specific = nullptr;
		switch (s)
		{
		case Status::Invalid: specific = m.invalid; break;
		case Status::Conflict: specific = m.conflict; break;
		case Status::Forbidden: specific = m.forbidden; break;
		case Status::Unauthorized: specific = m.unauthorized; break;
		case Status::NotFound: specific = m.notFound; break;
		default: break;
		}
		sendError(res, httpCode(s), specific ? specific : defaultMessage(s));
	}

	/* Cuts s to at most maxBytes without splitting a UTF-8 character */
	std::string snippet(const std::string& s, std::size_t maxBytes)
	{
		if (s.size() <= maxBytes)
			return s;
		std::size_t cut = maxBytes;
		while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80)
			cut--;
		return s.substr(0, cut) + "...";
	}

	/* The value of the sid cookie, or "" */
	std::string sessionToken(const httplib::Request& req)
	{
		const std::string header = req.get_header_value("Cookie");
		std::size_t pos = 0;
		while (pos < header.size())
		{
			std::size_t end = header.find(';', pos);
			if (end == std::string::npos)
				end = header.size();
			std::size_t a = pos;
			while (a < end && header[a] == ' ')
				a++;
			if (end - a >= 4 && header.compare(a, 4, "sid=") == 0)
				return header.substr(a + 4, end - a - 4);
			pos = end + 1;
		}
		return "";
	}

	/* The host part of a Host header: "localhost:8080" -> "localhost", "[::1]:8080" -> "[::1]" */
	std::string hostName(const std::string& hostHeader)
	{
		if (!hostHeader.empty() && hostHeader[0] == '[')
		{
			std::size_t close = hostHeader.find(']');
			return close == std::string::npos ? hostHeader : hostHeader.substr(0, close + 1);
		}
		return hostHeader.substr(0, hostHeader.find(':'));
	}

	bool isLoopbackName(const std::string& host)
	{
		return host == "localhost" || host == "127.0.0.1" || host == "::1" || host == "[::1]";
	}

	/* ---- JSON shapes ---- */

	void writeTopic(JsonWriter& j, const Topic& t, const std::string& me)
	{
		j.beginObject()
			.field("id", t.id)
			.field("title", t.title())
			.field("description", t.description)
			.field("author", t.author)
			.field("created", static_cast<long long>(t.created))
			.field("likes", t.likes())
			.field("likedByMe", t.isLikedBy(me))
			.field("postCount", t.posts.count())
			.endObject();
	}

	void writePost(JsonWriter& j, const Post& p, const std::string& me)
	{
		j.beginObject()
			.field("id", p.id)
			.field("author", p.author)
			.field("text", p.text)
			.field("created", static_cast<long long>(p.created))
			.field("edited", p.edited)
			.field("likes", p.likes())
			.field("likedByMe", p.isLikedBy(me))
			.field("replyCount", p.replies.count())
			.field("mine", p.author == me)
			.endObject();
	}

	void writeReply(JsonWriter& j, const Comment& r, const std::string& me)
	{
		j.beginObject()
			.field("id", r.id)
			.field("author", r.author)
			.field("text", r.text)
			.field("created", static_cast<long long>(r.created))
			.field("likes", r.likes())
			.field("likedByMe", r.isLikedBy(me))
			.field("mine", r.author == me)
			.endObject();
	}

	JsonWriter likeBody(bool liked, int likes)
	{
		JsonWriter j;
		j.beginObject().field("liked", liked).field("likes", likes).endObject();
		return j;
	}
}

struct WebServer::Impl
{
	using Handler = std::function<void(const httplib::Request&, httplib::Response&, const std::string& user)>;

	Forum& forum;
	httplib::Server svr;
	bool rootOk = false;
	bool loopbackOnly = true;
	std::mutex mu;
	HashMap<std::string, Session> sessions;
	HashMap<std::string, Throttle> throttles;

	explicit Impl(Forum& f) : forum(f) {}

	/* ---- sessions (callers hold mu) ---- */

	/* The signed-in user for this request, or "" */
	std::string authenticate(const httplib::Request& req)
	{
		std::string token = sessionToken(req);
		if (token.empty())
			return "";
		Session* s = sessions.find(token);
		if (!s)
			return "";
		std::time_t now = nowSec();
		if (now - s->lastSeen > kSessionIdle)
		{
			sessions.erase(token);
			return "";
		}
		s->lastSeen = now;
		return s->user;
	}

	void sweepExpired()
	{
		std::time_t now = nowSec();
		Vector<std::string> stale;
		for (auto entry : sessions)
			if (now - entry.value.lastSeen > kSessionIdle)
				stale.push(entry.key);
		for (const std::string& token : stale)
			sessions.erase(token);

		Vector<std::string> cold;
		for (auto entry : throttles)
			if (entry.value.lockedUntil < now && now - entry.value.windowStart > kFailureWindow)
				cold.push(entry.key);
		for (const std::string& key : cold)
			throttles.erase(key);
	}

	/* Signs user in: a brand-new token every time, so a token planted before sign-in is useless */
	void startSession(const httplib::Request& req, httplib::Response& res, const std::string& user)
	{
		sessions.erase(sessionToken(req));
		sweepExpired();

		std::string token = crypto::randomHex(32);
		sessions.insertOrAssign(token, Session{ user, nowSec() });
		res.set_header("Set-Cookie", "sid=" + token + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=86400");
	}

	void endSession(const httplib::Request& req, httplib::Response& res)
	{
		sessions.erase(sessionToken(req));
		res.set_header("Set-Cookie", "sid=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0");
	}

	/* ---- routing ---- */

	void add(bool isPost, const std::string& pattern, bool needAuth, Handler handler)
	{
		auto wrapped = [this, isPost, needAuth, handler](const httplib::Request& req, httplib::Response& res) {
			/* Everything below, including building the response, happens under the lock */
			std::lock_guard<std::mutex> lock(mu);

			if (isPost && req.get_header_value("X-Forum") != "1")
			{
				sendError(res, 403, "Missing X-Forum header.");
				return;
			}
			std::string user;
			if (needAuth)
			{
				user = authenticate(req);
				if (user.empty())
				{
					sendError(res, 401, "Sign in required.");
					return;
				}
			}
			handler(req, res, user);
		};
		if (isPost)
			svr.Post(pattern, wrapped);
		else
			svr.Get(pattern, wrapped);
	}

	void get(const std::string& pattern, bool needAuth, Handler h) { add(false, pattern, needAuth, std::move(h)); }
	void post(const std::string& pattern, bool needAuth, Handler h) { add(true, pattern, needAuth, std::move(h)); }

	static bool idParam(const httplib::Request& req, const char* name, int& id)
	{
		return parseId(req.path_params.at(name), id);
	}

	void registerRoutes()
	{
		/* ---- accounts ---- */

		get("/api/me", false, [this](const httplib::Request& req, httplib::Response& res, const std::string&) {
			std::string user = authenticate(req);
			JsonWriter j;
			j.beginObject();
			j.key("user");
			if (user.empty())
				j.null();
			else
				j.value(user);
			j.key("limits").beginObject()
				.field("username", static_cast<int>(Forum::kMaxUsername))
				.field("passwordMin", static_cast<int>(Forum::kMinPassword))
				.field("passwordMax", static_cast<int>(Forum::kMaxPassword))
				.field("title", static_cast<int>(Forum::kMaxTitle))
				.field("description", static_cast<int>(Forum::kMaxDescription))
				.field("text", static_cast<int>(Forum::kMaxText))
				.endObject();
			j.endObject();
			sendJson(res, 200, j);
		});

		post("/api/register", false, [this](const httplib::Request& req, httplib::Response& res, const std::string&) {
			std::string canonical;
			Status s = forum.registerUser(req.get_param_value("username"), req.get_param_value("password"), canonical);
			if (s == Status::Ok)
				startSession(req, res, canonical);
			JsonWriter j;
			j.beginObject().field("user", canonical).endObject();
			respond(res, s, Msgs{ "Username: 1-32 characters, no control characters. Password: 4-128 characters.",
				"That username is already taken." }, j);
		});

		post("/api/login", false, [this](const httplib::Request& req, httplib::Response& res, const std::string&) {
			const std::string username = req.get_param_value("username");
			const std::string key = req.remote_addr + "|" + toLowerAscii(username);
			std::time_t now = nowSec();

			Throttle* t = throttles.find(key);
			if (t && t->lockedUntil > now)
			{
				sendError(res, 429, "Too many failed attempts. Try again in " + std::to_string(t->lockedUntil - now) + " seconds.");
				return;
			}

			std::string canonical;
			Status s = forum.login(username, req.get_param_value("password"), canonical);
			if (s != Status::Ok)
			{
				if (!t)
					t = &throttles.insertOrAssign(key, Throttle{ 0, now, 0 });
				if (now - t->windowStart > kFailureWindow)
				{
					t->failures = 0;
					t->windowStart = now;
				}
				if (++t->failures >= kMaxFailures)
				{
					t->failures = 0;
					t->lockedUntil = now + kLockout;
				}
				sendError(res, 401, "Wrong username or password.");
				return;
			}

			throttles.erase(key);
			startSession(req, res, canonical);
			JsonWriter j;
			j.beginObject().field("user", canonical).endObject();
			sendJson(res, 200, j);
		});

		post("/api/logout", false, [this](const httplib::Request& req, httplib::Response& res, const std::string&) {
			endSession(req, res);
			sendJson(res, 200, okBody());
		});

		post("/api/password", true, [this](const httplib::Request& req, httplib::Response& res, const std::string& user) {
			Status s = forum.changePassword(user, req.get_param_value("old"), req.get_param_value("new"));
			respond(res, s, Msgs{ "The new password must be 4-128 characters.", nullptr, nullptr,
				"The current password is wrong." }, okBody());
		});

		/* ---- topics ---- */

		get("/api/topics", true, [this](const httplib::Request& req, httplib::Response& res, const std::string& user) {
			std::string sort = req.get_param_value("sort");
			SortMode mode = sort == "time" ? SortMode::Time : sort == "alpha" ? SortMode::Alpha : SortMode::None;
			Vector<int> order = forum.topicOrder(mode);

			JsonWriter j;
			j.beginObject().key("topics").beginArray();
			for (int idx : order)
				writeTopic(j, forum.topics()[idx], user);
			j.endArray().endObject();
			sendJson(res, 200, j);
		});

		post("/api/topics", true, [this](const httplib::Request& req, httplib::Response& res, const std::string& user) {
			int id = 0;
			Status s = forum.createTopic(user, req.get_param_value("title"), req.get_param_value("description"), id);
			respond(res, s, Msgs{ "A title (up to 100 characters) is required; the description can be at most 500.",
				"A topic with that title already exists." }, idBody(id));
		});

		get("/api/topics/:t", true, [this](const httplib::Request& req, httplib::Response& res, const std::string& user) {
			int tid = 0;
			const Topic* topic = idParam(req, "t", tid) ? forum.findTopic(tid) : nullptr;
			if (!topic)
			{
				sendError(res, 404, "No such topic.");
				return;
			}
			JsonWriter j;
			j.beginObject().key("topic");
			writeTopic(j, *topic, user);
			j.key("posts").beginArray();
			for (const Post& p : topic->posts)
				writePost(j, p, user);
			j.endArray().endObject();
			sendJson(res, 200, j);
		});

		post("/api/topics/:t/like", true, [this](const httplib::Request& req, httplib::Response& res, const std::string& user) {
			int tid = 0;
			bool liked = false;
			Status s = idParam(req, "t", tid) ? forum.likeTopic(user, tid, liked) : Status::NotFound;
			respond(res, s, Msgs{ nullptr, nullptr, nullptr, nullptr, "No such topic." },
				likeBody(liked, s == Status::Ok ? forum.findTopic(tid)->likes() : 0));
		});

		/* ---- posts ---- */

		post("/api/topics/:t/posts", true, [this](const httplib::Request& req, httplib::Response& res, const std::string& user) {
			int tid = 0, id = 0;
			Status s = idParam(req, "t", tid) ? forum.addPost(user, tid, req.get_param_value("text"), id) : Status::NotFound;
			respond(res, s, Msgs{ "Post text is required (up to 2000 characters).",
				"This topic already has a post with exactly that text.", nullptr, nullptr, "No such topic." }, idBody(id));
		});

		get("/api/topics/:t/posts/:p", true, [this](const httplib::Request& req, httplib::Response& res, const std::string& user) {
			int tid = 0, pid = 0;
			const Post* post = nullptr;
			if (idParam(req, "t", tid) && idParam(req, "p", pid))
				post = forum.findPost(tid, pid);
			if (!post)
			{
				sendError(res, 404, "No such post.");
				return;
			}
			const Topic* topic = forum.findTopic(tid);

			JsonWriter j;
			j.beginObject();
			j.key("topic").beginObject().field("id", topic->id).field("title", topic->title()).endObject();
			j.key("post");
			writePost(j, *post, user);
			j.key("replies").beginArray();
			for (const Comment& r : post->replies)
				writeReply(j, r, user);
			j.endArray().endObject();
			sendJson(res, 200, j);
		});

		post("/api/topics/:t/posts/:p/like", true, [this](const httplib::Request& req, httplib::Response& res, const std::string& user) {
			int tid = 0, pid = 0;
			bool liked = false;
			Status s = (idParam(req, "t", tid) && idParam(req, "p", pid)) ? forum.likePost(user, tid, pid, liked) : Status::NotFound;
			respond(res, s, Msgs{ nullptr, nullptr, nullptr, nullptr, "No such post." },
				likeBody(liked, s == Status::Ok ? forum.findPost(tid, pid)->likes() : 0));
		});

		post("/api/topics/:t/posts/:p/edit", true, [this](const httplib::Request& req, httplib::Response& res, const std::string& user) {
			int tid = 0, pid = 0;
			Status s = (idParam(req, "t", tid) && idParam(req, "p", pid))
				? forum.editPost(user, tid, pid, req.get_param_value("text")) : Status::NotFound;
			respond(res, s, Msgs{ "Post text is required (up to 2000 characters).",
				"This topic already has a post with exactly that text.", "You can only edit your own posts.",
				nullptr, "No such post." }, okBody());
		});

		post("/api/topics/:t/posts/:p/delete", true, [this](const httplib::Request& req, httplib::Response& res, const std::string& user) {
			int tid = 0, pid = 0;
			Status s = (idParam(req, "t", tid) && idParam(req, "p", pid)) ? forum.deletePost(user, tid, pid) : Status::NotFound;
			respond(res, s, Msgs{ nullptr, nullptr, "You can only delete your own posts.", nullptr, "No such post." }, okBody());
		});

		/* ---- replies ---- */

		post("/api/topics/:t/posts/:p/replies", true, [this](const httplib::Request& req, httplib::Response& res, const std::string& user) {
			int tid = 0, pid = 0, id = 0;
			Status s = (idParam(req, "t", tid) && idParam(req, "p", pid))
				? forum.addReply(user, tid, pid, req.get_param_value("text"), id) : Status::NotFound;
			respond(res, s, Msgs{ "Reply text is required (up to 2000 characters).", nullptr, nullptr, nullptr,
				"No such post." }, idBody(id));
		});

		post("/api/topics/:t/posts/:p/replies/:r/like", true, [this](const httplib::Request& req, httplib::Response& res, const std::string& user) {
			int tid = 0, pid = 0, rid = 0;
			bool liked = false;
			Status s = (idParam(req, "t", tid) && idParam(req, "p", pid) && idParam(req, "r", rid))
				? forum.likeReply(user, tid, pid, rid, liked) : Status::NotFound;
			int likes = 0;
			if (s == Status::Ok)
				for (const Comment& r : forum.findPost(tid, pid)->replies)
					if (r.id == rid)
						likes = r.likes();
			respond(res, s, Msgs{ nullptr, nullptr, nullptr, nullptr, "No such reply." }, likeBody(liked, likes));
		});

		/* ---- search ---- */

		get("/api/search", true, [this](const httplib::Request& req, httplib::Response& res, const std::string&) {
			const std::string q = req.get_param_value("q");
			Vector<SearchHit> hits = forum.search(q);

			JsonWriter j;
			j.beginObject().field("query", q).key("hits").beginArray();
			for (const SearchHit& h : hits)
				j.beginObject()
					.field("topicId", h.topicId)
					.field("postId", h.postId)
					.field("topicTitle", h.topicTitle)
					.field("text", snippet(h.text, kSnippetBytes))
					.endObject();
			j.endArray().endObject();
			sendJson(res, 200, j);
		});
	}
};

WebServer::WebServer(Forum& forum, const std::string& webRoot) : impl_(new Impl(forum))
{
	std::error_code ec;
	impl_->rootOk = std::filesystem::is_directory(webRoot, ec) && impl_->svr.set_mount_point("/", webRoot);

	impl_->svr.set_payload_max_length(64 * 1024);
	impl_->svr.set_read_timeout(10);
	impl_->svr.set_write_timeout(10);

	/* Defence in depth: nothing may be framed, sniffed, or load outside this origin */
	impl_->svr.set_default_headers({
		{ "Cache-Control", "no-store" },
		{ "X-Content-Type-Options", "nosniff" },
		{ "X-Frame-Options", "DENY" },
		{ "Referrer-Policy", "no-referrer" },
		{ "Content-Security-Policy",
			"default-src 'self'; base-uri 'none'; form-action 'self'; frame-ancestors 'none'" } });

	impl_->svr.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr) {
		sendError(res, 500, "Internal error.");
	});

	/* Bound to loopback, only accept requests that name a loopback host. This blocks
	   DNS-rebinding, where a hostile site points its own name at 127.0.0.1. */
	Impl* impl = impl_.get();
	impl_->svr.set_pre_routing_handler([impl](const httplib::Request& req, httplib::Response& res) {
		if (impl->loopbackOnly && !isLoopbackName(hostName(req.get_header_value("Host"))))
		{
			sendError(res, 403, "Unexpected Host header.");
			return httplib::Server::HandlerResponse::Handled;
		}
		return httplib::Server::HandlerResponse::Unhandled;
	});

	impl_->registerRoutes();
}

WebServer::~WebServer() = default;

bool WebServer::ready() const { return impl_->rootOk; }

int WebServer::bind(const std::string& host, int port)
{
	impl_->loopbackOnly = isLoopbackName(host);
	if (port == 0)
	{
		int chosen = impl_->svr.bind_to_any_port(host);
		return chosen > 0 ? chosen : -1;
	}
	return impl_->svr.bind_to_port(host, port) ? port : -1;
}

void WebServer::run() { impl_->svr.listen_after_bind(); }

void WebServer::stop() { impl_->svr.stop(); }
