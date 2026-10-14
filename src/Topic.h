#pragma once

#include "Post.h"

/* A discussion topic. The inherited text is the title. */
class Topic : public Comment
{
public:
	std::string description;
	Vector<Post> posts;

	Topic() = default;
	Topic(int id, std::string author, std::string title, std::string description, std::time_t created) :
		Comment(id, std::move(author), std::move(title), created), description(std::move(description))
	{ }

	const std::string& title() const { return text; }
};
