// serverP.c
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
#define SERVER_P_PORT 23893
#define LOCALHOST "127.0.0.1"
#define MAXBUFLEN 4096
#define NUM_TIMESLOTS 12

// Pricing constants
#define UPC_BASE_RATE 10.0
#define UPC_ADDITIONAL_RATE 7.0
#define HSC_BASE_RATE 15.0
#define HSC_ADDITIONAL_RATE 10.0
#define PEAK_MULTIPLIER 1.5
#define UPC_REFUND_RATE 7.0
#define HSC_REFUND_RATE 10.0

// Global variables
int udp_sock;

// Function prototypes
static void handle_request(char* request, struct sockaddr_in* client_addr, socklen_t client_len);
static void handle_price_request(char* username, char* reservations, struct sockaddr_in* client_addr, socklen_t client_len);
static void handle_refund_request(char* username, int user_id, char* space_code, char* timeslots_str,
                          struct sockaddr_in* client_addr, socklen_t client_len);
static double calculate_total_price(char* reservations);
static double calculate_refund(char* space_code, char* timeslots_str);
static int is_peak_hour(int timeslot);

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
    server_addr.sin_port = htons(SERVER_P_PORT);
    
    // Bind socket
    if (bind(udp_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(1);
    }
    
    printf("Server P is up and running using UDP on port %d.\n", SERVER_P_PORT);
    
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

static int is_peak_hour(int timeslot) {
    // Peak hours: 8-10 AM (slot 5) and 4-6 PM (slot 9)
    // Each timeslot represents 2 hours
    return (timeslot == 5 || timeslot == 9);
}

static void handle_request(char* request, struct sockaddr_in* client_addr, socklen_t client_len) {
    char command[50];
    sscanf(request, "%s", command);
    
    if (strcmp(command, "PRICE") == 0) {
        char username[100];
        char reservations[MAXBUFLEN];
        // Format: "PRICE username reservations_data"
        // Parse username
        int offset = strlen("PRICE ");
        if (sscanf(request + offset, "%s %[^\n]", username, reservations) >= 1) {
            // If no reservations data, it will be empty
            if (sscanf(request + offset, "%s %[^\n]", username, reservations) < 2) {
                strcpy(reservations, "NONE");
            }
            handle_price_request(username, reservations, client_addr, client_len);
        }
    } else if (strcmp(command, "REFUND") == 0) {
        char username[100], space_code[10], timeslots_str[MAXBUFLEN];
        int user_id;
        // Format: "REFUND username userid space_code timeslot1 timeslot2 ..."
        int offset = strlen("REFUND ");
        sscanf(request + offset, "%s %d %s %[^\n]", username, &user_id, space_code, timeslots_str);
        handle_refund_request(username, user_id, space_code, timeslots_str, client_addr, client_len);
    }
}

static void handle_price_request(char* username, char* reservations, struct sockaddr_in* client_addr, socklen_t client_len) {
    char response[MAXBUFLEN];
    
    printf("Server P received a pricing request from the main server.\n");
    
    printf("DEBUG: Server P received reservations: '%s'\n", reservations);
    
    // Calculate total price
    double total_price = calculate_total_price(reservations);
    
    printf("Calculated total price of $%.2f for %s.\n", total_price, username);
    
    sprintf(response, "PRICE:%.2f", total_price);
    
    sendto(udp_sock, response, strlen(response), 0,
           (struct sockaddr*)client_addr, client_len);
    
    printf("Server P finished sending the price to the main server.\n");
}

double calculate_total_price(char* reservations) {
    double total = 0.0;
    
    // Check if no reservations
    if (strcmp(reservations, "NONE") == 0) {
        printf("DEBUG: No reservations found\n");
        return 0.0;
    }
    
    // Count ALL timeslots per parking lot (not unique, count duplicates)
    int upc_timeslot_count = 0;
    int hsc_timeslot_count = 0;
    int upc_peak_count = 0;
    int hsc_peak_count = 0;
    
    printf("DEBUG: Parsing reservations: %s\n", reservations);
    
    // Parse reservations - format: "space1:slot1 slot2|space2:slot3 slot4|..."
    char reservations_copy[MAXBUFLEN];
    strcpy(reservations_copy, reservations);
    
    // Manually parse using | delimiter to avoid nested strtok
    char* current = reservations_copy;
    char* pipe_pos;
    
    while (current != NULL && *current != '\0') {
        // Find next pipe or end of string
        pipe_pos = strchr(current, '|');
        if (pipe_pos != NULL) {
            *pipe_pos = '\0';  // Terminate this token
        }
        
        // Now parse this space entry: "space_id:slot1 slot2 slot3"
        char space_id[10];
        char slots_str[MAXBUFLEN];
        
        if (sscanf(current, "%[^:]:%[^\n]", space_id, slots_str) == 2) {
            char lot_type = space_id[0];
            
            printf("DEBUG: Processing space %s (lot type: %c) with slots: %s\n", space_id, lot_type, slots_str);
            
            // Parse the slots and count them ALL (including duplicates across spaces)
            char slots_copy[MAXBUFLEN];
            strcpy(slots_copy, slots_str);
            char* slot_token = strtok(slots_copy, " ");
            while (slot_token != NULL) {
                int slot = atoi(slot_token);
                
                if (slot >= 1 && slot <= NUM_TIMESLOTS) {
                    if (lot_type == 'U') {
                        upc_timeslot_count++;
                        if (is_peak_hour(slot)) {
                            upc_peak_count++;
                        }
                        printf("DEBUG: Counted UPC slot %d (total UPC count now: %d)\n", slot, upc_timeslot_count);
                    } else if (lot_type == 'H') {
                        hsc_timeslot_count++;
                        if (is_peak_hour(slot)) {
                            hsc_peak_count++;
                        }
                        printf("DEBUG: Counted HSC slot %d (total HSC count now: %d)\n", slot, hsc_timeslot_count);
                    }
                }
                
                slot_token = strtok(NULL, " ");
            }
        }
        
        // Move to next space entry
        if (pipe_pos != NULL) {
            current = pipe_pos + 1;
        } else {
            current = NULL;  // No more entries
        }
    }
    
    printf("DEBUG: UPC total timeslots: %d (Peak: %d)\n", upc_timeslot_count, upc_peak_count);
    printf("DEBUG: HSC total timeslots: %d (Peak: %d)\n", hsc_timeslot_count, hsc_peak_count);
    
    // Calculate UPC cost
    // Each timeslot = 2 hours
    // First 2 hours at base rate, rest at additional rate
    if (upc_timeslot_count > 0) {
        int regular_timeslots = upc_timeslot_count - upc_peak_count;
        int peak_timeslots = upc_peak_count;
        
        printf("DEBUG: UPC - Regular timeslots: %d, Peak timeslots: %d\n", regular_timeslots, peak_timeslots);
        
        int total_hours_used = 0;
        double upc_cost = 0.0;
        
        // Process regular (non-peak) timeslots first
        if (regular_timeslots > 0) {
            int regular_hours = regular_timeslots * 2;
            
            if (total_hours_used < 2) {
                // Still in first 2 hours
                int base_hours = (regular_hours <= (2 - total_hours_used)) ? regular_hours : (2 - total_hours_used);
                upc_cost += base_hours * UPC_BASE_RATE;
                printf("DEBUG: UPC regular base hours: %d x $%.2f = $%.2f\n", base_hours, UPC_BASE_RATE, base_hours * UPC_BASE_RATE);
                total_hours_used += base_hours;
                regular_hours -= base_hours;
            }
            
            // Remaining regular hours at additional rate
            if (regular_hours > 0) {
                upc_cost += regular_hours * UPC_ADDITIONAL_RATE;
                printf("DEBUG: UPC regular additional hours: %d x $%.2f = $%.2f\n", regular_hours, UPC_ADDITIONAL_RATE, regular_hours * UPC_ADDITIONAL_RATE);
                total_hours_used += regular_hours;
            }
        }
        
        // Process peak timeslots
        if (peak_timeslots > 0) {
            int peak_hours = peak_timeslots * 2;
            
            if (total_hours_used < 2) {
                // Still in first 2 hours
                int base_hours = (peak_hours <= (2 - total_hours_used)) ? peak_hours : (2 - total_hours_used);
                upc_cost += base_hours * UPC_BASE_RATE * PEAK_MULTIPLIER;
                printf("DEBUG: UPC peak base hours: %d x $%.2f x %.2f = $%.2f\n", base_hours, UPC_BASE_RATE, PEAK_MULTIPLIER, base_hours * UPC_BASE_RATE * PEAK_MULTIPLIER);
                total_hours_used += base_hours;
                peak_hours -= base_hours;
            }
            
            // Remaining peak hours at additional rate with multiplier
            if (peak_hours > 0) {
                upc_cost += peak_hours * UPC_ADDITIONAL_RATE * PEAK_MULTIPLIER;
                printf("DEBUG: UPC peak additional hours: %d x $%.2f x %.2f = $%.2f\n", peak_hours, UPC_ADDITIONAL_RATE, PEAK_MULTIPLIER, peak_hours * UPC_ADDITIONAL_RATE * PEAK_MULTIPLIER);
                total_hours_used += peak_hours;
            }
        }
        
        printf("DEBUG: UPC total cost: $%.2f\n", upc_cost);
        total += upc_cost;
    }
    
    // Calculate HSC cost
    // Each timeslot = 2 hours
    // First 2 hours at base rate, rest at additional rate
    if (hsc_timeslot_count > 0) {
        int regular_timeslots = hsc_timeslot_count - hsc_peak_count;
        int peak_timeslots = hsc_peak_count;
        
        int total_hours_used = 0;
        double hsc_cost = 0.0;
        
        // Process regular (non-peak) timeslots first
        if (regular_timeslots > 0) {
            int regular_hours = regular_timeslots * 2;
            
            if (total_hours_used < 2) {
                // Still in first 2 hours
                int base_hours = (regular_hours <= (2 - total_hours_used)) ? regular_hours : (2 - total_hours_used);
                hsc_cost += base_hours * HSC_BASE_RATE;
                total_hours_used += base_hours;
                regular_hours -= base_hours;
            }
            
            // Remaining regular hours at additional rate
            if (regular_hours > 0) {
                hsc_cost += regular_hours * HSC_ADDITIONAL_RATE;
                total_hours_used += regular_hours;
            }
        }
        
        // Process peak timeslots
        if (peak_timeslots > 0) {
            int peak_hours = peak_timeslots * 2;
            
            if (total_hours_used < 2) {
                // Still in first 2 hours
                int base_hours = (peak_hours <= (2 - total_hours_used)) ? peak_hours : (2 - total_hours_used);
                hsc_cost += base_hours * HSC_BASE_RATE * PEAK_MULTIPLIER;
                total_hours_used += base_hours;
                peak_hours -= base_hours;
            }
            
            // Remaining peak hours at additional rate with multiplier
            if (peak_hours > 0) {
                hsc_cost += peak_hours * HSC_ADDITIONAL_RATE * PEAK_MULTIPLIER;
                total_hours_used += peak_hours;
            }
        }
        
        printf("DEBUG: HSC total cost: $%.2f\n", hsc_cost);
        total += hsc_cost;
    }
    
    printf("DEBUG: Final total price: $%.2f\n", total);
    return total;
}

static void handle_refund_request(char* username, int user_id, char* space_code, char* timeslots_str,
                          struct sockaddr_in* client_addr, socklen_t client_len) {
    char response[MAXBUFLEN];
    
    printf("Server P received a refund request from the main server.\n");
    
    // Calculate refund
    double refund = calculate_refund(space_code, timeslots_str);
    
    printf("Calculated refund of $%.2f for %s.\n", refund, username);
    
    sprintf(response, "REFUND:%.2f", refund);
    
    sendto(udp_sock, response, strlen(response), 0,
           (struct sockaddr*)client_addr, client_len);
    
    printf("Server P finished sending the refund amount to the main server.\n");
}

static double calculate_refund(char* space_code, char* timeslots_str) {
    double refund = 0.0;
    char lot_type = space_code[0];
    
    // Parse timeslots and count them
    int slot_count = 0;
    char timeslots_copy[MAXBUFLEN];
    strcpy(timeslots_copy, timeslots_str);
    
    char* token = strtok(timeslots_copy, " ");
    while (token != NULL) {
        slot_count++;
        token = strtok(NULL, " ");
    }
    
    // Calculate refund based on lot type
    // Each timeslot = 2 hours
    if (lot_type == 'U') {
        refund = slot_count * 2 * UPC_REFUND_RATE;
    } else if (lot_type == 'H') {
        refund = slot_count * 2 * HSC_REFUND_RATE;
    }
    
    return refund;
}