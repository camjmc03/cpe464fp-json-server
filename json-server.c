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
//#include "smartalloc.h"

#define DEBUG 1
#define DEBUG_VERBOSE 1



/* ----------------------------- *
 * -----GLOBAL DEFINITIONS------ *
 * ----------------------------- */
client_table *global_client_table = NULL;
int global_listening_port = -1;
int global_listening_sd = -1;
fd_set select_write_set, select_read_set;



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
        // also remove the client's socket from the select set
        FD_CLR(key, &select_read_set);
        // also remove the client's file descriptor entry if it exists
        if (c->file != NULL){
            if (global_client_table->clients[fileno(c->file)] != NULL){
                global_client_table->clients[fileno(c->file)] = NULL;
                // also remove the client's file descriptor from the select set
                if(FD_ISSET(fileno(c->file), &select_read_set)){
                    FD_CLR(fileno(c->file), &select_read_set);
                }
            }
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
    c->response = 0;
    c->quit_flag = 0;
    return c;
}



/*
 * frees the memory allocated for the client struct and its child allocations
 * params:
 *     c:   pointer to the client struct to be freed
 */
void deconstruct_client(client *c) {
    if (c->file != NULL){
        // double-check that the file entry is removed from client table first!!!
        // otherwise file descriptor is set to -1 and you can't get it out of the client table anymore
        client_table_remove(fileno(c->file), c);
        fclose(c->file);
        c->file = NULL;
    }
    close(c->socket);
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
        // if the client reads an end of http request header line, advance the client state to parse
        if (c->parse_buffer[c->parse_buffer_index - 1] == '\n'
        && c->parse_buffer[c->parse_buffer_index - 2] == '\r'
        && c->parse_buffer[c->parse_buffer_index - 3] == '\n'
        && c->parse_buffer[c->parse_buffer_index - 4] == '\r'){
            c->state = PARSE;
            // put the client in the write set because select now needs to trigger
            // on this socket regardless of whether it has data to be read
            FD_SET(c->socket, &select_write_set);
            // remove the client from the read set because it is no longer read-ready
            FD_CLR(c->socket, &select_read_set);
            return 0;
        } else {
            // client did not read an end of file character, return 0
            // continue reading from the client
            return 0;
        }
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
    char *quit_url = "/json/quit";
    char *about_url = "/json/about.json";
    char *implemented_url = "/json/implemented.json";
//    char *not_found_url = "/html/404.html";
   c->response  = 0;
    // parse the request line token from the parse buffer
    request_line = strtok(c->parse_buffer, "\r\n");
    if (request_line == NULL){
        // no request line found, send 404 error
        c->response = 0;
    } else {
        // parse the request type token from the request line
        request_type = strtok(request_line, " ");
        if (request_type == NULL) {
            // no request type found, send 404 error
            c->response = 0;
        } else {
            if (strcmp(request_type, "GET") != 0) {
                // request type is not GET, send 404 error
                c->response = 0;
            } else {
                // parse the request file token from the request line
                request_url = strtok(NULL, " ");
                if (request_url == NULL) {
                    // no request file found, send 404 error
                    c->response = 0;
                } else {
                    // check if the request file is the quit request
                    if (strcmp(request_url, quit_url) == 0) {
                        // the request is quit, set the quit flag and move on to generate
                        c->response = 1;

                    }
                    // check if the request file is the about request
                    if (strcmp(request_url, about_url) == 0) {
                        // the request is about, set the about flag and move on to generate
                        c->response = 2;

                    }
                    // check if the request file is the implemented request
                    if (strcmp(request_url, implemented_url) == 0) {
                        // the request is implemented, set the implemented flag and move on to generate
                        c->response = 3;

                    }
                    // if the request is not quit, about, or implemented, it is not supported and should return 404.

                }
            }
        }
    }

    // set client state to generate
    c->state = GENERATE;
    // not using files or file descriptors as triggers
    // put the client's socket in the write set for select to trigger on
    FD_SET(c->socket, &select_write_set);
    // remove the client from the read set because it is no longer read-ready
    FD_CLR(c->socket, &select_read_set);

    return 0;
}

/*
 * client generate function
 */
