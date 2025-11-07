#include <condition_variable>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

// jdbc headers
#include <jdbc/cppconn/prepared_statement.h>
#include <jdbc/cppconn/resultset.h>
#include <jdbc/cppconn/statement.h>
#include <jdbc/mysql_connection.h>
#include <jdbc/mysql_driver.h>
// httplib header
#include "/home/sancheetb/decs_project/cpp-httplib-master/httplib.h"

// server configuration
#define DATABASE "test2"
#define PASSWORD "Shata@1234"
#define SERVER_THREADS 12
#define POOL_SIZE 10

class LRUCache {
   private:
    int capacity;
    std::list<std::pair<std::string, std::string>> cacheList;
    std::unordered_map<std::string,
                       std::list<std::pair<std::string, std::string>>::iterator>
        cacheMap;
    std::mutex mtx;

   public:
    LRUCache(int cap) : capacity(cap) {}

    std::string get(std::string key) {
        std::lock_guard<std::mutex> lock(mtx);
        if (cacheMap.find(key) == cacheMap.end()) {
            return std::string();
        }
        cacheList.splice(cacheList.begin(), cacheList, cacheMap[key]);
        return cacheMap[key]->second;
    }

    void put(std::string key, std::string value) {
        std::lock_guard<std::mutex> lock(mtx);
        if (cacheMap.find(key) != cacheMap.end()) {
            cacheList.splice(cacheList.begin(), cacheList, cacheMap[key]);
            cacheMap[key]->second = value;
            return;
        }
        if (cacheList.size() == capacity) {
            cacheMap.erase(cacheList.back().first);
            cacheList.pop_back();
        }
        cacheList.emplace_front(key, value);
        cacheMap[key] = cacheList.begin();
    }

    bool remove(std::string key) {
        std::lock_guard<std::mutex> lock(mtx);
        if (cacheMap.find(key) == cacheMap.end()) {
            return false;
        }
        auto list_iterator = cacheMap[key];
        cacheList.erase(list_iterator);
        cacheMap.erase(key);
        return true;
    }

    std::string dumpToString() {
        std::lock_guard<std::mutex> lock(mtx);
        if (cacheList.empty()) {
            return "[ Cache is empty ]";
        }
        std::stringstream ss;
        ss << "[ Cache Contents (MRU -> LRU) ]\n";
        for (const auto& pair : cacheList) {
            ss << "  - (Key: \"" << pair.first << "\", Value: \"" << pair.second
               << "\")\n";
        }
        return ss.str();
    }
};

// initializing cache
LRUCache cache(5);

class ConnectionPool {
   private:
    size_t poolSize_;
    std::list<std::unique_ptr<sql::Connection>> pool_;
    std::mutex mtx_;
    std::condition_variable cv_;
    sql::mysql::MySQL_Driver* driver_;
    std::string url_;
    std::string user_;
    std::string pass_;
    std::string schema_;

   public:
    ConnectionPool(size_t poolSize, const std::string& url,
                   const std::string& user, const std::string& pass,
                   const std::string& schema)
        : poolSize_(poolSize),
          url_(url),
          user_(user),
          pass_(pass),
          schema_(schema) {
        try {
            driver_ = sql::mysql::get_mysql_driver_instance();
            for (size_t i = 0; i < poolSize_; ++i) {
                std::unique_ptr<sql::Connection> con(
                    driver_->connect(url_, user_, pass_));
                con->setSchema(schema_);
                pool_.push_back(std::move(con));
            }
            std::cout << "Connection pool initialized with " << poolSize_
                      << " connections." << std::endl;
        } catch (sql::SQLException& e) {
            std::cerr << "FATAL: Failed to initialize connection pool: "
                      << e.what() << std::endl;
            throw;  // Re-throw to stop the application
        }
    }

    ~ConnectionPool() {
        std::cout << "Connection pool shutting down." << std::endl;
    }

    std::unique_ptr<sql::Connection> getConnection() {
        std::unique_lock<std::mutex> lock(mtx_);

        // Wait until the pool is not empty
        cv_.wait(lock, [this] { return !pool_.empty(); });

        // Get the connection from the front of the list
        std::unique_ptr<sql::Connection> conn = std::move(pool_.front());
        pool_.pop_front();

        return conn;
    }

