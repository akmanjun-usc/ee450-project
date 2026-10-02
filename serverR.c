// serverR.c
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
#define SERVER_R_PORT 22893
#define LOCALHOST "127.0.0.1"
#define MAXBUFLEN 4096
#define MAX_SPACES 200
#define NUM_TIMESLOTS 12
#define MAX_SLOTS_STR 512
#define MAX_SPACE_INFO 1024

// Structure to hold parking space information
typedef struct {
    char space_id[10];      // e.g., "U666", "H777"
    int timeslots[NUM_TIMESLOTS];  // 0 = available, user_id = reserved
} ParkingSpace;

// Global variables
ParkingSpace spaces[MAX_SPACES];
int space_count = 0;
int udp_sock;

// Function prototypes
static void load_spaces();
static void handle_request(char* request, struct sockaddr_in* client_addr, socklen_t client_len);
static void handle_search_request(char* parking_lot, struct sockaddr_in* client_addr, socklen_t client_len);
static void handle_reserve_request(char* username, int user_id, char* space_code, char* timeslots_str, 
                           struct sockaddr_in* client_addr, socklen_t client_len);
static void handle_lookup_request(char* username, int user_id, struct sockaddr_in* client_addr, socklen_t client_len);
static void handle_cancel_request(char* username, int user_id, char* space_code, char* timeslots_str,
                          struct sockaddr_in* client_addr, socklen_t client_len);
static void handle_confirm_request(char* username, int user_id, char* confirmation,
                           struct sockaddr_in* client_addr, socklen_t client_len);
static int find_space_index(const char* space_id);

// Temporary storage for partial reservation
typedef struct {
    char space_code[10];
    int available_slots[NUM_TIMESLOTS];
    int available_count;
    int user_id;
    char username[100];
} PendingReservation;

PendingReservation pending;

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
    server_addr.sin_port = htons(SERVER_R_PORT);
    
    // Bind socket
    if (bind(udp_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(1);
    }
    
    // Load spaces from file into memory - file remains unchanged
    load_spaces();
    
    printf("Server R is up and running using UDP on port %d.\n", SERVER_R_PORT);
    
    // Main loop - receive and process requests
    while (1) {
        client_len = sizeof(client_addr);
        
        int recv_len = recvfrom(udp_sock, buffer, MAXBUFLEN - 1, 0,
                                (struct sockaddr*)&client_addr, &client_len);
        
        if (recv_len < 0) {
            perror("recvfrom failed");
            continue;
        }
        
        buffer[recv_len] = '\0';
        
        // Handle request
        handle_request(buffer, &client_addr, client_len);
    }
    
    close(udp_sock);
    return 0;
}

void load_spaces() {
    FILE* fp = fopen("spaces.txt", "r");
    if (fp == NULL) {
        perror("Error opening spaces.txt");
        exit(1);
    }
    
    space_count = 0;
    
    // Read spaces from file into memory
    // Format: space_id slot1 slot2 ... slot12
    while (fscanf(fp, "%s", spaces[space_count].space_id) == 1) {
        int i;
        for (i = 0; i < NUM_TIMESLOTS; i++) {
            if (fscanf(fp, "%d", &spaces[space_count].timeslots[i]) != 1) {
                fprintf(stderr, "Error reading timeslots for space %s\n", spaces[space_count].space_id);
                exit(1);
            }
        }
        
        space_count++;
        
        if (space_count >= MAX_SPACES) {
            fprintf(stderr, "Warning: Maximum number of spaces reached\n");
            break;
        }
    }
    
    fclose(fp);
}

static int find_space_index(const char* space_id) {
    int i;
    for (i = 0; i < space_count; i++) {
        if (strcmp(spaces[i].space_id, space_id) == 0) {
            return i;
        }
    }
    return -1;
}