int client_generate(client *c){
    char *implemented_json, *about_json, *quit_json, *not_found_html;
    char implemented_response[1024], about_response[1024], quit_response[1024], not_found_response[1024];
    implemented_json =      "[\n"
                            "{ \n\"feature\": \"about\",\n\"URL\": \"/json/about.json\"},{ \"feature\": \"quit\",\n\"URL\": \"/json/quit\"},]\n";

    about_json =            "{\n"
                            "  \"author\": \"John Bellardo\",  \"email\": \"bellardo@calpoly.edu\",  \"major\": \"CSC\"}\n";

    quit_json =             "{\n"
                            "  \"result\": \"success\"\n"
                            "}\n";

    not_found_html =        "<HTML>"
                            "<HEAD>"
                            "<TITLE>HTTP ERROR 404</TITLE>"
                            "</HEAD>"
                            "<BODY>404 Not Found. Your request could not be completed due to encountering HTTP error number 404."
                            "</BODY>"
                            "</HTML>";


    sprintf(implemented_response,
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %lu\r\n\r\n%s",
            strlen(implemented_json),
            implemented_json);


    sprintf(about_response,
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %lu\r\n\r\n%s",
            strlen(about_json),
            about_json);


    sprintf(quit_response,
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %lu\r\n\r\n%s",
            strlen(quit_json),
            quit_json);
    sprintf(not_found_response,
            "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\nContent-Length %lu\r\n\r\n%s",
            strlen(not_found_html),
            not_found_html);

    switch (c->response) {
        case 0:
            // not found
            c->generate_buffer_index += sprintf(c->generate_buffer , "%s", not_found_response);
            if(DEBUG_VERBOSE) fprintf(stderr, "Sending 404 response to client on socket %d\n", c->socket);
            break;
        case 1:
            // quit
            c->generate_buffer_index += sprintf(c->generate_buffer, "%s", quit_response);
            if(DEBUG_VERBOSE) fprintf(stderr, "Sending quit.json response to client on socket %d and exiting server\n", c->socket);
            c->quit_flag = 1;
            break;
        case 2:
            // about
            c->generate_buffer_index += sprintf(c->generate_buffer, "%s", about_response);
            if(DEBUG_VERBOSE) fprintf(stderr, "Sending about.json response to client on socket %d\n", c->socket);
            break;
        case 3:
            // implemented
            c->generate_buffer_index += sprintf(c->generate_buffer, "%s", implemented_response);
            if(DEBUG_VERBOSE) fprintf(stderr, "Sending implemented.json response to client on socket %d\n", c->socket);
            break;
        default:
            // not found
            c->generate_buffer_index += sprintf(c->generate_buffer, "%s", not_found_response);
            if(DEBUG_VERBOSE) fprintf(stderr, "Sending 404 response to client on socket %d\n", c->socket);
            break;
    }
    // advance client state to write
    c->state = WRITE;
    // reset the buffer index for the write function
    c->generate_buffer_index = 0;
    // if the client is in the write set, keep it there, if not, add it
    // no longer using files or file descriptors as triggers
    // put the client's socket in the write set for select to trigger on
    if (!FD_ISSET(c->socket, &select_write_set)){
        FD_SET(c->socket, &select_write_set);
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
    int byte_num = -1;
    size_t msg_len;

    // get the message length
    msg_len = strlen(c->generate_buffer);
    if(DEBUG_VERBOSE) fprintf(stderr, "Message length %lu bytes to client on socket %d\n", msg_len, c->socket);
    // write the message into the socket
    byte_num = send(c->socket, c->generate_buffer + c->generate_buffer_index, msg_len - c->generate_buffer_index, 0);
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
        // add socket to write set
        FD_SET(c->socket, &select_write_set);
        // remove socket from read set
        FD_CLR(c->socket, &select_read_set);
        return -1;
    } else  if (byte_num == 0){
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
        // data was written to socket.
        if (DEBUG_VERBOSE) fprintf(stderr, "Wrote %d bytes to client on socket %d\n", byte_num, c->socket);
        // update the generate buffer index
        c->generate_buffer_index += byte_num;

        if (c->generate_buffer_index == msg_len) {
            // full message written to socket. request fulfilled, close the connection.

            // reset the generate buffer index
            c->generate_buffer_index = 0;
            // advance client state to exit
            c->state = EXIT;
            // add socket to write set
            FD_SET(c->socket, &select_write_set);
            // remove socket from read set
            FD_CLR(c->socket, &select_read_set);
            return 0;
        } else if(c->generate_buffer_index > msg_len){
            // this should never happen, but just in case
            if (DEBUG) fprintf(stderr, "Error writing to client on socket %d: wrote more bytes than message length\n", c->socket);
            // reset the generate buffer index
            c->generate_buffer_index = 0;
            // advance client state to exit
            c->state = EXIT;
            // add socket to write set
            FD_SET(c->socket, &select_write_set);
            // remove socket from read set
            FD_CLR(c->socket, &select_read_set);
            return -1;
        } else {
            // full message not written to socket. continue writing.
            return 0;
        }
        // if the client did not write the entire message, return 0 to keep the client in the write set
        // so that select will trigger again when the socket is ready to be written to again
    }
    return 0;
}

