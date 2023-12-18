//
// Created by Cameron McClure-Coleman on 11/29/23.
//

/* ------------------ *
 * -----INCLUDES----- *
 * ------------------ */

#include "json-server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/errno.h>
#include <netdb.h>
#include <sys/stat.h>

#define DEBUG 0
#define DEBUG_VERBOSE 0



/* ----------------------------- *
 * -----GLOBAL DEFINITIONS------ *
 * ----------------------------- */
client_table *global_client_table = NULL;
struct addrinfo *global_server_addrinfo;
int global_listening_sd = -1;
fd_set select_write_set, select_read_set, select_error_set;




/* -------------------------------- *
 * -----CLIENT TABLE FUNCTIONS----- *
 * -------------------------------- */
/*
 * constructs a new client table struct
 * allocates memory for the client table and the client table's client entries
 * client table capacity is defined by CLIENT_TABLE_CAPACITY and is the maximum number of file descriptors that can
 * be tracked by the program / 2 since each client takes up a max of 2 file descriptors at once.
 * returns a pointer to the client table struct
 */
client_table *construct_client_table()
{
    client_table *ct;
    ct = calloc(1, sizeof(client_table));
    ct->client_count = 0;
    ct->max_clients = MAX_CLIENTS;
    ct->array_capacity = CLIENT_TABLE_ARRAY_SIZE;
    //client array already defined in client_table struct so space for it was allocated in the ct malloc
    return ct;
}
void deconstruct_client_table(client_table *table){
    int i;
    // free all clients in the table
    for (i = 0; i < table->array_capacity; i++){
        if (table->clients[i] != NULL){
            deconstruct_client(table->clients[i]);
        }
    }
    // client array is fixed size, so it does not need to be freed.
    // free the client table
    free(table);
}

/*
 * adds an entry for client c to the client table
 * if the client table is full, returns -1
 * if the client is passed in with a file descriptor, adds a second key to the table for the client with file descriptor
 * if the client already has a socket in the table the function does not increment the client count
 */
int client_table_add(int key, client *c) {
    if (key < 4 || key > global_client_table->array_capacity){
        // key is not valid, return error
        if (DEBUG) fprintf(stderr, "Error adding client to table: key is not a valid descriptor\n");
        return -1;
    }
    if (global_client_table->client_count >= global_client_table->max_clients) {
        // client table is full, return error
        if (DEBUG) fprintf(stderr, "Error adding client to table: client table is full\n");
        return -1;
    }
    // if the key already has an entry in the table, return error
    if (global_client_table->clients[key] != NULL) {
        if (DEBUG) fprintf(stderr, "Error adding client to table: key already has an entry in the table\n");
        return -1;
    }

    // if the passed in key is the clients socket, add the client to the table with the socket key
    // and increment the client count
    if (key == c->socket) {
        global_client_table->clients[key] = c;
        global_client_table->client_count++;
        // also add the client's socket descriptor to the select set
        FD_SET(key, &select_read_set);
        return 0;
    }
    // if the passed in key is the clients file descriptor, add the client to the table with the file descriptor key
    // and do not increment the client count
    if (key == fileno(c->file)) {
        global_client_table->clients[key] = c;
        // also add the client's file descriptor to the select set
        FD_SET(key, &select_read_set);
        return 0;
    }
    // if the passed in key is not the clients socket or file descriptor, return error
    if (DEBUG) fprintf(stderr, "Error adding client to table: key is not a descriptor for given client\n");

    // if the socket key already had a value do not increment the client count
    // i.e. a client already exists for that socket descriptor
    // also do not add the client to the table again, since it is already there.
    return 0;
}

/*
 * if the given key is the client's socket
 * removes the client with the given descriptor key from the client table
 * if the given key is the client's file descriptor
 * removes the entry for the client with the given file descriptor, but does not decrement the client count
 * or remove the entry for the client's socket.
 * if the entry does not exist in the table, returns NULL
 * if the given descriptor is not in the valid range, returns NULL
 * if the client is removed successfully, returns pointer to removed client
 */
client * client_table_remove(int key, client *c){
    if (key < 4 || key > global_client_table->array_capacity){
        // key is not valid, return error
        if (DEBUG) fprintf(stderr, "Error removing client from table: descriptor is not a valid key\n");
        return NULL;
    }
    // check that entry exists in table
    if (global_client_table->clients[key] == NULL){
        // client does not exist in table, return error
        if (DEBUG) fprintf(stderr, "Error removing client from table: client does not exist in table\n");
        return NULL;
    }
    // if the key is the client's socket, remove the client from the table and decrement the client count
    if (key == c->socket){
        global_client_table->clients[key] = NULL;
        //also remove the client's socket from the select set
        FD_CLR(key, &select_read_set);
        // also remove the client's file descriptor entry if it exists
        global_client_table->clients[fileno(c->file)] = NULL;
        // also remove the client's file descriptor from the select set
        if(FD_ISSET(fileno(c->file), &select_read_set)){
            FD_CLR(fileno(c->file), &select_read_set);
        }

        // if the socket descriptor is in the write set also remove from there
        if(FD_ISSET(c->socket, &select_write_set)){
            FD_CLR(c->socket, &select_write_set);
        }
        // decrement the client count.
        global_client_table->client_count--;
        return c;
    }
    // if the key is the client's file descriptor, remove the file descriptor entry from the table
    if (key == fileno(c->file)){
        global_client_table->clients[key] = NULL;
        // also remove the client's file descriptor from the select set
        if (FD_ISSET(key, &select_read_set)){
            FD_CLR(key, &select_read_set);
        }
        return c;

    }
    // if the key is not the client's socket or file descriptor, return error
    if (DEBUG) fprintf(stderr, "Error removing client from table: key is not a descriptor for given client\n");
    return NULL;
}

