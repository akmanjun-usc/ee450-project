// serverM.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <signal.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/wait.h>

// Port number definitions from the project specification
#define UDP_PORT_M 24893
#define TCP_PORT_GUEST 25893
#define SERVER_A_PORT 21893
#define SERVER_R_PORT 22893
#define SERVER_P_PORT 23893
#define LOCALHOST "127.0.0.1"
#define MAXBUFLEN 4096

// Structure to hold client information
typedef struct {
    int socket_fd;
    char username[100];
    int user_id;
    int is_guest;
    int is_authenticated;
} ClientInfo;

// Global socket descriptors
int udp_sock;
int tcp_listen_sock;
struct sockaddr_in udp_addr, server_a_addr, server_r_addr, server_p_addr;

static void init_sockets();
static char* encrypt_password(const char* password);
static int authenticate_user(ClientInfo* client, const char* username, const char* password);
static void handle_client(int client_sock);
static void handle_search_request(ClientInfo* client, const char* parking_lot);
static void handle_reserve_request(ClientInfo* client, const char* space_code, const char* timeslots);
static void handle_lookup_request(ClientInfo* client);
static void handle_cancel_request(ClientInfo* client, const char* space_code, const char* timeslots);
static int get_dynamic_port(int sock);

#if 1
int main() {
    // Initialize all sockets
    //  This initialzes the ServerM, ServerA, ServerR, ServerP and client side socket
    init_sockets();

    printf("[Server M] Booting up using UDP on port %d.\n", UDP_PORT_M);

    // Avoid zombie processes from forked children
    signal(SIGCHLD, SIG_IGN);

    // Main loop to accept client connections
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        // Accept new client connection
        int client_sock = accept(tcp_listen_sock, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_sock < 0) {
            perror("accept");
            continue;   // don't exit the server on transient errors
        }

        pid_t pid = fork();
        if (pid < 0) {
            // Fork failed; handle client synchronously as a fallback
            perror("fork");
            exit(0);
        }

        if (pid == 0) {
            // Child process: handle this client
            close(tcp_listen_sock);   // child doesn't accept new connections
            handle_client(client_sock);
            close(client_sock);
            exit(0);
        } else {
            // Parent process: done with this client socket
            close(client_sock);
        }
    }

    close(tcp_listen_sock);
    close(udp_sock);
    return 0;
}
#else
int main() {
    // Initialize all sockets
    //  This initialzes the ServerM, ServerA, ServerR, ServerP and client side socket
    init_sockets();
    
    printf("[Server M] Booting up using UDP on port %d.\n", UDP_PORT_M);
    
    // Main loop to accept client connections
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        // Accept new client connection
        int client_sock = accept(tcp_listen_sock, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_sock < 0) {
            perror("accept");
            exit(0); //continue;
        }
        // Handle this client
        //  the guidelines mention that racing conditions don't happen
        //  i.e. only after one client completes the execution next client starts
        //  Hence we can make this a blocking call
        handle_client(client_sock);
        
        // Client connection closed, ready for next client
    }
    
    close(tcp_listen_sock);
    close(udp_sock);
    return 0;
}
#endif