static void handle_request(char* request, struct sockaddr_in* client_addr, socklen_t client_len) {
    char command[50];
    sscanf(request, "%s", command);
    
    if (strcmp(command, "SEARCH") == 0) {
        char parking_lot[50];
        sscanf(request, "%s %s", command, parking_lot);
        handle_search_request(parking_lot, client_addr, client_len);
    } else if (strcmp(command, "RESERVE") == 0) {
        char username[100], space_code[10], timeslots_str[MAXBUFLEN];
        int user_id;
        // Format: "RESERVE username userid space_code timeslot1 timeslot2 ..."
        int offset = strlen("RESERVE ");
        sscanf(request + offset, "%s %d %s %[^\n]", username, &user_id, space_code, timeslots_str);
        handle_reserve_request(username, user_id, space_code, timeslots_str, client_addr, client_len);
    } else if (strcmp(command, "LOOKUP") == 0) {
        char username[100];
        int user_id;
        sscanf(request, "%s %s %d", command, username, &user_id);
        handle_lookup_request(username, user_id, client_addr, client_len);
    } else if (strcmp(command, "CANCEL") == 0) {
        char username[100], space_code[10], timeslots_str[MAXBUFLEN];
        int user_id;
        // Format: "CANCEL username userid space_code timeslot1 timeslot2 ..."
        int offset = strlen("CANCEL ");
        sscanf(request + offset, "%s %d %s %[^\n]", username, &user_id, space_code, timeslots_str);
        handle_cancel_request(username, user_id, space_code, timeslots_str, client_addr, client_len);
    } else if (strcmp(command, "CONFIRM") == 0) {
        char username[100], confirmation[10];
        int user_id;
        sscanf(request, "%s %s %d %s", command, username, &user_id, confirmation);
        handle_confirm_request(username, user_id, confirmation, client_addr, client_len);
    }
}

static void handle_search_request(char* parking_lot, struct sockaddr_in* client_addr, socklen_t client_len) {
    char response[MAXBUFLEN];
    int i, j;
    
    printf("Server R received an availability request from the main server.\n");
    
    response[0] = '\0';
    
    // Check if valid parking lot or "ALL"
    int valid_lot = (strcmp(parking_lot, "ALL") == 0 || 
                     strcmp(parking_lot, "UPC") == 0 || 
                     strcmp(parking_lot, "HSC") == 0);
    
    if (!valid_lot) {
        sprintf(response, "INVALID");
        sendto(udp_sock, response, strlen(response), 0,
               (struct sockaddr*)client_addr, client_len);
        printf("Server R finished sending the response to the main server.\n");
        return;
    }
    
    int found_any = 0;
    
    // Iterate through all spaces
    for (i = 0; i < space_count; i++) {
        char lot_type = spaces[i].space_id[0];
        
        // Filter by parking lot
        if (strcmp(parking_lot, "ALL") != 0) {
            if (strcmp(parking_lot, "UPC") == 0 && lot_type != 'U') continue;
            if (strcmp(parking_lot, "HSC") == 0 && lot_type != 'H') continue;
        }
        
        // Find available timeslots for this space
        char available_slots[MAX_SLOTS_STR] = "";
        int has_available = 0;
        
        for (j = 0; j < NUM_TIMESLOTS; j++) {
            if (spaces[i].timeslots[j] == 0) {
                char slot_str[10];
                sprintf(slot_str, "%d ", j + 1);
                strcat(available_slots, slot_str);
                has_available = 1;
            }
        }
        
        // If this space has available slots, add to response
        if (has_available) {
            // char space_info[MAXBUFLEN];
            char space_info[MAX_SPACE_INFO];
            // Remove trailing space
            if (strlen(available_slots) > 0) {
                available_slots[strlen(available_slots) - 1] = '\0';
            }
            // sprintf(space_info, "%s:%s|", spaces[i].space_id, available_slots);
            snprintf(space_info, MAX_SPACE_INFO, "%s:%s|", spaces[i].space_id, available_slots);
            strcat(response, space_info);
            found_any = 1;
        }
    }
    
    if (!found_any) {
        sprintf(response, "NONE:%s", parking_lot);
    } else {
        // Remove trailing '|'
        if (strlen(response) > 0 && response[strlen(response) - 1] == '|') {
            response[strlen(response) - 1] = '\0';
        }
    }
    
    sendto(udp_sock, response, strlen(response), 0,
           (struct sockaddr*)client_addr, client_len);
    
    printf("Server R finished sending the response to the main server.\n");
}

