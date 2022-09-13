#include "bs2pclib.hpp"

#include <utility>

namespace bs2pc {

std::vector<entity_key_values> deserialize_entities(char const * entities_string) {
	std::vector<entity_key_values> entities;
	while (true) {
		// Also checks if the string is empty. Not performing error checking though for simplicity.
		if (parse_token(entities_string).c_str()[0] != '{') {
			break;
		}
		entity_key_values entity;
		while (true) {
			std::string key = parse_token(entities_string);
			if (key.c_str()[0] == '}') {
				entities.emplace_back(std::move(entity));
				break;
			}
			if (!entities_string[0]) {
				// EOF without a closing brace.
				break;
			}
			std::string value = parse_token(entities_string);
			if (!entities_string[0] || value.c_str()[0] == '}') {
				// EOF without a closing brace, or a closing brace without data.
				break;
			}
			// Keeping key names with a leading underscore (utility comments) for 1:1 conversion.
			entity.emplace_back(std::move(key), std::move(value));
		}
	}
	return entities;
}

std::string serialize_entities(entity_key_values const * const entities, size_t const entity_count) {
	std::string entities_string;
	for (size_t i = 0; i < entity_count; ++i) {
		entity_key_values const & entity = entities[i];
		entities_string.append("{\n");
		for (entity_key_value_pair const & entity_key_value : entity) {
			entities_string.push_back('"');
			entities_string.append(entity_key_value.first);
			entities_string.append("\" \"");
			entities_string.append(entity_key_value.second);
			entities_string.append("\"\n");
		}
		entities_string.append("}\n");
	}
	return entities_string;
}

void convert_model_paths(
		entity_key_values * const entities,
		size_t const entity_count,
		uint32_t const version_from,
		uint32_t const version_to) {
	if (version_from == version_to) {
		// Let the conditionals assume that a Quake to Quake conversion is not performed, in particular.
		return;
	}
	for (size_t entity_number = 0; entity_number < entity_count; ++entity_number) {
		for (entity_key_value_pair & key_value_pair : entities[entity_number]) {
			std::string & value = key_value_pair.second;
			if (value.size() < 4) {
				continue;
			}
			char * const extension = value.data() + value.size() - 4;
			// Disregarding the key because it may be not only "model", but also "gibmodel", "shootmodel", or something
			// else.
			if (version_from == gbx_map_version) {
				if (!bs2pc_strncasecmp(extension, ".dol", 4)) {
					if (!bs2pc_strncasecmp(value.c_str(), "models/", sizeof("models/") - 1)) {
						extension[1] += int('m') - int('d');
						extension[2] += int('d') - int('o');
						if (version_to == id_map_version_quake) {
							// Invalidates the extension pointer.
							value = "progs/" + std::string(value.cbegin() + (sizeof("models/") - 1), value.cend());
						}
					}
				} else if (!bs2pc_strncasecmp(extension, ".spz", 4)) {
					if (!bs2pc_strncasecmp(value.c_str(), "sprites/", sizeof("sprites/") - 1)) {
						extension[3] += int('r') - int('z');
						if (version_to == id_map_version_quake) {
							// Invalidates the extension pointer.
							value = "progs/" + std::string(value.cbegin() + (sizeof("sprites/") - 1), value.cend());
						}
					}
				} else if (!bs2pc_strncasecmp(extension, ".bs2", 4)) {
					if (!bs2pc_strncasecmp(value.c_str(), "maps/", sizeof("maps/") - 1)) {
						extension[3] = (extension[2] == 'S' ? 'P' : 'p');
					}
				}
			} else {
				if (!bs2pc_strncasecmp(extension, ".mdl", 4)) {
					if (version_from == id_map_version_quake
							? !bs2pc_strncasecmp(value.c_str(), "progs/", sizeof("progs/") - 1)
							: !bs2pc_strncasecmp(value.c_str(), "models/", sizeof("models/") - 1)) {
						if (version_to == gbx_map_version) {
							extension[1] += int('d') - int('m');
							extension[2] += int('o') - int('d');
						}
						if (version_from == id_map_version_quake) {
							// Invalidates the extension pointer.
							value = "models/" + std::string(value.cbegin() + (sizeof("progs/") - 1), value.cend());
						}
					}
				} else if (!bs2pc_strncasecmp(extension, ".spr", 4)) {
					if (version_from == id_map_version_quake
							? !bs2pc_strncasecmp(value.c_str(), "progs/", sizeof("progs/") - 1)
							: !bs2pc_strncasecmp(value.c_str(), "sprites/", sizeof("sprites/") - 1)) {
						if (version_to == gbx_map_version) {
							extension[3] += int('z') - int('r');
						}
						if (version_from == id_map_version_quake) {
							// Invalidates the extension pointer.
							value = "sprites/" + std::string(value.cbegin() + (sizeof("progs/") - 1), value.cend());
						}
					}
				} else if (!bs2pc_strncasecmp(extension, ".bsp", 4)) {
					if (!bs2pc_strncasecmp(value.c_str(), "maps/", sizeof("maps/") - 1)) {
						extension[3] = '2';
					}
				}
			}
		}
	}
}

}
