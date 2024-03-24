#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <bson/bson.h>
#include <mongoc/mongoc.h>

#define PORT 8080

mongoc_client_t *client;
mongoc_collection_t *collection;

void connect_to_mongodb() {
    mongoc_init();

    client = mongoc_client_new("mongodb://localhost:27017");
    collection = mongoc_client_get_collection(client, "my_database", "my_collection");
}

void save_to_mongodb(const char *data) {
    bson_error_t error;
    bson_t *doc;

    doc = bson_new_from_json((const uint8_t *)data, -1, &error);
    if (!doc) {
        fprintf(stderr, "Failed to parse JSON: %s\n", error.message);
        return;
    }

    if (!mongoc_collection_insert_one(collection, doc, NULL, NULL, &error)) {
        fprintf(stderr, "Failed to insert document: %s\n", error.message);
    }

    bson_destroy(doc);
}

void disconnect_from_mongodb() {
    mongoc_collection_destroy(collection);
    mongoc_client_destroy(client);
    mongoc_cleanup();
}

void send_command(int sockfd, const char *command) {
    write(sockfd, command, strlen(command));
}

void read_response(int sockfd, char *buffer, int buffer_size) {
    read(sockfd, buffer, buffer_size);
}

void handle_post_request(int client_socket, const char *data) {
    connect_to_mongodb();
    save_to_mongodb(data);
    disconnect_from_mongodb();

    const char *response = "HTTP/1.1 200 OK\r\n"
                           "Content-Length: 0\r\n"
                           "\r\n";
    send(client_socket, response, strlen(response), 0);
}

void handle_get_request(int client_socket) {
    mongoc_cursor_t *cursor;
    const bson_t *doc;
    bson_error_t error;
    char *str;
    
    // Create an empty filter
    bson_t *filter = bson_new();

    // Query MongoDB collection to get all documents
    cursor = mongoc_collection_find_with_opts(collection, filter, NULL, NULL);

    // Construct the response
    char full_response[4096]; // Increased buffer size to accommodate multiple documents
    sprintf(full_response, "HTTP/1.1 200 OK\r\n"
                             "Content-Type: application/json\r\n"
                             "\r\n");

    // Iterate through the documents in the cursor
    while (mongoc_cursor_next(cursor, &doc)) {
        str = bson_as_json(doc, NULL);
        strcat(full_response, str);
        strcat(full_response, "\n");
        bson_free(str);
    }

    // Send the response to the client
    send(client_socket, full_response, strlen(full_response), 0);

    // Clean up cursor and filter
    mongoc_cursor_destroy(cursor);
    bson_destroy(filter);
}



int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    // Initialize MongoDB connection
    connect_to_mongodb();

    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    // Set server address and port
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Bind socket to address and port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_fd, 3) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    // Accept and handle incoming connections
    while (1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
            perror("Accept failed");
            exit(EXIT_FAILURE);
        }

        // Read HTTP request
        // For simplicity, assume that the request is a GET request
        handle_get_request(new_socket);

        // Close connection
        close(new_socket);
    }

    // Cleanup MongoDB connection
    disconnect_from_mongodb();

    return 0;
}
