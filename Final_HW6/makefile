# Makefile

# Compiler
CC = gcc

# Targets
SERVER_TARGET = BibakBOXServer
CLIENT_TARGET = BibakBOXClient

# Source files
SERVER_SRC = BibakBOXServer.c
CLIENT_SRC = BibakBOXClient.c

# Default target
all: $(SERVER_TARGET) $(CLIENT_TARGET)

# Server target
$(SERVER_TARGET): $(SERVER_SRC)
	$(CC) $^ -o $@

# Client target
$(CLIENT_TARGET): $(CLIENT_SRC)
	$(CC) $^ -o $@

# Clean
clean:
	rm -f $(SERVER_TARGET) $(CLIENT_TARGET)
