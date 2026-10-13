#pragma once

#include "Common.h"
#include <string>

class ProjectSerializer {
public:
    static bool save_to_file(Project& project, const std::string& filepath);
    static bool load_from_file(Project& project, const std::string& filepath);
    static std::string serialize_json(Project& project);
    static bool deserialize_json(Project& project, const std::string& json_str);
};
