#ifndef SYMBOLON_SQLITE_SINK_H
#define SYMBOLON_SQLITE_SINK_H

#include <cstdio>
#include <string>

#include "../core/mnemosyne.h"

struct sqlite3;
struct sqlite3_stmt;

// mnemosyne.c's sink implementation: core/ never writes files itself (see .claude/CLAUDE.md's
// no-OS-calls rule), so every observation/QSO record mnemosyne_observe()/mnemosyne_log_qso()
// builds is handed here instead -- one row into a SQLite database (WAL mode, two tables:
// "observations" and "qso_log") and a mirrored line into a CSV sidecar of the same name.
// The CSV exists purely so the data can be eyeballed without a SQLite client on hand -- the
// kickoff's own "SQLite + CSV" design for this file (see symbolon-kickoff-prompt.md's
// architecture tree).
class SqliteSink {
public:
    // db_path is opened (created if missing); two CSV files are opened alongside it, named
    // by replacing db_path's extension (or appending, if it has none) with
    // "_observations.csv" / "_qso_log.csv". Check is_open() before use -- a failure (bad
    // path, disk full, etc.) leaves every mnemosyne_sink_t callback a silent no-op rather
    // than crashing the caller's decode loop over a logging problem.
    explicit SqliteSink(const std::string& db_path);
    ~SqliteSink();

    SqliteSink(const SqliteSink&) = delete;
    SqliteSink& operator=(const SqliteSink&) = delete;

    bool is_open() const { return db_ != nullptr; }
    const std::string& last_error() const { return last_error_; }

    // Builds a mnemosyne_sink_t bound to this instance -- pass to mnemosyne_init(). Valid
    // only for this SqliteSink's lifetime.
    mnemosyne_sink_t as_sink();

private:
    void on_observation(const mnemosyne_observation_t* obs);
    void on_qso_complete(const mnemosyne_qso_t* qso);

    static void on_observation_trampoline(void* user, const mnemosyne_observation_t* obs);
    static void on_qso_complete_trampoline(void* user, const mnemosyne_qso_t* qso);

    sqlite3* db_ = nullptr;
    sqlite3_stmt* insert_observation_stmt_ = nullptr;
    sqlite3_stmt* insert_qso_stmt_ = nullptr;
    FILE* observations_csv_ = nullptr;
    FILE* qso_csv_ = nullptr;
    std::string last_error_;
};

#endif // SYMBOLON_SQLITE_SINK_H
