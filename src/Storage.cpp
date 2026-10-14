#include "Storage.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <sys/file.h>
#include <unistd.h>

namespace storage
{
	std::string escape(const std::string& field)
	{
		std::string out;
		for (char c : field)
		{
			switch (c)
			{
			case '\\': out += "\\\\"; break;
			case '\t': out += "\\t"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			default: out += c;
			}
		}
		return out;
	}

	std::string unescape(const std::string& field)
	{
		std::string out;
		for (std::size_t i = 0; i < field.size(); i++)
		{
			if (field[i] != '\\' || i + 1 == field.size())
			{
				out += field[i];
				continue;
			}
			switch (field[++i])
			{
			case 't': out += '\t'; break;
			case 'n': out += '\n'; break;
			case 'r': out += '\r'; break;
			default: out += field[i];  /* includes \\ */
			}
		}
		return out;
	}

	static Vector<std::string> splitTabs(const std::string& line)
	{
		Vector<std::string> fields;
		std::size_t start = 0;
		while (true)
		{
			std::size_t tab = line.find('\t', start);
			if (tab == std::string::npos)
			{
				fields.push(line.substr(start));
				break;
			}
			fields.push(line.substr(start, tab - start));
			start = tab + 1;
		}
		return fields;
	}

	/* Whole-string integer parse; false on empty, junk or overflow */
	static bool toLong(const std::string& s, long long& out)
	{
		if (s.empty())
			return false;
		char* end = nullptr;
		errno = 0;
		long long v = std::strtoll(s.c_str(), &end, 10);
		if (errno != 0 || *end != '\0')
			return false;
		out = v;
		return true;
	}

	static bool toInt(const std::string& s, int& out)
	{
		long long v = 0;
		if (!toLong(s, v) || v < 0 || v > 2000000000LL)
			return false;
		out = static_cast<int>(v);
		return true;
	}

	/* Likers are the remaining fields from index first onward */
	static void readLikers(const Vector<std::string>& f, int first, Comment& c)
	{
		for (int i = first; i < f.count(); i++)
		{
			std::string user = unescape(f[i]);
			if (!user.empty() && !c.isLikedBy(user))
				c.likedBy.push(user);
		}
	}

	static bool parseTopic(const Vector<std::string>& f, Topic& t)
	{
		int id = 0;
		long long created = 0;
		if (f.count() < 6 || !toInt(f[1], id) || id == 0 || !toLong(f[2], created))
			return false;
		t = Topic(id, unescape(f[3]), unescape(f[4]), unescape(f[5]), static_cast<std::time_t>(created));
		readLikers(f, 6, t);
		return true;
	}

	static bool parsePost(const Vector<std::string>& f, Post& p)
	{
		int id = 0;
		long long created = 0;
		if (f.count() < 6 || !toInt(f[1], id) || id == 0 || !toLong(f[2], created) ||
			(f[4] != "0" && f[4] != "1"))
			return false;
		p = Post(id, unescape(f[3]), unescape(f[5]), static_cast<std::time_t>(created));
		p.edited = f[4] == "1";
		readLikers(f, 6, p);
		return true;
	}

	static bool parseReply(const Vector<std::string>& f, Comment& r)
	{
		int id = 0;
		long long created = 0;
		if (f.count() < 5 || !toInt(f[1], id) || id == 0 || !toLong(f[2], created))
			return false;
		r = Comment(id, unescape(f[3]), unescape(f[4]), static_cast<std::time_t>(created));
		readLikers(f, 5, r);
		return true;
	}

