#include "dbic++.h"
#include <libpq-fe.h>
#include <libpq/libpq-fs.h>
#include <unistd.h>

#define DRIVER_NAME     "postgresql"
#define DRIVER_VERSION  "1.2"

#define PG2PARAM(res, r, c) PARAM_BINARY((unsigned char*)PQgetvalue(res, r, c), PQgetlength(res, r, c))

namespace dbi {

    char errormsg[8192];

    const char *typemap[] = {
        "%d", "int", "%u", "int", "%lu", "int", "%ld", "int",
        "%f", "float", "%lf", "float",
        "%s", "text"
    };

    inline int PQNTUPLES(PGresult *r) {
        int n = PQntuples(r);
        return n > 0 ? n : atoi(PQcmdTuples(r));
    }


    void PQ_PREPROCESS_QUERY(string &query) {
        int i, n = 0;
        char repl[128];
        string var;
        RE re("(?<!')(%(?:[dsfu]|l[dfu])|\\?)(?!')");

        while (re.PartialMatch(query, &var)) {
            for (i = 0; i < (int)(sizeof(typemap)/sizeof(char *)); i += 2) {
                if (var == typemap[i]) {
                    sprintf(repl, "$%d::%s", ++n, typemap[i+1]);
                    re.Replace(repl, &query);
                    i = -1;
                    break;
                }
            }
            if (i != -1) {
                sprintf(repl, "$%d", ++n);
                re.Replace(repl, &query);
            }
        }
    }

    void PQ_PROCESS_BIND(const char ***param_v, int **param_l, vector<Param> &bind) {
        *param_v = new const char*[bind.size()];
        *param_l = new int[bind.size()];

        for (unsigned int i = 0; i < bind.size(); i++) {
            bool isnull = bind[i].isnull;
            (*param_v)[i] = isnull ? 0 : bind[i].value.data();
            (*param_l)[i] = isnull ? 0 : bind[i].value.length();
        }
    }

    // ----------------------------------------------------------------------
    // Statement & Handle
    // ----------------------------------------------------------------------

    class PgHandle;
    class PgStatement : public AbstractStatement {
        protected:
        PgHandle *handle;
        private:
        string _sql;
        string _uuid;
        PGresult *_result;
        vector<string> _rsfields;
        vector<int> _rsfield_types;
        unsigned int _rowno, _rows, _cols;
        ResultRow _rsrow;
        ResultRowHash _rsrowhash;
        bool _async;

        void init();
        PGresult* prepare();
        void boom(const char *);

        public:
        PgStatement();
        ~PgStatement();
        PgStatement(string query,  PgHandle *h, bool async = false);
        void cleanup();
        string command();
        void checkReady(string);
        unsigned int rows();
        unsigned int columns();
        unsigned int execute();
        unsigned int execute(vector<Param>&);
        bool consumeResult();
        void prepareResult();
        unsigned long lastInsertID();
        ResultRow& fetchRow();
        ResultRowHash& fetchRowHash();
        vector<string> fields();
        bool finish();
        unsigned char* fetchValue(unsigned int r, unsigned int c, unsigned long *l = 0);
        unsigned int currentRow();
        void advanceRow();
    };

    class PgHandle : public AbstractHandle {
        protected:
        PGconn *conn;
        int tr_nesting;
        void boom(const char *);
        public:
        PgHandle();
        PgHandle(string user, string pass, string dbname, string h, string p);
        ~PgHandle();
        void cleanup();
        unsigned int execute(string sql);
        unsigned int execute(string sql, vector<Param> &bind);
        int socket();
        PgStatement* aexecute(string sql, vector<Param> &bind);
        void initAsync();
        bool isBusy();
        bool cancel();
        PgStatement* prepare(string sql);
        bool begin();
        bool commit();
        bool rollback();
        bool begin(string name);
        bool commit(string name);
        bool rollback(string name);
        void* call(string name, void* args);
        bool close();
        void reconnect(bool barf = false);
        int checkResult(PGresult*, string, bool barf = false);
        friend class PgStatement;
    };


    // ----------------------------------------------------------------------
    // DEFINITION
    // ----------------------------------------------------------------------

    void PgStatement::init() {
        _result = 0;
        _rowno  = 0;
        _rows   = 0;
        _cols   = 0;
        _uuid   = generateCompactUUID();
        _async  = false;
    }