/*
 * returns a pointer to the client with the given descriptor key from the client table
 * if the client does not exist in the table, returns NULL
 * if the given descriptor is not in the valid range, returns NULL
 */
client *client_table_get(int descriptor){
    // check that key is valid
    if (descriptor < 4 || descriptor > global_client_table->array_capacity){
        // key is not valid, return error
        if (DEBUG) fprintf(stderr, "Error getting client from table: descriptor is not a valid key\n");
        return NULL;
    }
    // check that client exists in table
    if (global_client_table->clients[descriptor] == NULL){
        // client does not exist in table, return error
        if (DEBUG) fprintf(stderr, "Error getting client from table: client does not exist in table\n");
        return NULL;
    }
    // return the client
    return global_client_table->clients[descriptor];
}

/*
 * returns the highest file descriptor in the client table
 * this is used as the first parameter to select()
 */
int get_highest_fd(){
    int i, highest_fd = global_listening_sd;
    // don't care about stdin, stdout, stderr
    // do care about listening socket on fd 3 tho.
    for (i = 3; i < global_client_table->array_capacity; i++){
        if (global_client_table->clients[i] != NULL){
            if (i > highest_fd){
                highest_fd = i;
            }
        }
    }
    return highest_fd;
}





/* --------------------------------- *
 * -----CLIENT STRUCT FUNCTIONS----- *
 * --------------------------------- */

/*
 * constructs a new client struct with the given parameters
 * allocates memory for the parse and generate buffers
 * params:
 *     socket_fd:               socket file descriptor for the client
 *     file:                    FILE* pointer for the client. this should initially be NULL and set after parse state
 *                              since it is unknown if the client needs to read from a file until after parse state
 *     parse_buffer_size:       number of bytes to be allocated for the parse buffer
 *     generate_buffer_size:    number of bytes to be allocated for the generate buffer
 */
client *construct_client( int socket_fd, FILE* file, int parse_buffer_size, int generate_buffer_size){
    client *c;

    c = malloc(sizeof(client));
    c->state = READ;
    c->socket = socket_fd;
    c->file = file;
    c->parse_buffer = calloc(1, generate_buffer_size);
    c->generate_buffer = calloc(1, generate_buffer_size);
    c->parse_buffer_size = parse_buffer_size;
    c->generate_buffer_size = generate_buffer_size;
    c->parse_buffer_index = 0;
    c->generate_buffer_index = 0;
    c->not_found_flag = 0;
    return c;
}



/*
 * frees the memory allocated for the client struct and its child allocations
 * params:
 *     c:   pointer to the client struct to be freed
 */
void deconstruct_client(client *c) {
    if (c->file != NULL){
        // remove from client table first!!! otherwise file descriptor is set to -1 and you can't get it out of the client table anymore
        client_table_remove(fileno(c->file), c);
        fclose(c->file);
    }
    if (c->socket != -1){
        close(c->socket);
    }
    if (c->parse_buffer != NULL){
        free(c->parse_buffer);
    }
    if (c->generate_buffer != NULL){
        free(c->generate_buffer);
    }
    if (c != NULL){
        free(c);
    }

}



/*
 * this function is called when select returns with the listening socket in the read-ready set.
 * it constructs a new client struct for the requested connection
 * and also puts the client struct in the client table and sets its status to read
 */
int accept_new_client(){
    int                     client_sd;
    struct sockaddr         client_addr = {0};
    socklen_t               client_addr_len = sizeof(client_addr);
    client                  *new_client_entry;

    if (DEBUG_VERBOSE) fprintf(stderr, "select() triggered for listening socket, accepting new client\n");

    client_sd = accept(
            global_listening_sd,                    // listening socket
            &client_addr,                           // client address info
            &client_addr_len);                      // size of client address info struct
    // check for errors returned by accept()
    if (client_sd < 0) {
        // if the error is that there are no new connections in the listen queue: not an error. don't print message.
        // return 0 to skip the new connection logic and error logic.
        if (errno == EWOULDBLOCK || errno == EAGAIN){
            return 0;
        } else if(DEBUG) {
            fprintf(stderr, "Error calling accept() to get new client: %s\n", strerror(errno));
            // if the error is an actual error other than no new connection, print error message
            // print errno error message
        }
        // return -1 if the error is something other than expected (no new connections in queue)
        return -1;
    } else if (client_sd == 0){
        // no new connections in the listen queue
        return 0;
    }

    // if no errors, set socket option for non-blocking to true
    if (fcntl(client_sd, F_SETFL, O_NONBLOCK, 1) < 0)
    {
        if (DEBUG) fprintf(stderr, "fcntl(NONBLOCK) for new client socket failed\n");
        return -1;
    }
    // socket is now set up, construct and add a new entry into the client table
    new_client_entry = construct_client(client_sd, NULL, 1024, 1024);
    client_table_add(client_sd, new_client_entry);
    if(DEBUG_VERBOSE) fprintf(stderr, "New client accepted and added to client table with socket descriptor: %d\n", client_sd);
    return 0;
}





/* -------------------------------- *
 * -----CLIENT STATE FUNCTIONS----- *
 * -------------------------------- */
