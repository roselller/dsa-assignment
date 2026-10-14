#include "Application.h"

#include <cstdlib>
#include <iostream>

namespace
{
	std::string trim(const std::string& s)
	{
		std::size_t a = 0, b = s.size();
		while (a < b && (s[a] == ' ' || s[a] == '\t'))
			a++;
		while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t'))
			b--;
		return s.substr(a, b - a);
	}

	std::string upper(std::string s)
	{
		for (char& c : s)
			if (c >= 'a' && c <= 'z')
				c = static_cast<char>(c - 'a' + 'A');
		return s;
	}

	/* "3" with count = 5 gives index 2; anything else is rejected */
	bool parseChoice(const std::string& s, int count, int& index)
	{
		if (s.empty() || s.size() > 6 || s.find_first_not_of("0123456789") != std::string::npos)
			return false;
		int n = std::atoi(s.c_str());
		if (n < 1 || n > count)
			return false;
		index = n - 1;
		return true;
	}

	/* One line of at most maxBytes, never cut in the middle of a UTF-8 character */
	std::string clip(const std::string& text, std::size_t maxBytes)
	{
		std::string line = Terminal::sanitize(text);
		if (line.size() <= maxBytes)
			return line;
		std::size_t cut = maxBytes;
		while (cut > 0 && (static_cast<unsigned char>(line[cut]) & 0xC0) == 0x80)
			cut--;
		return line.substr(0, cut) + "...";
	}

	std::string count(int n, const char* one, const char* many)
	{
		return std::to_string(n) + " " + (n == 1 ? one : many);
	}
}

Application::Application(Forum& forum, const Terminal& terminal) : forum_(forum), term_(terminal) { }

/* ---- input and output helpers ---- */

std::string Application::ask(const std::string& prompt)
{
	std::string line;
	if (!term_.readLine(prompt, line))
		throw EndOfInput{};
	return line;
}

std::string Application::askPassword(const std::string& prompt)
{
	std::string line;
	if (!term_.readPassword(prompt, line))
		throw EndOfInput{};
	return line;
}

bool Application::confirm(const std::string& question)
{
	std::string answer = upper(trim(ask(question + " [y/N] ")));
	return answer == "Y" || answer == "YES";
}

void Application::notice(const std::string& message, bool good)
{
	std::cout << (good ? term_.green() : term_.red()) << message << term_.reset() << '\n';
	term_.pause(800);
}

bool Application::report(Status status, const std::string& success, const std::string& invalid,
	const std::string& conflict, const std::string& forbidden)
{
	switch (status)
	{
	case Status::Ok:
		if (!success.empty())
			notice(success, true);
		return true;
	case Status::Invalid:
		notice(invalid.empty() ? "That input is not valid." : invalid, false);
		break;
	case Status::Conflict:
		notice(conflict.empty() ? "That already exists." : conflict, false);
		break;
	case Status::Forbidden:
		notice(forbidden.empty() ? "You are not allowed to do that." : forbidden, false);
		break;
	case Status::NotFound:
		notice("That no longer exists.", false);
		break;
	case Status::Unauthorized:
		notice("Wrong credentials.", false);
		break;
	case Status::Internal:
		notice("The change could not be saved to disk.", false);
		break;
	}
	return false;
}

std::string Application::option(const char* key, const char* label, const char* keyColor, bool underlined) const
{
	std::string s = "[";
	s += keyColor;
	s += key;
	s += term_.reset();
	s += "] ";
	if (underlined)
		s += term_.underline();
	s += label;
	if (underlined)
		s += term_.reset();
	return s;
}

void Application::printLogo() const
{
	std::cout << term_.blue() << R"(
  /\\\\\\\\\
/\\\////////       /\\\          /\\\
/\\\/              \/\\\         \/\\\
/\\\             /\\\\\\\\\\\  /\\\\\\\\\\\
\/\\\            \///////////  \///////////
 \//\\\               \/\\\         \/\\\
  \///\\\              \///          \///
     \////\\\\\\\\\
         \/////////
)" << term_.reset() << '\n';
}

/* ---- top level ---- */