void handle_reserve_request(char* username, int user_id, char* space_code, char* timeslots_str,
                           struct sockaddr_in* client_addr, socklen_t client_len) {
    char response[MAXBUFLEN];
    int requested_slots[NUM_TIMESLOTS];
    int requested_count = 0;
    
    printf("Server R received a reservation request from the main server.\n");
    
    // Parse timeslots
    char* token = strtok(timeslots_str, " ");
    while (token != NULL && requested_count < NUM_TIMESLOTS) {
        requested_slots[requested_count++] = atoi(token);
        token = strtok(NULL, " ");
    }
    
    // Find space
    int space_idx = find_space_index(space_code);
    if (space_idx == -1) {
        snprintf(response, MAXBUFLEN, "FAIL:Space not found");
        sendto(udp_sock, response, strlen(response), 0,
               (struct sockaddr*)client_addr, client_len);
        return;
    }
    
    // Check availability of requested slots
    int unavailable_slots[NUM_TIMESLOTS];
    int unavailable_count = 0;
    int available_slots[NUM_TIMESLOTS];
    int available_count = 0;
    
    int i;
    for (i = 0; i < requested_count; i++) {
        int slot_idx = requested_slots[i] - 1; // Convert to 0-based index
        
        if (slot_idx < 0 || slot_idx >= NUM_TIMESLOTS) {
            continue; // Invalid slot
        }
        
        if (spaces[space_idx].timeslots[slot_idx] == 0) {
            available_slots[available_count++] = requested_slots[i];
        } else {
            unavailable_slots[unavailable_count++] = requested_slots[i];
        }
    }
    
    if (unavailable_count == 0) {
        // All slots available
        printf("All requested time slots are available.\n");
        
        // Reserve all slots
        for (i = 0; i < available_count; i++) {
            int slot_idx = available_slots[i] - 1;
            spaces[space_idx].timeslots[slot_idx] = user_id;
        }
        
        // Format available slots for response
        char slots_str[MAX_SLOTS_STR] = "";
        for (i = 0; i < available_count; i++) {
            char slot[10];
            sprintf(slot, "%d ", available_slots[i]);
            strcat(slots_str, slot);
        }
        if (strlen(slots_str) > 0) {
            slots_str[strlen(slots_str) - 1] = '\0'; // Remove trailing space
        }
        
        printf("Successfully reserved %s at time slot(s) %s for %s.\n", space_code, slots_str, username);
        
        // Get all user's reservations to include in response
        char all_reservations[MAXBUFLEN] = "";
        int j, k;
        int found_any = 0;
        
        for (j = 0; j < space_count; j++) {
            char reserved_slots[MAX_SLOTS_STR] = "";
            int has_reservation = 0;
            
            // Find timeslots reserved by this user
            for (k = 0; k < NUM_TIMESLOTS; k++) {
                if (spaces[j].timeslots[k] == user_id) {
                    char slot_str[10];
                    sprintf(slot_str, "%d ", k + 1);
                    strcat(reserved_slots, slot_str);
                    has_reservation = 1;
                }
            }
            
            // If user has reservations in this space, add to response
            if (has_reservation) {
                char space_info[MAX_SPACE_INFO];
                // Remove trailing space
                if (strlen(reserved_slots) > 0) {
                    reserved_slots[strlen(reserved_slots) - 1] = '\0';
                }
                sprintf(space_info, "%s:%s|", spaces[j].space_id, reserved_slots);
                strcat(all_reservations, space_info);
                found_any = 1;
            }
        }
        
        // Remove trailing '|'
        if (strlen(all_reservations) > 0 && all_reservations[strlen(all_reservations) - 1] == '|') {
            all_reservations[strlen(all_reservations) - 1] = '\0';
        }
        
        // Response format: "SUCCESS:space:slots:ALL_RESERVATIONS:reservation_data"
        snprintf(response, MAXBUFLEN, "SUCCESS:%s:%s:ALL_RESERVATIONS:%s", 
                 space_code, slots_str, found_any ? all_reservations : "NONE");
        sendto(udp_sock, response, strlen(response), 0,
               (struct sockaddr*)client_addr, client_len);
    } else if (available_count > 0) {
        // Partial availability
        char unavailable_str[MAX_SLOTS_STR] = "";
        for (i = 0; i < unavailable_count; i++) {
            char slot[10];
            sprintf(slot, "%d ", unavailable_slots[i]);
            strcat(unavailable_str, slot);
        }
        if (strlen(unavailable_str) > 0) {
            unavailable_str[strlen(unavailable_str) - 1] = '\0';
        }
        
        printf("Time slot(s) %s not available for %s. Requesting to reserve rest available slots (Y/N).\n",
               unavailable_str, space_code);
        
        // Store pending reservation
        strcpy(pending.space_code, space_code);
        pending.available_count = available_count;
        for (i = 0; i < available_count; i++) {
            pending.available_slots[i] = available_slots[i];
        }
        pending.user_id = user_id;
        strcpy(pending.username, username);
        
        // Format available slots for response
        char available_str[MAX_SLOTS_STR] = "";
        for (i = 0; i < available_count; i++) {
            char slot[10];
            sprintf(slot, "%d ", available_slots[i]);
            strcat(available_str, slot);
        }
        if (strlen(available_str) > 0) {
            available_str[strlen(available_str) - 1] = '\0';
        }
        
        snprintf(response, MAXBUFLEN, "PARTIAL:%s:%s", unavailable_str, available_str);
        sendto(udp_sock, response, strlen(response), 0,
               (struct sockaddr*)client_addr, client_len);
    } else {
        // No slots available
        char unavailable_str[MAX_SLOTS_STR] = "";
        for (i = 0; i < unavailable_count; i++) {
            char slot[10];
            sprintf(slot, "%d ", unavailable_slots[i]);
            strcat(unavailable_str, slot);
        }
        if (strlen(unavailable_str) > 0) {
            unavailable_str[strlen(unavailable_str) - 1] = '\0';
        }
        
        printf("Time slot(s) %s not available for %s. Requesting to reserve rest available slots (Y/N).\n",
               unavailable_str, space_code);
        
        snprintf(response, MAXBUFLEN, "FAIL:All requested slots unavailable");
        sendto(udp_sock, response, strlen(response), 0,
               (struct sockaddr*)client_addr, client_len);
    }
}

