#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <time.h>


typedef struct File {
    int fd;
    FILE* handle;
    bool syscall;
} File;

typedef struct timeval timeval;
typedef struct timespec timespec;

typedef struct timestamp {
    timeval system;
    timeval user;
    timeval real;
} timestamp;

void print_timediff(timeval start, timeval end, const char* description);

/** Timing functions **/

timestamp get_timestamp() ;

timeval timespec_to_timeval(timespec time) ;

void print_timing(const timestamp start, const timestamp end, const char* description) ;

// Converts a subset of open() flags to fopen() mode
// Returned array should be free-ed
char* flags_to_char(const int flags) {
    char* char_flags = calloc(3, sizeof(char));
    if(flags & O_APPEND) {
        char_flags[0] = 'a';
        char_flags[1] = '\0';
    }
    if(flags & O_RDWR) {
        char_flags[0] = 'r';
        char_flags[1] = 'w';
        char_flags[2] = '\0';
    } else if(flags & O_WRONLY) {
        char_flags[0] = 'w';
        char_flags[1] = '\0';
    } else {
        // O_RDONLY == 0
        char_flags[0] = 'r';
        char_flags[1] = '\0';
    }
    return char_flags;
}

// returns 0 on error
File* file_open(const char* path, const int flags, bool syscall) {
    File* file = calloc(1, sizeof(File));
    file->syscall = syscall;
    if(syscall) {
        int result = open(path, flags, S_IRUSR | S_IWUSR | S_IRGRP);
        if(result < 0) {
            free(file);
            return NULL;
        } else {
            file->fd = result;
            return file;
        }
    } else {
        char* flags_char = flags_to_char(flags);
        file->handle = fopen(path, flags_char);
        free(flags_char);
        if(file->handle != NULL) {
            return file;
        } else {
            free(file);
            return NULL;
        }
    }
}

void file_close(File** file_ptr) {
    if((*file_ptr)->syscall) {
        close((*file_ptr)->fd);
    } else {
        fclose((*file_ptr)->handle);
    }
    free(*file_ptr);
    *file_ptr = NULL;
}

// Returns number of read bytes
// file is either (int*) or (FILE*)
unsigned file_read(File* file, unsigned size, void* buf) {
    if(file->syscall) {
        return (unsigned) read(file->fd, buf, size);
    } else {
        // multiply by size since fread returns number of *blocks* read
        return (unsigned) (size * (fread(buf, size, 1, file->handle)));
    }
}

// Returns number of written bytes
int file_write(File* file, unsigned size, const void* content) {
    if(file->syscall) {
        return (int) write(file->fd, content, size);
    } else {
        return (int) fwrite(content, size, 1, file->handle);
    }
}

void copy(const char* path_from, const char* path_to,
          const unsigned records, const unsigned record_size, bool syscalls) {
    fprintf(stderr, "Copying %ux%u bytes from \"%s\" to \"%s\"\n", records, record_size, path_from, path_to);

    File* source = file_open(path_from, O_RDONLY, syscalls);
    File* target = file_open(path_to, O_WRONLY | O_CREAT | O_TRUNC, syscalls);

    if(source == NULL || target == NULL) {
        fprintf(stderr, "Error opening file");
        exit(1);
    }

    char* buf = malloc(record_size);
    int bytes = record_size;

    for(int i = 0; i < records; ++i) {
        bytes = file_read(source, record_size, buf);
        file_write(target, bytes, buf);

        if(bytes < record_size) {
            fprintf(stderr, "File %s contained less than %u records. Copied %d", path_from, records, i);
            break;
        }
    }

    free(buf);
    file_close(&target);
    file_close(&source);

}

void generate(const char* path, const unsigned records, const unsigned record_size, bool syscalls) {
    copy("/dev/urandom", path, records, record_size, syscalls);
}

// offset - number of arguments before size parameters
void parse_params(int offset, char* argv[], int* records, int* record_size, bool* syscalls) {
    *records = atoi(argv[offset + 1]);
    *record_size = atoi(argv[offset + 2]);
    *syscalls = strcmp(argv[offset + 3], "sys") == 0;
}

int main(int argc, char* argv[]) {
    if(argc < 5) {
        fprintf(stderr, "Wrong number of arguments: %d\n", argc);
        return (2);
    }

    int records;
    int record_size;
    bool syscalls;
    char description[100];

    if(strcmp(argv[1], "generate") == 0) {
        const char* path = argv[2];
        parse_params(2, argv, &records, &record_size, &syscalls);
        generate(path, records, record_size, syscalls);
    } else if(strcmp(argv[1], "copy") == 0) {
        const char* from = argv[2];
        const char* to = argv[3];
        parse_params(3, argv, &records, &record_size, &syscalls);

        timestamp start = get_timestamp();
        copy(from, to, records, record_size, syscalls);
        timestamp end = get_timestamp();

        sprintf(description, "copy; %d; %d; %d;", records, record_size, syscalls);
        print_timing(start, end, description);

    } else if(strcmp(argv[1], "sort") == 0) {
        const char* path = argv[2];
        parse_params(2, argv, &records, &record_size, &syscalls);
    } else {
        fprintf(stderr, "Unidentified argument \'%s\"\n", argv[1]);
    }

    return 0;
}

/** Timing functions **/

timestamp get_timestamp() {
    timestamp ts;
    struct rusage ru;
    struct timespec real;

    clock_gettime(CLOCK_REALTIME, &real);
    ts.real = timespec_to_timeval(real);

    getrusage(RUSAGE_SELF, &ru);
    ts.system = ru.ru_stime;
    ts.user = ru.ru_utime;
    return ts;
}


void print_timing(const timestamp start, const timestamp end, const char* description) {
    char* buf = calloc(10 + strlen(description), sizeof(char));
    sprintf(buf, "%s user", description);
    print_timediff(start.user, end.user, buf);
    sprintf(buf, "%s system", description);
    print_timediff(start.system, end.system, buf);
    sprintf(buf, "%s real", description);
    print_timediff(start.real, end.real, buf);
    free(buf);
}


timeval timespec_to_timeval(timespec time) {
    timeval ts;
    ts.tv_sec = time.tv_sec;
    ts.tv_usec = time.tv_nsec / 1000;
    return ts;
}


void print_timediff(timeval start, timeval end, const char* description) {
    long double diff_ms = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_usec - start.tv_usec) / 1000;
    printf("%11s (ms): %10.5Lf\n", description, diff_ms);
}