    PGresult* PgStatement::prepare() {
        int tries, done;
        PGresult *response, *result;
        string query = _sql;
        PQ_PREPROCESS_QUERY(query);

        if (_async) {
            done = tries = 0;
            while (!done && tries < 2) {
                tries++;
                done = PQsendPrepare(handle->conn, _uuid.c_str(), query.c_str(), 0, 0);
                if (!done) { handle->reconnect(true); continue; }

                while (PQisBusy(handle->conn)) PQconsumeInput(handle->conn);
                result = PQgetResult(handle->conn);
                while ((response = PQgetResult(handle->conn))) PQclear(response);

                done = handle->checkResult(result, _sql);
                PQclear(result);
                if (!done) continue;

                done = PQsendDescribePrepared(handle->conn, _uuid.c_str());
                if (!done) continue;

                while (PQisBusy(handle->conn)) PQconsumeInput(handle->conn);
                result = PQgetResult(handle->conn);
                while ((response = PQgetResult(handle->conn))) PQclear(response);
                done = handle->checkResult(result, _sql);
            }

            if (!done) boom(PQerrorMessage(handle->conn));
        }
        else {
            done = tries = 0;
            while (!done && tries < 2) {
                tries++;
                result = PQprepare(handle->conn, _uuid.c_str(), query.c_str(), 0, 0);
                if (!result) boom("Unable to allocate statement");
                done = handle->checkResult(result, _sql);
                PQclear(result);

                if (!done) continue;

                result = PQdescribePrepared(handle->conn, _uuid.c_str());
                done = handle->checkResult(result, _sql);
            }

            if (!done) boom(PQerrorMessage(handle->conn));
        }

        return result;
    }

    PgStatement::~PgStatement() { cleanup(); }
    PgStatement::PgStatement(string query,  PgHandle *h, bool async) {
        init();
        _sql   = query;
        _async = async;
        handle = h;

        // prepare is always sync - only execute is really async.
        PGresult *result = prepare();

        _cols  = (unsigned int)PQnfields(result);

        for (int i = 0; i < (int)_cols; i++)
            _rsfields.push_back(PQfname(result, i));

        for (int i = 0; i < (int)_cols; i++)
            _rsfield_types.push_back(PQfformat(result, i));

        _rsrow.reserve(_cols);
        PQclear(result);
    }

    void PgStatement::cleanup() {
        finish();
    }

    string PgStatement::command() {
        return _sql;
    }

    void PgStatement::checkReady(string m) {
        if (!_result)
            throw RuntimeError((m + " cannot be called yet. call execute() first").c_str());
    }

    unsigned int PgStatement::rows() {
        checkReady("rows()");
        return _rows;
    }

    unsigned int PgStatement::execute() {
        int done, tries;
        finish();

        if (_async) {
            done = tries = 0;
            while (!done && tries < 2) {
                tries++;
                done = PQsendQueryPrepared(handle->conn, _uuid.c_str(), 0, 0, 0, 0, 0);
                if (!done) handle->reconnect(true);
            }
            if (!done) boom(PQerrorMessage(handle->conn));
        }
        else {
            done = tries = 0;
            while (!done && tries < 2) {
                tries++;
                _result = PQexecPrepared(handle->conn, _uuid.c_str(), 0, 0, 0, 0, 0);
                done = handle->checkResult(_result, _sql);
                if (!done) prepare();
            }
            _rows = (unsigned int)PQNTUPLES(_result);
        }

        // will return 0 for async queries.
        return _rows;
    }

    unsigned int PgStatement::execute(vector<Param> &bind) {
        int *param_l, done, tries;
        const char **param_v;

        finish();
        PQ_PROCESS_BIND(&param_v, &param_l, bind);

        if (_async) {
            done = tries = 0;
            while (!done && tries < 2) {
                tries++;
                done = PQsendQueryPrepared(handle->conn, _uuid.c_str(), bind.size(),
                                       (const char* const *)param_v, (const int*)param_l, 0, 0);
            }
            delete []param_v;
            delete []param_l;

            if (!done) boom(PQerrorMessage(handle->conn));
        }
        else {
            done = tries = 0;
            try {
                while (!done && tries < 2) {
                    tries++;
                    _result = PQexecPrepared(handle->conn, _uuid.c_str(), bind.size(),
                                       (const char* const *)param_v, (const int*)param_l, 0, 0);
                    done = handle->checkResult(_result, _sql);
                    if (!done) prepare();
                }
            } catch (exception &e) {
                delete []param_v;
                delete []param_l;
                throw e;
            }
            _rows = (unsigned int)PQNTUPLES(_result);
        }

        // will return 0 for async queries.
        return _rows;
    }

    bool PgStatement::consumeResult() {
        PQconsumeInput(handle->conn);
        return (PQisBusy(handle->conn) ? true : false);
    }

    void PgStatement::prepareResult() {
        PGresult *response;
        _result = PQgetResult(handle->conn);
        _rows   = (unsigned int)PQNTUPLES(_result);
        while ((response = PQgetResult(handle->conn))) PQclear(response);
        handle->checkResult(_result, _sql, true);
    }

