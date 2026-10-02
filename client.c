// client.c
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
#include <ctype.h>

// Port number definitions
#define SERVER_M_PORT 25893
#define LOCALHOST "127.0.0.1"
#define MAXBUFLEN 4096

// Global variables
int tcp_sock;
char username[100];
int is_guest = 0;
int is_authenticated = 0;

// Function prototypes
void connect_to_server();
int authenticate(const char* user, const char* pass);
void display_help();
void handle_search(char* command);
void handle_reserve(char* command);
void handle_lookup();
void handle_cancel(char* command);
int get_dynamic_port();

int main(int argc, char* argv[]) {
    char command_line[MAXBUFLEN];
    
    // Check command line arguments
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <username> <password>\n", argv[0]);
        exit(1);
    }
    
    printf("Client is up and running.\n");
    
    // Connect to server
    connect_to_server();
    
    // Authenticate
    if (!authenticate(argv[1], argv[2])) {
        // Authentication failed
        close(tcp_sock);
        exit(1);
    }
    
    // Main command loop
    while (1) {
        printf("\nPlease enter the command: ");
        
        if (fgets(command_line, sizeof(command_line), stdin) == NULL) {
            break;
        }
        
        // Remove newline
        command_line[strcspn(command_line, "\n")] = 0;
        
        // Skip empty commands
        if (strlen(command_line) == 0) {
            continue;
        }
        
        // Parse command
        char command[50];
        sscanf(command_line, "%s", command);
        
        // Convert to lowercase for comparison
        int i;
        for (i = 0; command[i]; i++) {
            command[i] = tolower(command[i]);
        }
        
        if (strcmp(command, "help") == 0) {
            display_help();
        } else if (strcmp(command, "search") == 0) {
            handle_search(command_line);
        } else if (strcmp(command, "reserve") == 0) {
            if (is_guest) {
                printf("Guests can only check availability. Please log in as a member for full access.\n");
                printf("---Start a new request---\n");
            } else {
                handle_reserve(command_line);
            }
        } else if (strcmp(command, "lookup") == 0) {
            if (is_guest) {
                printf("Guests can only check availability. Please log in as a member for full access.\n");
                printf("---Start a new request---\n");
            } else {
                handle_lookup();
            }
        } else if (strcmp(command, "cancel") == 0) {
            if (is_guest) {
                printf("Guests can only check availability. Please log in as a member for full access.\n");
                printf("---Start a new request---\n");
            } else {
                handle_cancel(command_line);
            }
        } else if (strcmp(command, "quit") == 0) {
            // Send quit to server
            char quit_msg[] = "QUIT";
            send(tcp_sock, quit_msg, strlen(quit_msg), 0);
            break;
        } else {
            printf("Unknown command. Type 'help' for available commands.\n");
        }
    }
    
    close(tcp_sock);
    return 0;
}

void connect_to_server() {
    struct sockaddr_in server_addr;
    
    // Create TCP socket
    tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_sock < 0) {
        perror("Socket creation failed");
        exit(1);
    }
    
    // Setup server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(LOCALHOST);
    server_addr.sin_port = htons(SERVER_M_PORT);
    
    // Connect to server
    if (connect(tcp_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        exit(1);
    }
}

// as given in spec
int get_dynamic_port() {
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    
    if (getsockname(tcp_sock, (struct sockaddr*)&addr, &addr_len) < 0) {
        perror("getsockname failed");
        return -1;
    }
    
    return ntohs(addr.sin_port);
}