// verification done!
static void init_sockets() {
    // Create UDP socket for backend server communication. This is for main serverM
    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) {
        perror("UDP socket creation failed");
        exit(1);
    }
    // Bind UDP socket
    memset(&udp_addr, 0, sizeof(udp_addr));
    udp_addr.sin_family = AF_INET;
    udp_addr.sin_addr.s_addr = inet_addr(LOCALHOST);
    udp_addr.sin_port = htons(UDP_PORT_M);
    if (bind(udp_sock, (struct sockaddr*)&udp_addr, sizeof(udp_addr)) < 0) {
        perror("UDP bind failed");
        exit(1);
    }
    
    // Setup backend server addresses
    memset(&server_a_addr, 0, sizeof(server_a_addr));
    server_a_addr.sin_family = AF_INET;
    server_a_addr.sin_addr.s_addr = inet_addr(LOCALHOST);
    server_a_addr.sin_port = htons(SERVER_A_PORT);
    
    memset(&server_r_addr, 0, sizeof(server_r_addr));
    server_r_addr.sin_family = AF_INET;
    server_r_addr.sin_addr.s_addr = inet_addr(LOCALHOST);
    server_r_addr.sin_port = htons(SERVER_R_PORT);
    
    memset(&server_p_addr, 0, sizeof(server_p_addr));
    server_p_addr.sin_family = AF_INET;
    server_p_addr.sin_addr.s_addr = inet_addr(LOCALHOST);
    server_p_addr.sin_port = htons(SERVER_P_PORT);
    
    // Create TCP socket for client connections
    tcp_listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_listen_sock < 0) {
        perror("TCP socket creation failed");
        exit(1);
    }
    // Allow address reuse because multiple clients will use same PORT on serverM's end
    int opt = 1;
    if (setsockopt(tcp_listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        exit(1);
    }
    // Bind TCP socket
    struct sockaddr_in tcp_addr;
    memset(&tcp_addr, 0, sizeof(tcp_addr));
    tcp_addr.sin_family = AF_INET;
    tcp_addr.sin_addr.s_addr = inet_addr(LOCALHOST);
    tcp_addr.sin_port = htons(TCP_PORT_GUEST);
    if (bind(tcp_listen_sock, (struct sockaddr*)&tcp_addr, sizeof(tcp_addr)) < 0) {
        perror("TCP bind failed");
        exit(1);
    }
    // Listen for connections
    if (listen(tcp_listen_sock, 10) < 0) {
        perror("listen failed");
        exit(1);
    }
}

static char* encrypt_password(const char* password) {
    static char encrypted[100];
    int i;
    
    for (i = 0; password[i] != '\0' && i < 99; i++) {
        char c = password[i];
        
        if (c >= 'A' && c <= 'Z') {
            // Uppercase letter
            encrypted[i] = ((c - 'A' + 3) % 26) + 'A';
        } else if (c >= 'a' && c <= 'z') {
            // Lowercase letter
            encrypted[i] = ((c - 'a' + 3) % 26) + 'a';
        } else if (c >= '0' && c <= '9') {
            // Digit
            encrypted[i] = ((c - '0' + 3) % 10) + '0';
        } else {
            // Special character - unchanged
            encrypted[i] = c;
        }
    }
    encrypted[i] = '\0';
    
    return encrypted;
}

int get_dynamic_port(int sock) {
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    
    if (getsockname(sock, (struct sockaddr*)&addr, &addr_len) < 0) {
        perror("getsockname failed");
        return -1;
    }
    
    return ntohs(addr.sin_port);
}

