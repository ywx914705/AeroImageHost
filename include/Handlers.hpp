#ifndef HANDLERS_HPP
#define HANDLERS_HPP

#include <string>
#include <vector>
#include <utility>
#include <cpprest/json.h>

web::json::value handleRegister(const std::string& account, const std::string& password);
web::json::value handleLogin(const std::string& account, const std::string& password);
web::json::value handleUpload(int user_id, const std::string& filename, const std::vector<char>& file_data, const std::string& content_type);
web::json::value handleListFiles(int user_id, int offset, int limit);
web::json::value handleDeleteFile(int user_id, const std::string& file_id);
std::pair<std::vector<char>, std::string> handleGetFile(const std::string& file_id, bool check_auth, int user_id, const std::string& user_agent);
web::json::value handleShare(const std::string& file_id);
web::json::value handleSetPublic(int user_id, const std::string& file_id);

#endif
