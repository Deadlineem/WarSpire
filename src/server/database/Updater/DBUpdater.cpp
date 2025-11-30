/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "DBUpdater.h"
#include "BuiltInConfig.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "DatabaseLoader.h"
#include "GitRevision.h"
#include "Log.h"
#include "QueryResult.h"
#include "StartProcess.h"
#include "UpdateFetcher.h"
#include <boost/filesystem/operations.hpp>
#include <fstream>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#include <urlmon.h>
#pragma comment(lib, "urlmon.lib")
bool DownloadFile(const std::string& url, const std::string& dest) {
    return SUCCEEDED(URLDownloadToFileA(nullptr, url.c_str(), dest.c_str(), 0, nullptr));
}
#else
#include <fstream>
#include <curl/curl.h> // optional; could replace with Boost.Asio
bool DownloadFile(const std::string& url, const std::string& dest) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    FILE* fp = fopen(dest.c_str(), "wb");
    if (!fp) { curl_easy_cleanup(curl); return false; }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, nullptr);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    fclose(fp);
    curl_easy_cleanup(curl);
    return res == CURLE_OK;
}
#endif

std::string DBUpdaterUtil::GetCorrectedMySQLExecutable()
{
    if (!corrected_path().empty())
        return corrected_path();
    else
        return BuiltInConfig::GetMySQLExecutable();
}

bool DBUpdaterUtil::CheckExecutable()
{
    boost::filesystem::path exe(GetCorrectedMySQLExecutable());
    if (!is_regular_file(exe))
    {
        exe = Trinity::SearchExecutableInPath("mysql");
        if (!exe.empty() && is_regular_file(exe))
        {
            // Correct the path to the cli
            corrected_path() = absolute(exe).generic_string();
            return true;
        }

        TC_LOG_FATAL("sql.updates", "Didn't find any executable MySQL binary at \'{}\' or in path, correct the path in the *.conf (\"MySQLExecutable\").",
            absolute(exe).generic_string());

        return false;
    }
    return true;
}

std::string& DBUpdaterUtil::corrected_path()
{
    static std::string path;
    return path;
}

// Auth Database
template<>
std::string DBUpdater<LoginDatabaseConnection>::GetConfigEntry()
{
    return "Updates.Auth";
}

template<>
std::string DBUpdater<LoginDatabaseConnection>::GetTableName()
{
    return "Auth";
}

template<>
std::string DBUpdater<LoginDatabaseConnection>::GetBaseFile()
{
    return BuiltInConfig::GetSourceDirectory() +
        "/sql/base/auth_database.sql";
}

template<>
bool DBUpdater<LoginDatabaseConnection>::IsEnabled(uint32 const updateMask)
{
    // This way silences warnings under msvc
    return (updateMask & DatabaseLoader::DATABASE_LOGIN) ? true : false;
}

// World Database
template<>
std::string DBUpdater<WorldDatabaseConnection>::GetConfigEntry()
{
    return "Updates.World";
}

template<>
std::string DBUpdater<WorldDatabaseConnection>::GetTableName()
{
    return "World";
}

template<>
std::string DBUpdater<WorldDatabaseConnection>::GetBaseFile()
{
    return GitRevision::GetFullDatabase();
}

template<>
bool DBUpdater<WorldDatabaseConnection>::IsEnabled(uint32 const updateMask)
{
    // This way silences warnings under msvc
    return (updateMask & DatabaseLoader::DATABASE_WORLD) ? true : false;
}

template<>
BaseLocation DBUpdater<WorldDatabaseConnection>::GetBaseLocationType()
{
    return LOCATION_DOWNLOAD;
}

// Character Database
template<>
std::string DBUpdater<CharacterDatabaseConnection>::GetConfigEntry()
{
    return "Updates.Character";
}

template<>
std::string DBUpdater<CharacterDatabaseConnection>::GetTableName()
{
    return "Character";
}

template<>
std::string DBUpdater<CharacterDatabaseConnection>::GetBaseFile()
{
    return BuiltInConfig::GetSourceDirectory() +
        "/sql/base/characters_database.sql";
}

template<>
bool DBUpdater<CharacterDatabaseConnection>::IsEnabled(uint32 const updateMask)
{
    // This way silences warnings under msvc
    return (updateMask & DatabaseLoader::DATABASE_CHARACTER) ? true : false;
}