static int authenticate_user(ClientInfo* client, const char* username, const char* password) {
    char send_buf[MAXBUFLEN];
    char recv_buf[MAXBUFLEN];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    
    printf("Server M received username %s and password ******.\n", username);
    
    // Check if guest
    if (strcmp(username, "guest") == 0 && strcmp(password, "123456") == 0) {
        // Guest authentication
        printf("Server M sent the authentication request to Server A.\n");
        
        // Format: "guest 123456"
        sprintf(send_buf, "%s %s", username, password);
        
        if (sendto(udp_sock, send_buf, strlen(send_buf), 0, 
                   (struct sockaddr*)&server_a_addr, sizeof(server_a_addr)) < 0) {
            perror("sendto Server A failed");
            return 0;
        }
        
        // Receive response from Server A
        int recv_len = recvfrom(udp_sock, recv_buf, MAXBUFLEN - 1, 0,
                                (struct sockaddr*)&from_addr, &from_len);
        if (recv_len < 0) {
            perror("recvfrom Server A failed");
            return 0;
        }
        recv_buf[recv_len] = '\0';
        
        int received_port = ntohs(from_addr.sin_port);
        printf("Server M received the response from Server A using UDP over port %d.\n", received_port);
        
        // Parse response - format: "SUCCESS:GUEST:0" or "FAIL"
        if (strncmp(recv_buf, "SUCCESS:GUEST", 13) == 0) {
            client->is_guest = 1;
            client->user_id = 0;
            strcpy(client->username, "guest");
            
            // Send success to client
            int client_port = get_dynamic_port(client->socket_fd);
            printf("Server M sent the response to the client using TCP over port %d.\n", client_port);
            
            sprintf(send_buf, "SUCCESS:GUEST");
            send(client->socket_fd, send_buf, strlen(send_buf), 0);
            
            return 1;
        } else {
            // Send failure to client
            int client_port = get_dynamic_port(client->socket_fd);
            printf("Server M sent the response to the client using TCP over port %d.\n", client_port);
            
            sprintf(send_buf, "FAIL");
            send(client->socket_fd, send_buf, strlen(send_buf), 0);
            
            return 0;
        }
    }
    
    // Member authentication - encrypt password
    char* encrypted_pwd = encrypt_password(password);
    
    printf("Server M sent the authentication request to Server A.\n");
    
    // Format: "username encrypted_password"
    sprintf(send_buf, "%s %s", username, encrypted_pwd);
    
    if (sendto(udp_sock, send_buf, strlen(send_buf), 0,
               (struct sockaddr*)&server_a_addr, sizeof(server_a_addr)) < 0) {
        perror("sendto Server A failed");
        return 0;
    }
    
    // Receive response from Server A
    int recv_len = recvfrom(udp_sock, recv_buf, MAXBUFLEN - 1, 0,
                            (struct sockaddr*)&from_addr, &from_len);
    if (recv_len < 0) {
        perror("recvfrom Server A failed");
        return 0;
    }
    recv_buf[recv_len] = '\0';
    
    int received_port = ntohs(from_addr.sin_port);
    printf("Server M received the response from Server A using UDP over port %d.\n", received_port);
    
    // Parse response - format: "SUCCESS:userid" or "FAIL"
    if (strncmp(recv_buf, "SUCCESS:", 8) == 0) {
        int user_id;
        sscanf(recv_buf, "SUCCESS:%d", &user_id);
        
        client->is_guest = 0;
        client->user_id = user_id;
        strcpy(client->username, username);
        
        // Send success to client
        int client_port = get_dynamic_port(client->socket_fd);
        printf("Server M sent the response to the client using TCP over port %d.\n", client_port);
        
        sprintf(send_buf, "SUCCESS:%d", user_id);
        send(client->socket_fd, send_buf, strlen(send_buf), 0);
        
        return 1;
    } else {
        // Send failure to client
        int client_port = get_dynamic_port(client->socket_fd);
        printf("Server M sent the response to the client using TCP over port %d.\n", client_port);
        
        sprintf(send_buf, "FAIL");
        send(client->socket_fd, send_buf, strlen(send_buf), 0);
        
        return 0;
    }
}

static void handle_search_request(ClientInfo* client, const char* parking_lot) {
    char send_buf[MAXBUFLEN];
    char recv_buf[MAXBUFLEN];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    int client_port = get_dynamic_port(client->socket_fd);
    
    // Print appropriate message
    if (client->is_guest) {
        printf("Server M received an availability request from Guest for %s using TCP over port %d.\n",
               parking_lot, client_port);
    } else {
        printf("Server M received an availability request from %s for %s using TCP over port %d.\n",
               client->username, parking_lot, client_port);
    }
    
    // Forward to Server R
    printf("Server M sent the availability request to Server R.\n");
    
    // Format: "SEARCH parking_lot"
    sprintf(send_buf, "SEARCH %s", parking_lot);
    
    if (sendto(udp_sock, send_buf, strlen(send_buf), 0,
               (struct sockaddr*)&server_r_addr, sizeof(server_r_addr)) < 0) {
        perror("sendto Server R failed");
        return;
    }
    
    // Receive response from Server R
    int recv_len = recvfrom(udp_sock, recv_buf, MAXBUFLEN - 1, 0,
                            (struct sockaddr*)&from_addr, &from_len);
    if (recv_len < 0) {
        perror("recvfrom Server R failed");
        return;
    }
    recv_buf[recv_len] = '\0';
    
    int received_port = ntohs(from_addr.sin_port);
    printf("Server M received the response from Server R using UDP over port %d.\n", received_port);
    
    // Forward response to client
    printf("Server M sent the availability information to the client.\n");
    send(client->socket_fd, recv_buf, strlen(recv_buf), 0);
}

