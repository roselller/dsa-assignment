#pragma once

#include <ctime>
#include <string>

#include "Vector.h"

/* Anything a user writes and others can like: a reply on its own, and the base of
   Post and Topic. Ids come from the forum (unique across topics, posts and replies). */
class Comment
{
public:
	int id = 0;
	std::string author;
	std::string text;
	std::time_t created = 0;
	Vector<std::string> likedBy;

	Comment() = default;
	Comment(int id, std::string author, std::string text, std::time_t created) :
		id(id), author(std::move(author)), text(std::move(text)), created(created)
	{ }

	int likes() const { return likedBy.count(); }
	bool isLikedBy(const std::string& user) const;

	/* Likes if user has not liked this yet, otherwise removes the like.
	   Returns true if user likes it afterwards. One like per user. */
	bool toggleLike(const std::string& user);

	/* "YYYY-MM-DD HH:MM:SS" in local time */
	std::string createdString() const;
};
