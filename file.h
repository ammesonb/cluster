#ifndef CLUSTER_FILE_H
#define CLUSTER_FILE_H

#include <vector>
#include <string>

using std::vector;
using std::string;

namespace Cluster {
    enum sync {NEWER, OLDER, MERGE};
    enum file_act {PUSH, REQUEST, NOOP};

    class File {
        public:
            File(string name, sync mode);
            bool verify_checksum(string checksum);
            file_act select_action(string checksum, time_t mtime, bool merged);

            bool operator==(const File &other) {
                return (this->name.compare(other.name)) == 0;
            }
        private:
            string name;
            string checksum;
            sync mode;
            time_t mtime;
    };

    class Dir {
        public:
            vector<File*> get_contents();

            bool operator==(const Dir &other) {
                return (this->name.compare(other.name)) == 0;
            }
        private:
            string name;
            sync mode;
    };

    class FileList {
        public:
            void AddDirs(vector<Dir*> dirs);
            void AddFiles(vector<File*> files);
            void RecheckDirs();

        private:
            // This will have ALL files
            vector<File*> synced_files;
            // This is really "watched" dirs which will load into the files list
            vector<Dir*> synced_dirs;
    };
}
#endif