#if 1
void handle_reserve_request(ClientInfo* client, const char* space_code, const char* timeslots) {
    char send_buf[MAXBUFLEN];
    char recv_buf[MAXBUFLEN];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    int client_port = get_dynamic_port(client->socket_fd);
    
    printf("Server M received a reservation request from %s using TCP over port %d.\n",
           client->username, client_port);
    
    // Forward to Server R
    printf("Server M sent the reservation request to Server R.\n");
    
    // Format: "RESERVE username userid space_code timeslots"
    snprintf(send_buf, MAXBUFLEN, "RESERVE %s %d %s %s", client->username, client->user_id, space_code, timeslots);
    
    if (sendto(udp_sock, send_buf, strlen(send_buf), 0,
               (struct sockaddr*)&server_r_addr, sizeof(server_r_addr)) < 0) {
        perror("sendto Server R failed");
        return;
    }
    
    // Receive response from Server R
    int recv_len = recvfrom(udp_sock, recv_buf, MAXBUFLEN - 1, 0,
                            (struct sockaddr*)&from_addr, &from_len);
    if (recv_len < 0) {
        perror("recvfrom Server R failed");
        return;
    }
    recv_buf[recv_len] = '\0';
    
    int received_port = ntohs(from_addr.sin_port);
    printf("Server M received the response from Server R using UDP over port %d.\n", received_port);
    
    // Check if partial availability
    if (strncmp(recv_buf, "PARTIAL:", 8) == 0) {
        // Forward to client for confirmation
        printf("Server M sent the partial reservation confirmation request to the client.\n");
        send(client->socket_fd, recv_buf, strlen(recv_buf), 0);
        
        // Receive Y/N from client
        char confirm_buf[10];
        recv_len = recv(client->socket_fd, confirm_buf, sizeof(confirm_buf) - 1, 0);
        if (recv_len <= 0) {
            return;
        }
        confirm_buf[recv_len] = '\0';
        
        // Forward confirmation to Server R
        printf("Server M sent the confirmation response to Server R.\n");
        
        snprintf(send_buf, MAXBUFLEN, "CONFIRM %s %d %s", client->username, client->user_id, confirm_buf);
        
        if (sendto(udp_sock, send_buf, strlen(send_buf), 0,
                   (struct sockaddr*)&server_r_addr, sizeof(server_r_addr)) < 0) {
            perror("sendto Server R failed");
            return;
        }
        
        // Receive final result
        recv_len = recvfrom(udp_sock, recv_buf, MAXBUFLEN - 1, 0,
                            (struct sockaddr*)&from_addr, &from_len);
        if (recv_len < 0) {
            perror("recvfrom Server R failed");
            return;
        }
        recv_buf[recv_len] = '\0';
    }
    
    // Check if reservation successful
    if (strncmp(recv_buf, "SUCCESS:", 8) == 0) {
        // Parse the response to extract reservation data
        // Format: "SUCCESS:space:slots:ALL_RESERVATIONS:reservation_data"
        char* all_res_marker = strstr(recv_buf, ":ALL_RESERVATIONS:");
        char reservations_buf[MAXBUFLEN - 200];
        char success_part[MAXBUFLEN];
        
        if (all_res_marker != NULL) {
            // Extract the part before ALL_RESERVATIONS for client
            int prefix_len = all_res_marker - recv_buf;
            strncpy(success_part, recv_buf, prefix_len);
            success_part[prefix_len] = '\0';
            
            // Extract reservation data after ALL_RESERVATIONS:
            strcpy(reservations_buf, all_res_marker + strlen(":ALL_RESERVATIONS:"));
        } else {
            // Fallback if format is different
            strcpy(success_part, recv_buf);
            strcpy(reservations_buf, "NONE");
        }
        
        // Now send pricing request to Server P with the reservation data
        printf("Server M sent the pricing request to Server P.\n");
        
        snprintf(send_buf, MAXBUFLEN, "PRICE %s %s", client->username, reservations_buf);
        
        if (sendto(udp_sock, send_buf, strlen(send_buf), 0,
                   (struct sockaddr*)&server_p_addr, sizeof(server_p_addr)) < 0) {
            perror("sendto Server P failed");
            return;
        }
        
        // Receive price from Server P
        char price_buf[MAXBUFLEN - 200];
        recv_len = recvfrom(udp_sock, price_buf, MAXBUFLEN - 1, 0,
                            (struct sockaddr*)&from_addr, &from_len);
        if (recv_len < 0) {
            perror("recvfrom Server P failed");
            return;
        }
        price_buf[recv_len] = '\0';
        
        received_port = ntohs(from_addr.sin_port);
        printf("Server M received the pricing information from Server P using UDP over port %d.\n", received_port);
        
        // Combine result with price
        // success_part format: "SUCCESS:space:timeslots"
        // price_buf format: "PRICE:amount"
        snprintf(recv_buf, MAXBUFLEN, "%s:%s", success_part, price_buf);
    }
    
    // Send final result to client
    printf("Server M sent the reservation result to the client.\n");
    send(client->socket_fd, recv_buf, strlen(recv_buf), 0);
}
#else
static void handle_reserve_request(ClientInfo* client, const char* space_code, const char* timeslots) {
    char send_buf[MAXBUFLEN];
    char recv_buf[MAXBUFLEN];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    int client_port = get_dynamic_port(client->socket_fd);
    
    printf("Server M received a reservation request from %s using TCP over port %d.\n",
           client->username, client_port);
    
    // Forward to Server R
    printf("Server M sent the reservation request to Server R.\n");
    
    // Format: "RESERVE username userid space_code timeslots"
    snprintf(send_buf, MAXBUFLEN, "RESERVE %s %d %s %s", client->username, client->user_id, space_code, timeslots);
    
    if (sendto(udp_sock, send_buf, strlen(send_buf), 0,
               (struct sockaddr*)&server_r_addr, sizeof(server_r_addr)) < 0) {
        perror("sendto Server R failed");
        return;
    }
    
    // Receive response from Server R
    int recv_len = recvfrom(udp_sock, recv_buf, MAXBUFLEN - 1, 0,
                            (struct sockaddr*)&from_addr, &from_len);
    if (recv_len < 0) {
        perror("recvfrom Server R failed");
        return;
    }
    recv_buf[recv_len] = '\0';
    
    int received_port = ntohs(from_addr.sin_port);
    printf("Server M received the response from Server R using UDP over port %d.\n", received_port);
    
    // Check if partial availability
    if (strncmp(recv_buf, "PARTIAL:", 8) == 0) {
        // Forward to client for confirmation
        printf("Server M sent the partial reservation confirmation request to the client.\n");
        send(client->socket_fd, recv_buf, strlen(recv_buf), 0);
        
        // Receive Y/N from client
        char confirm_buf[10];
        recv_len = recv(client->socket_fd, confirm_buf, sizeof(confirm_buf) - 1, 0);
        if (recv_len <= 0) {
            return;
        }
        confirm_buf[recv_len] = '\0';
        
        // Forward confirmation to Server R
        printf("Server M sent the confirmation response to Server R.\n");
        
        snprintf(send_buf, MAXBUFLEN, "CONFIRM %s %d %s", client->username, client->user_id, confirm_buf);
        
        if (sendto(udp_sock, send_buf, strlen(send_buf), 0,
                   (struct sockaddr*)&server_r_addr, sizeof(server_r_addr)) < 0) {
            perror("sendto Server R failed");
            return;
        }
        
        // Receive final result
        recv_len = recvfrom(udp_sock, recv_buf, MAXBUFLEN - 1, 0,
                            (struct sockaddr*)&from_addr, &from_len);
        if (recv_len < 0) {
            perror("recvfrom Server R failed");
            return;
        }
        recv_buf[recv_len] = '\0';
    }
    
    // Check if reservation successful
    if (strncmp(recv_buf, "SUCCESS:", 8) == 0) {
        // First, get user's current reservations from Server R
        printf("Server M sent a lookup request to Server R.\n");
        
        snprintf(send_buf, MAXBUFLEN, "LOOKUP %s %d", client->username, client->user_id);
        
        if (sendto(udp_sock, send_buf, strlen(send_buf), 0,
                   (struct sockaddr*)&server_r_addr, sizeof(server_r_addr)) < 0) {
            perror("sendto Server R failed");
            return;
        }
        
        // Receive reservations from Server R
        char reservations_buf[MAXBUFLEN - 200];
        recv_len = recvfrom(udp_sock, reservations_buf, MAXBUFLEN - 1, 0,
                            (struct sockaddr*)&from_addr, &from_len);
        if (recv_len < 0) {
            perror("recvfrom Server R failed");
            return;
        }
        reservations_buf[recv_len] = '\0';
        
        received_port = ntohs(from_addr.sin_port);
        printf("Server M received the response from Server R using UDP over port %d.\n", received_port);
        
        // Now send pricing request to Server P with the reservation data
        printf("Server M sent the pricing request to Server P.\n");
        
        snprintf(send_buf, MAXBUFLEN, "PRICE %s %s", client->username, reservations_buf);
        
        if (sendto(udp_sock, send_buf, strlen(send_buf), 0,
                   (struct sockaddr*)&server_p_addr, sizeof(server_p_addr)) < 0) {
            perror("sendto Server P failed");
            return;
        }
        
        // Receive price from Server P
        char price_buf[MAXBUFLEN];
        recv_len = recvfrom(udp_sock, price_buf, MAXBUFLEN - 1, 0,
                            (struct sockaddr*)&from_addr, &from_len);
        if (recv_len < 0) {
            perror("recvfrom Server P failed");
            return;
        }
        price_buf[recv_len] = '\0';
        
        received_port = ntohs(from_addr.sin_port);
        printf("Server M received the pricing information from Server P using UDP over port %d.\n", received_port);
        
        // Combine result with price
        // recv_buf format: "SUCCESS:space:timeslots"
        // price_buf format: "PRICE:amount"
        strcat(recv_buf, ":");
        strcat(recv_buf, price_buf);
    }
    
    // Send final result to client
    printf("Server M sent the reservation result to the client.\n");
    send(client->socket_fd, recv_buf, strlen(recv_buf), 0);
}
#endif

