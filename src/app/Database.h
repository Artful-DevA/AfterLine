// SPDX-License-Identifier: MIT
//
// Thin RAII wrapper around SQLite. The library database is only an index:
// meeting folders are the source of truth and the database can be rebuilt
// from them at any time.
#pragma once

#include <QString>
#include <QVariant>
#include <QVector>

struct sqlite3;
struct sqlite3_stmt;

namespace afterline {

class Database {
public:
    Database() = default;
    ~Database();
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    bool open(const QString &path, QString *error);
    void close();
    bool isOpen() const { return m_db != nullptr; }
    bool exec(const QString &sql, QString *error = nullptr);
    QString lastError() const;

    class Query {
    public:
        Query(Database &db, const QString &sql);
        ~Query();
        Query(const Query &) = delete;
        Query &operator=(const Query &) = delete;
        bool ok() const { return m_stmt != nullptr; }
        Query &bind(int index, const QVariant &value);  // 1-based
        bool step();                                    // true while a row is available
        bool run();                                     // executes, ignoring rows
        QVariant value(int column) const;
        QString text(int column) const;
        double real(int column) const;
        qint64 integer(int column) const;

    private:
        Database &m_db;
        sqlite3_stmt *m_stmt = nullptr;
        bool m_done = false;
    };

private:
    sqlite3 *m_db = nullptr;
};

}  // namespace afterline
