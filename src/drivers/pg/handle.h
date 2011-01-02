#ifndef _DBICXX_PG_HANDLE_H
#define _DBICXX_PG_HANDLE_H

namespace dbi {

    class PgHandle : public AbstractHandle {
        private:
        string _sql;
        PGresult* _pgexec(string sql);
        PGresult* _pgexec(string sql, vector<Param> &bind);

        // execute queries directly and don't save results.
        void _pgexecDirect(string sql);

        PGresult *_result;

        protected:
        int tr_nesting;
        void boom(const char *);

        public:
        PGconn *conn;

        PgHandle();
        PgHandle(string user, string pass, string dbname, string h, string p);
        ~PgHandle();
        void cleanup();

        PgStatement* prepare(string sql);
        uint32_t     execute(string sql);
        uint32_t     execute(string sql, vector<Param> &bind);

        PgResult*    result();

        int  socket();
        void initAsync();
        bool isBusy();
        bool cancel();

        PgResult* aexecute(string sql);
        PgResult* aexecute(string sql, vector<Param> &bind);

        bool begin();
        bool commit();
        bool rollback();
        bool begin(string name);
        bool commit(string name);
        bool rollback(string name);

        bool close();
        void reconnect();

        uint64_t write(string table, FieldSet &fields, IO*);
        void setTimeZoneOffset(int, int);
        void setTimeZone(char *);
        string escape(string);
        string driver();
    };
}

#endif