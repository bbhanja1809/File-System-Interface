#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

void recursive_delete(const char *path) {
    DIR *dir;
    struct dirent *entry;
    char child_path[1024];

    if ((dir = opendir(path)) == NULL) {
        perror("opendir");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        snprintf(child_path, sizeof(child_path), "%s/%s", path, entry->d_name);

        if (entry->d_type == DT_DIR) {
            recursive_delete(child_path);
        } else {
            if (unlink(child_path) != 0) {
                perror("unlink");
            }
        }
    }

    closedir(dir);
    rmdir(path); // Remove the current directory after its contents are deleted
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <directory>\n", argv[0]);
        return 1;
    }

    recursive_delete(argv[1]);
    return 0;
}
