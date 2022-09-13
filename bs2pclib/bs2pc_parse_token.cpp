#include "bs2pclib.hpp"

#include <string>

namespace bs2pc {

std::string parse_token(char const * & data) {
	if (!data) {
		return std::string();
	}

	char character;

	while (true) {
		// Skip whitespace.
		while (static_cast<unsigned char>(character = *data) <= ' ') {
			if (!character) {
				// End of file.
				return std::string();
			}
			++data;
		}

		// Skip // comments.
		if (character == '/' && data[1] == '/') {
			while (*data && *data != '\n') {
				++data;
			}
		} else {
			break;
		}
	}

	std::string token;

	// Handle quoted strings specially.
	if (character == '"') {
		++data;
		while (true) {
			character = *data++;
			if (character == '"' || !character) {
				return token;
			}
			token.push_back(character);
		}
	}

	// Parse single characters.
	if (character == '{' ||
			character == '}' ||
			character == ')' ||
			character == '(' ||
			character == '\'' ||
			character == ':') {
		token.push_back(character);
		++data;
		return token;
	}

	// Parse a regular word.
	do {
		token.push_back(character);
		++data;
		character = *data;
		if (character == '{' ||
				character == '}' ||
				character == ')' ||
				character == '(' ||
				character == '\'' ||
				character == ':') {
			break;
		}
	} while (static_cast<unsigned int>(character) > ' ');

	return token;
}

}
