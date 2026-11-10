// SPDX-License-Identifier: MIT
#include "app/Database.h"

#include <sqlite3.h>

namespace afterline {

Database::~Database() { close(); }

bool Database::open(const QString &path, QString *error)
{
    close();
    if (sqlite3_open_v2(path.toUtf8().constData(), &m_db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        if (error)
            *error = lastError();
        close();
        return false;
    }
    sqlite3_busy_timeout(m_db, 3000);
    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA synchronous=NORMAL");
    exec("PRAGMA foreign_keys=ON");
    return true;
}

void Database::close()
{
    if (m_db) {
        sqlite3_close_v2(m_db);
        m_db = nullptr;
    }
}

QString Database::lastError() const
{
    return m_db ? QString::fromUtf8(sqlite3_errmsg(m_db)) : QStringLiteral("database not open");
}

bool Database::exec(const QString &sql, QString *error)
{
    if (!m_db)
        return false;
    char *msg = nullptr;
    if (sqlite3_exec(m_db, sql.toUtf8().constData(), nullptr, nullptr, &msg) != SQLITE_OK) {
        if (error)
            *error = QString::fromUtf8(msg ? msg : "error");
        sqlite3_free(msg);
        return false;
    }
    return true;
}

Database::Query::Query(Database &db, const QString &sql) : m_db(db)
{
    if (db.m_db)
        sqlite3_prepare_v2(db.m_db, sql.toUtf8().constData(), -1, &m_stmt, nullptr);
}

Database::Query::~Query()
{
    if (m_stmt)
        sqlite3_finalize(m_stmt);
}

Database::Query &Database::Query::bind(int index, const QVariant &v)
{
    if (!m_stmt)
        return *this;
    if (v.isNull()) {
        sqlite3_bind_null(m_stmt, index);
        return *this;
    }
    switch (v.typeId()) {
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::Bool:
    case QMetaType::UInt:
        sqlite3_bind_int64(m_stmt, index, v.toLongLong());
        break;
    case QMetaType::Double:
        sqlite3_bind_double(m_stmt, index, v.toDouble());
        break;
    default: {
        const QByteArray s = v.toString().toUtf8();
        sqlite3_bind_text(m_stmt, index, s.constData(), int(s.size()), SQLITE_TRANSIENT);
    }
    }
    return *this;
}

bool Database::Query::step()
{
    if (!m_stmt || m_done)
        return false;
    const int rc = sqlite3_step(m_stmt);
    if (rc == SQLITE_ROW)
        return true;
    m_done = true;
    return false;
}

bool Database::Query::run()
{
    if (!m_stmt)
        return false;
    int rc;
    while ((rc = sqlite3_step(m_stmt)) == SQLITE_ROW) {
    }
    m_done = true;
    return rc == SQLITE_DONE;
}

QVariant Database::Query::value(int column) const
{
    switch (sqlite3_column_type(m_stmt, column)) {
    case SQLITE_INTEGER: return QVariant(qint64(sqlite3_column_int64(m_stmt, column)));
    case SQLITE_FLOAT: return QVariant(sqlite3_column_double(m_stmt, column));
    case SQLITE_NULL: return QVariant();
    default: return QVariant(text(column));
    }
}

QString Database::Query::text(int column) const
{
    const auto *p = reinterpret_cast<const char *>(sqlite3_column_text(m_stmt, column));
    return p ? QString::fromUtf8(p) : QString();
}

double Database::Query::real(int column) const { return sqlite3_column_double(m_stmt, column); }
qint64 Database::Query::integer(int column) const { return sqlite3_column_int64(m_stmt, column); }

}  // namespace afterline