int Application::run()
{
	try
	{
		while (true)
		{
			if (user_.empty())
			{
				if (!loginMenu())
					break;
			}
			else
			{
				mainMenu();
			}
		}
	}
	catch (const EndOfInput&)
	{
		std::cout << "\nEnd of input. Goodbye!\n";
	}
	return 0;
}

bool Application::loginMenu()
{
	term_.clear();
	std::cout << term_.green() << "==========" << term_.blue() << "LOGIN MENU" << term_.green() << "=========="
		<< term_.reset() << '\n'
		<< option("1", "Exit", term_.red()) << '\n'
		<< "[2] Register\n"
		<< "[3] Login\n";

	std::string choice = trim(ask(std::string(term_.magenta()) + "Your choice? " + term_.reset()));
	if (choice == "1")
	{
		term_.clear();
		std::cout << term_.magenta() << "Goodbye!" << term_.reset() << '\n';
		return false;
	}
	if (choice == "2")
		registerAccount();
	else if (choice == "3")
		logIn();
	else
		notice("Invalid option!", false);
	return true;
}

void Application::mainMenu()
{
	term_.clear();
	std::cout << term_.yellow() << "Logged in as " << Terminal::sanitize(user_) << term_.reset() << '\n';
	printLogo();
	std::cout << term_.green() << "==========" << term_.blue() << "Welcome to the C++ Programming Forum!" << term_.green()
		<< "==========" << term_.reset() << '\n'
		<< option("1", "Log Out", term_.red()) << '\n'
		<< "[2] See Topics\n"
		<< "[3] Change Password\n";

	std::string choice = trim(ask(std::string(term_.magenta()) + "Your choice? " + term_.reset()));
	if (choice == "1")
		user_.clear();
	else if (choice == "2")
		topicsMenu();
	else if (choice == "3")
		changePassword();
	else
		notice("Invalid option!", false);
}

/* ---- accounts ---- */

void Application::registerAccount()
{
	term_.clear();
	std::string username = ask("Username: ");
	std::string password = askPassword("Password: ");

	std::string canonical;
	Status s = forum_.registerUser(username, password, canonical);
	if (report(s, "Account registered successfully!",
		"Username: 1-32 characters. Password: 4-128 characters.",
		"There is already an account with that username. Please try again."))
		user_ = canonical;
}

void Application::logIn()
{
	term_.clear();
	std::string username = ask("Username: ");
	std::string password = askPassword("Password: ");

	std::string canonical;
	if (report(forum_.login(username, password, canonical), "Logged in successfully!", "", "", ""))
		user_ = canonical;
}

void Application::changePassword()
{
	term_.clear();
	std::string oldPassword = askPassword("Enter old password: ");
	std::string newPassword = askPassword("Enter new password: ");

	Status s = forum_.changePassword(user_, oldPassword, newPassword);
	if (s == Status::Unauthorized)
		notice("Old password does not match!", false);
	else
		report(s, "Password changed successfully!", "The new password must be 4-128 characters.");
}

/* ---- topics ---- */

void Application::topicsMenu()
{
	while (true)
	{
		SortMode mode = forum_.cliSortMode();
		Vector<int> order = forum_.topicOrder(mode);

		term_.clear();
		std::cout << option("E", "Go Back to Main Menu", term_.red()) << '\n'
			<< option("C", "Create a new Topic", term_.green()) << '\n'
			<< option("T", "Sort Topics by Time (newest first)", term_.blue(), mode == SortMode::Time) << '\n'
			<< option("A", "Sort Topics by Alphabet", term_.blue(), mode == SortMode::Alpha) << '\n'
			<< option("O", "Oldest First (order of creation)", term_.blue(), mode == SortMode::None) << '\n'
			<< option("S", "Search topics and posts", term_.blue()) << "\n\n"
			<< term_.cyan() << "Topics:" << term_.reset() << '\n';

		if (order.empty())
			std::cout << "(no topics yet)\n";
		for (int i = 0; i < order.count(); i++)
		{
			const Topic& t = forum_.topics()[order[i]];
			std::cout << term_.yellow() << '[' << i + 1 << "] " << term_.underline() << clip(t.title(), 70) << term_.reset()
				<< "  by " << clip(t.author, 32) << ", " << count(t.posts.count(), "post", "posts")
				<< ", " << count(t.likes(), "like", "likes") << '\n';
		}

		std::string choice = upper(trim(ask(std::string("\n") + term_.magenta() + "Your Choice? " + term_.reset())));
		int index = 0;
		if (choice == "E")
			return;
		else if (choice == "C")
			newTopic();
		else if (choice == "T")
			report(forum_.setCliSortMode(SortMode::Time), "");
		else if (choice == "A")
			report(forum_.setCliSortMode(SortMode::Alpha), "");
		else if (choice == "O")
			report(forum_.setCliSortMode(SortMode::None), "");
		else if (choice == "S")
			searchMenu();
		else if (parseChoice(choice, order.count(), index))
			topicMenu(forum_.topics()[order[index]].id);
		else
			notice("Invalid input! Please try again!", false);
	}
}