/*
 * client read function
 */
int client_read(client *c){
    int byte_num;
    // read 1024 bytes from socket into the parse buffer
    // if there isn't enough space in the buffer, realloc the buffer
    byte_num = recv(c->socket, c->parse_buffer + c->parse_buffer_index, c->parse_buffer_size - c->parse_buffer_index, 0);
    if (byte_num < 0){
        // check reason for error
        if (errno == EWOULDBLOCK || errno == EAGAIN){
            // no data to read, return 0
            return 0;
        }
        // error occurred
        if (DEBUG) fprintf(stderr, "Error reading from client on socket %d: %s\n", c->socket,  strerror(errno));
        return -1;
    } else if (byte_num == 0){
        // client closed connection
        if (DEBUG_VERBOSE) fprintf(stderr, "Client on socket %d closed connection\n", c->socket);
        // set client state to exit
        c->state = EXIT;
        // put the client in the write set because select now needs to trigger
        // on this socket regardless of whether it has data to be read
        FD_SET(c->socket, &select_write_set);
        // remove the client from the read set because it is no longer read-ready
        FD_CLR(c->socket, &select_read_set);
        return 0;
    } else {
        // data was read
        if (DEBUG_VERBOSE) fprintf(stderr, "Read %d bytes from client on socket %d\n", byte_num, c->socket);
        // update the parse buffer index
        c->parse_buffer_index += byte_num;
        // if the client read 1024 bytes from the socket and stopped because the buffer was full,
        // realloc the buffer
        if (c->parse_buffer_index == c->parse_buffer_size){
            c->parse_buffer_size += 1024;
            c->parse_buffer = realloc(c->parse_buffer, c->parse_buffer_size);
            return 0;
        }
        // advance client state to parse
        c->state = PARSE;
        // put the client in the write set because select now needs to trigger
        // on this socket regardless of whether it has data to be read
        FD_SET(c->socket, &select_write_set);
        // remove the client from the read set because it is no longer read-ready
        FD_CLR(c->socket, &select_read_set);
        return 0;
    }
    return 0;
}

/*
 * client parse function
 * this function parses the http request from the parse buffer
 * only handles GET requests
 * if the request is not a GET request, return 404 error
 * if the request is a GET request, check if the requested file exists
 * if the file exists, open it and set the client's file pointer to it
 * if the file does not exist, also return 404 error
 * if the file exists, advance the client state to generate (or server exit if quit was requested)
 */
int client_parse(client *c){
    char *request_type, *request_url, *request_line;
    char *filepath;
    char *quit_url = "/json/quit";

    // parse the request line from the parse buffer
    request_line = strtok(c->parse_buffer, "\r\n");
    if (request_line == NULL){
        // no request line found, send 404 error
        c->file = fopen("./json/404.html", "r");
        c->not_found_flag = 1;
    } else {
        // parse the request type from the request line
        request_type = strtok(request_line, " ");
        if (request_type == NULL) {
            // no request type found, send 404 error
            c->file = fopen("./json/404.html", "r");
            c->not_found_flag = 1;
        } else {
            if (strcmp(request_type, "GET") != 0) {
                // request type is not GET, send 404 error
                c->file = fopen("./json/404.html", "r");
                c->not_found_flag = 1;
            } else {
                // parse the request file from the request line
                request_url = strtok(NULL, " ");
                if (request_url == NULL) {
                    // if the request url is just "/"  or nothing then the file path is "/json/implemented.json"
                    c->file = fopen("./json/implemented.json", "r");
                } else {
                    // if the request url is not just "/" then use the rest of it as the file path
                    // first check if the file is in the json directory
                    if (strncmp(request_url, "/json/", 6) == 0) {
                        // file is in the json directory or is the quit request.
                        // check for quit
                        if (strcmp(request_url, quit_url) == 0){
                            // the request is quit, tell the server to quit
                            server_exit();
                        }
                        // the request is not quit, so attempt to open the file
                        filepath = malloc(strlen(request_url) + 2);
                        strcpy(filepath, ".");
                        strcat(filepath, request_url);
                        c->file = fopen(filepath, "r");
                        if (c->file == NULL) {
                            // file is not in the json directory, send 404 error
                            c->file = fopen("./json/404.html", "r");
                            c->state = GENERATE;
                            FD_SET(fileno(c->file), &select_read_set);
                            client_table_add(fileno(c->file), c);
                            FD_CLR(c->socket, &select_write_set);
                            c->not_found_flag = 1;
                        } else {
                            // file was successfully opened. advance client state to generate
                            c->state = GENERATE;
                            FD_SET(fileno(c->file), &select_read_set);
                            client_table_add(fileno(c->file), c);
                            FD_CLR(c->socket, &select_write_set);
                            return 0;
                        }

                    } else {
                        // file is not in the json directory, send 404 error
                        c->file = fopen("./json/404.html", "r");
                        c->not_found_flag = 1;
                    }
                }
            }
        }

    }
    // set client state to generate
    c->state = GENERATE;
    // put the client in the read set because select now
    // needs to trigger when the file is ready to read from
    FD_SET(fileno(c->file), &select_read_set);
    client_table_add(fileno(c->file), c);
    // remove the client from the write set because it is no longer write-ready
    FD_CLR(c->socket, &select_write_set);

    // set descriptor to non blocking
    if (fcntl(fileno(c->file), F_SETFL, O_NONBLOCK, 1) < 0)
    {
        if (DEBUG) fprintf(stderr, "fcntl(NONBLOCK) for new client file descriptor failed\n");
        // set client state to exit to close the connection
        c->state = EXIT;
        // add socket to write set
        FD_SET(c->socket, &select_write_set);
        // remove socket from read set
        FD_CLR(c->socket, &select_read_set);
    }
    return -1;
}

