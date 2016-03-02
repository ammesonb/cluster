#include "file.h"
#include "common.h"

namespace Cluster {
    void FileList::AddDirs(vector<Dir*> dirs) {/*{{{*/
        ITERVECTOR(dirs, it) {
            Dir *d = *it;
            if (VECTORFIND(synced_dirs, d)) continue;
            synced_dirs.push_back(d);

            vector<File*> dir_files = d->get_contents();
            ITERVECTOR(dir_files, it2) {
                File *f = *it2;
                if (!(INVECTOR(this->synced_files, f)))
                    this->synced_files.push_back(f);
            }
        }
    }/*}}}*/

    void FileList::AddFiles(vector<File*> files) {/*{{{*/
        ITERVECTOR(files, it) {
            File *f = *it;
            if (!(INVECTOR(this->synced_files, f)))
                this->synced_files.push_back(f);
        }
    }/*}}}*/

    void FileList::RecheckDirs() {/*{{{*/
        AddDirs(this->synced_dirs);
    }/*}}}*/

    void FileList::Save(string file) {
        SyncUnion su;
        std::ostringstream oss;
        oss << "DIRS\n";
        ITERVECTOR(this->synced_dirs, it) {
            Dir *d = *it;
            su.s = d->mode;
            oss << d->name << " " << su.si << "\n";
        }

        oss << "FILES\n";
        ITERVECTOR(this->synced_files, it) {
            File *f = *it;
            su.s = f->mode;
            oss << f->name << " " << su.si << "\n";
        }
        oss << "END\n";

        string data = oss.str();

        FILE* sfile = fopen(file.c_str(), "w");
        fwrite(data.c_str(), data.length(), sizeof(char), sfile);
        fclose(sfile);
    }

    void FileList::Load(string file) {/*{{{*/
        // False is dir, true is file
        bool is_file = false;
        string filedata = read_file((char*)file.c_str());
        start_split(filedata, string("\n"), STRLITFIX("synclist"));
        string line = get_split(STRLITFIX("synclist"));
        vector<File*> files;
        vector<Dir*> dirs;
        while (line.compare("END") != 0) {
            if (line.compare("DIRS") == 0) is_file = false;
            else if (line.compare("FILES") == 0) is_file = true;

            start_split(line, string(" "), STRLITFIX("syncattrs"));

            string name = get_split(STRLITFIX("syncattrs"));
            int sync_int = atoi(get_split(STRLITFIX("syncattrs")).c_str());
            SyncUnion su;
            su.si = sync_int;

            if (is_file) {
                File *f = new File(name, su.s);
                files.push_back(f);
            } else {
                Dir *d = new Dir(name, su.s);
                dirs.push_back(d);
            }
            end_split(STRLITFIX("syncattrs"));
        }
        end_split(STRLITFIX("synclist"));

        this->AddDirs(dirs);
        this->AddFiles(files);
    }/*}}}*/

    File::File(string name, Sync mode) {/*{{{*/
        this->name = name;
        this->mode = mode;
        this->mtime = get_file_mtime((char*)name.c_str());
        this->checksum = hash_file((char*)name.c_str());
    }/*}}}*/

    bool File::VerifyChecksum(string checksum) {/*{{{*/
        return (checksum.compare(this->checksum) == 0);
    }/*}}}*/

    file_act File::SelectAction(string checksum, time_t mtime, bool merged) {/*{{{*/
        // TODO will this checksum be the file or new data
        //      Can it be used instead of timestamps accurately?
        if (checksum.compare(this->checksum) == 0) return NOOP;
        if (this->mode == Sync::NEWER) {
            if (mtime > this->mtime) return REQUEST;
            else if (mtime < this->mtime) return PUSH;
            return NOOP;
        } else if (this->mode == Sync::OLDER) {
            if (mtime < this->mtime) return PUSH;
            else if (mtime > this->mtime) return REQUEST;
            return NOOP;
        } else if (this->mode == Sync::MERGE) {
            // TODO Find a way to merge files?
            //      How does this choose whether to push an update or receive one?
            return NOOP;
        }

        return NOOP;
    }/*}}}*/

    Dir::Dir(string name, Sync mode) {/*{{{*/
        this->name = name;
        this->mode = mode;
    }/*}}}*/

    vector<File*> Dir::get_contents() {/*{{{*/
        vector<File*> files;
        vector<string> filenames = get_directory_files((char*)this->name.c_str());
        ITERVECTOR(filenames, it) {
            string file = *it;
            File *f = new File(file, this->mode);
            files.push_back(f);
        }
        return files;
    }/*}}}*/
}
