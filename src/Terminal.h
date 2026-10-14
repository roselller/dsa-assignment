#pragma once

#include <string>

/* Console input and output for the terminal client. When stdin or stdout is not a
   terminal (a pipe, a file, a test) it switches to plain mode: no colours, no screen
   clearing and no delays. NO_COLOR is honoured too. */
class Terminal
{
	bool interactive_;
	bool color_;

public:
	Terminal();

	bool interactive() const { return interactive_; }

	/* ANSI colour codes, or empty strings when colour is off */
	const char* reset() const { return color_ ? "\x1B[0m" : ""; }
	const char* underline() const { return color_ ? "\x1B[4m" : ""; }
	const char* red() const { return color_ ? "\x1B[91m" : ""; }
	const char* green() const { return color_ ? "\x1B[92m" : ""; }
	const char* yellow() const { return color_ ? "\x1B[93m" : ""; }
	const char* blue() const { return color_ ? "\x1B[94m" : ""; }
	const char* magenta() const { return color_ ? "\x1B[95m" : ""; }
	const char* cyan() const { return color_ ? "\x1B[96m" : ""; }

	/* Clears the screen (interactive only) */
	void clear() const;

	/* Waits so a short message can be read (interactive only) */
	void pause(int milliseconds) const;

	/* Shows prompt and reads one line. Returns false at end of input. */
	bool readLine(const std::string& prompt, std::string& out) const;

	/* Like readLine, but does not echo what is typed when on a terminal */
	bool readPassword(const std::string& prompt, std::string& out) const;

	/* text with control characters (such as escape sequences) replaced by '?', so stored
	   text can never drive the terminal. Newlines and tabs become spaces unless
	   keepNewlines is set. */
	static std::string sanitize(const std::string& text, bool keepNewlines = false);
};
