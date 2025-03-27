#ifndef NETWORK_UTILS_H
#define NETWORK_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Function to create a TCP socket
int create_tcp_socket() {
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd == -1) {
        perror("Error creating socket");
        return -1;
    }
    return socket_fd;
}

// Function to bind a socket to a specific address and port
int bind_socket(int socket_fd, const char *address, int port) {
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(address);
    server_addr.sin_port = htons(port);

    if (bind(socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Error binding socket");
        return -1;
    }
    return 0;
}

// Function to listen for incoming connections on a socket
int listen_for_connections(int socket_fd, int backlog) {
    if (listen(socket_fd, backlog) == -1) {
        perror("Error listening on socket");
        return -1;
    }
    return 0;
}

// Function to accept an incoming connection
int accept_connection(int socket_fd) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    int new_socket_fd = accept(socket_fd, (struct sockaddr *)&client_addr, &client_addr_len);
    if (new_socket_fd == -1) {
        perror("Error accepting connection");
        return -1;
    }
    return new_socket_fd;
}

// Function to send data over a socket
int send_data(int socket_fd, const void *buffer, size_t length, int flags) {
    int bytes_sent = send(socket_fd, buffer, length, flags);
    if (bytes_sent == -1) {
        perror("Error sending data");
        return -1;
    }
    return bytes_sent;
}

// Function to receive data from a socket
int receive_data(int socket_fd, void *buffer, size_t length, int flags) {
    int bytes_received = recv(socket_fd, buffer, length, flags);
    if (bytes_received == -1) {
        perror("Error receiving data");
        return -1;
    }
    return bytes_received;
}

// Function to close a socket
int close_socket(int socket_fd) {
    if (close(socket_fd) == -1) {
        perror("Error closing socket");
        return -1;
    }
    return 0;
}

#endif /* NETWORK_UTILS_H */