void Application::newTopic()
{
	term_.clear();
	std::string title = ask("Topic Title: ");
	std::string description = ask("Topic Description (optional): ");

	int id = 0;
	report(forum_.createTopic(user_, title, description, id), "Topic created!",
		"The title is required (up to 100 characters); the description can be at most 500.",
		"A topic with that title already exists!");
}

void Application::topicMenu(int topicId)
{
	while (true)
	{
		const Topic* topic = forum_.findTopic(topicId);
		if (!topic)
			return;

		term_.clear();
		std::cout << option("E", "Go back to other Topics", term_.red()) << '\n'
			<< option("L", topic->isLikedBy(user_) ? "Unlike Topic" : "Like Topic", term_.blue()) << '\n'
			<< option("R", "Create a new Post", term_.green()) << "\n\n"
			<< term_.underline() << term_.yellow() << Terminal::sanitize(topic->title()) << term_.reset() << '\n'
			<< "Created by: " << Terminal::sanitize(topic->author) << '\n'
			<< "Created at: " << topic->createdString() << '\n'
			<< count(topic->likes(), "Like", "Likes") << '\n'
			<< term_.yellow() << Terminal::sanitize(topic->description, true) << term_.reset() << "\n\n"
			<< term_.cyan() << "Posts:" << term_.reset() << '\n';

		if (topic->posts.empty())
			std::cout << "(no posts yet)\n";
		for (int i = 0; i < topic->posts.count(); i++)
		{
			const Post& p = topic->posts[i];
			std::cout << term_.yellow() << '[' << i + 1 << "] " << term_.underline() << clip(p.text, 70) << term_.reset() << '\n'
				<< "    Posted by " << clip(p.author, 32) << " at " << p.createdString() << ", "
				<< count(p.likes(), "like", "likes") << ", " << count(p.replies.count(), "reply", "replies")
				<< (p.edited ? " (edited)" : "") << '\n';
		}

		std::string choice = upper(trim(ask(std::string("\n") + term_.magenta() + "Your choice? " + term_.reset())));
		int index = 0;
		if (choice == "E")
			return;
		else if (choice == "L")
		{
			bool liked = false;
			if (report(forum_.likeTopic(user_, topicId, liked), ""))
				notice(liked ? "You liked this topic." : "You removed your like.", true);
		}
		else if (choice == "R")
			newPost(topicId);
		else if (parseChoice(choice, topic->posts.count(), index))
			postMenu(topicId, topic->posts[index].id);
		else
			notice("Invalid input! Please try again!", false);
	}
}

void Application::newPost(int topicId)
{
	term_.clear();
	std::string text = ask("Enter text: ");
	int id = 0;
	report(forum_.addPost(user_, topicId, text, id), "Posted!",
		"The text is required (up to 2000 characters).", "Post already exists in this topic!");
}