	LoadResult load(const std::string& path, Snapshot& out, int& skippedLines)
	{
		skippedLines = 0;
		std::ifstream in(path, std::ios::binary);
		if (!in)
			return LoadResult::Missing;

		std::string line;
		if (!std::getline(in, line) || line != "FORUM\t2")
			return LoadResult::Corrupt;

		int topicIdx = -1, postIdx = -1;  /* where POST and REPLY lines attach; -1 after a bad parent */
		int maxId = 0;

		while (std::getline(in, line))
		{
			if (line.empty())
				continue;

			Vector<std::string> f = splitTabs(line);
			const std::string& tag = f[0];
			bool ok = false;

			if (tag == "META" && f.count() >= 3)
			{
				ok = toInt(f[1], out.nextId) && toInt(f[2], out.cliSortMode);
			}
			else if (tag == "ACCOUNT" && f.count() == 4)
			{
				Account acc;
				acc.username = unescape(f[1]);
				acc.salt = unescape(f[2]);
				acc.hash = unescape(f[3]);
				if (!acc.username.empty())
				{
					out.accounts.push(std::move(acc));
					ok = true;
				}
			}
			else if (tag == "TOPIC")
			{
				topicIdx = postIdx = -1;
				Topic t;
				if (parseTopic(f, t))
				{
					maxId = t.id > maxId ? t.id : maxId;
					out.topics.push(std::move(t));
					topicIdx = out.topics.count() - 1;
					ok = true;
				}
			}
			else if (tag == "POST")
			{
				postIdx = -1;
				Post p;
				if (topicIdx >= 0 && parsePost(f, p))
				{
					maxId = p.id > maxId ? p.id : maxId;
					Topic& t = out.topics[topicIdx];
					t.posts.push(std::move(p));
					postIdx = t.posts.count() - 1;
					ok = true;
				}
			}
			else if (tag == "REPLY")
			{
				Comment r;
				if (topicIdx >= 0 && postIdx >= 0 && parseReply(f, r))
				{
					maxId = r.id > maxId ? r.id : maxId;
					out.topics[topicIdx].posts[postIdx].replies.push(std::move(r));
					ok = true;
				}
			}

			if (!ok)
				skippedLines++;
		}

		/* Never hand out an id that is already in use, whatever META said */
		if (out.nextId <= maxId)
			out.nextId = maxId + 1;
		return LoadResult::Ok;
	}

	static void writeLikers(std::ostream& os, const Comment& c)
	{
		for (const std::string& user : c.likedBy)
			os << '\t' << escape(user);
	}

	bool save(const std::string& path, int nextId, int cliSortMode,
		const Vector<Account>& accounts, const Vector<Topic>& topics)
	{
		const std::string tmp = path + ".tmp";
		{
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			if (!out)
				return false;

			out << "FORUM\t2\n";
			out << "META\t" << nextId << '\t' << cliSortMode << '\n';
			for (const Account& a : accounts)
				out << "ACCOUNT\t" << escape(a.username) << '\t' << escape(a.salt) << '\t' << escape(a.hash) << '\n';

			for (const Topic& t : topics)
			{
				out << "TOPIC\t" << t.id << '\t' << static_cast<long long>(t.created) << '\t' << escape(t.author)
					<< '\t' << escape(t.text) << '\t' << escape(t.description);
				writeLikers(out, t);
				out << '\n';

				for (const Post& p : t.posts)
				{
					out << "POST\t" << p.id << '\t' << static_cast<long long>(p.created) << '\t' << escape(p.author)
						<< '\t' << (p.edited ? '1' : '0') << '\t' << escape(p.text);
					writeLikers(out, p);
					out << '\n';

					for (const Comment& r : p.replies)
					{
						out << "REPLY\t" << r.id << '\t' << static_cast<long long>(r.created) << '\t' << escape(r.author)
							<< '\t' << escape(r.text);
						writeLikers(out, r);
						out << '\n';
					}
				}
			}
			out.flush();
			if (!out)
			{
				std::remove(tmp.c_str());
				return false;
			}
		}
		if (std::rename(tmp.c_str(), path.c_str()) != 0)
		{
			std::remove(tmp.c_str());
			return false;
		}
		return true;
	}
}

DataLock::~DataLock()
{
	if (fd_ >= 0)
		close(fd_);  /* closing drops the flock */
}

bool DataLock::acquire(const std::string& dir)
{
	fd_ = open((dir + "/forum.lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
	if (fd_ < 0)
		return false;
	if (flock(fd_, LOCK_EX | LOCK_NB) != 0)
	{
		close(fd_);
		fd_ = -1;
		return false;
	}
	return true;
}