    unsigned long PgStatement::lastInsertID() {
        checkReady("lastInsertID()");
        ResultRow r = fetchRow();
        return r.size() > 0 ? atol(r[0].value.c_str()) : 0;
    }

    ResultRow& PgStatement::fetchRow() {
        checkReady("fetchRow()");

        _rsrow.clear();

        if (_rowno < _rows) {
            _rowno++;
            for (unsigned int i = 0; i < _cols; i++)
                _rsrow.push_back(PQgetisnull(_result, _rowno-1, i) ?
                    PARAM(null()) : PG2PARAM(_result, _rowno-1, i));
        }

        return _rsrow;
    }

    ResultRowHash& PgStatement::fetchRowHash() {
        checkReady("fetchRowHash()");

        _rsrowhash.clear();

        if (_rowno < _rows) {
            _rowno++;
            for (unsigned int i = 0; i < _cols; i++)
                _rsrowhash[_rsfields[i]] = PQgetisnull(_result, _rowno-1, i) ?
                    PARAM(null()) : PG2PARAM(_result, _rowno-1, i);
        }

        return _rsrowhash;
    }

    vector<string> PgStatement::fields() {
        return _rsfields;
    }

    unsigned int PgStatement::columns() {
        return _cols;
    }

    bool PgStatement::finish() {
        if (_result)
            PQclear(_result);

        _rowno  = 0;
        _rows   = 0;
        _result = 0;

        return true;
    }

    unsigned char* PgStatement::fetchValue(unsigned int r, unsigned int c, unsigned long *l) {
        checkReady("fetchValue()");
        if (l) *l = PQgetlength(_result, r, c);
        return PQgetisnull(_result, r, c) ? 0 : (unsigned char*)PQgetvalue(_result, r, c);
    }

    unsigned int PgStatement::currentRow() {
        return _rowno;
    }

    void PgStatement::advanceRow() {
        _rowno = _rowno <= _rows ? _rowno + 1 : _rowno;
    }

    void PgStatement::boom(const char* m) {
        if (PQstatus(handle->conn) == CONNECTION_BAD)
            throw ConnectionError(m);
        else
            throw RuntimeError(m);
    }

    PgHandle::PgHandle() { tr_nesting = 0; }
    PgHandle::PgHandle(string user, string pass, string dbname, string h, string p) {
        tr_nesting = 0;
        string conninfo;

        string host = h;
        string port = p;

        conninfo += "host='" + host + "' ";
        conninfo += "port='" + port + "' ";
        conninfo += "user='" + user + "' ";
        conninfo += "password='" + pass + "' ";
        conninfo += "dbname='" + dbname + "' ";

        conn = PQconnectdb(conninfo.c_str());

        if (!conn)
            throw ConnectionError("Unable to allocate db handle");
        else if (PQstatus(conn) == CONNECTION_BAD)
            throw ConnectionError(PQerrorMessage(conn));
    }

    PgHandle::~PgHandle() {
        cleanup();
    }

    void PgHandle::cleanup() {
        // This gets called only on dlclose, so the wrapper dbi::Handle
        // closes connections and frees memory.
        if (conn)
            PQfinish(conn);
        conn = 0;
    }

    unsigned int PgHandle::execute(string sql) {
        unsigned int rows;
        int done, tries;
        PGresult *result;
        string query  = sql;

        done = tries = 0;
        PQ_PREPROCESS_QUERY(query);
        while (!done && tries < 2) {
            tries++;
            result = PQexec(conn, query.c_str());
            done = checkResult(result, sql);
        }

        if (!done) boom(PQerrorMessage(conn));

        rows = (unsigned int)PQNTUPLES(result);
        PQclear(result);
        return rows;
    }

    unsigned int PgHandle::execute(string sql, vector<Param> &bind) {
        int *param_l;
        const char **param_v;
        unsigned int rows;
        int done, tries;
        PGresult *result;
        string query = sql;

        done = tries = 0;
        PQ_PREPROCESS_QUERY(query);
        try {
            while (!done && tries < 2) {
                tries++;
                PQ_PROCESS_BIND(&param_v, &param_l, bind);
                result = PQexecParams(conn, query.c_str(), bind.size(),
                                        0, (const char* const *)param_v, param_l, 0, 0);
                done = checkResult(result, sql);
            }
        }
        catch (exception &e) {
            delete []param_v;
            delete []param_l;
            throw e;
        }

        rows = (unsigned int)PQNTUPLES(result);
        PQclear(result);
        return rows;
    }

    int PgHandle::socket() {
        return PQsocket(conn);
    }

    PgStatement* PgHandle::aexecute(string sql, vector<Param> &bind) {
        PgStatement *st = new PgStatement(sql, this, true);
        st->execute(bind);
        return st;
    }

    void PgHandle::initAsync() {
        if(!PQisnonblocking(conn)) PQsetnonblocking(conn, 1);
    }