/*
 * client generate function
 */
int client_generate(client *c){
    int byte_num;
    // generate the http header
    // check if the file being read from is the 404.html file
    // generate the body by reading from the file descriptor

    // header step only needs to be done at the beginning of the generate state (when index is 0)
    if(c->generate_buffer_index == 0){
        // generate the http header
        c->generate_buffer_index += sprintf(c->generate_buffer + c->generate_buffer_index, "HTTP/1.1 200 OK\r\n");
        if (c->not_found_flag){
            // if the file is the 404.html file, use the html content type
            c->generate_buffer_index += sprintf(c->generate_buffer + c->generate_buffer_index, "Content-Type: text/html\r\n");
        } else {
            // if the file is a json file, use the json content type
            c->generate_buffer_index += sprintf(c->generate_buffer + c->generate_buffer_index, "Content-Type: application/json\r\n");
        }
        // add the end of header line
        c->generate_buffer_index += sprintf(c->generate_buffer + c->generate_buffer_index, "\r\n");
    }
    // generate the body by reading from the file descriptor
    // read as many bytes as possible from the file descriptor into the generate buffer
    // if the generate buffer is full, realloc it
    byte_num = read(fileno(c->file), c->generate_buffer + c->generate_buffer_index, c->generate_buffer_size - c->generate_buffer_index);

    // if end of file reached, close the file and set the client state to exit
    if (byte_num < c->generate_buffer_size - c->generate_buffer_index && byte_num > 0){
        c->generate_buffer_index += byte_num;
        // end of file reached before end of buffer, close the file and set the client state to WRITE
        // add the end of http response to the generate buffer
        // advance client state to write
        c->state = WRITE;
        FD_CLR(fileno(c->file), &select_read_set);
        // remove file entry from client table first then close the file
        client_table_remove(fileno(c->file), c);
        fclose(c->file);
        // put the client in the write set because select now needs to trigger
        // when the socket is ready to be written to
        FD_SET(c->socket, &select_write_set);
        // remove the client from the read set because it is no longer read-ready
        FD_CLR(c->socket, &select_read_set);
        // add a null terminator to the end of the generated response for the write function
        c->generate_buffer[c->generate_buffer_index] = '\0';
        // reset the generate index to 0 for write
        c->generate_buffer_index = 0;

    //TODO: check if its even possible to do partial reads. examine man page more closely and confirm.
    } else if (byte_num == c->generate_buffer_size - c->generate_buffer_index){
        // end of buffer reached before end of file, realloc the buffer
        c->generate_buffer_index += byte_num;
        c->generate_buffer_size += 1024;
        c->generate_buffer = realloc(c->generate_buffer, c->generate_buffer_size);
        // zero the new memory
        memset(c->generate_buffer + c->generate_buffer_index, 0, 1024);
    } else {
        // error occurred
        if (DEBUG) fprintf(stderr, "Error reading from file descriptor %d: %s\n", fileno(c->file), strerror(errno));
        // set client state to exit to close the connection
        c->state = EXIT;
        if (c->file != NULL){
            client_table_remove(fileno(c->file), c);
            fclose(c->file);
        }
        FD_CLR(fileno(c->file), &select_read_set);
        // add socket to write set
        FD_SET(c->socket, &select_write_set);
        // remove socket from read set
        FD_CLR(c->socket, &select_read_set);
        return -1;
    }

    return 0;
}

/*
 * client write function
 * writes the contents of the generate buffer into the socket
 * if the entire buffer is written, or end of http response is reached
 * if the entire buffer is not written
 */
int client_write(client *c){
    int byte_num;
    uint32_t msg_len;

    // get the message length
    msg_len = strlen(c->generate_buffer);
    // write the message into the socket
    byte_num = send(c->socket, c->generate_buffer + c->generate_buffer_index, msg_len, 0);
    if (byte_num < 0){
        // check reason for error
        if (errno == EWOULDBLOCK || errno == EAGAIN){
            // no data to write, return 0
            return 0;
        }
        // error occurred
        if (DEBUG) fprintf(stderr, "Error writing to client on socket %d: %s\n", c->socket,  strerror(errno));
        // set client state to exit to close the connection
        c->state = EXIT;
        if (c->file != NULL){
            client_table_remove(fileno(c->file), c);
            fclose(c->file);
        }
        FD_CLR(fileno(c->file), &select_read_set);
        // add socket to write set
        FD_SET(c->socket, &select_write_set);
        // remove socket from read set
        FD_CLR(c->socket, &select_read_set);
        return -1;
    } else if (byte_num == 0){
        // client closed connection
        if (DEBUG_VERBOSE) fprintf(stderr, "Client on socket %d closed connection\n", c->socket);
        // set client state to exit to close the connection
        c->state = EXIT;
        // add socket to write set
        FD_SET(c->socket, &select_write_set);
        // remove socket from read set
        FD_CLR(c->socket, &select_read_set);
        return 0;
    } else {
        // data was written
        if (DEBUG_VERBOSE) fprintf(stderr, "Wrote %d bytes to client on socket %d\n", byte_num, c->socket);
        // update the generate buffer index
        c->generate_buffer_index += byte_num;
        // if the client wrote the entire generate buffer, advance the client state to exit
        if (c->generate_buffer_index == byte_num) {
            // full buffer written to socket. request fulfilled, close the connection.
            c->generate_buffer_index = 0;
            // advance client state to exit
            c->state = EXIT;
            // add socket to write set
            FD_SET(c->socket, &select_write_set);
            // remove socket from read set
            FD_CLR(c->socket, &select_read_set);
            return 0;
        }
    }
    return 0;
}