// authenticate guest or member: serverM <-> serverA
int authenticate(const char* user, const char* pass) {
    char send_buf[MAXBUFLEN];
    char recv_buf[MAXBUFLEN];
    
    // Send authentication request
    sprintf(send_buf, "%s %s", user, pass);
    
    printf("%s sent an authentication request to the main server.\n", user);
    
    if (send(tcp_sock, send_buf, strlen(send_buf), 0) < 0) {
        perror("send failed");
        return 0;
    }
    
    // Receive authentication response
    int recv_len = recv(tcp_sock, recv_buf, MAXBUFLEN - 1, 0);
    if (recv_len < 0) {
        perror("recv failed");
        return 0;
    }
    recv_buf[recv_len] = '\0';
    
    // Parse response
    if (strncmp(recv_buf, "SUCCESS:GUEST", 13) == 0) {
        printf("You have been granted guest access.\n");
        strcpy(username, "guest");
        is_guest = 1;
        is_authenticated = 1;
        return 1;
    } else if (strncmp(recv_buf, "SUCCESS:", 8) == 0) {
        printf("%s received the authentication result.\n", user);
        printf("Authentication successful.\n");
        strcpy(username, user);
        is_guest = 0;
        is_authenticated = 1;
        return 1;
    } else {
        printf("Authentication failed: username or password is incorrect.\n");
        return 0;
    }
}

void display_help() {
    if (is_guest) {
        printf("Please enter the command: <search <parking lot>>, <quit>\n");
    } else {
        printf("Please enter the command: <search <parking lot>>, <reserve <space> <timeslots>>, <lookup>, <cancel <space> <timeslots>>, <quit>\n");
    }
}

void handle_search(char* command_line) {
    char send_buf[MAXBUFLEN];
    char recv_buf[MAXBUFLEN];
    char parking_lot[50] = "ALL";
    
    // Parse parking lot name if provided
    char command[50];
    if (sscanf(command_line, "%s %s", command, parking_lot) < 1) {
        return;
    }
    
    // If only "search" was entered, use "ALL"
    if (sscanf(command_line, "%s %s", command, parking_lot) == 1) {
        strcpy(parking_lot, "ALL");
    }
    
    // Send search request
    sprintf(send_buf, "SEARCH %s", parking_lot);
    
    if (is_guest) {
        printf("Guest sent an availability request to the main server.\n");
    } else {
        printf("%s sent an availability request to the main server.\n", username);
    }
    
    if (send(tcp_sock, send_buf, strlen(send_buf), 0) < 0) {
        perror("send failed");
        return;
    }
    
    // Receive response
    int recv_len = recv(tcp_sock, recv_buf, MAXBUFLEN - 1, 0);
    if (recv_len < 0) {
        perror("recv failed");
        return;
    }
    recv_buf[recv_len] = '\0';
    
    int client_port = get_dynamic_port();
    printf("The client received the response from the main server using TCP over port %d.\n", client_port);
    
    // Parse and display response
    if (strcmp(recv_buf, "INVALID") == 0) {
        printf("Invalid parking lot name.\n");
    } else if (strncmp(recv_buf, "NONE:", 5) == 0) {
        char lot_name[50];
        sscanf(recv_buf, "NONE:%s", lot_name);
        if (strcmp(lot_name, "ALL") == 0) {
            printf("No available spaces in any parking lot.\n");
        } else {
            printf("No available spaces in %s.\n", lot_name);
        }
    } else {
        // Parse format: "space1:slot1 slot2|space2:slot3 slot4|..."
        char* space_token = strtok(recv_buf, "|");
        while (space_token != NULL) {
            char space_id[10];
            char slots[MAXBUFLEN];
            
            if (sscanf(space_token, "%[^:]:%[^\n]", space_id, slots) == 2) {
                printf("%s: Time slot(s) %s\n", space_id, slots);
            }
            
            space_token = strtok(NULL, "|");
        }
    }
    
    printf("---Start a new request---\n");
}

