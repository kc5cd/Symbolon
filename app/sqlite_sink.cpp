#include "sqlite_sink.h"

#include <sqlite3.h>

namespace {

// Wraps a CSV field in double quotes, doubling any embedded quote -- standard CSV escaping.
// FT8 message text is realistically restricted to uppercase letters/digits/space/+-/., but
// this makes the CSV correct regardless rather than assuming that holds forever.
std::string csv_quote(const std::string& s)
{
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') {
            out += "\"\"";
        } else {
            out += c;
        }
    }
    out += "\"";
    return out;
}

// True iff the file was empty before this open (i.e. just created) -- used to decide whether
// to write a CSV header line.
bool file_is_empty(FILE* f)
{
    std::fseek(f, 0, SEEK_END);
    long pos = std::ftell(f);
    std::fseek(f, 0, SEEK_END); // stay at the end -- this is an append-mode file
    return pos == 0;
}

} // namespace

SqliteSink::SqliteSink(const std::string& db_path)
{
    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        last_error_ = db_ != nullptr ? sqlite3_errmsg(db_) : "sqlite3_open failed";
        if (db_ != nullptr) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        return;
    }

    char* errmsg = nullptr;
    const char* schema_sql =
        "PRAGMA journal_mode=WAL;"
        "CREATE TABLE IF NOT EXISTS observations ("
        "  utc_us INTEGER NOT NULL,"
        "  call_de TEXT NOT NULL,"
        "  call_to TEXT NOT NULL,"
        "  band TEXT NOT NULL,"
        "  text TEXT NOT NULL,"
        "  freq_hz REAL NOT NULL,"
        "  snr_db REAL NOT NULL,"
        "  is_beacon_token INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS qso_log ("
        "  utc_us INTEGER NOT NULL,"
        "  my_call TEXT NOT NULL,"
        "  peer_call TEXT NOT NULL,"
        "  snr_i_sent INTEGER NOT NULL,"
        "  snr_i_got INTEGER NOT NULL,"
        "  asymmetry_db INTEGER NOT NULL"
        ");";
    if (sqlite3_exec(db_, schema_sql, nullptr, nullptr, &errmsg) != SQLITE_OK) {
        last_error_ = errmsg != nullptr ? errmsg : "schema creation failed";
        sqlite3_free(errmsg);
        sqlite3_close(db_);
        db_ = nullptr;
        return;
    }

    const char* insert_obs_sql =
        "INSERT INTO observations (utc_us, call_de, call_to, band, text, freq_hz, snr_db, is_beacon_token) "
        "VALUES (?,?,?,?,?,?,?,?);";
    const char* insert_qso_sql =
        "INSERT INTO qso_log (utc_us, my_call, peer_call, snr_i_sent, snr_i_got, asymmetry_db) "
        "VALUES (?,?,?,?,?,?);";
    if (sqlite3_prepare_v2(db_, insert_obs_sql, -1, &insert_observation_stmt_, nullptr) != SQLITE_OK ||
        sqlite3_prepare_v2(db_, insert_qso_sql, -1, &insert_qso_stmt_, nullptr) != SQLITE_OK) {
        last_error_ = sqlite3_errmsg(db_);
        sqlite3_finalize(insert_observation_stmt_);
        sqlite3_finalize(insert_qso_stmt_);
        sqlite3_close(db_);
        db_ = nullptr;
        insert_observation_stmt_ = nullptr;
        insert_qso_stmt_ = nullptr;
        return;
    }

    // Derive CSV sidecar paths from db_path's own stem (see sqlite_sink.h's own note) --
    // find the last '.' after the last path separator, if any, so a directory name with a
    // dot in it doesn't get mistaken for an extension.
    size_t slash = db_path.find_last_of("/\\");
    size_t dot = db_path.find_last_of('.');
    std::string stem = (dot != std::string::npos && (slash == std::string::npos || dot > slash))
        ? db_path.substr(0, dot)
        : db_path;

    observations_csv_ = std::fopen((stem + "_observations.csv").c_str(), "a");
    qso_csv_ = std::fopen((stem + "_qso_log.csv").c_str(), "a");
    if (observations_csv_ != nullptr && file_is_empty(observations_csv_)) {
        std::fprintf(observations_csv_, "utc_us,call_de,call_to,band,text,freq_hz,snr_db,is_beacon_token\n");
        std::fflush(observations_csv_);
    }
    if (qso_csv_ != nullptr && file_is_empty(qso_csv_)) {
        std::fprintf(qso_csv_, "utc_us,my_call,peer_call,snr_i_sent,snr_i_got,asymmetry_db\n");
        std::fflush(qso_csv_);
    }
}