int client_exit(client *c){
    // client should be removed from select sets by client_table_remove(), however, I am paranoid

    // remove the client from the client table
    client_table_remove(c->socket, c);
    // close the client's socket if it exists
    if (c->socket != -1){
        close(c->socket);
        FD_CLR(c->socket, &select_read_set);
    }
    // clean up client's file descriptor if it exists
    if (c->file != NULL){
        client_table_remove(fileno(c->file), c);
        fclose(c->file);
        FD_CLR(fileno(c->file), &select_read_set);
    }
    if(DEBUG_VERBOSE) fprintf(stderr, "Client on socket %d in EXIT state. closing server connection.\n", c->socket);
    // free the client struct
    deconstruct_client(c);
    return 0;
}




/* -------------------------------- *
 * ----------POLL CLIENTS---------- *
 * -------------------------------- */

/*
 * this function poll's sockets and fds to see if they are ready to read from or write to
 * (this includes the server listening socket).
 * the function then calls state specific functions to perform computations for clients associated
 * with those sockets or file descriptors depending on the client's state
 */

int poll_clients(){
    int ready_num, i, highest_fd = -1;
    client* current_client;
    // poll for all read-ready socket descriptors
    highest_fd = get_highest_fd();
    ready_num = select(highest_fd + 1, &select_read_set, &select_write_set, NULL, 0);

    // select will return whenever a client or the listening socket is ready to be read from
    if (ready_num == -1) {
        // error occurred
        if (DEBUG) fprintf(stderr, "Error selecting for read-ready socket descriptors\n");
    }
    if(DEBUG_VERBOSE) fprintf(stderr, "select returned with %d ready descriptors\n", ready_num);
    // ret contains the number of ready descriptors in the read_set
    // check if the listening socket is read ready
    if (FD_ISSET(global_listening_sd, &select_read_set)) {
        // if the listening socket is read-ready, it has a client to accept
        // call accept new client
        accept_new_client();
        // accept_new_client() should have printed to stderr if an error occured
        // new client accepted and added to client table
    } else {
        // if the server's listening socket is not in the read set, then re-add it
        FD_SET(global_listening_sd, &select_read_set);
    }
    // loop through clients and see if they are in the read-ready set
    for (i = 4; i < global_client_table->array_capacity; i++){
        current_client = global_client_table->clients[i];
        if (current_client != NULL){
            // client exists for this key, switch on it's state
            switch (current_client->state){
                case READ:
                    // depends on socket status being read-ready
                    // independent of file descriptor status

                    // check if the file descriptor key is in the read-ready set
                    if (FD_ISSET(i, &select_read_set)){
                        // if it is read-ready check that it is the client's socket descriptor
                        if (current_client->socket == i){
                            if(DEBUG_VERBOSE) fprintf(stderr, "select pulled read_set client in READ state on socket %d\n", current_client->socket);
                            client_read(current_client);
                        }
                        // if the read function advanced the client to the next state,
                        // it will have removed the socket from the read set and added it to the write set
                    } else {
                        // the socket was removed from the set by select() due to not being read-ready.
                        // add the socket back into the set for the next select() call.
                        FD_SET(current_client->socket, &select_read_set);
                    }
                    break;

                case PARSE:
                    // independent of socket read status
                    // independent of file descriptor status
                    // if a client is in the parse state its socket will likely be empty
                    // (or at least the server doesn't care what's in there yet)
                    // so it will not be flagged by select() if in the read set.
                    // but it should be flagged by select() if in the write set.
                    if (FD_ISSET(i, &select_write_set)){
                        if (current_client->socket == i) {
                            if(DEBUG_VERBOSE) fprintf(stderr, "select pulled write_set client in PARSE state on socket %d\n", current_client->socket);
                            client_parse(current_client);
                        }

                    } else{
                        // the socket was removed from the set by select() due to not being write-ready.
                        // add the socket back into the set for the next select() call.
                        FD_SET(current_client->socket, &select_write_set);
                    }
                    break;

                case GENERATE:
                    // independent of socket status
                    // depends on file descriptor status being read-ready

                    // file descriptor is created at the end of the parse set for the correct file to read from
                    // it is also added to the select_read_set but if that fails it will also be added here
                    if (FD_ISSET(i, &select_read_set)){
                        // if the current descriptor is read-ready,
                        // check that it is the current client's file descriptor
                        if(fileno(current_client->file) == i){
                            if(DEBUG_VERBOSE) fprintf(stderr, "select pulled read_set client in GENERATE state on socket %d\n", current_client->socket);
                            client_generate(current_client);
                        }
                    } else {
                        // file descriptor was removed from select_read_set by select() due to not being read-ready.
                        // add it back into the set for the next select() call.
                        FD_SET(i, &select_read_set);
                        // for clients that just entered the generate state:
                        // file descriptor is created at the end of the parse set for the correct file to read from
                        // it is also added to the select_read_set but if that fails it will also be added here
                    }
                    break;

                case WRITE:
                    // depends on socket status being read-ready
                    // independent of file descriptor status

                    // check if the client's socket is in the write-ready set
                    if(FD_ISSET(i, &select_write_set)){
                        // if the socket is write-ready, check that this is the client's socket descriptor
                        if(current_client->socket == i){
                            if(DEBUG_VERBOSE) fprintf(stderr, "select pulled write_set client in WRITE state on socket %d\n", current_client->socket);
                            client_write(current_client);
                        }
                    } else {
                        // the socket was removed from select_write_set by select() due to not being write-ready.
                        // add the socket back into the set for the next select() call.
                        FD_SET(current_client->socket, &select_write_set);
                    }
                    break;

                case EXIT:
                    if (FD_ISSET(i, &select_write_set)){
                        if (current_client->socket == i) {
                            if(DEBUG_VERBOSE) fprintf(stderr, "select pulled write_set client in EXIT state on socket %d\n", current_client->socket);
                            client_exit(current_client);
                        }

                    } else{
                        // the socket was removed from the set by select() due to not being write-ready.
                        // add the socket back into the set for the next select() call.
                        FD_SET(current_client->socket, &select_write_set);
                    }
                default:
                    break;
            }
        }
    }
    // re-add all fds to the select sets since they were removed by select if they weren't ready
    return 0;
}