void handle_confirm_request(char* username, int user_id, char* confirmation,
                           struct sockaddr_in* client_addr, socklen_t client_len) {
    char response[MAXBUFLEN];
    
    if (strcmp(confirmation, "Y") == 0 || strcmp(confirmation, "y") == 0) {
        printf("User confirmed partial reservation.\n");
        
        // Reserve the available slots
        int space_idx = find_space_index(pending.space_code);
        if (space_idx != -1) {
            int i;
            for (i = 0; i < pending.available_count; i++) {
                int slot_idx = pending.available_slots[i] - 1;
                spaces[space_idx].timeslots[slot_idx] = pending.user_id;
            }
            
            // Format slots for response
            char slots_str[MAX_SLOTS_STR] = "";
            for (i = 0; i < pending.available_count; i++) {
                char slot[10];
                sprintf(slot, "%d ", pending.available_slots[i]);
                strcat(slots_str, slot);
            }
            if (strlen(slots_str) > 0) {
                slots_str[strlen(slots_str) - 1] = '\0';
            }
            
            printf("Successfully reserved %s at time slot(s) %s for %s.\n", 
                   pending.space_code, slots_str, pending.username);
            
            // Get all user's reservations to include in response
            char all_reservations[MAXBUFLEN] = "";
            int j, k;
            int found_any = 0;
            
            for (j = 0; j < space_count; j++) {
                char reserved_slots[MAX_SLOTS_STR] = "";
                int has_reservation = 0;
                
                // Find timeslots reserved by this user
                for (k = 0; k < NUM_TIMESLOTS; k++) {
                    if (spaces[j].timeslots[k] == user_id) {
                        char slot_str[10];
                        sprintf(slot_str, "%d ", k + 1);
                        strcat(reserved_slots, slot_str);
                        has_reservation = 1;
                    }
                }
                
                // If user has reservations in this space, add to response
                if (has_reservation) {
                    char space_info[MAX_SPACE_INFO];
                    // Remove trailing space
                    if (strlen(reserved_slots) > 0) {
                        reserved_slots[strlen(reserved_slots) - 1] = '\0';
                    }
                    sprintf(space_info, "%s:%s|", spaces[j].space_id, reserved_slots);
                    strcat(all_reservations, space_info);
                    found_any = 1;
                }
            }
            
            // Remove trailing '|'
            if (strlen(all_reservations) > 0 && all_reservations[strlen(all_reservations) - 1] == '|') {
                all_reservations[strlen(all_reservations) - 1] = '\0';
            }
            
            // Response format: "SUCCESS:space:slots:ALL_RESERVATIONS:reservation_data"
            snprintf(response, MAXBUFLEN, "SUCCESS:%s:%s:ALL_RESERVATIONS:%s", 
                     pending.space_code, slots_str, found_any ? all_reservations : "NONE");
        } else {
            snprintf(response, MAXBUFLEN, "FAIL:Space not found");
        }
    } else {
        printf("Reservation cancelled.\n");
        snprintf(response, MAXBUFLEN, "FAIL:User declined partial reservation");
    }
    
    sendto(udp_sock, response, strlen(response), 0,
           (struct sockaddr*)client_addr, client_len);
}

