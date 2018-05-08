// Wojciech Geisler
// 2018-04

#define _XOPEN_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include "common.h"
#include <stdlib.h>
#include <memory.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <mqueue.h>
#include <fcntl.h>
#include <time.h>

#define MAX_TOKENS 5
const char *const WHITESPACE = " \r\n\t";

typedef struct {
    char *toks[MAX_TOKENS + 1];
    int size;
} tokens;

int client_id = -1;
mqd_t client_queue;
mqd_t server_queue;
char *queue_name = NULL;

// Splits string on whitespace
tokens *tokenize(char *string);

arith_op char_to_op(char c);


void send(const uint8_t mtype, const void *content, size_t content_len) {
    msgbuf req = {
            .sender_id = client_id, .sender_pid = getpid(),
            .mtype = mtype,
            .content = {0}
    };
    memcpy(req.content, content, content_len);

    OK(mq_send(server_queue, (char*)&req, MSG_SZ, PRIORITY), "Error sending message");
}

void onexit(void) {
    send(STOP, queue_name, 0);

    fprintf(stderr, "Removing queue %d\n", client_queue);
    mq_close(client_queue);
    mq_unlink(queue_name);
}

void sigint_handler(int signal) {
    (void) signal;
    exit(0);
}

msgbuf *receive(void) {
    msgbuf *buff = calloc(1, sizeof(msgbuf));
    unsigned priority;
    mq_receive(client_queue, (char*)buff, MSG_SZ, &priority);
    assert(PRIORITY == priority);
    return buff;
}

void register_client(void) {
    fprintf(stderr, "Registering with queue %s\n", queue_name);
    send(REGISTER, queue_name, 1 + strlen(queue_name));

    msgbuf *resp = receive();
    client_id = *(int *) resp->content;
    printf("Client id is %d\n", client_id);
    free(resp);
}

void handle_mirror(char *message) {
    send(MIRROR, message, strlen(message) + 1);

    msgbuf *resp = receive();
    printf("MIRROR result: '%s'\n", resp->content);
    free(resp);
}

void handle_calc(const tokens *command) {
    arith_req req = {
            .op = char_to_op(command->toks[2][0]),
            .arg1 = atoi(command->toks[1]),
            .arg2 = atoi(command->toks[3])
    };
    send(CALC, &req, sizeof(req));

    msgbuf *resp = receive();
    printf("CALC result: %d\n", *(int *) resp->content);
    free(resp);
};

void handle_time(void) {
    send(TIME, NULL, 0);
    msgbuf *resp = receive();
    printf("TIME result: %s", resp->content);
    free(resp);
}

void handle_end(void) {
    send(END, NULL, 0);
}

arith_op char_to_op(char c) {
    switch(c) {
        case '+':
            return ADD;
        case '-':
            return SUB;
        case '*':
            return MUL;
        case '/':
            return DIV;
        default:
            fprintf(stderr, "Unknown arithemetic operator\n");
            exit(1);
    }
}

char* generate_queue_name(void) {
    const int LEN = 6;
    char *name = calloc(LEN + 2, sizeof(char));
    name[0] = '/';
    for(int i = 1; i < LEN + 1; ++i){
        name[i] = (char) (rand() % ('z' - 'a' + 1) + 'a');
    }
    return name;
}

void create_queue(void) {
    queue_name = generate_queue_name();
    struct mq_attr attr = {
            .mq_flags = 0, .mq_maxmsg = 10, .mq_curmsgs = 0, .mq_msgsize = MSG_SZ
    };
    client_queue = mq_open(queue_name, O_RDONLY | O_CREAT | O_EXCL, 0644, &attr);
    OK(client_queue, "Creating client queue failed");
}

int main(int argc, char *argv[]) {
    srand((unsigned int) (time(NULL) * getpid()));
    signal(SIGINT, &sigint_handler);

    FILE *input;
    if(argc <= 1 || strcmp("-", argv[1]) == 0) {
        fprintf(stderr, "Reading input from stdin\n");
        input = stdin;
    } else {
        input = fopen(argv[1], "r");
        if(input == NULL) {
            fprintf(stderr, "Opening input file '%s' failed: %s\n", argv[1], strerror(errno));
        }
    }

    server_queue = mq_open(SERVER_QUEUE, O_WRONLY);
    OK(server_queue, "Opening server queue failed");

    create_queue();
    atexit(&onexit);
    register_client();

    char *line = NULL;
    size_t length = 0;
    while(getline(&line, &length, input) > 0) {
        printf("Executing '%.*s':\n", (int) (strlen(line) - 1), line);

        tokens *command = tokenize(line);
        if(strcmp(command->toks[0], "MIRROR") == 0) {
            if(command->size < 2) {
                fprintf(stderr, "You must provider string to be mirrored\n");
            } else {
                handle_mirror(command->toks[1]);
            }
        } else if(strcmp(command->toks[0], "CALC") == 0) {
            if(command->size < 4) {
                fprintf(stderr, "You must provider <operand> <operator> <operand>\n");
            } else {
                handle_calc(command);
            }
        } else if(strcmp(command->toks[0], "TIME") == 0) {
            handle_time();
        } else if(strcmp(command->toks[0], "END") == 0) {
            handle_end();
            exit(0);
        } else {
            fprintf(stderr, "Unknown command '%s'\n", command->toks[0]);
        }
    }
    free(line);

    exit(0);

    return 0;
}

// Reused code from 5.1

// Splits string on whitespace
tokens *tokenize(char *string) {
    tokens *result = calloc(1, sizeof(tokens));
    size_t wordlen;

    // find beginning of string
    string += strspn(string, WHITESPACE);

    while(string != NULL && *string != '\0') {
        wordlen = strcspn(string, WHITESPACE);
        result->toks[result->size] = calloc(1, wordlen);
        strncpy(result->toks[result->size], string, wordlen);
        ++result->size;

        string += wordlen;
        string += strspn(string, WHITESPACE); // skip whitespace
    }
    return result;
}