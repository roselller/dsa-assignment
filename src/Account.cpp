#include "Account.h"

#include "Sha256.h"

Account Account::create(const std::string& username, const std::string& password)
{
	Account acc;
	acc.username = username;
	acc.setPassword(password);
	return acc;
}

bool Account::checkPassword(const std::string& password) const
{
	return crypto::constantTimeEquals(crypto::hashPassword(password, salt), hash);
}

void Account::setPassword(const std::string& password)
{
	salt = crypto::randomHex(16);
	hash = crypto::hashPassword(password, salt);
}
