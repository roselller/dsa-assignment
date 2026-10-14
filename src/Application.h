#pragma once

#include <string>

#include "Forum.h"
#include "Terminal.h"

/* The terminal front-end: menus on top of a Forum. */
class Application
{
public:
	Application(Forum& forum, const Terminal& terminal);

	/* Runs until the user exits or the input ends. Returns the process exit code. */
	int run();

private:
	/* Thrown when the input ends, to unwind out of whatever menu is open */
	struct EndOfInput { };

	std::string ask(const std::string& prompt);
	std::string askPassword(const std::string& prompt);
	bool confirm(const std::string& question);

	/* Shows a one-line message, then pauses briefly (when interactive) */
	void notice(const std::string& message, bool good);

	/* Reports a failed Forum call; returns true for Status::Ok */
	bool report(Status status, const std::string& success, const std::string& invalid = "",
		const std::string& conflict = "", const std::string& forbidden = "");

	bool loginMenu();   /* false when the user chose Exit */
	void mainMenu();

	void registerAccount();
	void logIn();
	void changePassword();

	void topicsMenu();
	void newTopic();
	void topicMenu(int topicId);
	void newPost(int topicId);
	void postMenu(int topicId, int postId);
	void editPost(int topicId, int postId);
	void searchMenu();

	void printLogo() const;
	std::string option(const char* key, const char* label, const char* keyColor, bool underlined = false) const;

	Forum& forum_;
	const Terminal& term_;
	std::string user_;
};