// Hotfix Database
template<>
std::string DBUpdater<HotfixDatabaseConnection>::GetConfigEntry()
{
    return "Updates.Hotfix";
}

template<>
std::string DBUpdater<HotfixDatabaseConnection>::GetTableName()
{
    return "Hotfixes";
}

template<>
std::string DBUpdater<HotfixDatabaseConnection>::GetBaseFile()
{
    return GitRevision::GetHotfixesDatabase();
}

template<>
bool DBUpdater<HotfixDatabaseConnection>::IsEnabled(uint32 const updateMask)
{
    // This way silences warnings under msvc
    return (updateMask & DatabaseLoader::DATABASE_HOTFIX) ? true : false;
}

template<>
BaseLocation DBUpdater<HotfixDatabaseConnection>::GetBaseLocationType()
{
    return LOCATION_DOWNLOAD;
}

// All
template<class T>
BaseLocation DBUpdater<T>::GetBaseLocationType()
{
    return LOCATION_REPOSITORY;
}

template<class T>
bool DBUpdater<T>::Create(DatabaseWorkerPool<T>& pool)
{
    TC_LOG_INFO("sql.updates", "Database \"{}\" does not exist, automatically creating it...",
        pool.GetConnectionInfo()->database);

    // Path of temp file
    static Path const temp("create_table.sql");

    std::ofstream file(temp.generic_string());
    if (!file.is_open())
    {
        TC_LOG_FATAL("sql.updates", "Failed to create temporary query file \"{}\"!", temp.generic_string());
        return false;
    }

    file << "CREATE DATABASE `" << pool.GetConnectionInfo()->database
        << "` DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci\n\n";
    file.close();

    try
    {
        DBUpdater<T>::ApplyFile(pool, pool.GetConnectionInfo()->host, pool.GetConnectionInfo()->user, pool.GetConnectionInfo()->password,
            pool.GetConnectionInfo()->port_or_socket, "", pool.GetConnectionInfo()->ssl, temp);
    }
    catch (UpdateException&)
    {
        TC_LOG_FATAL("sql.updates", "Failed to create database {}! Does the user (named in *.conf) have `CREATE`, `ALTER`, `DROP`, `INSERT` and `DELETE` privileges on the MySQL server?", pool.GetConnectionInfo()->database);
        boost::filesystem::remove(temp);
        return false;
    }

    TC_LOG_INFO("sql.updates", "Done.");
    boost::filesystem::remove(temp);
    return true;
}


template<class T>
bool DBUpdater<T>::Update(DatabaseWorkerPool<T>& pool)
{
    if (!DBUpdaterUtil::CheckExecutable())
        return false;

    TC_LOG_INFO("sql.updates", "Updating {} database...", DBUpdater<T>::GetTableName());

    Path const sourceDirectory(BuiltInConfig::GetSourceDirectory());

    if (!is_directory(sourceDirectory))
    {
        TC_LOG_ERROR("sql.updates", "DBUpdater: The given source directory {} does not exist, change the path to the directory where your sql directory exists (for example c:\\source\\trinitycore). Shutting down.", sourceDirectory.generic_string());
        return false;
    }

    UpdateFetcher updateFetcher(sourceDirectory, [&](std::string const& query) { DBUpdater<T>::Apply(pool, query); },
        [&](Path const& file) { DBUpdater<T>::ApplyFile(pool, file); },
        [&](std::string const& query) -> QueryResult { return DBUpdater<T>::Retrieve(pool, query); });

    UpdateResult result;
    try
    {
        result = updateFetcher.Update(
            sConfigMgr->GetBoolDefault("Updates.Redundancy", true),
            sConfigMgr->GetBoolDefault("Updates.AllowRehash", true),
            sConfigMgr->GetBoolDefault("Updates.ArchivedRedundancy", false),
            sConfigMgr->GetIntDefault("Updates.CleanDeadRefMaxCount", 3));
    }
    catch (UpdateException&)
    {
        return false;
    }

    std::string const info = Trinity::StringFormat("Containing {} new and {} archived updates.",
        result.recent, result.archived);

    if (!result.updated)
        TC_LOG_INFO("sql.updates", ">> {} database is up-to-date! {}", DBUpdater<T>::GetTableName(), info);
    else
        TC_LOG_INFO("sql.updates", ">> Applied {} {}. {}", result.updated, result.updated == 1 ? "query" : "queries", info);

    return true;
}

