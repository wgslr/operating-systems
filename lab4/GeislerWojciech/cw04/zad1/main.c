#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>

pid_t dater_pid = 0;

pid_t spawn_dater(void);

void handler(int signal){
    if(signal == SIGTSTP){
        if(dater_pid == 0) {
            dater_pid = spawn_dater();
        } else {
            kill(SIGKILL, dater_pid);
            dater_pid = 0;
            printf("Oczekuję na CTRL+Z - kontynuacja albo CTR+C - zakonczenie programu\n");
        }
    } else if(signal == SIGINT) {
        printf("Odebrano sygnał SIGINT");
        kill(SIGKILL, dater_pid);
        exit(0);
    } else {
        fprintf(stderr, "Signal handler in pid %d received unexpected signal %d\n", getpid(), signal);
    }
}

pid_t spawn_dater(void) {
    int pid = fork();
    if(pid == 0) {
        int result = execl("dater.sh", "dater.sh", NULL);
        fprintf(stderr, "Could not spawn date-printing script: %d\n", result);
        exit(1);
    } else {
        return pid;
    }
}


int main(int argc, char* argv[]) {
    (void) argc;
    (void) argv;

    dater_pid = spawn_dater();

    printf("dater pid is %d\n", dater_pid);

    signal(SIGTSTP, &handler);

    // TODO use sigaction
    signal(SIGINT, &handler);
    signal(SIGCHLD, &handler);

    while(true) {}
}

