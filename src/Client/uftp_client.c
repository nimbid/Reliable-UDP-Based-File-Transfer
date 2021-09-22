/* *****************************
 * @\file	uftp_client.c
 * @\author	Nimish Bhide
 * @\brief	Implements a reliable, UDP based FTP client. 
 * @\date	15 Sept, 2021.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <fcntl.h> // for open
#include <unistd.h> // for close
#include <errno.h>
#include <time.h>


#define BUFFSIZE (2048)
#define TIMEOUT  (4)
#define RETRY_LIMIT (30)


// Struct that forms a packet to be sent.
typedef struct frame_s{
    char data[BUFFSIZE];
    long int id;
    long int len;
}frame_t;


// Helper function to print error messages to CLI.
static void print_error(const char *message)
{
    printf("%s\n", message);
    exit(EXIT_FAILURE);
}


// Helper function to send a negative ACK to the client.
static void send_error(int socket, const struct sockaddr *dest_addr, socklen_t dest_len)
{  
    int error_ack = -1;
    sendto(socket, &error_ack, sizeof(error_ack), 0, dest_addr, dest_len);
}

/* Custom implementation of recvfrom() that polls a non-blocking
 * socket for data until timeout is reached.  
 * 
 * On receiving data-> returns no. of bytes received.
 * On timeout-> closes the socket and exits with an error.
 */
static ssize_t my_recv_from(
    int socket, void *restrict buffer, size_t length,
    int flags, struct sockaddr *restrict address,
    socklen_t *restrict address_len)
{   
    ssize_t nbytes = 0;
    time_t start = 0, end = 0;
    while ((nbytes = recvfrom(socket, buffer, length, flags, address, address_len)) <= 0)
    {   
        if (errno == EWOULDBLOCK)
        {
            end = time(0);
            if (start == 0)
            {
                start = time(0);
            }
            if (end - start > TIMEOUT)
            {
                close(socket);
                print_error("Timed out waiting for response from server.\n");
            }
        }
    }
    return nbytes;
}