static void handle_lookup_request(ClientInfo* client) {
    char send_buf[MAXBUFLEN];
    char recv_buf[MAXBUFLEN];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    int client_port = get_dynamic_port(client->socket_fd);
    
    printf("Server M received a lookup request from %s using TCP over port %d.\n",
           client->username, client_port);
    
    // Forward to Server R
    printf("Server M sent the lookup request to Server R.\n");
    
    // Format: "LOOKUP username userid"
    sprintf(send_buf, "LOOKUP %s %d", client->username, client->user_id);
    
    if (sendto(udp_sock, send_buf, strlen(send_buf), 0,
               (struct sockaddr*)&server_r_addr, sizeof(server_r_addr)) < 0) {
        perror("sendto Server R failed");
        return;
    }
    
    // Receive response from Server R
    int recv_len = recvfrom(udp_sock, recv_buf, MAXBUFLEN - 1, 0,
                            (struct sockaddr*)&from_addr, &from_len);
    if (recv_len < 0) {
        perror("recvfrom Server R failed");
        return;
    }
    recv_buf[recv_len] = '\0';
    
    int received_port = ntohs(from_addr.sin_port);
    printf("Server M received the response from Server R using UDP over port %d.\n", received_port);
    
    // Forward to client
    printf("Server M sent the lookup result to the client.\n");
    send(client->socket_fd, recv_buf, strlen(recv_buf), 0);
}

