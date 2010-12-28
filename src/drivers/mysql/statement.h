#ifndef _DBICXX_MYSQL_STATEMENT_H
#define _DBICXX_MYSQL_STATEMENT_H

namespace dbi {
    class MySqlHandle;
    class MySqlStatementResult;

    class MySqlStatement : public AbstractStatement {
        private:
        string _sql;
        string _uuid;

        MYSQL_STMT *_stmt;
        MySqlBind  *_result;

        protected:
        MYSQL *conn;

        void init();
        void boom(const char*);
        uint32_t storeResult();

        public:
        ~MySqlStatement();
        MySqlStatement(string sql, MYSQL *conn);

        string command();

        uint32_t execute();
        uint32_t execute(vector<Param> &bind);
        uint64_t lastInsertID();

        void cleanup();
        void finish();

        MySqlStatementResult* result();
    };
}

#endif