void handle_reserve(char* command_line) {
    char send_buf[MAXBUFLEN];
    char recv_buf[MAXBUFLEN];
    char space_code[50];
    char timeslots[MAXBUFLEN - 100];
    
    // Parse command: "reserve space_code timeslot1 timeslot2 ..."
    int offset = strlen("reserve ");
    if (strlen(command_line) <= offset) {
        printf("Error: Space code and timeslot(s) are required. Please specify a space code and at least one timeslot.\n");
        return;
    }
    
    // Try to parse space_code and timeslots
    int parsed = sscanf(command_line + offset, "%s %[^\n]", space_code, timeslots);
    
    if (parsed < 2) {
        printf("Error: Space code and timeslot(s) are required. Please specify a space code and at least one timeslot.\n");
        return;
    }
    
    // Validate space_code format
    // Space code should be: letter followed by 3 digits (e.g., U777, H666)
    if (strlen(space_code) != 4) {
        printf("Error: Space code and timeslot(s) are required. Please specify a space code and at least one timeslot.");
        return;
    }
    
    // Check first character is a letter (U or H)
    if (space_code[0] != 'U' && space_code[0] != 'H' && 
        space_code[0] != 'u' && space_code[0] != 'h') {
        printf("Error: Invalid space code format. Space code must start with 'U' (UPC) or 'H' (HSC).\n");
        return;
    }
    
    // Check next 3 characters are digits
    int i;
    for (i = 1; i < 4; i++) {
        if (space_code[i] < '0' || space_code[i] > '9') {
            printf("Error: Space code and timeslot(s) are required. Please specify a space code and at least one timeslot.");
            return;
        }
    }
    
    // Validate that timeslots are numeric
    char timeslots_copy[MAXBUFLEN - 100];
    strcpy(timeslots_copy, timeslots);
    char* token = strtok(timeslots_copy, " ");
    int has_valid_timeslot = 0;
    
    while (token != NULL) {
        // Check if token is a number
        int j;
        int is_numeric = 1;
        for (j = 0; token[j] != '\0'; j++) {
            if (token[j] < '0' || token[j] > '9') {
                is_numeric = 0;
                break;
            }
        }
        
        if (!is_numeric || strlen(token) == 0) {
            printf("Error: Space code and timeslot(s) are required. Please specify a space code and at least one timeslot.\n");
            return;
        }
        
        // Check if timeslot is in valid range (1-12)
        int slot = atoi(token);
        if (slot < 1 || slot > 12) {
            printf("Error: Space code and timeslot(s) are required. Please specify a space code and at least one timeslot.\n");
            return;
        }
        
        has_valid_timeslot = 1;
        token = strtok(NULL, " ");
    }
    
    if (!has_valid_timeslot) {
        printf("Error: Space code and timeslot(s) are required. Please specify a space code and at least one timeslot.\n");
        return;
    }
    
    // Convert space code to uppercase for consistency
    space_code[0] = (space_code[0] == 'u') ? 'U' : ((space_code[0] == 'h') ? 'H' : space_code[0]);
    
    // Send reserve request
    snprintf(send_buf, MAXBUFLEN, "RESERVE %s %s", space_code, timeslots);
    
    printf("%s sent a reservation request to the main server.\n", username);
    
    if (send(tcp_sock, send_buf, strlen(send_buf), 0) < 0) {
        perror("send failed");
        return;
    }
    
    // Receive response
    int recv_len = recv(tcp_sock, recv_buf, MAXBUFLEN - 1, 0);
    if (recv_len < 0) {
        perror("recv failed");
        return;
    }
    recv_buf[recv_len] = '\0';
    
    int client_port = get_dynamic_port();
    
    // Check if partial availability
    if (strncmp(recv_buf, "PARTIAL:", 8) == 0) {
        char unavailable[MAXBUFLEN], available[MAXBUFLEN];
        sscanf(recv_buf, "PARTIAL:%[^:]:%[^\n]", unavailable, available);
        
        printf("Time slot(s) %s not available. Do you want to reserve the remaining slots? (Y/N): ", unavailable);
        
        char response[10];
        if (fgets(response, sizeof(response), stdin) == NULL) {
            return;
        }
        response[strcspn(response, "\n")] = 0;
        
        // Send Y/N response
        if (send(tcp_sock, response, strlen(response), 0) < 0) {
            perror("send failed");
            return;
        }
        
        // Receive final result
        recv_len = recv(tcp_sock, recv_buf, MAXBUFLEN - 1, 0);
        if (recv_len < 0) {
            perror("recv failed");
            return;
        }
        recv_buf[recv_len] = '\0';
    }
    
    printf("The client received the response from the main server using TCP over port %d.\n", client_port);
    
    // Parse final response
    if (strncmp(recv_buf, "SUCCESS:", 8) == 0) {
        char space[10], slots[MAXBUFLEN];
        // Format: "SUCCESS:space:slots:PRICE:amount" or "SUCCESS:space:slots:ALL_RESERVATIONS:...:PRICE:amount"
        
        // Find PRICE marker
        char* price_marker = strstr(recv_buf, ":PRICE:");
        if (price_marker != NULL) {
            // Extract space and slots from beginning
            char success_part[MAXBUFLEN];
            int prefix_len = price_marker - recv_buf;
            strncpy(success_part, recv_buf, prefix_len);
            success_part[prefix_len] = '\0';
            
            // Parse the SUCCESS part
            if (sscanf(success_part, "SUCCESS:%[^:]:%[^:]", space, slots) >= 2) {
                printf("Reservation successful for %s at time slot(s) %s.\n", space, slots);
                
                // Extract price
                double price;
                if (sscanf(price_marker, ":PRICE:%lf", &price) == 1) {
                    printf("Total cost: $%.2f\n", price);
                }
            }
        } else {
            printf("Reservation successful.\n");
        }
    } else {
        printf("Reservation failed. No slots were reserved.\n");
    }
    
    printf("---Start a new request---\n");
}

