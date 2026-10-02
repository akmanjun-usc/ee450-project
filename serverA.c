// serverA.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/wait.h>

// Port number definitions
#define SERVER_A_PORT 21893
#define LOCALHOST "127.0.0.1"
#define MAXBUFLEN 4096
#define MAX_MEMBERS 1000

// Structure to hold member information
typedef struct {
    int user_id;
    char username[100];
    char encrypted_password[100];
} Member;

// Global variables
Member members[MAX_MEMBERS];
int member_count = 0;
int udp_sock;

// Function prototypes
static void load_members();
static int authenticate_member(const char* username, const char* encrypted_password);
static void handle_authentication_request(char* request, struct sockaddr_in* client_addr, socklen_t client_len);

int main() {
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;
    char buffer[MAXBUFLEN];
    
    // Create UDP socket
    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) {
        perror("Socket creation failed");
        exit(1);
    }
    // Setup server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(LOCALHOST);
    server_addr.sin_port = htons(SERVER_A_PORT);
    // Bind socket
    if (bind(udp_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(1);
    }
    // Load members from file
    load_members();
    
    printf("[Server A] Booting up using UDP on port %d.\n", SERVER_A_PORT);
    
    // Main loop - receive and process authentication requests
    while (1) {
        client_len = sizeof(client_addr);
        
        int recv_len = recvfrom(udp_sock, buffer, MAXBUFLEN - 1, 0,
                                (struct sockaddr*)&client_addr, &client_len);
        if (recv_len < 0) {
            perror("recvfrom failed");
            continue;
        }
        buffer[recv_len] = '\0';
        // Handle authentication request
        handle_authentication_request(buffer, &client_addr, client_len);
    }
    
    close(udp_sock);
    return 0;
}

static void load_members() {
    FILE* fp = fopen("members.txt", "r");
    if (fp == NULL) {
        perror("Error opening members.txt");
        exit(1);
    }
    
    member_count = 0;
    
    // Read members from file
    // Format: userid username encrypted_password
    while (fscanf(fp, "%d %s %s", 
                  &members[member_count].user_id,
                  members[member_count].username,
                  members[member_count].encrypted_password) == 3) {
        member_count++;
        if (member_count >= MAX_MEMBERS) {
            fprintf(stderr, "Warning: Maximum number of members reached\n");
            break;
        }
    }
    
    fclose(fp);
}

static int authenticate_member(const char* username, const char* encrypted_password) {
    int i;
    // Search for matching username and password
    for (i = 0; i < member_count; i++) {
        if (strcmp(members[i].username, username) == 0 &&
            strcmp(members[i].encrypted_password, encrypted_password) == 0) {
            return members[i].user_id;
        }
    }
    return -1; // Not found
}

static void handle_authentication_request(char* request, struct sockaddr_in* client_addr, socklen_t client_len) {
    char username[100], password[100];
    char response[MAXBUFLEN];
    
    // Parse request - format: "username password"
    sscanf(request, "%s %s", username, password);
    
    printf("[Server A] Received username %s and password ******.\n", username);
    
    // Check if guest
    if (strcmp(username, "guest") == 0 && strcmp(password, "123456") == 0) {
        printf("[Server A] Guest has been authenticated.\n");
        // Send success response for guest
        sprintf(response, "SUCCESS:GUEST:0");
        if (sendto(udp_sock, response, strlen(response), 0,
                   (struct sockaddr*)client_addr, client_len) < 0) {
            perror("sendto failed");
        }
        return;
    }
    
    // Member authentication - password should already be encrypted
    int user_id = authenticate_member(username, password);
    if (user_id != -1) {
        // Authentication successful
        printf("[Server A] Member %s has been authenticated.\n", username);
        // create the success command
        sprintf(response, "SUCCESS:%d", user_id);
        // send to serverM
        if (sendto(udp_sock, response, strlen(response), 0,
                   (struct sockaddr*)client_addr, client_len) < 0) {
            perror("sendto failed");
        }
    } else {
        // Authentication failed
        printf("[Server A] The username %s or password ****** is incorrect.\n", username);
        // create the failure command
        sprintf(response, "FAIL");
        // send to serverM
        if (sendto(udp_sock, response, strlen(response), 0,
                   (struct sockaddr*)client_addr, client_len) < 0) {
            perror("sendto failed");
        }
    }
}