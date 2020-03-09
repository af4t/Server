/**
 * EQEmulator: Everquest Server Emulator
 * Copyright (C) 2001-2020 EQEmulator Development Team (https://github.com/EQEmu/Server)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY except by those people which sell it, which
 * are required to give you total support for your newly bought product;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
*/

#include <string>
#include <cstdio>
#include <iterator>
#include "database_dump_service.h"
#include "../eqemu_logsys.h"
#include "../string_util.h"
#include "../eqemu_config.h"
#include "../database_schema.h"
#include "../file_util.h"

#define DATABASE_DUMP_PATH "backups/"

/**
 * @param cmd
 * @param return_result
 * @return
 */
std::string DatabaseDumpService::execute(const std::string &cmd, bool return_result = true)
{
	const char *file_name = "db-exec-result.txt";

	if (return_result) {
		std::system((cmd + " > " + file_name).c_str());
	}
	else {
		std::system((cmd).c_str());
	}

	std::string result;

	if (return_result) {
		std::ifstream file(file_name);
		result = {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
		std::remove(file_name);

	}

	return result;
}

/**
 * @return bool
 */
bool DatabaseDumpService::IsMySQLInstalled()
{
	std::string version_output = GetMySQLVersion();

	return version_output.find("mysql") != std::string::npos && version_output.find("Ver") != std::string::npos;
}

/**
 * Linux
 * @return bool
 */
bool DatabaseDumpService::IsTarAvailable()
{
	std::string version_output = execute("tar --version");

	return version_output.find("GNU tar") != std::string::npos;
}

/**
 * Windows TODO
 * @return bool
 */
bool DatabaseDumpService::IsRarAvailable()
{
	std::string version_output = execute("winrar --version");

	return version_output.find("todo") != std::string::npos;
}

/**
 * @return
 */
bool DatabaseDumpService::HasCompressionBinary()
{
	return IsTarAvailable() || IsRarAvailable();
}

/**
 * @return
 */
std::string DatabaseDumpService::GetMySQLVersion()
{
	std::string version_output = execute("mysql --version");

	return trim(version_output);
}

/**
 * @return
 */
std::string DatabaseDumpService::GetBaseMySQLDumpCommand()
{
	auto config = EQEmuConfig::get();

	return fmt::format(
		"mysqldump -u {} -p{} -h {} {}",
		config->DatabaseUsername,
		config->DatabasePassword,
		config->DatabaseHost,
		config->DatabaseDB
	);
}

/**
 * @return
 */
std::string DatabaseDumpService::GetPlayerTablesList()
{
	std::string              tables_list;
	std::vector<std::string> tables = DatabaseSchema::GetPlayerTables();
	for (const auto          &table : tables) {
		tables_list += table + " ";
	}

	return trim(tables_list);
}

/**
 * @return
 */
std::string DatabaseDumpService::GetLoginTableList()
{
	std::string              tables_list;
	std::vector<std::string> tables = DatabaseSchema::GetLoginTables();
	for (const auto          &table : tables) {
		tables_list += table + " ";
	}

	return trim(tables_list);
}

/**
 * @return
 */
std::string DatabaseDumpService::GetSystemTablesList()
{
	std::string tables_list;

	std::vector<std::string> tables = DatabaseSchema::GetServerTables();
	for (const auto          &table : tables) {
		tables_list += table + " ";
	}

	tables = DatabaseSchema::GetStateTables();
	for (const auto &table : tables) {
		tables_list += table + " ";
	}

	tables = DatabaseSchema::GetVersionTables();
	for (const auto &table : tables) {
		tables_list += table + " ";
	}

	return trim(tables_list);
}

/**
 * @return
 */
std::string DatabaseDumpService::GetContentTablesList()
{
	std::string tables_list;

	std::vector<std::string> tables = DatabaseSchema::GetContentTables();
	for (const auto          &table : tables) {
		tables_list += table + " ";
	}

	return trim(tables_list);
}

/**
 * @return
 */
std::string GetDumpDate()
{

	time_t    now = time(nullptr);
	struct tm time_struct{};
	char      buf[80];
	time_struct = *localtime(&now);
	strftime(buf, sizeof(buf), "%Y-%m-%d", &time_struct);

	std::string time = buf;

	return time;
}

/**
 * @return
 */
std::string DatabaseDumpService::GetSetDumpPath()
{
	return !GetDumpPath().empty() ? GetDumpPath() : DATABASE_DUMP_PATH;
}
/**
 * @return
 */
std::string DatabaseDumpService::GetDumpFileNameWithPath()
{
	return GetSetDumpPath() + GetDumpFileName();
}

void DatabaseDumpService::Dump()
{
	if (!IsMySQLInstalled()) {
		LogError("MySQL is not installed; Please check your PATH for a valid MySQL installation");
		return;
	}

	LogInfo("MySQL installed [{}]", GetMySQLVersion());

	SetDumpFileName(EQEmuConfig::get()->DatabaseDB + '-' + GetDumpDate());

	auto config = EQEmuConfig::get();

	LogInfo(
		"Database [{}] Host [{}] Username [{}]",
		config->DatabaseDB,
		config->DatabaseHost,
		config->DatabaseUsername
	);

	std::string options = "--compact --allow-keywords --extended-insert";

	if (IsDumpWithNoData()) {
		options += " --no-data";
	}

	if (IsDumpNoTableLock()) {
		options += " --skip-lock-tables";
	}

	std::string tables_to_dump;
	std::string dump_descriptor;

	if (!IsDumpAllTables()) {
		if (IsDumpPlayerTables()) {
			tables_to_dump += GetPlayerTablesList();
			dump_descriptor += "-player";
		}

		if (IsDumpSystemTables()) {
			tables_to_dump += GetSystemTablesList();
			dump_descriptor += "-system";
		}

		if (IsDumpContentTables()) {
			tables_to_dump += GetContentTablesList();
			dump_descriptor += "-content";
		}

		if (IsDumpLoginServerTables()) {
			tables_to_dump += GetLoginTableList();
			dump_descriptor += "-login";
		}
	}

	if (!dump_descriptor.empty()) {
		SetDumpFileName(GetDumpFileName() + dump_descriptor);
	}

	std::string execute_command = fmt::format(
		"{} {} {} > {}.sql",
		GetBaseMySQLDumpCommand(),
		options,
		tables_to_dump,
		GetDumpFileNameWithPath()
	);

	if (!FileUtil::exists(GetSetDumpPath())) {
		FileUtil::mkdir(GetSetDumpPath());
	}

	execute(execute_command, false);

	if (!tables_to_dump.empty()) {
		LogInfo("Dumping Tables [{}]", tables_to_dump);
	}

	LogInfo("Database dump created at [{}.sql]", GetDumpFileNameWithPath());

	if (IsDumpWithCompression()) {
		if (HasCompressionBinary()) {
			LogInfo("Compression requested... Compressing dump [{}.sql]", GetDumpFileNameWithPath());

			if (IsTarAvailable()) {
				execute(
					fmt::format(
						"tar -zcvf {}.tar.gz -C {} {}.sql",
						GetDumpFileNameWithPath(),
						GetSetDumpPath(),
						GetDumpFileName()
					)
				);
				LogInfo("Compressed dump created at [{}.tar.gz]", GetDumpFileNameWithPath());
			}
		}
		else {
			LogWarning("Compression requested but binary not found... Skipping...");
		}
	}

//	LogDebug("[{}] dump-path", GetSetDumpPath());
//	LogDebug("[{}] compression", (IsDumpWithCompression() ? "true" : "false"));
//	LogDebug("[{}] has-compression-binary", (HasCompressionBinary() ? "true" : "false"));
//	LogDebug("[{}] content", (IsDumpContentTables() ? "true" : "false"));
//	LogDebug("[{}] no-data", (IsDumpWithNoData() ? "true" : "false"));
//	LogDebug("[{}] login", (IsDumpLoginServerTables() ? "true" : "false"));
//	LogDebug("[{}] player", (IsDumpPlayerTables() ? "true" : "false"));
//	LogDebug("[{}] system", (IsDumpSystemTables() ? "true" : "false"));
}

bool DatabaseDumpService::IsDumpSystemTables() const
{
	return dump_system_tables;
}

void DatabaseDumpService::SetDumpSystemTables(bool dump_system_tables)
{
	DatabaseDumpService::dump_system_tables = dump_system_tables;
}

bool DatabaseDumpService::IsDumpContentTables() const
{
	return dump_content_tables;
}

void DatabaseDumpService::SetDumpContentTables(bool dump_content_tables)
{
	DatabaseDumpService::dump_content_tables = dump_content_tables;
}

bool DatabaseDumpService::IsDumpPlayerTables() const
{
	return dump_player_tables;
}

void DatabaseDumpService::SetDumpPlayerTables(bool dump_player_tables)
{
	DatabaseDumpService::dump_player_tables = dump_player_tables;
}

bool DatabaseDumpService::IsDumpLoginServerTables() const
{
	return dump_login_server_tables;
}

void DatabaseDumpService::SetDumpLoginServerTables(bool dump_login_server_tables)
{
	DatabaseDumpService::dump_login_server_tables = dump_login_server_tables;
}

bool DatabaseDumpService::IsDumpWithNoData() const
{
	return dump_with_no_data;
}

void DatabaseDumpService::SetDumpWithNoData(bool dump_with_no_data)
{
	DatabaseDumpService::dump_with_no_data = dump_with_no_data;
}

bool DatabaseDumpService::IsDumpAllTables() const
{
	return dump_all_tables;
}

void DatabaseDumpService::SetDumpAllTables(bool dump_all_tables)
{
	DatabaseDumpService::dump_all_tables = dump_all_tables;
}
bool DatabaseDumpService::IsDumpNoTableLock() const
{
	return dump_no_table_lock;
}

void DatabaseDumpService::SetDumpNoTableLock(bool dump_no_table_lock)
{
	DatabaseDumpService::dump_no_table_lock = dump_no_table_lock;
}

bool DatabaseDumpService::IsDumpWithCompression() const
{
	return dump_with_compression;
}

void DatabaseDumpService::SetDumpWithCompression(bool dump_with_compression)
{
	DatabaseDumpService::dump_with_compression = dump_with_compression;
}

const std::string &DatabaseDumpService::GetDumpPath() const
{
	return dump_path;
}

void DatabaseDumpService::SetDumpPath(const std::string &dump_path)
{
	DatabaseDumpService::dump_path = dump_path;
}

void DatabaseDumpService::SetDumpFileName(const std::string &dump_file_name)
{
	DatabaseDumpService::dump_file_name = dump_file_name;
}

const std::string &DatabaseDumpService::GetDumpFileName() const
{
	return dump_file_name;
}
