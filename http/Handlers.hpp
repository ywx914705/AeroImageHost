#pragma once

#include <string>
#include <vector>
#include <utility>
#include "HandlerResult.hpp"

HandlerResult handleRegister(const std::string& account, const std::string& password);
HandlerResult handleLogin(const std::string& account, const std::string& password, const std::string& client_ip);
HandlerResult handleUpload(int user_id, const std::string& filename, const std::vector<unsigned char>& file_data, const std::string& content_type);
HandlerResult handleRequestUploadUrl(int user_id, const std::string& filename, const std::string& content_type, size_t file_size);
HandlerResult handleConfirmUpload(int user_id, const std::string& file_id, const std::string& filename, const std::string& content_type, size_t file_size);
HandlerResult handleMultipartInit(int user_id, const std::string& filename, const std::string& content_type, size_t file_size);
HandlerResult handleMultipartUploadChunk(int user_id, const std::string& upload_id, int part_number, const std::vector<unsigned char>& data);
HandlerResult handleMultipartComplete(int user_id, const std::string& upload_id, const std::string& filename, const std::string& content_type, size_t file_size, int total_chunks);
HandlerResult handleMultipartCleanup(int user_id, const std::string& upload_id);
HandlerResult handleListFiles(int user_id, int offset, int limit, const std::string& search_keyword = "", const std::string& type = "", const std::string& sort = "time", const std::string& order = "desc");
HandlerResult handleDeleteFile(int user_id, const std::string& file_id);
HandlerResult handleBatchDeleteFiles(int user_id, const std::vector<std::string>& file_ids);
HandlerResult handleShare(const std::string& file_id);
HandlerResult handleSetPublic(int user_id, const std::string& file_id);
HandlerResult handleSetPublic(int user_id, const std::string& file_id, bool is_public);
HandlerResult handleGetPresignUrl(int user_id, const std::string& file_id);
void invalidateUserFilesCache(int user_id);
HandlerResult handleStats();
void cleanupOrphanChunks();
HandlerResult handleSendVerificationCode(const std::string& email, const std::string& client_ip);
HandlerResult handleEmailRegister(const std::string& account, const std::string& password, const std::string& email, const std::string& code);

// 水印相关
HandlerResult handleAddWatermark(int user_id, const std::string& file_id, 
                                  const std::string& text, const std::string& position, int opacity);
HandlerResult handleRemoveWatermark(int user_id, const std::string& file_id);
HandlerResult handleGetWatermarkConfig(int user_id, const std::string& file_id);
