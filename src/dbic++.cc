#include "dbic++.h"

#define CONNECT_FUNC(f) ((AbstractHandle* (*)(string, string, string, string, string, char*)) f)

namespace dbi {

    bool _trace;
    int  _trace_fd;

    map<string, Driver *> drivers;
    string_list_t available_drivers();

    string generateCompactUUID() {
        string rv;
        char hex[3];
        unsigned char uuid[16];

        uuid_generate(uuid);

        for (int i = 0; i < 16; i++) {
            sprintf(hex, "%02X", uuid[i]);
            rv += hex;
        }

        return rv;
    }

    bool dbiInitialize(string path) {
        string filename;
        struct dirent *file;
        struct stat st;

        Driver* (*info)(void);
        pcrecpp::RE re("\\.so\\.\\d+|\\.dylib");

        _trace_fd       = 1;
        drivers["null"] = NULL;

        DIR *dir = opendir(path.c_str());
        if (!dir)
            return false;

        while((file = readdir(dir))) {
            filename = path + "/" + string(file->d_name);

            lstat(filename.c_str(), &st);
            if (!S_ISREG(st.st_mode))
                continue;
            if (!re.PartialMatch(file->d_name))
                continue;

            void *handle = dlopen(filename.c_str(), RTLD_NOW|RTLD_LOCAL);

            if (handle != NULL) {
                if ((info = (Driver* (*)(void)) dlsym(handle, "dbdInfo"))) {
                    Driver *driver = info();
                    driver->handle = handle;
                    driver->connect = CONNECT_FUNC(dlsym(handle, "dbdConnect"));

                    if (driver->connect == NULL)
                        throw InvalidDriverError(dlerror());

                    if (drivers[driver->name]) {
                        if (_trace)
                            logMessage(_trace_fd, "WARNING: Already loaded " + driver->name +
                                       " driver. Ignoring: " + filename);
                        dlclose(handle);
                        delete driver;
                    }
                    else {
                        drivers[driver->name] = driver;
                    }
                }
                else {
                    logMessage(_trace_fd, "WARNING: Ignoring" + filename + ":" + dlerror());
                }
            }
            else {
                logMessage(_trace_fd, "WARNING: Ignoring" + filename + ":" + dlerror());
            }
        }

        closedir(dir);
        atexit(dbiShutdown);
        return true;
    }

    void dbiShutdown() {
        for (map<string, Driver*>::iterator iter = drivers.begin(); iter != drivers.end(); ++iter) {
            Driver *driver = iter->second;
            if (driver) {
                dlclose(driver->handle);
                delete driver;
            }
        }
        drivers.clear();
    }

    string_list_t available_drivers () {
        string_list_t list;

        if (!drivers.size())
            dbiInitialize();
        for(map<string, Driver *>::iterator iter = drivers.begin(); iter != drivers.end(); ++iter) {
            list.push_back(iter->first);
        }

        return list;
    }

    void initCheck(string driver_name) {
        if (!drivers.size()) {
            dbiInitialize("./lib/dbic++");
            dbiInitialize();
        }

        if (!drivers[driver_name])
            throw InvalidDriverError("Unable to find the '" + driver_name + "' driver.");
    }

    bool trace() {
        return _trace;
    }

    void trace(bool flag) {
        _trace = flag;
    }

    void trace(bool flag, int fd)  {
        _trace = flag;
        _trace_fd = fd;
    }

    string join(param_list_t &p, string delim) {
        unsigned i;
        stringstream out;
        if (p.size() > 0) {
            for (i = 0; i < p.size() - 1; i++)
                out << p[i] << delim;
            out << p[p.size()-1];
        }
        return out.str();
    }

    string formatParams(string sql, param_list_t &p) {
        string message(sql);

        if (p.size() > 0) message += " ~ " + join(p, ", ");
        return message;
    }

    void logMessage(int fd, string msg) {
        long n;
        char buffer[512];
        time_t now = time(NULL);
        struct tm *now_tm = localtime(&now);
        struct timeval tv;
        struct timezone tz;

        gettimeofday(&tv, &tz);

        strftime(buffer, 512, "[%FT%H:%M:", now_tm);
        n = write(fd, buffer, strlen(buffer));

        sprintf(buffer, "%02.3f] ", (float)now_tm->tm_sec + (float)tv.tv_usec/1000000.0);

        n += write(fd, buffer, strlen(buffer));
        n += write(fd, msg.data(), msg.length());
        n += write(fd, "\n", 1);
    }
}
