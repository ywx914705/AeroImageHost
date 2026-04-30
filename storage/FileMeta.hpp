#pragma once
#include <string>
#include <vector>
#include <ctime>
#include "Utils.hpp"

struct FileMeta {
    std::string file_id;
    int user_id = 0;
    std::string filename;
    long long size = 0;
    std::string mime_type;
    std::string md5;
    int width = 0;
    int height = 0;
    time_t upload_time = 0;
    bool is_public = false;
    long long view_count = 0;
    std::string allow_domains;
};

class FileMetaDAO {
public:
    static FileMetaDAO& instance();
    bool save(const FileMeta& meta);
    FileMeta get(const std::string& file_id);
    std::vector<FileMeta> listByUser(int user_id, int offset, int limit);
    std::vector<FileMeta> listByUserWithSearch(int user_id, const std::string& keyword, int offset, int limit);
    int countByUser(int user_id);
    int countByUserWithSearch(int user_id, const std::string& keyword);
    bool del(const std::string& file_id);
    bool updatePublic(const std::string& file_id, bool is_public);
    bool updateAllowDomains(const std::string& file_id, const std::string& domains);
    bool incrementViewCount(const std::string& file_id);
    int cleanupOrphanedFiles();
};