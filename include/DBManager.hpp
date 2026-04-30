#pragma once
#include <mysql/mysql.h>
#include <string>

class DBManager {
    static std::string generateSalt();
    static std::string hashPassword(const std::string& password, const std::string& salt);
public:
    static DBManager& getInstance();

    bool connect(const std::string& host, const std::string& user,
                 const std::string& passwd, const std::string& db,
                 unsigned int port = 3306);

    static MYSQL* getConnection();
    static void releaseConnection(MYSQL* conn);

    bool queryAccount(const std::string& account, const std::string& password,
                      std::string& username, std::string& avatarUrl);

    bool queryUsernameByAccount(const std::string& account,
                                std::string& username);
    bool updateUsername(const std::string& account,
                        const std::string& new_username);
    std::string queryUserAvatar(const std::string& account);
    bool updateUserAvatar(const std::string& account,
                          const std::string& avatarUrl);
    std::string getError() const;

private:
    DBManager() = default;
    ~DBManager() = default;
    DBManager(const DBManager&) = delete;
    DBManager& operator=(const DBManager&) = delete;

    mutable std::string error_;
};