#include "dbic++.h"
#include <unistd.h>

/*------------------------------------------------------------------------------

   To compile:

   g++ -Iinc -Llib -rdynamic -o demo demo.cc -ldbic++ -ldl -lpcrecpp -luuid

------------------------------------------------------------------------------*/

using namespace std;
using namespace dbi;

int main(int argc, char *argv[]) {
    ResultRowHash row;
    string driver(argc > 1 ? argv[1] : "postgresql");

    // Handle h (driver, user, password, database, host, port);
    Handle h (driver, getlogin(), "", "dbicpp");

    // Set trace on and log queries to stderr
    trace(true, fileno(stderr));

    // create table.
    cout << endl;
    cout << "-- creating table --" << endl;

    // create test table
    h.execute("drop table if exists users");
    h.execute("create table users(id serial, name text, email text, primary key (id))");

    // create table.
    cout << endl;
    cout << "-- inserting test data --" << endl;

    // insert some test data
    Statement ins (h, "insert into users(name, email) values(?, ?)");
    ins % "Apple Arthurton", "apple@example.com", execute();
    ins % "Benny Arthurton", "benny@example.com", execute();

    cout << endl;
    cout << "-- simple select --" << endl;

    Statement sel (h, "select id, name, email from users where id >= ? and id < ?");

    // bind and execute the statement.
    sel % 1L, 10L;
    Result *res = sel.query();
    while (res->read(row))
        cout << row["id"]    << "\t"
             << row["name"]  << "\t"
             << row["email"] << endl;

    delete res;

    // nested transaction
    cout << endl;
    cout << "-- nested transactions --" << endl;

    cout << "starting transaction one" << endl;
    h.begin("transaction_one");
    cout << "deleted rows with id = 1" << endl;
    h.execute("delete from users where id = 1");

    cout << "starting transaction two" << endl;
    h.begin("transaction_two");
    cout << "deleted rows with id = 2" << endl;
    h.execute("delete from users where id = 2");

    cout << "rolling back last statement" << endl;
    h.rollback("transaction_two");
    h.commit("transaction_one");

    // reset select statement and fetch all results.
    cout << endl;
    cout << "-- select all rows --" << endl;
    sel  << "select * from users";
    res  = sel.query();
    while (res->read(row))
        cout << row["id"]    << "\t"
             << row["name"]  << "\t"
             << row["email"] << endl;

    cout << endl;
    delete res;

    cout << "-- bulk copy in --" << endl;

    IOStream buffer;
    buffer.write("sally\tsally@local\n");
    buffer.write("jonas\tjonas@local\n");

    FieldSet fields(2, "name", "email");
    cout << "written rows: "
         << h.write("users", fields, &buffer)
         << endl;

    cout << endl;
    cout << "-- select all rows --" << endl;
    res = sel.query();
    while (res->read(row))
        cout << row["id"]    << "\t"
             << row["name"]  << "\t"
             << row["email"] << endl;

    delete res;
}
