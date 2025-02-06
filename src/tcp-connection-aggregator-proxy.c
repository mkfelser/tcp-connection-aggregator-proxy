#include <stdio.h>        // printf
#include <stdlib.h>       // exit
#include <netinet/in.h>   // struct sockaddr_in
#include <arpa/inet.h>    //inet_ntoa
#include <unistd.h>       // close
#include <fcntl.h>        // F_GETFL
#include <errno.h>        // errno
#include <string.h>       // strcmp

#define BUFFER_SIZE 256
#define MAX_CLIENTS 10

void print_usage(const char *const prog_name) {
    printf("Usage: %s -s <server_ip> -p <server_port> -l <listening_port>\n", prog_name);
}

int parse_arguments(int argc, char *argv[], char **server_ip, int *server_port, int *listening_port) {
    int opt;
    while ((opt = getopt(argc, argv, "s:p:l:")) != -1) {
        switch (opt) {
            case 's':
                *server_ip = optarg;
                break;
            case 'p':
                *server_port = atoi(optarg);
                break;
            case 'l':
                *listening_port = atoi(optarg);
                break;
            default:
                print_usage(argv[0]);
                return -1;
        }
    }
    if (*server_ip == NULL || *server_port == 0 || *listening_port == 0) {
        print_usage(argv[0]);
        return -1;
    }
    return 0;
}

int connect_to_server(const char * const ip_address, const int port) {
    int sockfd;

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Server socket creation failed");
        return -1;
    }

    // Set socket to non-blocking mode
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("set server socket to non-blocking failed");
        close(sockfd);
        return -1;
    }

    // Define server address
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port); // Modbus TCP port
    server_addr.sin_addr.s_addr = inet_addr(ip_address); // Server IP address

    // Connect to server
    int result = connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (result < 0 && errno != EINPROGRESS) {
        perror("Connection to server failed");
        close(sockfd);
        return -1;
    }

    // use select() to wait for the connection to complete
    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(sockfd, &writefds);
    struct timeval tv;
    tv.tv_sec = 25; // Timeout in seconds
    tv.tv_usec = 0;

    result = select(sockfd + 1, NULL, &writefds, NULL, &tv);
    if (result <= 0) {
        perror("Connection timed out or failed");
        close(sockfd);
        return -1;
    }

    // Set socket back to blocking mode
    if (fcntl(sockfd, F_SETFL, flags) == -1) {
        perror("revert server socket options failed");
        // ignore error
    }
    return sockfd;
}

