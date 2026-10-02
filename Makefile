# Makefile for EE450 Socket Programming Project

# Compiler settings
CC = gcc
CFLAGS = -Wall -g

# Directories
OBJ_DIR = obj
EXEC_DIR = exec

# Source files
CLIENT_SRC = client.c
SERVERM_SRC = serverM.c
SERVERA_SRC = serverA.c
SERVERR_SRC = serverR.c
SERVERP_SRC = serverP.c

# Object files
CLIENT_OBJ = $(OBJ_DIR)/client.o
SERVERM_OBJ = $(OBJ_DIR)/serverM.o
SERVERA_OBJ = $(OBJ_DIR)/serverA.o
SERVERR_OBJ = $(OBJ_DIR)/serverR.o
SERVERP_OBJ = $(OBJ_DIR)/serverP.o

# Executables in exec directory
CLIENT_EXEC_DIR = $(EXEC_DIR)/client
SERVERM_EXEC_DIR = $(EXEC_DIR)/serverM
SERVERA_EXEC_DIR = $(EXEC_DIR)/serverA
SERVERR_EXEC_DIR = $(EXEC_DIR)/serverR
SERVERP_EXEC_DIR = $(EXEC_DIR)/serverP

# Executables in root (for TAs)
CLIENT_EXEC = client
SERVERM_EXEC = serverM
SERVERA_EXEC = serverA
SERVERR_EXEC = serverR
SERVERP_EXEC = serverP

# Default target - build everything
all: directories build_execs copy_to_root

# Create necessary directories
directories:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(EXEC_DIR)

# Build all executables in exec directory
build_execs: $(CLIENT_EXEC_DIR) $(SERVERM_EXEC_DIR) $(SERVERA_EXEC_DIR) $(SERVERR_EXEC_DIR) $(SERVERP_EXEC_DIR)

# Copy executables to root directory
copy_to_root: build_execs
	@cp $(CLIENT_EXEC_DIR) $(CLIENT_EXEC)
	@cp $(SERVERM_EXEC_DIR) $(SERVERM_EXEC)
	@cp $(SERVERA_EXEC_DIR) $(SERVERA_EXEC)
	@cp $(SERVERR_EXEC_DIR) $(SERVERR_EXEC)
	@cp $(SERVERP_EXEC_DIR) $(SERVERP_EXEC)

# Client
$(CLIENT_EXEC_DIR): $(CLIENT_OBJ)
	$(CC) $(CFLAGS) -o $(CLIENT_EXEC_DIR) $(CLIENT_OBJ)

$(CLIENT_OBJ): $(CLIENT_SRC)
	$(CC) $(CFLAGS) -c -o $(CLIENT_OBJ) $(CLIENT_SRC)

# Server M
$(SERVERM_EXEC_DIR): $(SERVERM_OBJ)
	$(CC) $(CFLAGS) -o $(SERVERM_EXEC_DIR) $(SERVERM_OBJ)

$(SERVERM_OBJ): $(SERVERM_SRC)
	$(CC) $(CFLAGS) -c -o $(SERVERM_OBJ) $(SERVERM_SRC)

# Server A
$(SERVERA_EXEC_DIR): $(SERVERA_OBJ)
	$(CC) $(CFLAGS) -o $(SERVERA_EXEC_DIR) $(SERVERA_OBJ)

$(SERVERA_OBJ): $(SERVERA_SRC)
	$(CC) $(CFLAGS) -c -o $(SERVERA_OBJ) $(SERVERA_SRC)

# Server R
$(SERVERR_EXEC_DIR): $(SERVERR_OBJ)
	$(CC) $(CFLAGS) -o $(SERVERR_EXEC_DIR) $(SERVERR_OBJ)

$(SERVERR_OBJ): $(SERVERR_SRC)
	$(CC) $(CFLAGS) -c -o $(SERVERR_OBJ) $(SERVERR_SRC)

# Server P
$(SERVERP_EXEC_DIR): $(SERVERP_OBJ)
	$(CC) $(CFLAGS) -o $(SERVERP_EXEC_DIR) $(SERVERP_OBJ)

$(SERVERP_OBJ): $(SERVERP_SRC)
	$(CC) $(CFLAGS) -c -o $(SERVERP_OBJ) $(SERVERP_SRC)

# Clean build artifacts
clean:
	rm -rf $(OBJ_DIR) $(EXEC_DIR)
	rm -f $(CLIENT_EXEC) $(SERVERM_EXEC) $(SERVERA_EXEC) $(SERVERR_EXEC) $(SERVERP_EXEC)

# Clean and rebuild
rebuild: clean all

# Phony targets
.PHONY: all directories build_execs copy_to_root clean rebuild