    bool PgHandle::isBusy() {
        return PQisBusy(conn) ? true : false;
    }

    bool PgHandle::cancel() {
        int rc;
        char error[512];

        PGcancel *cancel = PQgetCancel(conn);
        if (!cancel)
            boom("Invalid handle or nothing to cancel");

        rc = PQcancel(cancel, error, 512);
        PQfreeCancel(cancel);

        if (rc != 1)
            boom(error);
        else
            return true;
    }

    PgStatement* PgHandle::prepare(string sql) {
        return new PgStatement(sql, this);
    }

    bool PgHandle::begin() {
        execute("BEGIN WORK");
        tr_nesting++;
        return true;
    };

    bool PgHandle::commit() {
        execute("COMMIT");
        tr_nesting--;
        return true;
    };

    bool PgHandle::rollback() {
        execute("ROLLBACK");
        tr_nesting--;
        return true;
    };

    bool PgHandle::begin(string name) {
        if (tr_nesting == 0)
            begin();
        execute("SAVEPOINT " + name);
        tr_nesting++;
        return true;
    };

    bool PgHandle::commit(string name) {
        execute("RELEASE SAVEPOINT " + name);
        tr_nesting--;
        if (tr_nesting == 1)
            commit();
        return true;
    };

    bool PgHandle::rollback(string name) {
        execute("ROLLBACK TO SAVEPOINT " + name);
        tr_nesting--;
        if (tr_nesting == 1)
            rollback();
        return true;
    };

    void* PgHandle::call(string name, void* args) {
        return NULL;
    }

    bool PgHandle::close() {
        if (conn) PQfinish(conn);
        conn = 0;
        return true;
    }

    void PgHandle::reconnect(bool barf) {

        if (barf && PQstatus(conn) != CONNECTION_BAD)
            throw ConnectionError(PQerrorMessage(conn));

        PQreset(conn);
        if (PQstatus(conn) == CONNECTION_BAD) {
            if (tr_nesting == 0) {
                char conninfo[8192];
                snprintf(conninfo, 8192, "dbname=%s user=%s password=%s host=%s port=%s",
                    PQdb(conn), PQuser(conn), PQpass(conn), PQhost(conn), PQport(conn));
                PQfinish(conn);
                conn = PQconnectdb(conninfo);
                if (PQstatus(conn) == CONNECTION_BAD) throw ConnectionError(PQerrorMessage(conn));
                fprintf(stderr, "[WARNING] Socket changed during auto reconnect to database %s on host %s\n",
                    PQdb(conn), PQhost(conn));
            }
            else throw ConnectionError(PQerrorMessage(conn));
        }
        else {
            fprintf(stderr, "[NOTICE] Auto reconnected on same socket to database %s on host %s\n",
                PQdb(conn), PQhost(conn));
        }
    }


    int PgHandle::checkResult(PGresult *result, string sql, bool barf) {
        switch(PQresultStatus(result)) {
            case PGRES_TUPLES_OK:
            case PGRES_COPY_OUT:
            case PGRES_COPY_IN:
            case PGRES_EMPTY_QUERY:
            case PGRES_COMMAND_OK:
                return 1;
            case PGRES_BAD_RESPONSE:
            case PGRES_FATAL_ERROR:
            case PGRES_NONFATAL_ERROR:
                if (PQstatus(conn) == CONNECTION_BAD && !barf) {
                    reconnect();
                    PQclear(result);
                    return 0;
                }
                snprintf(errormsg, 8192, "In SQL: %s\n\n %s", sql.c_str(), PQresultErrorMessage(result));
                PQclear(result);
                throw RuntimeError((const char*)errormsg);
                break;
            default:
                if (PQstatus(conn) == CONNECTION_BAD && !barf) {
                    reconnect();
                    PQclear(result);
                    return 0;
                }
                snprintf(errormsg, 8192, "In SQL: %s\n\n Unknown error, check logs.", sql.c_str());
                PQclear(result);
                throw RuntimeError(errormsg);
                break;
        }
        return 1;
    }

    void PgHandle::boom(const char* m) {
        if (PQstatus(conn) == CONNECTION_BAD)
            throw ConnectionError(m);
        else
            throw RuntimeError(m);
    }
}

using namespace std;
using namespace dbi;

extern "C" {
    PgHandle* dbdConnect(string user, string pass, string dbname, string host, string port) {
        if (host == "")
            host = "127.0.0.1";
        if (port == "0" || port == "")
            port = "5432";

        return new PgHandle(user, pass, dbname, host, port);
    }

    Driver* dbdInfo(void) {
        Driver *driver  = new Driver;
        driver->name    = DRIVER_NAME;
        driver->version = DRIVER_VERSION;
        return driver;
    }
}
