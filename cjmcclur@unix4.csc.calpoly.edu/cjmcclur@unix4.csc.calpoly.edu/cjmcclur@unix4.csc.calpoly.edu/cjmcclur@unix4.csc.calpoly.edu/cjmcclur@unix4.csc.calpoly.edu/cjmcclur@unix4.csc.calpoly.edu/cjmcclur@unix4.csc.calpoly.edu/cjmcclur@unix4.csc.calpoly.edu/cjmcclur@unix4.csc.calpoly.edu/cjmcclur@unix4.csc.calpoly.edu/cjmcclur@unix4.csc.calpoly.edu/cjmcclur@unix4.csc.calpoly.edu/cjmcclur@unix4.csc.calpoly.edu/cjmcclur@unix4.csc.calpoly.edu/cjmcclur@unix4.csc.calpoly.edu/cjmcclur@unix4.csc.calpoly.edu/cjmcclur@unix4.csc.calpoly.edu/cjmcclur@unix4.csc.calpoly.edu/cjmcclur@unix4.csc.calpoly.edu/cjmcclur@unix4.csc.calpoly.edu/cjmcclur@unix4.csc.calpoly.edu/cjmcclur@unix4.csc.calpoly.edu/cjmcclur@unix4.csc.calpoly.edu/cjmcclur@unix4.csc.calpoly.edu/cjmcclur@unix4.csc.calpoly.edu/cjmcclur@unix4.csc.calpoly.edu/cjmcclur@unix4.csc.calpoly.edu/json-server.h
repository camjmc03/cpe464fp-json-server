//
// Created by Cameron McClure-Coleman on 11/29/23.
//

#ifndef PROJECT_4_JSON_SERVER_H
#define PROJECT_4_JSON_SERVER_H

/* ----------------------------- *
 * -----INCLUDE STATEMENTS------ *
 * ----------------------------- */
#include <stdio.h>
#include <sys/socket.h>


/* ----------------------------- *
 * -----CONSTANT DEFINITIONS---- *
 * ----------------------------- */

/* ----------------------------- *
 * -----STRUCT DEFINITIONS------ *
 * ----------------------------- */

enum client_state {
    READ,
    PARSE,
    GENERATE,
    WRITE,
    EXIT
};

typedef struct client {
    enum client_state state;
    FILE *file;
    int socket;
    char *parse_buffer;
    char *generate_buffer;
    int parse_buffer_size;
    int generate_buffer_size;
    int parse_buffer_index;
    int generate_buffer_index;
    int not_found_flag;
}client;


/*
 * 1024 is the max number of file descriptors for a process on most linux systems
 * clients take up at most 2 file descriptors at once, so 508 is the limit of clients in the client table
 * (stdin, stdout, stderr, and server listen socket should take up 0, 1, 2, and 3 respectively)
 * 1024 is the size of the hash array so that it is a perfect hash with file descriptor number as the key.
 * this means clients can have 2 entries in the table at once: one with socket key and one with fd key.
 * this makes select() logic very easy because you can use either as a key.
 */

// I don't care that the first 4 are wasted, I don't want to subtract 4 from the fd for indexing, im lazy, be quiet.
#define MAX_CLIENTS 508
#define CLIENT_TABLE_ARRAY_SIZE 1024

/*
 * client_count is the number of clients in the table
 * max_clients is the maximum number of clients that can be in the table at once
 * array_capacity is the size of the array, not the max number of clients. hash stuff.
 * client_entries is an array of client_entry pointers. this is the table: indexed by file descriptor.
 * the client_entry struct contains a pointer to the client and the hash key (client's socket or fd).
 */
typedef struct client_table {
    client *clients[CLIENT_TABLE_ARRAY_SIZE]; //array of client pointers indexed by file descriptor
    int client_count;
    int max_clients;
    int array_capacity; //this is the size of the array, not the number of clients in the table
}client_table;



/* ----------------------------- *
 * -----FUNCTION PROTOTYPES----- *
 * ----------------------------- */

// client table functions
client_table *construct_client_table();
void deconstruct_client_table(client_table *table);
int client_table_add(int key, client *c);
client * client_table_remove(int key, client *c);
client *client_table_get(int descriptor);

// client functions
void deconstruct_client(client *c);
client *construct_client(int socket_fd, FILE *file, int parse_buffer_size, int generate_buffer_size);
int accept_new_client();

// client state functions
int poll_clients();
int client_read(client *c);

// listen socket functions
int init_listening_socket(int argc, char *argv[]);

// server functions
void server_exit();

#endif //PROJECT_4_JSON_SERVER_H