static void handle_lookup_request(char* username, int user_id, struct sockaddr_in* client_addr, socklen_t client_len) {
    char response[MAXBUFLEN];
    int i, j;
    
    printf("Server R received a lookup request from the main server.\n");
    
    response[0] = '\0';
    int found_any = 0;
    
    // Iterate through all spaces
    for (i = 0; i < space_count; i++) {
        char reserved_slots[MAX_SLOTS_STR] = "";
        int has_reservation = 0;
        
        // Find timeslots reserved by this user
        for (j = 0; j < NUM_TIMESLOTS; j++) {
            if (spaces[i].timeslots[j] == user_id) {
                char slot_str[10];
                sprintf(slot_str, "%d ", j + 1);
                strcat(reserved_slots, slot_str);
                has_reservation = 1;
                printf("DEBUG: user %d has reservation in space %s time slot %d\n", user_id, spaces[i].space_id, j);
            }
        }
        
        // If user has reservations in this space, add to response
        if (has_reservation) {
            char space_info[MAX_SPACE_INFO];
            // Remove trailing space
            if (strlen(reserved_slots) > 0) {
                reserved_slots[strlen(reserved_slots) - 1] = '\0';
            }
            snprintf(space_info, MAX_SPACE_INFO, "%s:%s|", spaces[i].space_id, reserved_slots);
            strcat(response, space_info);
            found_any = 1;
        }
    }
    
    if (!found_any) {
        sprintf(response, "NONE");
    } else {
        // Remove trailing '|'
        if (strlen(response) > 0 && response[strlen(response) - 1] == '|') {
            response[strlen(response) - 1] = '\0';
        }
    }
    
    sendto(udp_sock, response, strlen(response), 0,
           (struct sockaddr*)client_addr, client_len);
    
    printf("Server R finished sending the reservation information to the main server.\n");
}

static void handle_cancel_request(char* username, int user_id, char* space_code, char* timeslots_str,
                          struct sockaddr_in* client_addr, socklen_t client_len) {
    char response[MAXBUFLEN];
    int requested_slots[NUM_TIMESLOTS];
    int requested_count = 0;
    
    printf("Server R received a cancellation request from the main server.\n");
    
    // Parse timeslots
    char* token = strtok(timeslots_str, " ");
    while (token != NULL && requested_count < NUM_TIMESLOTS) {
        requested_slots[requested_count++] = atoi(token);
        token = strtok(NULL, " ");
    }
    
    // Find space
    int space_idx = find_space_index(space_code);
    if (space_idx == -1) {
        sprintf(response, "FAIL:Space not found");
        sendto(udp_sock, response, strlen(response), 0,
               (struct sockaddr*)client_addr, client_len);
        return;
    }
    
    // Check if all requested slots are reserved by this user
    int i;
    int all_valid = 1;
    char invalid_slots[MAX_SLOTS_STR] = "";
    
    for (i = 0; i < requested_count; i++) {
        int slot_idx = requested_slots[i] - 1;
        
        if (slot_idx < 0 || slot_idx >= NUM_TIMESLOTS) {
            all_valid = 0;
            break;
        }
        
        if (spaces[space_idx].timeslots[slot_idx] != user_id) {
            all_valid = 0;
            char slot[10];
            sprintf(slot, "%d ", requested_slots[i]);
            strcat(invalid_slots, slot);
        }
    }
    
    if (!all_valid) {
        if (strlen(invalid_slots) > 0) {
            invalid_slots[strlen(invalid_slots) - 1] = '\0';
        }
        printf("No reservation found for %s at %s time slot(s) %s.\n", username, space_code, 
               (strlen(invalid_slots) > 0) ? invalid_slots : "specified");
        
        sprintf(response, "FAIL:Slots not reserved by user");
        sendto(udp_sock, response, strlen(response), 0,
               (struct sockaddr*)client_addr, client_len);
        return;
    }
    
    // Cancel the reservation
    // char slots_str[MAXBUFLEN] = "";
    char slots_str[MAX_SLOTS_STR] = "";
    for (i = 0; i < requested_count; i++) {
        int slot_idx = requested_slots[i] - 1;
        spaces[space_idx].timeslots[slot_idx] = 0;
        
        char slot[10];
        sprintf(slot, "%d ", requested_slots[i]);
        strcat(slots_str, slot);
    }
    
    if (strlen(slots_str) > 0) {
        slots_str[strlen(slots_str) - 1] = '\0';
    }
    
    printf("Successfully cancelled reservation for %s at time slot(s) %s for %s.\n",
           space_code, slots_str, username);
    
    snprintf(response, MAXBUFLEN, "SUCCESS:%s:%s", space_code, slots_str);
    sendto(udp_sock, response, strlen(response), 0,
           (struct sockaddr*)client_addr, client_len);
}