/* --------------------------------- *
 * -----LISTEN SOCKET FUNCTIONS----- *
 * --------------------------------- */
/*
 * this function initializes a listening socket bound to the given address
 * it returns -1 if an error occurs
 * otherwise, it fills out the global server_addrinfo struct with the address info for the socket
 */
int init_listening_socket_specified(char* address_arg) {
    char ipv4_str[INET_ADDRSTRLEN];
    char ipv6_str[INET6_ADDRSTRLEN];
    unsigned int host_order_port;
    struct sockaddr_in input_addr, *server_addr = NULL;
    struct sockaddr_in6 input_addr6, *server_addr6 = NULL;
    socklen_t len;
    int socket_fd = -1, error, option_on = 1;

    // clear the input sructs
    memset(&input_addr, 0, sizeof(input_addr));
    memset(&input_addr6, 0, sizeof(input_addr6));

    if (DEBUG_VERBOSE) fprintf(stderr, "Address specified in args. Initializing listening socket on %s\n", address_arg);
    // check if the address is ipv4
    if (inet_pton(AF_INET, address_arg, &(input_addr.sin_addr)) == 1)
    {
        if(DEBUG_VERBOSE) fprintf(stderr, "Address specified in args is ipv4\n");
        // create socket
        error = (socket_fd = socket(AF_INET, SOCK_STREAM, 0));
        if (error < 0) {
            perror("Can't open stream socket.");
            return -1;
        }

        // set input_addr
        input_addr.sin_family = AF_INET;
        input_addr.sin_addr.s_addr = inet_addr(address_arg);
        input_addr.sin_port = 0;

        // bind using input_addr
        error = bind(socket_fd, (struct sockaddr *) &input_addr, sizeof(input_addr));
        if (error < 0) {
            perror("bind() failed");
            close(socket_fd);
            return -1;
        }

        // getsockname to get the port number used by bind().
        server_addr = malloc(sizeof(struct sockaddr_in));
        memset(server_addr, 0, sizeof(*server_addr));
        len = sizeof(*server_addr);
        getsockname(socket_fd, (struct sockaddr *) server_addr, &len);
    } else if (inet_pton(AF_INET6, address_arg, &(input_addr6.sin6_addr)) == 1){
        if(DEBUG_VERBOSE) fprintf(stderr, "Address specified in args is ipv6\n");
        // create socket
        error = (socket_fd = socket(AF_INET6, SOCK_STREAM, 0));
        if (error < 0) {
            perror("Can't open stream socket.");
            return -1;
        }

        // set input_addr
        input_addr6.sin6_family = AF_INET6;
//        input_addr6.sin6_addr should already be set by the inet_pton call.
        input_addr6.sin6_port = 0;

        // bind using input_addr
        error = bind(socket_fd, (struct sockaddr *) &input_addr6, sizeof(input_addr6));
        if (error < 0) {
            perror("bind() failed");
            close(socket_fd);
            return -1;
        }

        // getsockname to get the port number used by bind().
        server_addr6 = malloc(sizeof(struct sockaddr_in6));
        memset(server_addr6, 0, sizeof(*server_addr6));
        len = sizeof(*server_addr6);
        getsockname(socket_fd, (struct sockaddr *) server_addr6, &len);
    } else {
        // address is not ipv4 or ipv6
        if (DEBUG) fprintf(stderr, "Error: address specified in args is not a valid ipv4 or ipv6 address\n");
        return -1;
    }

    if(DEBUG_VERBOSE){
        fprintf(stderr, "getsockname() returned struct for bound address:\n");
        if (server_addr != NULL){
            fprintf(stderr, "\tAddress Family: %d\n", server_addr->sin_family);
            inet_ntop(AF_INET, &server_addr->sin_addr, ipv4_str, sizeof(ipv4_str));
            fprintf(stderr, "\tAddress: ");
            fprintf(stderr, "%s\n", ipv4_str);
            host_order_port = ntohs(server_addr->sin_port);
            fprintf(stderr, "\tPort: %d\n", host_order_port);
        } else if (server_addr6 != NULL){
            fprintf(stderr, "\tAddress Family: %d\n", server_addr6->sin6_family);
            inet_ntop(AF_INET6, &server_addr6->sin6_addr, ipv6_str, sizeof(ipv6_str));
            fprintf(stderr, "\tAddress: ");
            fprintf(stderr, "%s\n", ipv6_str);
            host_order_port = ntohs(server_addr6->sin6_port);
            fprintf(stderr, "\tPort: %d\n", host_order_port);
        }
    }


    // set global_server_addrinfo to the address info struct returned by getsockname
    if (server_addr != NULL){
        global_server_addrinfo = malloc(sizeof(struct addrinfo));
        global_server_addrinfo->ai_family = AF_INET;
        global_server_addrinfo->ai_socktype = SOCK_STREAM;
        global_server_addrinfo->ai_protocol = server_addr->sin_port;
        global_server_addrinfo->ai_addrlen = sizeof(*server_addr);
        global_server_addrinfo->ai_addr = (struct sockaddr *) server_addr;
        global_server_addrinfo->ai_canonname = NULL;
    } else if (server_addr6 != NULL){
        global_server_addrinfo = malloc(sizeof(struct addrinfo));
        global_server_addrinfo->ai_family = AF_INET6;
        global_server_addrinfo->ai_socktype = SOCK_STREAM;
        global_server_addrinfo->ai_protocol = server_addr6->sin6_port;
        global_server_addrinfo->ai_addrlen = sizeof(*server_addr6);
        global_server_addrinfo->ai_addr = (struct sockaddr *) server_addr6;
        global_server_addrinfo->ai_canonname = NULL;

    }

    // set socket option for non-blocking to true
    if (fcntl(socket_fd, F_SETFL, O_NONBLOCK, 1) < 0)
    {
        close(socket_fd);
        perror("fcntl(NONBLOCK) failed");
    }
    // set socket option to allow for reuse of the address immediately after closing. (good for testing)
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR,
                   (char *)&option_on,sizeof(option_on)) < 0)
    {
        close(socket_fd);
        perror("setsockopt(SO_REUSEADDR) failed");
    }

    return socket_fd;
}
/*
 * this function initializes a listening socket bound to all interfaces on the first available port
 * it returns -1 if an error occurs
 * otherwise, it fills out the global server_addrinfo struct with the address info for the socket
 */