int create_listening_socket(const uint16_t port) {
    int sockfd;

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Listening socket creation failed");
        return -1;
    }

    // Set socket options to reuse address
    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        // ignore error
    }

    // Define server address
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind socket to address
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Binding listening socket failed");
        close(sockfd);
        return -1;
    }

    // Listen for connections
     if (listen(sockfd, 5) < 0) {
        perror("Listen failed");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

int update_fdset(fd_set *const readfds, int *const client_sockets, const int listening_sockfd) {
    int max_sd;

    FD_ZERO(readfds);
 
    // Add listening socket to set
    FD_SET(listening_sockfd, readfds);
    max_sd = listening_sockfd;
  
    // Add child sockets to set
    for (int i = 0; i < MAX_CLIENTS; i++) {
      int sd = client_sockets[i];
      // If valid socket descriptor then add to read list
      if (sd > 0)
         FD_SET(sd, readfds);
      // Highest file descriptor number, needed for the select function
      if (sd > max_sd)
        max_sd = sd;
    }
    return max_sd;
}

const char * get_client_name_str(const int sd) {
    #define get_client_name_str_STRING_LEN 50
    static char buffer[get_client_name_str_STRING_LEN];
    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);
    getpeername(sd, (struct sockaddr *)&client_addr, (socklen_t *)&addrlen);
    snprintf(buffer, get_client_name_str_STRING_LEN, "%s:%d", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
    return buffer;
}

// return -1 on client error, -2 on server related error
#define SERVER_ERROR  -2
#define CLIENT_ERROR -1
int forward_request_handle_reply(int client_socket, int server_socket, char *buffer, ssize_t bytes_read) {
    // Forward data to server
    ssize_t bytes_sent = send(server_socket, buffer, bytes_read, 0);
    if (bytes_sent <= 0) {
        perror("Forwarding to server failed");
        return SERVER_ERROR;
    }

    // Read response from server
    struct timeval tv;
    tv.tv_sec = 5; // Timeout in seconds
    tv.tv_usec = 0;

    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(server_socket, &readfds);

    int retval = select(server_socket + 1, &readfds, NULL, NULL, &tv);
    if (retval == -1) {
        perror("select error");
        return SERVER_ERROR;
    } else if (retval == 0) {
        printf("Timeout occurred! No data received from server.\n");
        return CLIENT_ERROR;
    }

    bytes_read = recv(server_socket, buffer, BUFFER_SIZE, 0);
    if (bytes_read <= 0) {
        perror("Reading from server failed or connection closed");
        return SERVER_ERROR;
    }

    // Forward response to client
    bytes_sent = send(client_socket, buffer, bytes_read, 0);
    if (bytes_sent <= 0) {
        perror("Forwarding to client failed");
        return CLIENT_ERROR;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    char *server_ip = NULL;
    int server_port = 0;
    int listening_port = 0;

    if (parse_arguments(argc, argv, &server_ip, &server_port, &listening_port) < 0) {
        return -1;
    }
    printf("Server IP: %s, Server Port: %d, Listening Port: %d\n", server_ip, server_port, listening_port);

    int server_sockfd = connect_to_server(server_ip, server_port);
    if (server_sockfd < 0) {
        perror("connect_to_server failed");
        return -1;
    }
    printf("Connected to server %s:%i\n", server_ip, server_port);

    int listening_sockfd = create_listening_socket(listening_port);
    if (listening_sockfd < 0) {
        perror("create_listening_server failed");
        close (server_sockfd);
        return -1;
    }


    int client_sockets[MAX_CLIENTS] = {0};
    while (1) {
        fd_set readfds;
        int max_sd = update_fdset(&readfds, client_sockets, listening_sockfd);
      
        // Wait for an activity on one of the sockets
        int activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);
        if ((activity < 0) && (errno != EINTR)) {
            perror("select error");
            continue;
        }

        // If something happened on the listening socket, then it's an incoming connection
        if (FD_ISSET(listening_sockfd, &readfds)) {
            struct sockaddr_in client_addr;
            socklen_t addrlen = sizeof(client_addr);
            int new_socket = accept(listening_sockfd, (struct sockaddr *)&client_addr, (socklen_t *)&addrlen);
            if (new_socket < 0) {
                perror("accept on listening socket failed");
                // ignore
            } else {
                // Add new socket to array of sockets
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (client_sockets[i] == 0) {
                        client_sockets[i] = new_socket;
                        printf("\n%d: New connection from %s on socket %d\n", i, get_client_name_str(new_socket), new_socket);
                        break;
                    }
                }
            }
        }

        // else it's some I/O operation on some other socket
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = client_sockets[i];
            if (FD_ISSET(sd, &readfds)) {
                char buffer[BUFFER_SIZE];
                ssize_t bytes_read;
                // Check if it was for closing, and also read the incoming message
                if ((bytes_read = read(sd, buffer, BUFFER_SIZE)) == 0) {
                    // Somebody disconnected, get his details and print
                    printf("\n%d: Host disconnected %s\n", i, get_client_name_str(sd));
                    // Close the socket and mark as 0 in list for reuse
                    close(sd);
                    client_sockets[i] = 0;
                } else {
                    //printf("%d: Received from client (%li bytes)\n", i, bytes_read);
                    printf("%d", i);
                    fflush(stdout);
                    const int status = forward_request_handle_reply(sd, server_sockfd, buffer, bytes_read);
                   if (status == CLIENT_ERROR) {
                        printf("\n%d: forward_request_handle_reply failed. Closing socket %d (%s)\n", i, sd, get_client_name_str(sd));
                        close(sd);
                        client_sockets[i] = 0;
                    } else if (status == SERVER_ERROR) {
                       printf("\n%d: forward_request_handle_reply failed. Closing all sockets\n", i);
                       // close all client sockets
                        for (int i = 0; i < MAX_CLIENTS; i++) {
                            if (client_sockets[i] != 0){
                                close(client_sockets[i]);
                                client_sockets[i] = 0;
                            }
                        }
                        // recpnnect to server
                        close(server_sockfd);
                        server_sockfd = connect_to_server(server_ip, server_port);
                        if (server_sockfd < 0) {
                            perror("reconnect_to_server failed");
                            return -1;
                        }
                        printf("re-connected to server %s:%i\n", server_ip, server_port);
                       break;
                    }
                }
            }
        }
    }

    close(listening_sockfd);
    close(server_sockfd);
    return 0;
}