void handle_lookup() {
    char send_buf[MAXBUFLEN];
    char recv_buf[MAXBUFLEN];
    
    // Send lookup request
    sprintf(send_buf, "LOOKUP");
    
    printf("%s sent a lookup request to the main server.\n", username);
    
    if (send(tcp_sock, send_buf, strlen(send_buf), 0) < 0) {
        perror("send failed");
        return;
    }
    
    // Receive response
    int recv_len = recv(tcp_sock, recv_buf, MAXBUFLEN - 1, 0);
    if (recv_len < 0) {
        perror("recv failed");
        return;
    }
    recv_buf[recv_len] = '\0';
    
    int client_port = get_dynamic_port();
    printf("The client received the response from the main server using TCP over port %d.\n", client_port);
    
    // Parse and display response
    if (strcmp(recv_buf, "NONE") == 0) {
        printf("You have no current reservations.\n");
    } else {
        printf("Your reservations:\n");
        
        // Parse format: "space1:slot1 slot2|space2:slot3 slot4|..."
        char* space_token = strtok(recv_buf, "|");
        while (space_token != NULL) {
            char space_id[10];
            char slots[MAXBUFLEN];
            
            if (sscanf(space_token, "%[^:]:%[^\n]", space_id, slots) == 2) {
                printf("%s: Time slot(s) %s\n", space_id, slots);
            }
            
            space_token = strtok(NULL, "|");
        }
    }
    
    printf("---Start a new request---\n");
}

