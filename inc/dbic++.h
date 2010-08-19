#ifndef _DBICXX_BASE_H
#define _DBICXX_BASE_H

#include <iostream>
#include <sstream>
#include <string>
#include <cstdarg>
#include <cstdio>
#include <vector>
#include <map>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <dirent.h>
#include <dlfcn.h>
#include <pcrecpp.h>
#include <uuid/uuid.h>

#undef  uint
#undef  ulong
#define uint  unsigned int
#define ulong unsigned long

#define DBI_VERSION      0.2.6

namespace dbi {
    struct null {};
    struct execute {};
}

#include "dbic++/error.h"
#include "dbic++/param.h"
#include "dbic++/container.h"
#include "dbic++/cpool.h"

#define DEFAULT_DRIVER_PATH "/usr/lib/dbic++"

/*
    Section: About

    What is dbic++ ?:

    dbic++ is a database client library written in C++ which comes with support for the following databases,

        * PostgreSQL >= 8.0
        * MySQL      >= 5.0

    Drivers for the following will be available soon.

        * SQLite >= 3
        * Oracle >= 9.2


    Main Features:

        * Simple API to maximize cross database support.
        * Supports nested transactions.
        * Auto reconnect, re-prepare & execute statements again unless inside a transaction.
        * Provides APIs for async queries and a simple reactor API built on libevent.

    Reading:

    At the very least you need to know how <AbstractHandle>, <AbstractStatement>, <Handle>,
    and <Statement> work. It is also recommended that you read through documentation on <Param>.

    Examples:

    dbic++ source distributions comes with some examples under src/examples/

    Compiling:

    To compile code that uses dbic++, use pkg-config to get the correct compiler flags.

    (begin code)
    g++ `pkg-config --libs --cflags dbic++` -o example example.cc
    (end)

    Benchmarks:

    You can run these yourself if you're keen. The makefile might need some tweaks
    on non-debian based distributions. The following results were obtained on a
    Core2DUO 2.53GHz, 4GB, 5200rpm drive with stock database configs.

    * The dataset size is 50 rows of ~64 bytes each.
    * 5000 SELECT queries run back to back, fetching all rows and printing them.

    (begin code)
    cd bench/ && make

    ./benchmarks.sh

    + Setting up sample data (50 rows)
      * mysql
      * postgresql
    + Done

    ./benchmarks.sh -w bench

    + Benchmarking mysql

      * mysql      user 0.36 sys 0.06 real 1.47
      * mysql++    user 0.49 sys 0.02 real 1.62
      * dbicpp     user 0.44 sys 0.08 real 1.63

    + Benchmarking postgresql

      * pq         user 0.40 sys 0.04 real 1.46
      * dbicpp     user 0.69 sys 0.12 real 1.58

    + Finished
    (end)

*/

#define DBI_TYPE_INT     1
#define DBI_TYPE_TIME    2
#define DBI_TYPE_TEXT    3
#define DBI_TYPE_FLOAT   4
#define DBI_TYPE_NUMERIC 5
#define DBI_TYPE_BOOLEAN 6
#define DBI_TYPE_BLOB    7

namespace dbi {
    extern bool _trace;
    extern int  _trace_fd;

    class AbstractStatement;
    class AbstractResultSet;
}

#include "dbic++/util.h"
#include "dbic++/io.h"
#include "dbic++/io_stream.h"
#include "dbic++/io_filestream.h"
#include "dbic++/abstract_handle.h"
#include "dbic++/abstract_resultset.h"
#include "dbic++/abstract_statement.h"
#include "dbic++/handle.h"
#include "dbic++/statement.h"

#endif