static void handle_cancel_request(ClientInfo* client, const char* space_code, const char* timeslots) {
    char send_buf[MAXBUFLEN];
    char recv_buf[MAXBUFLEN];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    int client_port = get_dynamic_port(client->socket_fd);
    
    printf("Server M received a cancellation request from %s using TCP over port %d.\n",
           client->username, client_port);
    
    // Forward to Server R
    printf("Server M sent the cancellation request to Server R.\n");
    
    // Format: "CANCEL username userid space_code timeslots"
    sprintf(send_buf, "CANCEL %s %d %s %s", client->username, client->user_id, space_code, timeslots);
    
    if (sendto(udp_sock, send_buf, strlen(send_buf), 0,
               (struct sockaddr*)&server_r_addr, sizeof(server_r_addr)) < 0) {
        perror("sendto Server R failed");
        return;
    }
    
    // Receive response from Server R
    int recv_len = recvfrom(udp_sock, recv_buf, MAXBUFLEN - 1, 0,
                            (struct sockaddr*)&from_addr, &from_len);
    if (recv_len < 0) {
        perror("recvfrom Server R failed");
        return;
    }
    recv_buf[recv_len] = '\0';
    
    int received_port = ntohs(from_addr.sin_port);
    printf("Server M received the response from Server R using UDP over port %d.\n", received_port);
    
    // Check if cancellation successful
    if (strncmp(recv_buf, "SUCCESS:", 8) == 0) {
        // Get refund from Server P
        printf("Server M sent the refund request to Server P.\n");
        
        sprintf(send_buf, "REFUND %s %d %s %s", client->username, client->user_id, space_code, timeslots);
        
        if (sendto(udp_sock, send_buf, strlen(send_buf), 0,
                   (struct sockaddr*)&server_p_addr, sizeof(server_p_addr)) < 0) {
            perror("sendto Server P failed");
            return;
        }
        
        // Receive refund from Server P
        char refund_buf[MAXBUFLEN];
        recv_len = recvfrom(udp_sock, refund_buf, MAXBUFLEN - 1, 0,
                            (struct sockaddr*)&from_addr, &from_len);
        if (recv_len < 0) {
            perror("recvfrom Server P failed");
            return;
        }
        refund_buf[recv_len] = '\0';
        
        received_port = ntohs(from_addr.sin_port);
        printf("Server M received the refund information from Server P using UDP over port %d.\n", received_port);
        
        // Combine result with refund
        strcat(recv_buf, ":");
        strcat(recv_buf, refund_buf);
    }
    
    // Send result to client
    send(client->socket_fd, recv_buf, strlen(recv_buf), 0);
}

