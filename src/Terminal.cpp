#include "Terminal.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

#include <termios.h>
#include <unistd.h>

Terminal::Terminal() :
	interactive_(isatty(STDIN_FILENO) && isatty(STDOUT_FILENO)),
	color_(interactive_ && !std::getenv("NO_COLOR"))
{ }

void Terminal::clear() const
{
	if (interactive_)
		std::cout << "\x1B[2J\x1B[H";
}

void Terminal::pause(int milliseconds) const
{
	if (interactive_)
	{
		std::cout.flush();
		std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
	}
}

bool Terminal::readLine(const std::string& prompt, std::string& out) const
{
	std::cout << prompt << std::flush;
	bool ok = static_cast<bool>(std::getline(std::cin, out));
	if (ok && !out.empty() && out.back() == '\r')
		out.pop_back();
	if (!interactive_)
		std::cout << '\n';  /* the input was not echoed, so end the prompt's line */
	return ok;
}

bool Terminal::readPassword(const std::string& prompt, std::string& out) const
{
	if (!interactive_)
		return readLine(prompt, out);

	termios original{};
	bool changed = false;
	if (tcgetattr(STDIN_FILENO, &original) == 0)
	{
		termios quiet = original;
		quiet.c_lflag &= ~static_cast<tcflag_t>(ECHO);
		changed = tcsetattr(STDIN_FILENO, TCSAFLUSH, &quiet) == 0;
	}

	std::cout << prompt << std::flush;
	bool ok = static_cast<bool>(std::getline(std::cin, out));
	if (changed)
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
	std::cout << '\n';
	return ok;
}

std::string Terminal::sanitize(const std::string& text, bool keepNewlines)
{
	std::string out;
	out.reserve(text.size());
	for (unsigned char c : text)
	{
		if (c == '\n')
			out += keepNewlines ? '\n' : ' ';
		else if (c == '\t')
			out += keepNewlines ? '\t' : ' ';
		else if (c < 0x20 || c == 0x7f)
			out += '?';
		else
			out += static_cast<char>(c);
	}
	return out;
}
