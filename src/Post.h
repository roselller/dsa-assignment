#pragma once

#include "Comment.h"

/* A post inside a topic. Its text is the post body; replies hang off it. */
class Post : public Comment
{
public:
	Vector<Comment> replies;
	bool edited = false;

	Post() = default;
	Post(int id, std::string author, std::string text, std::time_t created) :
		Comment(id, std::move(author), std::move(text), created)
	{ }
};