void Application::postMenu(int topicId, int postId)
{
	while (true)
	{
		const Post* post = forum_.findPost(topicId, postId);
		if (!post)
			return;
		const bool mine = post->author == user_;

		term_.clear();
		std::cout << option("E", "Go Back to Topic", term_.red()) << '\n'
			<< option("L", post->isLikedBy(user_) ? "Unlike Post" : "Like Post", term_.blue()) << '\n'
			<< option("R", "Create a new Reply", term_.green()) << '\n';
		if (mine)
			std::cout << option("P", "Edit Post", term_.green()) << '\n'
				<< option("D", "Delete Post", term_.green()) << '\n';

		std::cout << '\n' << term_.yellow() << term_.underline() << Terminal::sanitize(post->text, true) << term_.reset() << '\n'
			<< "Posted at: " << post->createdString() << (post->edited ? " (edited)" : "") << '\n'
			<< "Posted by: " << Terminal::sanitize(post->author) << '\n'
			<< count(post->likes(), "Like", "Likes") << "\n\n"
			<< term_.cyan() << "Replies:" << term_.reset() << '\n';

		if (post->replies.empty())
			std::cout << "(no replies yet)\n";
		for (int i = 0; i < post->replies.count(); i++)
		{
			const Comment& r = post->replies[i];
			std::cout << term_.yellow() << '[' << i + 1 << "] " << term_.underline() << clip(r.text, 70) << term_.reset() << '\n'
				<< "    Posted by " << clip(r.author, 32) << " at " << r.createdString() << ", "
				<< count(r.likes(), "like", "likes") << (r.isLikedBy(user_) ? " (you like this)" : "") << '\n';
		}
		if (!post->replies.empty())
			std::cout << "Enter a reply number to like or unlike that reply.\n";

		std::string choice = upper(trim(ask(std::string("\n") + term_.magenta() + "Your Choice? " + term_.reset())));
		int index = 0;
		bool liked = false;
		if (choice == "E")
			return;
		else if (choice == "L")
		{
			if (report(forum_.likePost(user_, topicId, postId, liked), ""))
				notice(liked ? "You liked this post." : "You removed your like.", true);
		}
		else if (choice == "R")
		{
			std::string text = ask("Enter reply: ");
			int id = 0;
			report(forum_.addReply(user_, topicId, postId, text, id), "Reply added!",
				"The reply is required (up to 2000 characters).");
		}
		else if (choice == "P" && mine)
			editPost(topicId, postId);
		else if (choice == "D" && mine)
		{
			if (confirm("Delete this post and all of its replies?") &&
				report(forum_.deletePost(user_, topicId, postId), "Post deleted."))
				return;
		}
		else if (parseChoice(choice, post->replies.count(), index))
		{
			if (report(forum_.likeReply(user_, topicId, postId, post->replies[index].id, liked), ""))
				notice(liked ? "You liked this reply." : "You removed your like.", true);
		}
		else
			notice("Invalid input! Please try again!", false);
	}
}

void Application::editPost(int topicId, int postId)
{
	std::string text = ask("Enter new post text (empty to cancel): ");
	if (trim(text).empty())
		return;
	report(forum_.editPost(user_, topicId, postId, text), "Post updated!",
		"The text is required (up to 2000 characters).", "This topic already has a post with that text.",
		"You can only edit your own posts.");
}

/* ---- search ---- */

void Application::searchMenu()
{
	term_.clear();
	std::string query = trim(ask("Search for: "));
	if (query.empty())
		return;

	while (true)
	{
		Vector<SearchHit> hits = forum_.search(query);

		term_.clear();
		std::cout << term_.cyan() << "Results for \"" << Terminal::sanitize(query) << "\" (" << count(hits.count(), "match", "matches")
			<< "):" << term_.reset() << '\n';
		for (int i = 0; i < hits.count(); i++)
		{
			const SearchHit& h = hits[i];
			std::cout << term_.yellow() << '[' << i + 1 << "] " << term_.reset() << (h.postId == 0 ? "Topic: " : "Post in ")
				<< clip(h.topicTitle, 50) << (h.postId == 0 ? "" : ": ") << (h.postId == 0 ? "" : clip(h.text, 60)) << '\n';
		}

		std::string choice = upper(trim(ask(std::string("\nEnter a number to open it, or E to go back: "))));
		int index = 0;
		if (choice == "E" || choice.empty())
			return;
		if (parseChoice(choice, hits.count(), index))
		{
			if (hits[index].postId == 0)
				topicMenu(hits[index].topicId);
			else
				postMenu(hits[index].topicId, hits[index].postId);
		}
		else
			notice("Invalid input! Please try again!", false);
	}
}