int init_listening_socket_any(){
    char ipv4_str[INET_ADDRSTRLEN];
    unsigned int host_order_port;
    struct sockaddr_in input_addr, *my_addr;
    socklen_t len;
    int socket_fd, error, option_on = 1;
    if (DEBUG) fprintf(stderr, "Address not specified in args. Initializing listening socket on all interfaces\n");
    // create socket
    error = (socket_fd = socket(AF_INET, SOCK_STREAM, 0));
    if (error < 0) {
        perror("Can't open stream socket.");
        return -1;
    }

    // set input_addr
    memset(&input_addr, 0, sizeof(input_addr));
    input_addr.sin_family = AF_INET;
    input_addr.sin_addr.s_addr = AF_UNSPEC;
    input_addr.sin_port = 0;

    // bind using input_addr
    error = bind(socket_fd, (struct sockaddr *) &input_addr, sizeof(input_addr));
    if (error < 0) {
        perror("bind() failed");
        close(socket_fd);
        return -1;
    }

    // getsockname to get the port number used by bind().
    my_addr = malloc(sizeof(struct sockaddr_in));
    memset(my_addr, 0, sizeof(*my_addr));
    len = sizeof(*my_addr);
    getsockname(socket_fd, (struct sockaddr *) my_addr, &len);


    if(DEBUG_VERBOSE){
        fprintf(stderr, "getsockname() returned struct for bound address:\n");
        fprintf(stderr, "\tAddress Family: %d\n", my_addr->sin_family);

        inet_ntop(AF_INET, &my_addr->sin_addr, ipv4_str, sizeof(ipv4_str));
        fprintf(stderr, "\tAddress: ");
        fprintf(stderr, "%s\n", ipv4_str);
        host_order_port = ntohs(my_addr->sin_port);
        fprintf(stderr, "\tPort: %d\n", host_order_port);
    }


    // set global_server_addrinfo to the address info struct returned by getsockname
    global_server_addrinfo = malloc(sizeof(struct addrinfo));
    global_server_addrinfo->ai_family = AF_INET;
    global_server_addrinfo->ai_socktype = SOCK_STREAM;
    global_server_addrinfo->ai_protocol = my_addr->sin_port;
    global_server_addrinfo->ai_addrlen = sizeof(*my_addr);
    global_server_addrinfo->ai_addr = (struct sockaddr *) my_addr;
    global_server_addrinfo->ai_canonname = NULL;


    // set socket option for non-blocking to true
    if (fcntl(socket_fd, F_SETFL, O_NONBLOCK, 1) < 0)
    {
        close(socket_fd);
        perror("fcntl(NONBLOCK) failed");
    }
    // set socket option to allow for reuse of the address immediately after closing. (good for testing)
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR,
                   (char *)&option_on,sizeof(option_on)) < 0)
    {
        close(socket_fd);
        perror("setsockopt(SO_REUSEADDR) failed");
    }

    return socket_fd;
}

