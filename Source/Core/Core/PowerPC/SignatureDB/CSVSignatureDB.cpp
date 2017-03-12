// Copyright 2016 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <cstdio>
#include <fstream>
#include <sstream>

#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"

#include "Core/PowerPC/SignatureDB/CSVSignatureDB.h"

// CSV separated with tabs
// Checksum | Size | Symbol | [Object Location |] Object Name
bool CSVSignatureDB::Load(const std::string& file_path, SignatureDB::FuncDB& database) const
{
	std::string line;
	std::ifstream ifs;
	OpenFStream(ifs, file_path, std::ios_base::in);

	if (!ifs)
		return false;
	for (size_t i = 1; std::getline(ifs, line); i += 1)
	{
		std::istringstream iss(line);
		u32 checksum, size;
		std::string tab, symbol, object_location, object_name;

		iss >> std::hex >> checksum >> std::hex >> size;
		if (iss && std::getline(iss, tab, '\t'))
		{
			if (std::getline(iss, symbol, '\t') && std::getline(iss, object_location, '\t'))
				std::getline(iss, object_name);
			SignatureDB::DBFunc func;
			func.name = symbol;
			func.size = size;
			// Doesn't have an object location
			if (object_name.empty())
			{
				func.object_name = object_location;
			}
			else
			{
				func.object_location = object_location;
				func.object_name = object_name;
			}
			database[checksum] = func;
		}
		else
		{
			WARN_LOG(OSHLE, "CSV database failed to parse line %zu", i);
		}
	}

	return true;
}

bool CSVSignatureDB::Save(const std::string& file_path, const SignatureDB::FuncDB& database) const
{
	File::IOFile f(file_path, "w");

	if (!f)
	{
		ERROR_LOG(OSHLE, "CSV database save failed");
		return false;
	}
	for (const auto& func : database)
	{
		// The object name/location are unused for the time being.
		// To be implemented.
		fprintf(f.GetHandle(), "%08x\t%08x\t%s\t%s\t%s\n", func.first, func.second.size,
			func.second.name.c_str(), func.second.object_location.c_str(),
			func.second.object_name.c_str());
	}

	INFO_LOG(OSHLE, "CSV database save successful");
	return true;
}