// main
int main(int argc, char **argv)
{
    // Check for invalid input from CLI.
    if ((argc != 3) || (atoi(argv[2]) < 5000))
    {   
        // Print out error message explaining correct way to input.
        printf("Invalid input/port.\n");
        printf("Usage --> ./[%s] [Server IP Address] [Port Number]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in srv_addr;           // Server address.
	struct sockaddr_in cln_addr;           // Client address.
    socklen_t srv_addrlen = sizeof(srv_addr);  // Length of addresses.

	int fd;				                   // Server socket.
    struct stat st;                        // Stores the file attributes for GET.
    off_t file_size;                       // File size.
    frame_t frame;                         // Structure for storing message frames.

    char rcvd_cmd[100];                    // Received command.
    char snd_cmd[20];                      // Command to be sent.
    char snd_filename[128];                // Received filename.

    FILE *file_ptr;

    // Create a UDP socket.
    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) 
    {
        print_error("Cannot create socket.\n");
        return 0;
    }

    // Set socket as non-blocking.
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    // Bind the socket to a valid client IP address and port.
    memset(&cln_addr, 0, sizeof(cln_addr));
	cln_addr.sin_family = AF_INET;
	cln_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	cln_addr.sin_port = htons(0);

    // Print error if bind fails.
    if (bind(fd, (struct sockaddr *)&cln_addr, sizeof(cln_addr)) < 0) 
    {
		print_error("Bind failed.\n");
		return 0;
	}

    // Set server address and port.
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
	srv_addr.sin_addr.s_addr = inet_addr(argv[1]);
	srv_addr.sin_port = htons(atoi(argv[2]));

    printf("Client started.\n");

    while(1)
    {
        printf("\nAvailable commands:\n 1. get [filename] \n 2. put [filename] \n" \
        " 3. delete [filename] \n 4. ls (lists current directory on server) \n 5. exit \n");		
		scanf(" %[^\n]%*c", rcvd_cmd);

        sscanf(rcvd_cmd, "%s %s", snd_cmd, snd_filename); // Separate the actual command and filename from receive input.

        /* **************************** Handle GET **************************** */

        if ((strcmp(snd_cmd, "get") == 0) && (strcmp(snd_filename, "\0") != 0))
        {   
            int failure_ack = -1;
            int outer_ack = 0;
            printf("Getting file: %s\n", snd_filename);

            // Send command to server.
            if ((sendto(fd, rcvd_cmd, sizeof(rcvd_cmd), 0, (struct sockaddr *)&srv_addr, srv_addrlen) < 0))
            {
                print_error("Couldn't send command.\n");
            }

            my_recv_from(fd, &outer_ack, sizeof(outer_ack), 0, (struct sockaddr *)&srv_addr, &srv_addrlen); // Receive error.
            // printf("ACK for command received: %d\n", outer_ack);
            if (outer_ack == failure_ack)
            {
                print_error("No command received by server.\n");
            }

            // Check for read permissions error.
            my_recv_from(fd, &outer_ack, sizeof(outer_ack), 0, (struct sockaddr *)&srv_addr, &srv_addrlen);
            // printf("ACK for permission error: %d\n\n", outer_ack);
            if (outer_ack == failure_ack)
            {   
                if (outer_ack == failure_ack)
                    print_error("No permission to read file.\n");
            }

            // Check if that file exists locally and has write permissions; else create a new file.
            /* Change file writing logic later if needed */
            if ((access(snd_filename, F_OK) == 0) && access(snd_filename, W_OK) == 0)
            {   
                // printf("Existing file block\n");

                // Write to this file.
                long int frames_to_receive = 0;
                long int rcvd_bytes = 0;

                // Receive no. of bytes.
                my_recv_from(fd, &frames_to_receive, sizeof(frames_to_receive), 0, (struct sockaddr *) &srv_addr, (socklen_t *) &srv_addrlen); 
                printf("Frames to receive: %ld\n", frames_to_receive);

                if (frames_to_receive > 0)
                {
                    // Send ACK for no. of frames to client.
                    sendto(fd, &frames_to_receive, sizeof(frames_to_receive), 0, (struct sockaddr *)&srv_addr, srv_addrlen);

                    // Open file to write.
                    file_ptr = fopen(snd_filename, "wb");

                    // Receive all frames. Send ACK for each using stop-and-wait.
                    for (int i = 1; i <= frames_to_receive; i++)
                    {
                        // Initialise frame struct with zeros.
                        memset(&frame, 0, sizeof(frame));

                        my_recv_from(fd, &frame, sizeof(frame), 0, (struct sockaddr *)&srv_addr, (socklen_t *)&srv_addrlen); // Receive frame.
                        sendto(fd, &frame.id, sizeof(frame.id), 0, (struct sockaddr *)&srv_addr, srv_addrlen);           // Send ACK for frame.

                        // If frame ID is repeated, drop it. Keep track using i.
                        if (frame.id != i)
                        {
                            i--;
                        }
                        else
                        {
                            fwrite(frame.data, 1, frame.len, file_ptr);
                            rcvd_bytes += frame.len;
                        }

                        // printf("Frame %ld received.\n", frame.id);
                    }

                    printf("File received; size is %ld bytes.\n", rcvd_bytes);
                    fclose(file_ptr);
                }
                else
                {
                    print_error("File is empty.\n");
                }
            }
            else
            {
                // Create new file.
                // printf("New file block\n");
                long int frames_to_receive = 0;
                long int rcvd_bytes = 0;
                
                // Receive no. of bytes.
                my_recv_from(fd, &frames_to_receive, sizeof(frames_to_receive), 0, (struct sockaddr *) &srv_addr, (socklen_t *) &srv_addrlen);
                // printf("Frames to receive: %ld\n", frames_to_receive);

                if (frames_to_receive > 0)
                {   
                    // Send ACK for no. of frames to client.
                    sendto(fd, &frames_to_receive, sizeof(frames_to_receive), 0, (struct sockaddr *) &srv_addr, srv_addrlen);

                    // Open file to write.
                    file_ptr = fopen(snd_filename, "wb");

                    // Receive all frames. Send ACK for each using stop-and-wait.
                    for (int i = 1; i <= frames_to_receive; i++)
                    {
                        // Initialise frame struct with zeros.
                        memset(&frame, 0, sizeof(frame));

                        my_recv_from(fd, &frame, sizeof(frame), 0, (struct sockaddr *)&srv_addr, (socklen_t *)&srv_addrlen); // Receive frame.
                        sendto(fd, &frame.id, sizeof(frame.id), 0, (struct sockaddr *)&srv_addr, srv_addrlen);           // Send ACK for frame.

                        // If frame ID is repeated, drop it. Keep track using i.
                        if (frame.id != i)
                        {
                            i--;
                        }
                        else
                        {
                            fwrite(frame.data, 1, frame.len, file_ptr);
                            rcvd_bytes += frame.len;
                        }

                        // printf("Frame %ld received.\n", frame.id);
                    }

                    printf("File received; size is %ld bytes.\n", rcvd_bytes);
                    fclose(file_ptr);
                }
                else
                {
                    print_error("File is empty.\n");
                    send_error(fd, (struct sockaddr *)&srv_addr, srv_addrlen);
                }
            }
        }


        /* **************************** Handle PUT **************************** */

        else if ((strcmp(snd_cmd, "put") == 0) && (strcmp(snd_filename, "\0") != 0))
        {   
            int failure_ack = -1;
            int outer_ack = 0;
            printf("Sending file: %s\n", snd_filename);

            // Send command to server.
            if ((sendto(fd, rcvd_cmd, sizeof(rcvd_cmd), 0, (struct sockaddr *)&srv_addr, srv_addrlen) < 0))
            {
                print_error("Couldn't send command.\n");
            }

            // // Check for server not receiving command.
            my_recv_from(fd, &outer_ack, sizeof(outer_ack), 0, (struct sockaddr *)&srv_addr, &srv_addrlen);
            // printf("ACK for command received: %d\n", outer_ack);
            if (outer_ack == failure_ack)
            {
                print_error("No command received by server.\n");
            }
            
            // Check if that file exists and has read permissions.
            if ((access(snd_filename, F_OK) == 0) && access(snd_filename, R_OK) == 0)
            {
                long int num_frames = 0;
                long int bytes_sent = 0;
                long int ack = 0;
                int retries = 0;
                int drops = 0;
                int is_timed_out = 0;

                if (stat(snd_filename, &st) < 0)
                {
                    print_error("Failed to get file size.\n");
                }
                // Store file size of the file to be fetched.
                file_size = st.st_size; 
                printf("File size: %ld\n", file_size);

                // Open the file.
                file_ptr = fopen(snd_filename, "rb");

                // Calculate no. of frames to send.
                if ((file_size % BUFFSIZE) != 0)
                {   
                    num_frames = (file_size / BUFFSIZE) + 1;
                }
                else
                {
                    num_frames = (file_size / BUFFSIZE);
                }
                // printf("Packets to send: %ld\n", num_frames);

                // Advertise no. of packets to be sent to the receiver.
                sendto(fd, &num_frames, sizeof(num_frames), 0, (struct sockaddr *)&srv_addr, srv_addrlen);

                // Check if the server received the expected no. of frames to be sent.
                my_recv_from(fd, &outer_ack, sizeof(outer_ack), 0, (struct sockaddr *) &srv_addr, (socklen_t *) &srv_addrlen);
                // printf("Recd frames ack %d\n", outer_ack);

                // Check for ACK sent by server for frames.
                if (outer_ack == failure_ack)
                {
                    print_error("File empty error from server.\n");
                }
                
                // If server ACKs expected no. of frames, proceed with sending data.
                for (int i =1; i <= num_frames; i++)
                {
                    memset(&frame, 0, sizeof(frame));
                    ack = 0;
                    retries = 0;
                    frame.id = i;
                    frame.len = fread(frame.data, 1, BUFFSIZE, file_ptr);

                    bytes_sent += sendto(fd, &frame, sizeof(frame), 0, (struct sockaddr *)&srv_addr, srv_addrlen);    // Send a frame.
                    my_recv_from(fd, &ack, sizeof(ack), 0, (struct sockaddr *)&srv_addr, (socklen_t *)&srv_addrlen);      // Receive a frame.

                    // Send each frame and retry until it is acknowledged, as long as retries < RETRY_LIMIT.
                    while ((frame.id != ack) && (retries <= RETRY_LIMIT))
                    {   
                        drops++;
                        bytes_sent += sendto(fd, &frame, sizeof(frame), 0, (struct sockaddr *)&srv_addr, srv_addrlen);
                        my_recv_from(fd, &ack, sizeof(ack), 0, (struct sockaddr *)&srv_addr, (socklen_t *)&srv_addrlen);
                        retries++;
                        printf("Frame %ld dropped %d times; retries: %d.\n", frame.id, drops, retries);
                    }

                    if (retries == RETRY_LIMIT)
                    {
                        is_timed_out = 1;
                    }

                    retries = 0;
                    drops = 0;

                    // In case of timeout, fail the file transfer.
                    if (is_timed_out == 1)
                    {
                        print_error("File not sent. PUT failed.\n");
                    }
    
                    // printf("Frame %ld; ACK %ld\n", frame.id, ack);
                }
                printf("File sent succesfully: %ld bytes.\n", bytes_sent);
                fclose(file_ptr);
            }
            else
            {   // Error: Either file doesn't exist or user lacks permission to read.
                print_error("Invalid filename.\n");
            }
        }


        /* **************************** Handle DEL **************************** */

        else if ((strcmp(snd_cmd, "delete") == 0) && (strcmp(snd_filename, "\0") != 0))
        {   
            int ack = 0;
            int failure_ack = -1;
            printf("Deleting file: %s\n", snd_filename);

            // Send DEL command to server.
            if ((sendto(fd, rcvd_cmd, sizeof(rcvd_cmd), 0, (struct sockaddr *)&srv_addr, srv_addrlen) < 0))
            {
                print_error("Couldn't send command.\n");
            }

            my_recv_from(fd, &ack, sizeof(ack), 0, (struct sockaddr *)&srv_addr, &srv_addrlen); // Receive error.
            // printf("ACK for command received: %d\n", ack);
            if (ack == failure_ack)
            {
                print_error("No command received by server.\n");
            }

            // Check for error returned by server for whether file exists.
            my_recv_from(fd, &ack, sizeof(ack), 0, (struct sockaddr *)&srv_addr, &srv_addrlen);
            // printf("ACK received: %d\n", ack);
            if (ack == -1)
            {
                print_error("You don't have permissions to delete or file doesn't exist.\n");
            }
            else
            {
                printf("File deleted successfully.\n");
            }
        }


        /* **************************** Handle LS **************************** */

        else if (strcmp(snd_cmd, "ls") == 0)
        {   
            int ack = 0;
            int failure_ack = -1;
            int success_ack = 1;
            char ls_output[1024];
            memset(ls_output, 0, sizeof(ls_output)); // Write zeros to the file_list.

            // Send LS command to server.
            if (sendto(fd, rcvd_cmd, sizeof(rcvd_cmd), 0, (struct sockaddr *)&srv_addr, srv_addrlen) < 0)
            {
                print_error("Couldn't send command.\n");
            }

            // Check if server received the command.
            my_recv_from(fd, &ack, sizeof(ack), 0, (struct sockaddr *)&srv_addr, &srv_addrlen); // Receive error.
            if (ack == failure_ack)
            {
                print_error("No command received by server.\n");
            }

            if (my_recv_from(fd, &ack, sizeof(ack), 0, (struct sockaddr *)&srv_addr, &srv_addrlen) > 0)
            {   
                if (ack == failure_ack)
                {
                    print_error("Error listing directory.\n");
                }
            }

            // Receive ls output from the server. Send ACK=1 if successful.
            my_recv_from(fd, ls_output, 1024, 0,  (struct sockaddr *) &srv_addr, (socklen_t *) &srv_addrlen);
            // printf("List length recvd: %ld\n", strlen(ls_output));
            // printf("List bytes received: %ld\n", y);
            if (strlen(ls_output) > 0)
            {   
                sendto(fd, &success_ack, sizeof(success_ack), 0, (struct sockaddr *) &srv_addr, srv_addrlen); 
            }
            else
            {
                sendto(fd, &failure_ack, sizeof(failure_ack), 0, (struct sockaddr *) &srv_addr, srv_addrlen);
            }
            
            // If list received is not NULL, print it.
            if (ls_output[0] != '\0')
            {
                printf("\nList of files:\n%s \n", ls_output);
            }
            else
            {
                print_error("Received list is empty.\n");
            }
        }


        /* **************************** Handle EXIT **************************** */

        else if (strcmp(snd_cmd, "exit") == 0)
        {   
            printf("Closing UDP client.\n");
            close(fd); // Close the socket.
            exit(EXIT_SUCCESS);
        }


        /* **************************** Handle Invalid Cases **************************** */

        else
        {   // If user input is an invalid command, ask for input again.
            printf("Unknown command. Please choose one from the given list.\n");
            continue;
        }

        printf("Nothing else left.\n");

    } //end of while

    // Exit process after closing the socket.
    close(fd);
    exit(EXIT_SUCCESS);
}