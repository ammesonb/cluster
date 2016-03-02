#ifndef CLUSTER_FILE_H
#define CLUSTER_FILE_H

#include <sstream>
#include <vector>
#include <string>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

using std::vector;
using std::string;

namespace Cluster {
    enum class Sync : int {NEWER, OLDER, MERGE};
    union SyncUnion {Sync s; int si;};
    enum file_act {PUSH=0, REQUEST=1, NOOP=2};

    class File {
        public:
            File(string name, Sync mode);

            string name;
            Sync mode;

            bool VerifyChecksum(string checksum);
            file_act SelectAction(string checksum, time_t mtime, bool merged);

            bool operator==(const File &other) {
                return (this->name.compare(other.name)) == 0;
            }

        private:
            string checksum;
            time_t mtime;
    };

    class Dir {
        public:
            Dir(string name, Sync mode);

            string name;
            Sync mode;

            vector<File*> get_contents();

            bool operator==(const Dir &other) {
                return (this->name.compare(other.name)) == 0;
            }
    };

    class FileList {
        public:
            void AddDirs(vector<Dir*> dirs);
            void AddFiles(vector<File*> files);
            void RecheckDirs();
            void Save(string file);
            void Load(string file);

        private:
            // This will have ALL files
            vector<File*> synced_files;
            // This is really "watched" dirs which will load into the files list
            vector<Dir*> synced_dirs;
    };
}
#endif