template<class T>
bool DBUpdater<T>::Populate(DatabaseWorkerPool<T>& pool)
{
    if (!DBUpdaterUtil::CheckExecutable())
        return false;

    const std::string dbName = DBUpdater<T>::GetTableName();

    // Only force download/apply for World and Hotfixes
    if (dbName != "World" && dbName != "Hotfixes")
    {
        // Original behavior: only populate if empty
        QueryResult const result = Retrieve(pool, "SHOW TABLES");
        if (result && (result->GetRowCount() > 0))
            return true;
    }

    TC_LOG_INFO("sql.updates", "Updating the {} database to latest...", dbName);

    std::string const p = DBUpdater<T>::GetBaseFile();
    if (p.empty())
    {
        TC_LOG_INFO("sql.updates", ">> No base file provided, skipped!");
        return true;
    }

    Path base(p);

    // For World and Hotfixes, always download latest file
    std::string downloadUrl;
    if (dbName == "World")
        downloadUrl = "https://warspire.fpr.net/download/sql/TDB_full_world_1125.25101_2025_10_29.sql";
    else if (dbName == "Hotfixes")
        downloadUrl = "https://warspire.fpr.net/download/sql/TDB_full_hotfixes_1125.25101_2025_10_29.sql";

    if (!downloadUrl.empty())
    {
        // Check AllowAutoDBUpdate in configuration
        bool autoUpdate = sConfigMgr->GetBoolDefault("AllowAutoDBUpdate", false);

        std::string userInput;
        if (autoUpdate)
        {
            TC_LOG_INFO("sql.updates", "AllowAutoDBUpdate=1, automatically proceeding with database update.");
            userInput = "y";
        }
        else
        {
            std::cout << "Do you want to download and apply the latest " << dbName
                << " database update? [y/N]: ";
            std::getline(std::cin, userInput);
        }

        if (userInput == "y" || userInput == "Y")
        {
            TC_LOG_INFO("sql.updates", "Downloading latest base SQL from {} ...", downloadUrl);
            if (!DownloadFile(downloadUrl, base.generic_string()))
            {
                TC_LOG_FATAL("sql.updates", "Failed to download {}. Manual download required!", downloadUrl);
                return false;
            }
            TC_LOG_INFO("sql.updates", "Successfully downloaded {}", base.generic_string());
        }
        else
        {
            // Ask if user wants to use an existing local file
            std::cout << "Do you want to use an existing local SQL file instead? [y/N]: ";
            std::getline(std::cin, userInput);
            if (userInput == "y" || userInput == "Y")
            {
                std::string localFile;
                std::cout << "Enter full path to local SQL file: ";
                std::getline(std::cin, localFile);

                if (localFile.empty())
                {
                    TC_LOG_INFO("sql.updates", "No local file provided, skipping database update.");
                }

                base = Path(localFile);
                TC_LOG_INFO("sql.updates", "Using existing local file '{}'", base.generic_string());
            }
            else
            {
                TC_LOG_INFO("sql.updates", "Update canceled by user.");

            }
        }
    }

    // Apply base SQL
    TC_LOG_INFO("sql.updates", ">> Applying '{}'...", base.generic_string());
    try
    {
        ApplyFile(pool, base);
    }
    catch (UpdateException&)
    {

    }

    TC_LOG_INFO("sql.updates", ">> {} Database update completed!", dbName);
    return true;
}


template<class T>
QueryResult DBUpdater<T>::Retrieve(DatabaseWorkerPool<T>& pool, std::string const& query)
{
    return pool.Query(query.c_str());
}

template<class T>
void DBUpdater<T>::Apply(DatabaseWorkerPool<T>& pool, std::string const& query)
{
    pool.DirectExecute(query.c_str());
}

template<class T>
void DBUpdater<T>::ApplyFile(DatabaseWorkerPool<T>& pool, Path const& path)
{
    DBUpdater<T>::ApplyFile(pool, pool.GetConnectionInfo()->host, pool.GetConnectionInfo()->user, pool.GetConnectionInfo()->password,
        pool.GetConnectionInfo()->port_or_socket, pool.GetConnectionInfo()->database, pool.GetConnectionInfo()->ssl, path);
}