SqliteSink::~SqliteSink()
{
    if (insert_observation_stmt_ != nullptr) {
        sqlite3_finalize(insert_observation_stmt_);
    }
    if (insert_qso_stmt_ != nullptr) {
        sqlite3_finalize(insert_qso_stmt_);
    }
    if (db_ != nullptr) {
        sqlite3_close(db_);
    }
    if (observations_csv_ != nullptr) {
        std::fclose(observations_csv_);
    }
    if (qso_csv_ != nullptr) {
        std::fclose(qso_csv_);
    }
}

mnemosyne_sink_t SqliteSink::as_sink()
{
    mnemosyne_sink_t sink;
    sink.on_observation = &SqliteSink::on_observation_trampoline;
    sink.on_qso_complete = &SqliteSink::on_qso_complete_trampoline;
    sink.user = this;
    return sink;
}

void SqliteSink::on_observation_trampoline(void* user, const mnemosyne_observation_t* obs)
{
    static_cast<SqliteSink*>(user)->on_observation(obs);
}

void SqliteSink::on_qso_complete_trampoline(void* user, const mnemosyne_qso_t* qso)
{
    static_cast<SqliteSink*>(user)->on_qso_complete(qso);
}

void SqliteSink::on_observation(const mnemosyne_observation_t* obs)
{
    if (db_ != nullptr && insert_observation_stmt_ != nullptr) {
        sqlite3_reset(insert_observation_stmt_);
        sqlite3_bind_int64(insert_observation_stmt_, 1, (sqlite3_int64)obs->utc_us);
        sqlite3_bind_text(insert_observation_stmt_, 2, obs->call_de, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_observation_stmt_, 3, obs->call_to, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_observation_stmt_, 4, obs->band, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_observation_stmt_, 5, obs->text, -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(insert_observation_stmt_, 6, (double)obs->freq_hz);
        sqlite3_bind_double(insert_observation_stmt_, 7, (double)obs->snr_db);
        sqlite3_bind_int(insert_observation_stmt_, 8, obs->is_beacon_token ? 1 : 0);
        if (sqlite3_step(insert_observation_stmt_) != SQLITE_DONE) {
            last_error_ = sqlite3_errmsg(db_);
        }
    }

    if (observations_csv_ != nullptr) {
        std::fprintf(observations_csv_, "%llu,%s,%s,%s,%s,%g,%g,%d\n",
            (unsigned long long)obs->utc_us,
            csv_quote(obs->call_de).c_str(), csv_quote(obs->call_to).c_str(),
            csv_quote(obs->band).c_str(), csv_quote(obs->text).c_str(),
            (double)obs->freq_hz, (double)obs->snr_db, obs->is_beacon_token ? 1 : 0);
        std::fflush(observations_csv_);
    }
}

void SqliteSink::on_qso_complete(const mnemosyne_qso_t* qso)
{
    if (db_ != nullptr && insert_qso_stmt_ != nullptr) {
        sqlite3_reset(insert_qso_stmt_);
        sqlite3_bind_int64(insert_qso_stmt_, 1, (sqlite3_int64)qso->utc_us);
        sqlite3_bind_text(insert_qso_stmt_, 2, qso->my_call, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_qso_stmt_, 3, qso->peer_call, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(insert_qso_stmt_, 4, qso->snr_i_sent);
        sqlite3_bind_int(insert_qso_stmt_, 5, qso->snr_i_got);
        sqlite3_bind_int(insert_qso_stmt_, 6, qso->asymmetry_db);
        if (sqlite3_step(insert_qso_stmt_) != SQLITE_DONE) {
            last_error_ = sqlite3_errmsg(db_);
        }
    }

    if (qso_csv_ != nullptr) {
        std::fprintf(qso_csv_, "%llu,%s,%s,%d,%d,%d\n",
            (unsigned long long)qso->utc_us,
            csv_quote(qso->my_call).c_str(), csv_quote(qso->peer_call).c_str(),
            qso->snr_i_sent, qso->snr_i_got, qso->asymmetry_db);
        std::fflush(qso_csv_);
    }
}
