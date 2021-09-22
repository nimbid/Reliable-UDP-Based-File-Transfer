# Compiler options.
CC = gcc
CFLAGS = -Wall -Werror

# Folders
SRC = src
OBJ = obj
RUN = run
SERVER = Server
CLIENT = Client

# Directory structure:
# src/Client/uftp_client.c
# src/Server/uftp_server.c
# obj/ --> want uftp_server.o and uftp_client.o to be here.
# run/ --> want uftp_server and uftp_client execs to be here.

# Create obj/ if not already present. This will run when make is parsed
# before any of the targets are run.
$(shell mkdir -p $(OBJ))

all                 : $(RUN)/uftp_client $(RUN)/uftp_server

$(RUN)/uftp_client  : $(OBJ)/uftp_client.o
					$(CC) -o $(RUN)/uftp_client $(OBJ)/uftp_client.o

$(OBJ)/uftp_client.o: $(SRC)/$(CLIENT)/uftp_client.c
					$(CC) $(CFLAGS) -o $(OBJ)/uftp_client.o -c $(SRC)/$(CLIENT)/uftp_client.c 

$(RUN)/uftp_server  : $(OBJ)/uftp_server.o
					$(CC) -o $(RUN)/uftp_server $(OBJ)/uftp_server.o

$(OBJ)/uftp_server.o: $(SRC)/$(SERVER)/uftp_server.c
					$(CC) $(CFLAGS) -o $(OBJ)/uftp_server.o -c $(SRC)/$(SERVER)/uftp_server.c

clean:
	rm $(OBJ)/uftp_client.o
	rm $(OBJ)/uftp_server.o
	rm $(RUN)/uftp_client
	rm $(RUN)/uftp_server