template<class T>
void DBUpdater<T>::ApplyFile(DatabaseWorkerPool<T>& pool, std::string const& host, std::string const& user,
    std::string const& password, std::string const& port_or_socket, std::string const& database, std::string const& ssl,
    Path const& path)
{
    std::vector<std::string> args;
    args.reserve(9);

    // CLI Client connection info
    args.emplace_back("-h" + host);
    args.emplace_back("-u" + user);

    if (!password.empty())
        args.emplace_back("-p" + password);

    // Check if we want to connect through ip or socket (Unix only)
#ifdef _WIN32

    if (host == ".")
        args.emplace_back("--protocol=PIPE");
    else
        args.emplace_back("-P" + port_or_socket);

#else

    if (!std::isdigit(port_or_socket[0]))
    {
        // We can't check if host == "." here, because it is named localhost if socket option is enabled
        args.emplace_back("-P0");
        args.emplace_back("--protocol=SOCKET");
        args.emplace_back("-S" + port_or_socket);
    }
    else
        // generic case
        args.emplace_back("-P" + port_or_socket);

#endif

    // Set the default charset to utf8
    args.emplace_back("--default-character-set=utf8mb4");

    // Set max allowed packet to 1 GB
    args.emplace_back("--max-allowed-packet=1GB");

#if !defined(MARIADB_VERSION_ID) && MYSQL_VERSION_ID >= 80000

    if (ssl == "ssl")
        args.emplace_back("--ssl-mode=REQUIRED");

#if MYSQL_VERSION_ID >= 90400

    // Since MySQL 9.4 command line client commands are disabled by default
    // We need to enable them to use `SOURCE` command
    args.emplace_back("--commands=ON");

#endif

#else

    if (ssl == "ssl")
        args.emplace_back("--ssl");

#endif

    // Execute sql file
    args.emplace_back("-e");
    args.emplace_back(Trinity::StringFormat("BEGIN; SOURCE {}; COMMIT;", path.generic_string()));

    // Database
    if (!database.empty())
        args.emplace_back(database);

    // Invokes a mysql process which doesn't leak credentials to logs
    int32 const ret = Trinity::StartProcess(DBUpdaterUtil::GetCorrectedMySQLExecutable(), std::move(args),
        "sql.updates", "", true);

    if (ret != EXIT_SUCCESS)
    {
        bool defaultExists = is_regular_file(path);
        std::string dbName = pool.GetConnectionInfo()->database;

        if (!defaultExists)
        {
            TC_LOG_ERROR("sql.updates",
                "Database update aborted or failed for '{}'.\n"
                "You declined to download or provide a custom SQL file, and no default TDB SQL file was found at '{}'.\n"
                "Please manually place a valid SQL file in the correct location, enable AutoDBUpdate, or re-run the configuration.",
                dbName, path.generic_string());
        }
        else
        {
            TC_LOG_WARN("sql.updates",
                "Database update for '{}' was skipped or failed.\n"
                "You declined both online and custom local SQL updates, but a default local file was found:\n"
                "    {}\n"
                "Would you like to use this default SQL file for database setup?",
                dbName, path.generic_string());

            std::string response;
            std::cout << "Use default TrinityCore SQL (TDB) files? [y/N]: ";
            std::getline(std::cin, response);

            if (response == "y" || response == "Y")
            {
                TC_LOG_INFO("sql.updates", "Applying default local SQL file '{}'...", path.generic_string());
                try
                {
                    ApplyFile(pool, path);
                }
                catch (UpdateException&)
                {
                    TC_LOG_FATAL("sql.updates",
                        "Default SQL file '{}' could not be applied. "
                        "Please verify the file is valid or re-run the configuration.",
                        path.generic_string());
                    throw;
                }
                return; // Successfully applied fallback
            }
            else
            {
                TC_LOG_INFO("sql.updates",
                    "User declined to apply default local SQL file. Database '{}' remains unchanged.",
                    dbName);
            }
        }

        throw UpdateException("Database update canceled or failed");
    }
}

template class TC_DATABASE_API DBUpdater<LoginDatabaseConnection>;
template class TC_DATABASE_API DBUpdater<WorldDatabaseConnection>;
template class TC_DATABASE_API DBUpdater<CharacterDatabaseConnection>;
template class TC_DATABASE_API DBUpdater<HotfixDatabaseConnection>;
