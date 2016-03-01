#include "file.h"
#include "common.h"

namespace Cluster {
    File::File(string name, sync mode) {
        this->name = name;
        this->mode = mode;
        this->mtime = get_file_mtime((char*)name.c_str());
        this->checksum = hash_file((char*)name.c_str());
    }

    bool File::verify_checksum(string checksum) {
        return (checksum.compare(this->checksum) == 0);
    }

    file_act File::select_action(string checksum, time_t mtime, bool merged) {
        // TODO will this checksum be the file or new data
        //      Can it be used instead of timestamps accurately?
        if (checksum.compare(this->checksum) == 0) return NOOP;
        if (this->mode == NEWER) {
            if (mtime > this->mtime) return REQUEST;
            else if (mtime < this->mtime) return PUSH;
            return NOOP;
        } else if (this->mode == OLDER) {
            if (mtime < this->mtime) return PUSH;
            else if (mtime > this->mtime) return REQUEST;
            return NOOP;
        } else if (this->mode == MERGE) {
            // TODO Find a way to merge files?
            //      How does this choose whether to push an update or receive one?
            return NOOP;
        }

        return NOOP;
    }


    vector<File*> Dir::get_contents() {
        vector<File*> files;
        vector<string> filenames = get_directory_files((char*)this->name.c_str());
        ITERVECTOR(filenames, it) {
            string file = *it;
            File *f = new File(file, this->mode);
            files.push_back(f);
        }
        return files;
    }
}