    void returnConnection(std::unique_ptr<sql::Connection> conn) {
        if (!conn) {
            return;  // Don't return nullptrs
        }

        {
            std::lock_guard<std::mutex> lock(mtx_);
            // Add the connection back to the list
            pool_.push_back(std::move(conn));
        }  // Mutex is released

        // Notify one waiting thread that a connection is available
        cv_.notify_one();
    }
};

int main() {
    httplib::Server svr;

    // initializing connection pool
    ConnectionPool dbPool(POOL_SIZE, "tcp://127.0.0.1:3306", "root", PASSWORD,
                          DATABASE);

    svr.Post("/test", [&](const httplib::Request& req, httplib::Response& res) {
        res.set_content(cache.dumpToString(), "text/plain");
        std::cout << "Replying to test\n";
    });

    svr.Post("/create", [&](const httplib::Request& req,
                            httplib::Response& res) {
        auto key_it = req.params.find("key");
        auto val_it = req.params.find("val");

        if (key_it != req.params.end() && val_it != req.params.end()) {
            std::string key = key_it->second;
            std::string val = val_it->second;

            std::unique_ptr<sql::Connection> con;
            try {
                con = dbPool.getConnection();
            } catch (const std::exception& e) {
                std::cerr << "Failed to get connection from pool: " << e.what()
                          << std::endl;
                res.status = 503;  // Service Unavailable
                res.set_content("Server busy, please try again later.",
                                "text/plain");
                return;
            }

            try {
                std::unique_ptr<sql::PreparedStatement> pstmt(
                    con->prepareStatement(
                        "INSERT INTO kv_store(`key`, `value`) VALUES (?, ?)"));
                pstmt->setString(1, key);
                pstmt->setString(2, val);

                int rowsAffected = pstmt->executeUpdate();

                if (rowsAffected > 0) {
                    cache.put(key, val);
                    res.set_content("Stored successfully", "text/plain");
                    std::cout << "Insert successful" << std::endl;
                } else {
                    res.set_content("Insert failed!", "text/plain");
                    std::cout << "Insert failed!" << std::endl;
                }
            } catch (sql::SQLException& e) {
                if (e.getErrorCode() == 1062) {  // Duplicate key
                    try {
                        std::unique_ptr<sql::PreparedStatement> pstmt(
                            con->prepareStatement(
                                "SELECT value FROM kv_store WHERE `key` = ?"));
                        pstmt->setString(1, key);
                        std::unique_ptr<sql::ResultSet> result(
                            pstmt->executeQuery());
                        if (result->next()) {
                            cache.put(key, result->getString("value"));
                        }
                    } catch (sql::SQLException& e2) {
                        std::cerr << "SQL Error (in catch): " << e2.what()
                                  << std::endl;
                    }
                    res.set_content("Insert failed: key already exists!",
                                    "text/plain");
                    std::cout << "Insert failed: key already exists!"
                              << std::endl;
                } else {
                    std::cerr << "SQL Error: " << e.what() << std::endl;
                    res.set_content("An internal server error occurred.",
                                    "text/plain");
                    res.status = 500;
                }
            }

            dbPool.returnConnection(std::move(con));

        } else {
            res.status = 400;
            res.set_content("Create message not received properly",
                            "text/plain");
            std::cout << "Invalid create request" << std::endl;
        }
    });

    svr.Get("/read", [&](const httplib::Request& req, httplib::Response& res) {
        auto key_it = req.params.find("key");
        if (key_it != req.params.end()) {
            std::string key = key_it->second;
            std::string val = cache.get(key);

            if (!val.empty()) {
                res.set_content(val, "text/plain");
                std::cout << "returned value (from cache) " << val
                          << " for key " << key << std::endl;
            } else {
                std::unique_ptr<sql::Connection> con;
                try {
                    con = dbPool.getConnection();
                } catch (const std::exception& e) {
                    std::cerr
                        << "Failed to get connection from pool: " << e.what()
                        << std::endl;
                    res.status = 503;
                    res.set_content("Server busy, please try again later.",
                                    "text/plain");
                    return;
                }

                try {
                    std::unique_ptr<sql::PreparedStatement> pstmt(
                        con->prepareStatement(
                            "SELECT value FROM kv_store WHERE `key` = ?"));
                    pstmt->setString(1, key);
                    std::unique_ptr<sql::ResultSet> result(
                        pstmt->executeQuery());

                    if (result->next()) {
                        std::string value = result->getString("value");
                        res.set_content(value, "text/plain");
                        cache.put(key, value);  // Add to cache
                        std::cout << "returned value (from DB) " << value
                                  << " for key " << key << std::endl;
                    } else {
                        res.set_content("-1", "text/plain");
                        std::cout << "No entry found for key:" << key
                                  << std::endl;
                    }
                } catch (sql::SQLException& e) {
                    std::cerr << "SQL Error: " << e.what() << std::endl;
                    res.set_content("An internal server error occurred.",
                                    "text/plain");
                    res.status = 500;
                }

                dbPool.returnConnection(std::move(con));
            }
        } else {
            res.status = 400;
            res.set_content("Sent request is improper\n", "text/plain");
            std::cout << "Invalid read request" << std::endl;
        }
    });

    svr.Delete("/delete", [&](const httplib::Request& req,
                              httplib::Response& res) {
        auto key_it = req.params.find("key");
        if (key_it != req.params.end()) {
            std::string key = key_it->second;

            std::unique_ptr<sql::Connection> con;
            try {
                con = dbPool.getConnection();
            } catch (const std::exception& e) {
                std::cerr << "Failed to get connection from pool: " << e.what()
                          << std::endl;
                res.status = 503;
                res.set_content("Server busy, please try again later.",
                                "text/plain");
                return;
            }

            try {
                std::unique_ptr<sql::PreparedStatement> pstmt(
                    con->prepareStatement(
                        "DELETE FROM kv_store WHERE `key` = ?"));
                pstmt->setString(1, key);
                int rowsAffected = pstmt->executeUpdate();

                cache.remove(key);  // Remove from cache regardless

                if (rowsAffected > 0) {
                    std::string message = "Key " + key + " deleted";
                    res.set_content(message, "text/plain");
                    std::cout << "Delete successful! (" << rowsAffected
                              << " row(s) removed)" << std::endl;
                } else {
                    std::string message = "Key " + key + " not found";
                    res.set_content(message, "text/plain");
                    std::cout << "No entry found for key: " << key << std::endl;
                }
            } catch (sql::SQLException& e) {
                std::cerr << "SQL Error: " << e.what() << std::endl;
                res.set_content("An internal server error occurred.",
                                "text/plain");
                res.status = 500;
            }

            dbPool.returnConnection(std::move(con));

        } else {
            res.status = 400;
            res.set_content("Sent request is improper\n", "text/plain");
            std::cout << "Invalid delete request" << std::endl;
        }
    });

    svr.Put("/update", [&](const httplib::Request& req,
                           httplib::Response& res) {
        auto key_it = req.params.find("key");
        auto val_it = req.params.find("val");
        if (key_it != req.params.end() && val_it != req.params.end()) {
            std::string key = key_it->second;
            std::string val = val_it->second;

            std::unique_ptr<sql::Connection> con;
            try {
                con = dbPool.getConnection();
            } catch (const std::exception& e) {
                std::cerr << "Failed to get connection from pool: " << e.what()
                          << std::endl;
                res.status = 503;
                res.set_content("Server busy, please try again later.",
                                "text/plain");
                return;
            }

            try {
                std::unique_ptr<sql::PreparedStatement> pstmt(
                    con->prepareStatement(
                        "UPDATE kv_store SET `value` = ? WHERE `key` = ?"));
                pstmt->setString(1, val);
                pstmt->setString(2, key);
                int rowsAffected = pstmt->executeUpdate();

                if (rowsAffected > 0) {
                    cache.put(key, val);  // Update cache
                    std::string message = "Update successful!";
                    res.set_content(message, "text/plain");
                    std::cout << message << std::endl;
                } else {
                    res.set_content("-1", "text/plain");
                    std::cout << "No entry found for the specified key."
                              << std::endl;
                }
            } catch (sql::SQLException& e) {
                std::cerr << "SQL Error: " << e.what() << std::endl;
                res.set_content("An internal server error occurred.",
                                "text/plain");
                res.status = 500;
            }

            dbPool.returnConnection(std::move(con));

        } else {
            res.status = 400;
            res.set_content("Sent request is improper\n", "text/plain");
            std::cout << "Invalid update request" << std::endl;
        }
    });

    svr.new_task_queue = [] { return new httplib::ThreadPool(SERVER_THREADS); };
    std::cout << "Server running on http://localhost:8080\n";
    svr.listen("0.0.0.0", 8080);
}

