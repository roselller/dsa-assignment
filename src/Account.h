#pragma once

#include <string>

/* A user account. Only a salted hash of the password is kept. */
class Account
{
public:
	std::string username;
	std::string salt;
	std::string hash;

	Account() = default;

	/* New account with a fresh random salt */
	static Account create(const std::string& username, const std::string& password);

	bool checkPassword(const std::string& password) const;

	/* Replaces the password, using a fresh salt */
	void setPassword(const std::string& password);
};
