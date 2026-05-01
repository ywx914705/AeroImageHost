#ifndef HANDLERS_HPP
#define HANDLERS_HPP

#include <string>
#include <vector>
#include <utility>
#include <cpprest/json.h>

web::json::value handleRegister(const std::string& account, const std::string& password);
web::json::value handleLogin(const std::string& account, const std::string& password);
web::json::value handleUpload(int user_id, const std::string& filename, const std::vector<char>& file_data, const std::string& content_type);
web::json::value handleListFiles(int user_id, int offset, int limit, const std::string& search_keyword = "");
web::json::value handleDeleteFile(int user_id, const std::string& file_id);
web::json::value handleBatchDeleteFiles(int user_id, const std::vector<std::string>& file_ids);
std::pair<std::vector<char>, std::string> handleGetFile(const std::string& file_id, bool check_auth, int user_id, const std::string& user_agent);
web::json::value handleShare(const std::string& file_id);
web::json::value handleSetPublic(int user_id, const std::string& file_id);
web::json::value handleRequestUploadUrl(int user_id, const std::string& filename, const std::string& content_type, size_t file_size);
web::json::value handleConfirmUpload(int user_id, const std::string& file_id, const std::string& filename, const std::string& content_type, size_t file_size);
web::json::value handleStats();
web::json::value handleSendVerificationCode(const std::string& email);
web::json::value handleEmailRegister(const std::string& account, const std::string& password, const std::string& email, const std::string& code);

#endif