static void handle_client(int client_sock) {
    ClientInfo client;
    memset(&client, 0, sizeof(client));
    client.socket_fd = client_sock;
    
    // Phase 1: Authentication
    //  Client sends authentication request by giving username and password
    char auth_buf[MAXBUFLEN];
    int recv_len = recv(client_sock, auth_buf, MAXBUFLEN - 1, 0);
    if (recv_len <= 0) {
        close(client_sock);
        return;
    }
    auth_buf[recv_len] = '\0';
    
    // Parse username and password - format: "username password"
    char username[100], password[100];
    sscanf(auth_buf, "%s %s", username, password);
    
    // Authenticate
    if (!authenticate_user(&client, username, password)) {
        // Authentication failed, close connection
        close(client_sock);
        return;
    }
    
    client.is_authenticated = 1;
    
    // Phase 2: Handle requests
    while (1) {
        char request_buf[MAXBUFLEN];
        recv_len = recv(client_sock, request_buf, MAXBUFLEN - 1, 0);
        
        if (recv_len <= 0) {
            // Connection closed or error
            break;
        }
        
        request_buf[recv_len] = '\0';
        
        // Parse command
        char command[50];
        sscanf(request_buf, "%s", command);
        
        if (strcmp(command, "QUIT") == 0) {
            break;
        } else if (strcmp(command, "SEARCH") == 0) {
            char parking_lot[50] = "ALL";
            sscanf(request_buf, "%s %s", command, parking_lot);
            handle_search_request(&client, parking_lot);
        } else if (strcmp(command, "RESERVE") == 0) {
            char space_code[50], timeslots[MAXBUFLEN];
            // Format: "RESERVE space_code timeslot1 timeslot2 ..."
            int offset = strlen("RESERVE ");
            sscanf(request_buf + offset, "%s %[^\n]", space_code, timeslots);
            handle_reserve_request(&client, space_code, timeslots);
        } else if (strcmp(command, "LOOKUP") == 0) {
            handle_lookup_request(&client);
        } else if (strcmp(command, "CANCEL") == 0) {
            char space_code[50], timeslots[MAXBUFLEN];
            // Format: "CANCEL space_code timeslot1 timeslot2 ..."
            int offset = strlen("CANCEL ");
            sscanf(request_buf + offset, "%s %[^\n]", space_code, timeslots);
            handle_cancel_request(&client, space_code, timeslots);
        }
    }
    
    close(client_sock);
}
