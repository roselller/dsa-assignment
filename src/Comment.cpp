#include "Comment.h"

bool Comment::isLikedBy(const std::string& user) const
{
	return likedBy.indexOf(user) >= 0;
}

bool Comment::toggleLike(const std::string& user)
{
	if (likedBy.remove(user))
		return false;
	likedBy.push(user);
	return true;
}

std::string Comment::createdString() const
{
	std::tm parts{};
	localtime_r(&created, &parts);
	char buf[32];
	std::size_t n = std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &parts);
	return std::string(buf, n);
}