/*
 * calls one of two functions to bind a socket for listening using the given address.
 * port is dynamically allocated, not given as a parameter
 * if no parameters are given, calls init_listening_socket_any() to bind to all interfaces
 * if one parameter is given, calls init_listening_socket_specified() to bind to the specified address
 *  params:
 *      argc        Number of arguments passed into main() from the command line.
 *      argv        Array of arguments. if argc is 2, this contains the specified address to bind to.
 *                  if argc is 1, this contains the program name.
 */
int init_listening_socket(int argc, char *argv[]){
    int         socket_fd = -1, port = -1;
    // if no arguments are given, use any address
    if (argc == 1){
        socket_fd = init_listening_socket_any();
    }
    // if one argument is given, use the specified address
    else if (argc == 2){
        socket_fd = init_listening_socket_specified(argv[1]);
    }
    // if more than one argument is given, print usage message and exit
    else{
        printf("Usage: json-server [address]\n");
        exit(1);
    }

    // set global socket fd to the socket fd
    if (socket_fd < 1){
        if (DEBUG) fprintf(stderr, "Error initializing socket from bound address\n");
        return -1;
    }

    global_listening_sd = socket_fd;


    //debug message to print out the global address info struct


    if (global_server_addrinfo->ai_family == AF_INET){
        port = ntohs(((struct sockaddr_in *) global_server_addrinfo->ai_addr)->sin_port);
    } else if (global_server_addrinfo->ai_family == AF_INET6){
        port = ntohs(((struct sockaddr_in6 *) global_server_addrinfo->ai_addr)->sin6_port);
    }

    global_server_addrinfo->ai_protocol = port;

    if(DEBUG_VERBOSE) {
        fprintf(stderr, "global server addr info is as follows:\n");
        fprintf(stderr, "\tAddress Family: %d\n", global_server_addrinfo->ai_family);
        int host_order_port;
        char ipv4_str[INET_ADDRSTRLEN];
        char ipv6_str[INET6_ADDRSTRLEN];
        if (global_server_addrinfo->ai_family == AF_INET){
            inet_ntop(AF_INET, &(((struct sockaddr_in *) global_server_addrinfo->ai_addr)->sin_addr), ipv4_str, sizeof(ipv4_str));
            fprintf(stderr, "\tAddress: ");
            fprintf(stderr, "%s\n", ipv4_str);
            host_order_port = ntohs(((struct sockaddr_in *) global_server_addrinfo->ai_addr)->sin_port);
            fprintf(stderr, "\tPort: %d\n", host_order_port);
        } else if (global_server_addrinfo->ai_family == AF_INET6){
            inet_ntop(AF_INET6, &(((struct sockaddr_in6 *) global_server_addrinfo->ai_addr)->sin6_addr), ipv6_str, sizeof(ipv6_str));
            fprintf(stderr, "\tAddress: ");
            fprintf(stderr, "%s\n", ipv6_str);
            host_order_port = ntohs(((struct sockaddr_in6 *) global_server_addrinfo->ai_addr)->sin6_port);
            fprintf(stderr, "\tPort: %d\n", host_order_port);
        }

    }
    // get the port number from the address info struct
    // print out the socket open message
    printf("HTTP server is using TCP port %d\n"
           "HTTPS server is using TCP port -1\n", port);
    fflush(stdout);
    return 0;
}





/* -------------------------- *
 * -----server functions----- *
 * -------------------------- */

void server_exit(){
    // close all open files
    // close global listening socket
    if (global_listening_sd != -1) {
        close(global_listening_sd);
    }
    // close all sockets in client table and free client table
    if (global_client_table != NULL) {
        deconstruct_client_table(global_client_table);
    }
    // if server_addrinfo is not null, free it
    if (global_server_addrinfo != NULL){
        free(global_server_addrinfo);
    }
    // exit cleanly
    printf("Server exiting cleanly.");
    exit(0);
}

void sigint_handler(int sig)
{
    if (SIGINT == sig || SIGTERM == sig) {
        server_exit();
    }
}

int initialize_sigint(){
    struct sigaction sa;
    sa.sa_handler = &sigint_handler;
    sigfillset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (-1 == sigaction(SIGINT, &sa, NULL))
    {
        perror("Couldn't set signal handler for SIGINT");
        return 2;
    }
    if (-1 == sigaction(SIGTERM, &sa, NULL))
    {
        perror("Couldn't set signal handler for SIGTERM\n");
        return 2;
    }
    return 0;
}






/* ------------------------------- *
 * -----MAIN PROGRAM FUNCTION----- *
 * ------------------------------- */

int main(int argc, char *argv[]) {
    int     error;

    /*---program initialization---*/
    // set up sigint handler
    if ((initialize_sigint()) != 0){
        fprintf (stderr, "Couldn't initialize sigint handler\n");
        server_exit();
    }

    // set up listening socket
    global_server_addrinfo = malloc(sizeof(struct sockaddr_in6));
    error = init_listening_socket(argc, argv);
    if (error != 0){
        server_exit();
    }

    // add listening socket to read_set
    FD_SET(global_listening_sd, &select_read_set);
    
    // initialize client table
    global_client_table = construct_client_table();
    if (global_client_table == NULL){
        fprintf(stderr, "Couldn't initialize client table\n");
        server_exit();
    }

    // kick off listening for connection requests on the server listening socket
    error = listen(global_listening_sd, 8);
    if (error == -1){
        // error occurred print it to sterror
        if (DEBUG) fprintf(stderr, "Error listening for new connections: %s\n", strerror(errno));
    }

    /*---main loop---*/
    while (1){
        poll_clients();
    }
    return 0;
}