int client_exit(client *c){
    int     q_flag = 0;
    q_flag = c->quit_flag;
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
        c->file = NULL;
        FD_CLR(fileno(c->file), &select_read_set);
    }
    if(DEBUG_VERBOSE) fprintf(stderr, "Client on socket %d in EXIT state. closing server connection.\n", c->socket);
    // free the client struct
    deconstruct_client(c);
    if (q_flag == 1){
        server_exit();
    }
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
                    if (FD_ISSET(i, &select_write_set)){
                        // if the current descriptor is read-ready,
                        // check that it is the current client's file descriptor
                        if(current_client->socket == i){
                            if(DEBUG_VERBOSE) fprintf(stderr, "select pulled write_set client in GENERATE state on socket %d\n", current_client->socket);
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




    // set socket option for non-blocking to true
    if (fcntl(socket_fd, F_SETFL, O_NONBLOCK, 1) < 0)
    {
        close(socket_fd);
        if (server_addr != NULL){
            free(server_addr);
        } else if (server_addr6 != NULL){
            free(server_addr6);
        }
        perror("fcntl(NONBLOCK) failed");
    }
    // set socket option to allow for reuse of the address immediately after closing. (good for testing)
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR,
                   (char *)&option_on,sizeof(option_on)) < 0)
    {
        close(socket_fd);
        close(socket_fd);
        if (server_addr != NULL){
            free(server_addr);
        } else if (server_addr6 != NULL){
            free(server_addr6);
        }
        perror("setsockopt(SO_REUSEADDR) failed");
    }

    // set global variables to the address info struct returned by getsockname
    global_listening_sd = socket_fd;
    if (server_addr != NULL){
        global_listening_port = ntohs(server_addr->sin_port);
    } else if (server_addr6 != NULL){
        global_listening_port = ntohs(server_addr6->sin6_port);
    }
    // free the address info struct since it is no longer needed
    if (server_addr != NULL){
        free(server_addr);
    } else if (server_addr6 != NULL){
        free(server_addr6);
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
    struct sockaddr_in6 input_addr, *server_addr6;
    socklen_t len;
    int socket_fd, error, option_on = 1;
    if (DEBUG) fprintf(stderr, "Address not specified in args. Initializing listening socket on all interfaces\n");
    // create socket
    error = (socket_fd = socket(AF_INET6, SOCK_STREAM, 0));
    if (error < 0) {
        perror("Can't open stream socket.");
        return -1;
    }

    // set input_addr
    memset(&input_addr, 0, sizeof(input_addr));
    input_addr.sin6_family = AF_INET6;
    input_addr.sin6_addr = in6addr_any;
    input_addr.sin6_port = 0;

    // bind using input_addr
    error = bind(socket_fd, (struct sockaddr *) &input_addr, sizeof(input_addr));
    if (error < 0) {
        perror("bind() failed");
        close(socket_fd);
        return -1;
    }

    // getsockname to get the port number used by bind().
    server_addr6= malloc(sizeof(struct sockaddr_in6));
    memset(server_addr6, 0, sizeof(*server_addr6
    ));
    len = sizeof(*server_addr6);
    getsockname(socket_fd, (struct sockaddr *) server_addr6, &len);


    if(DEBUG_VERBOSE){
        fprintf(stderr, "getsockname() returned struct for bound address:\n");
        fprintf(stderr, "\tAddress Family: %d\n", server_addr6->sin6_family);

        inet_ntop(AF_INET, &server_addr6->sin6_addr, ipv4_str, sizeof(ipv4_str));
        fprintf(stderr, "\tAddress: ");
        fprintf(stderr, "%s\n", ipv4_str);
        host_order_port = ntohs(server_addr6->sin6_port);
        fprintf(stderr, "\tPort: %d\n", host_order_port);
    }

    


    // set socket option for non-blocking to true
    if (fcntl(socket_fd, F_SETFL, O_NONBLOCK, 1) < 0)
    {
        close(socket_fd);
        free(server_addr6);
        perror("fcntl(NONBLOCK) failed");
    }
    // set socket option to allow for reuse of the address immediately after closing. (good for testing)
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR,
                   (char *)&option_on,sizeof(option_on)) < 0)
    {
        close(socket_fd);
        free(server_addr6);
        perror("setsockopt(SO_REUSEADDR) failed");
    }

    // set global variables using the info in the address info struct returned by getsockname
    global_listening_sd = socket_fd;
    global_listening_port = ntohs(server_addr6->sin6_port);
    // free the address info struct since it is no longer needed
    free(server_addr6);

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
        // sets the global listening sd and the global listening port variables
    }
    // if one argument is given, use the specified address
    else if (argc == 2){
        socket_fd = init_listening_socket_specified(argv[1]);
        // sets the global listening sd and the global listening port variables

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

    port = global_listening_port;

    //debug message to print out the global address info struct

    
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
    // close global listening socket
    close(global_listening_sd);
    // exit cleanly
    printf("Server exiting cleanly.\n");
    fflush(stdout);
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
