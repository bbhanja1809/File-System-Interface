#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <utime.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#define MAX_PATH_LEN 1024
#define SYNC_INTERVAL_SECONDS 5 // Check for changes every 5 seconds

void sync_directories(const char *src_path, const char *dst_path);
void sync_items(const char *src_path, const char *dst_path);
void update_permissions(const char *src_path, const char *dst_path);
void report_change(const char *path, char change_type);
void copy_file(const char *src_file, const char *dst_file);
void handle_timer(int signal);

char src_path[MAX_PATH_LEN];
char dst_path[MAX_PATH_LEN];

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <source directory> <destination directory>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    strcpy(src_path, argv[1]);
    strcpy(dst_path, argv[2]);

    // Set up signal handler for timer
    struct sigaction sa;
    sa.sa_handler = handle_timer;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGALRM, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    // Set up timer
    struct itimerval timer;
    timer.it_interval.tv_sec = SYNC_INTERVAL_SECONDS;
    timer.it_interval.tv_usec = 0;
    timer.it_value = timer.it_interval;
    if (setitimer(ITIMER_REAL, &timer, NULL) == -1) {
        perror("setitimer");
        exit(EXIT_FAILURE);
    }

    sync_directories(src_path, dst_path);

    return 0;
}

void sync_directories(const char *src_path, const char *dst_path) {
    sync_items(src_path, dst_path);
    update_permissions(src_path, dst_path);
}


void sync_items(const char *src_path, const char *dst_path) {
    // Check for deletions and additions in both source and destination directories
    DIR *src_dir = opendir(src_path);
    DIR *dst_dir = opendir(dst_path);

    if (!src_dir || !dst_dir) {
        perror("opendir");
        exit(EXIT_FAILURE);
    }

    struct dirent *entry;
    while ((entry = readdir(src_dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue; // Skip current and parent directories
        }

        char src_item[MAX_PATH_LEN];
        char dst_item[MAX_PATH_LEN];
        snprintf(src_item, MAX_PATH_LEN, "%s/%s", src_path, entry->d_name);
        snprintf(dst_item, MAX_PATH_LEN, "%s/%s", dst_path, entry->d_name);

        struct stat src_stat, dst_stat;
        if (stat(src_item, &src_stat) == -1 && errno != ENOENT) {
            perror("stat");
            exit(EXIT_FAILURE);
        }

        if (stat(dst_item, &dst_stat) == -1) {
            if (errno == ENOENT) {
                // File or directory exists in the source but not in the destination
                if (S_ISDIR(src_stat.st_mode)) {
                    // Recursively synchronize subdirectories
                    mkdir(dst_item, src_stat.st_mode);
                    report_change(dst_item, '+');
                    sync_items(src_item, dst_item);
                } else {
                    // File exists in source but not in destination
                    copy_file(src_item, dst_item);
                    report_change(dst_item, '+');
                }
            } else {
                perror("stat");
                exit(EXIT_FAILURE);
            }
        }
    }

    // Check for deletions in the destination directory
    while ((entry = readdir(dst_dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue; // Skip current and parent directories
        }

        char src_item[MAX_PATH_LEN];
        char dst_item[MAX_PATH_LEN];
        snprintf(src_item, MAX_PATH_LEN, "%s/%s", src_path, entry->d_name);
        snprintf(dst_item, MAX_PATH_LEN, "%s/%s", dst_path, entry->d_name);

        struct stat src_stat, dst_stat;
        if (stat(src_item, &src_stat) == -1 && errno != ENOENT) {
            perror("stat");
            exit(EXIT_FAILURE);
        }

        if (stat(dst_item, &dst_stat) == -1) {
            if (errno == ENOENT) {
                // File or directory exists in the destination but not in the source
                if (S_ISDIR(dst_stat.st_mode)) {
                    // Recursively synchronize subdirectories
                    sync_items(dst_item, src_item);
                } else {
                    // File exists in destination but not in source
                    remove(dst_item);
                    report_change(dst_item, '-');
                }
            } else {
                perror("stat");
                exit(EXIT_FAILURE);
            }
        }
    }

    closedir(src_dir);
    closedir(dst_dir);
}



void update_permissions(const char *src_path, const char *dst_path) {
    struct stat src_stat, dst_stat;
    if (stat(src_path, &src_stat) == -1 || stat(dst_path, &dst_stat) == -1) {
        perror("stat");
        exit(EXIT_FAILURE);
    }

    if (chmod(dst_path, src_stat.st_mode) == -1) {
        perror("chmod");
        exit(EXIT_FAILURE);
    }

    struct utimbuf times;
    times.actime = src_stat.st_atime;
    times.modtime = src_stat.st_mtime;
    if (utime(dst_path, &times) == -1) {
        perror("utime");
        exit(EXIT_FAILURE);
    }

    if (S_ISDIR(src_stat.st_mode)) {
        DIR *src_dir = opendir(src_path);
        if (!src_dir) {
            perror("opendir");
            exit(EXIT_FAILURE);
        }

        struct dirent *entry;
        while ((entry = readdir(src_dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue; // Skip current and parent directories
            }

            char src_item[MAX_PATH_LEN];
            char dst_item[MAX_PATH_LEN];
            snprintf(src_item, MAX_PATH_LEN, "%s/%s", src_path, entry->d_name);
            snprintf(dst_item, MAX_PATH_LEN, "%s/%s", dst_path, entry->d_name);

            update_permissions(src_item, dst_item); // Recursively update permissions for subdirectories
        }

        closedir(src_dir);
    }
}

void report_change(const char *path, char change_type) {
    printf("[%c] %s\n", change_type, path);
}

void copy_file(const char *src_file, const char *dst_file) {
    FILE *src_fp = fopen(src_file, "rb");
    if (!src_fp) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    FILE *dst_fp = fopen(dst_file, "wb");
    if (!dst_fp) {
        perror("fopen");
        fclose(src_fp);
        exit(EXIT_FAILURE);
    }

    char buffer[4096];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), src_fp)) > 0) {
        if (fwrite(buffer, 1, bytes_read, dst_fp) != bytes_read) {
            perror("fwrite");
            fclose(src_fp);
            fclose(dst_fp);
            exit(EXIT_FAILURE);
        }
    }

    if (ferror(src_fp)) {
        perror("fread");
        fclose(src_fp);
        fclose(dst_fp);
        exit(EXIT_FAILURE);
    }

    if (fclose(src_fp) != 0 || fclose(dst_fp) != 0) {
        perror("fclose");
        exit(EXIT_FAILURE);
    }
}

void handle_timer(int signal) {
    // This function will be called periodically by the timer
    printf("Checking for changes...\n");
    sync_directories(src_path, dst_path);
}