void handle_cancel(char* command_line) {
    char send_buf[MAXBUFLEN];
    char recv_buf[MAXBUFLEN];
    char space_code[50];
    char timeslots[MAXBUFLEN];
    
    // Parse command: "cancel space_code timeslot1 timeslot2 ..."
    int offset = strlen("cancel ");
    if (strlen(command_line) <= offset) {
        printf("Error: Space code and timeslot(s) are required. Please specify what to cancel.\n");
        return;
    }
    
    // Try to parse space_code and timeslots
    int parsed = sscanf(command_line + offset, "%s %[^\n]", space_code, timeslots);
    
    if (parsed < 2) {
        printf("Error: Space code and timeslot(s) are required. Please specify what to cancel.\n");
        return;
    }
    
    // Validate space_code format
    // Space code should be: letter followed by 3 digits (e.g., U777, H666)
    if (strlen(space_code) != 4) {
        printf("Error: Space code and timeslot(s) are required. Please specify what to cancel.\n");
        return;
    }
    
    // Check first character is a letter (U or H)
    if (space_code[0] != 'U' && space_code[0] != 'H' && 
        space_code[0] != 'u' && space_code[0] != 'h') {
        printf("Error: Invalid space code format. Space code must start with 'U' (UPC) or 'H' (HSC).\n");
        return;
    }
    
    // Check next 3 characters are digits
    int i;
    for (i = 1; i < 4; i++) {
        if (space_code[i] < '0' || space_code[i] > '9') {
            printf("Error: Space code and timeslot(s) are required. Please specify what to cancel.\n");
            return;
        }
    }
    
    // Validate that timeslots are numeric
    char timeslots_copy[MAXBUFLEN];
    strcpy(timeslots_copy, timeslots);
    char* token = strtok(timeslots_copy, " ");
    int has_valid_timeslot = 0;
    
    while (token != NULL) {
        // Check if token is a number
        int j;
        int is_numeric = 1;
        for (j = 0; token[j] != '\0'; j++) {
            if (token[j] < '0' || token[j] > '9') {
                is_numeric = 0;
                break;
            }
        }
        
        if (!is_numeric || strlen(token) == 0) {
            printf("Error: Invalid timeslot '%s'. Timeslots must be numeric values.\n", token);
            return;
        }
        
        // Check if timeslot is in valid range (1-12)
        int slot = atoi(token);
        if (slot < 1 || slot > 12) {
            printf("Error: Invalid timeslot %d. Timeslots must be between 1 and 12.\n", slot);
            return;
        }
        
        has_valid_timeslot = 1;
        token = strtok(NULL, " ");
    }
    
    if (!has_valid_timeslot) {
        printf("Error: Space code and timeslot(s) are required. Please specify what to cancel.\n");
        return;
    }
    
    // Convert space code to uppercase for consistency
    space_code[0] = (space_code[0] == 'u') ? 'U' : ((space_code[0] == 'h') ? 'H' : space_code[0]);
    
    // Send cancel request
    snprintf(send_buf, MAXBUFLEN, "CANCEL %s %s", space_code, timeslots);
    
    printf("%s sent a cancellation request to the main server.\n", username);
    
    if (send(tcp_sock, send_buf, strlen(send_buf), 0) < 0) {
        perror("send failed");
        return;
    }
    
    // Receive response
    int recv_len = recv(tcp_sock, recv_buf, MAXBUFLEN - 1, 0);
    if (recv_len < 0) {
        perror("recv failed");
        return;
    }
    recv_buf[recv_len] = '\0';
    
    int client_port = get_dynamic_port();
    printf("The client received the response from the main server using TCP over port %d.\n", client_port);
    
    // Parse response
    if (strncmp(recv_buf, "SUCCESS:", 8) == 0) {
        char space[10], slots[MAXBUFLEN], refund_info[MAXBUFLEN];
        // Format: "SUCCESS:space:slots:REFUND:amount"
        if (sscanf(recv_buf, "SUCCESS:%[^:]:%[^:]:%s", space, slots, refund_info) >= 2) {
            printf("Cancellation successful for %s at time slot(s) %s.\n", space, slots);
            
            if (strncmp(refund_info, "REFUND:", 7) == 0) {
                double refund;
                sscanf(refund_info, "REFUND:%lf", &refund);
                printf("Refund amount: $%.2f\n", refund);
            }
        }
    } else {
        printf("Cancellation failed: You do not have reservations for the specified slots.\n");
    }
    
    printf("---Start a new request---\n");
}