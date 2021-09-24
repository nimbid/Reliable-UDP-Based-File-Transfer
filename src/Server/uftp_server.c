/* *****************************
 * @\file	uftp_server.c
 * @\author	Nimish Bhide
 * @\brief	Implements a reliable, UDP based FTP server. 
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
#include <dirent.h>
#include <errno.h>
#include <time.h>


#define BUFFSIZE (51200)
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


// Returns a list of files in given directory.
static int get_file_list(char *list)
{
    struct dirent *d; // Directory entry pointer.
    DIR *dr = opendir("."); // Open current directory.
    char temp_str[1024];
    memset(temp_str, 0, sizeof(temp_str)); // Write zeros to the string.

    if (!dr)
    {   
        print_error("Error reading directory.");
        exit(EXIT_FAILURE);
    }
    
    while ((d = readdir(dr)) != NULL)
    {
        strcat(temp_str, d->d_name);
        strcat(temp_str, "\n");
    }

    // printf("%s\n", temp_str);
    FILE *fp = fopen(list, "wb");
    fprintf(fp, "%s", temp_str);
    fclose(fp);
    closedir(dr);
    return 0;
}


/* Custom implementation of recvfrom() that polls a non-blocking
 * socket for data until timeout is reached.
 *
 * Special parameters: no_timeout-> 1 if function should not exit()
 * on timeout; 0 otherwise.
 * 
 * On receiving data-> returns no. of bytes received.
 * On timeout-> If no_timeout=0, exit with error.
 *              If no_timeout=1, return -1.
 */
static ssize_t my_recv_from(
    int socket, void *restrict buffer, size_t length,
    int flags, struct sockaddr *restrict address,
    socklen_t *restrict address_len, int no_timeout)
{   
    ssize_t nbytes = 0;
    time_t start = 0, end = 0;
    while ((nbytes = recvfrom(socket, buffer, length, flags, address, address_len)) <= 0)
    {   
        end = time(0);
        if (start == 0)
        {
            start = time(0);
        }
        if (end - start > TIMEOUT)
        {   
            if (no_timeout == 1)
            {
                return -1;
            }
            else
            {
                close(socket);
                print_error("Timed out waiting for response from server.\n");
            }
        }
    }
    return nbytes;
}


// main()
int main(int argc, char **argv)
{
    // Check for invalid input from CLI.
    if ((argc != 2) || (atoi(argv[1]) < 5000))
    {   
        // Print out error message explaining correct way to input.
        printf("Invalid input/port.\n");
        printf("Usage --> ./[%s] [Port Number]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int srv_port = atoi(argv[1]);          // Store server port received in input.
    struct sockaddr_in srv_addr;           // Server address.
    struct sockaddr_in cln_addr;           // Client address.
    socklen_t cln_addrlen = sizeof(cln_addr);  // Length of addresses.

    int recvlen;                           // Bytes received.
    int fd;				                   // Server socket.
    struct stat st;                        // Stores the file attributes for GET.
    off_t file_size;                       // File size.
    struct timeval t_out = {0, 0};         // Stores timeout info.
    frame_t frame;                         // Structure for storing message frames.

    char rcvd_msg[BUFFSIZE];               // Received message buffer.
    char rcvd_cmd[20];                     // Received command.
    char rcvd_filename[128];               // Received filename.

    FILE *file_ptr;

    // Create a UDP socket.
    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) 
    {
        print_error("Cannot create socket.\n");
        return 0;
    }

    // Bind the socket to a valid IP address and port.
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    srv_addr.sin_port = htons(atoi(argv[1]));
    
    if (bind(fd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) 
    {
        print_error("Bind failed.\n");
        return 0;
    }

    printf("Server started: Listening on port %d\n", srv_port);

    while(1)
    {
        bzero(rcvd_msg, BUFFSIZE);   // Zero out the received message buffer before receiving the message.
        recvlen = recvfrom(fd, rcvd_msg, BUFFSIZE, 0, (struct sockaddr *)&cln_addr, &cln_addrlen); // Receive command.
        
        // Check if packet containing command is empty.
        if (recvlen < 0)
        {
            printf("No data received.\n");
            send_error(fd, (struct sockaddr *)&cln_addr, cln_addrlen);
            continue;
        }
        else
        {
            printf("Command received.\n");
            int success_ack = 1;
            sendto(fd, &success_ack, sizeof(success_ack), 0, (struct sockaddr *)&cln_addr, cln_addrlen); // Send success ACK to client.
        }
        // rcvd_msg[recvlen] = 0;
		printf("Received message: \"%s\" (%d bytes)\n", rcvd_msg, recvlen);

        // Read and parse the received command and write message to buffer.
        if (sscanf(rcvd_msg, "%s %s", rcvd_cmd, rcvd_filename) < 1)
        {   
            print_error("Failed to read command.\n");
            exit(EXIT_FAILURE);
        }

        /* **************************** Handle GET **************************** */

        // Check for valid command and filename.
        if ((strcmp(rcvd_cmd, "get") == 0) && (strcmp(rcvd_filename, "\0") != 0))
        {
            printf("Command received: %s for filename %s\n", rcvd_cmd,rcvd_filename);

            // int failure_ack = -1;
            int outer_ack = -1;
            int pos_ack = 1;

            // Check if that file exists and has read permissions.
            if ((access(rcvd_filename, F_OK) == 0) && access(rcvd_filename, R_OK) == 0)
            {   
                // Send positive ACK to client for permissions.
                sendto(fd, &pos_ack, sizeof(pos_ack), 0, (struct sockaddr *)&cln_addr, cln_addrlen);

                long int num_frames = 0;
                long int ack = 0;
                int retries = 0;
                int drops = 0;
                int is_timed_out = 0;
                long int bytes_recvd = 0;
                long int bytes_sent = 0;

                if (stat(rcvd_filename, &st) < 0)
                {
                    print_error("Failed to get file size.\n");
                }
                // Store file size of the file to be fetched.
                file_size = st.st_size; 
                // printf("File size: %lld\n", file_size);
                
                // Set timeout.
                t_out.tv_sec = TIMEOUT;
                t_out.tv_usec = 0;

                // Set socket options.
                if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&t_out, sizeof(struct timeval)) < 0)
                {
                    print_error("Failed to set socket timeout.\n");
                }

                // Open the file.
                file_ptr = fopen(rcvd_filename, "rb");

                // Calculate no. of frames to send.
                if ((file_size % BUFFSIZE) != 0)
                {   
                    num_frames = (file_size / BUFFSIZE) + 1;
                }
                else
                {
                    num_frames = (file_size / BUFFSIZE);
                }
                printf("Packets to send: %ld\n", num_frames);

                // Advertise no. of packets to be sent to the receiver.
                sendto(fd, &num_frames, sizeof(num_frames), 0, (struct sockaddr *) &cln_addr, cln_addrlen);

                // Check if the client received the expected no. of frames to be sent.  
                recvfrom(fd, &outer_ack, sizeof(outer_ack), 0, (struct sockaddr *) &cln_addr, (socklen_t *) &cln_addrlen);
                printf("Recd frames ACK: %d\n", outer_ack);

                // Check for ACK sent by server for frames.
                if (outer_ack <= 0)
                {
                    print_error("Error sending num_frames to server.\n");
                }
                
                // If client ACKs expected no. of frames, proceed with sending data.
                /* if possible add cumulative ack logic here for window size of 10 */
                for (int i =1; i <= num_frames; i++)
                {
                    memset(&frame, 0, sizeof(frame));
                    ack = 0;
                    retries = 0;
                    frame.id = i;
                    frame.len = fread(frame.data, 1, BUFFSIZE, file_ptr);

                    sendto(fd, &frame, sizeof(frame), 0, (struct sockaddr *)&cln_addr, cln_addrlen); // Send a frame.
                    printf("Frame no. %ld sent\n", frame.id);

                    // Send each frame and retry until it is acknowledged, as long as retries < RETRY_LIMIT.
                    while (retries <= RETRY_LIMIT)
                    {   
                        bytes_recvd = my_recv_from(fd, &ack, sizeof(ack), 0, (struct sockaddr *)&cln_addr, (socklen_t *)&cln_addrlen, 1);
                        if ((bytes_recvd < 0) || (frame.id != ack))
                        {
                            drops++;
                            sendto(fd, &frame, sizeof(frame), 0, (struct sockaddr *)&cln_addr, cln_addrlen);
                            retries++;
                            printf("Frame %ld dropped %d times; retries: %d.\n", frame.id, drops, retries);
                        }
                        else
                        {
                            break; // Got the correct ACK.
                        }
                    }

                    bytes_sent += frame.len;

                    if (retries == RETRY_LIMIT)
                    {
                        is_timed_out = 1;
                    }

                    retries = 0;
                    drops = 0;

                    // In case of timeout, fail the file transfer.
                    if (is_timed_out == 1)
                    {
                        print_error("File not sent. GET failed.\n");
                    }

                    // printf("Frame %ld; ACK %ld\n", frame.id, ack);

                }

                printf("File sent succesfully: %ld bytes.\n", bytes_sent);
                fclose(file_ptr);
                t_out.tv_sec = 0;
                t_out.tv_usec = 0;
                setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&t_out, sizeof(struct timeval));  // Disable timeout.
            }
            else
            {   // Have the server return an error message to the client. If the client receives this, it should print error and exit.
                printf("Invalid filename.\n");
                send_error(fd, (struct sockaddr *)&cln_addr, cln_addrlen);
            }
        }

        /* **************************** Handle PUT **************************** */
        
        else if ((strcmp(rcvd_cmd, "put") == 0) && (strcmp(rcvd_filename, "\0") != 0))
        {   
            printf("Command received: %s for filename %s\n", rcvd_cmd,rcvd_filename);

            // Check if that file exists and has write permissions; else create a new file.
            if ((access(rcvd_filename, F_OK) == 0) && access(rcvd_filename, W_OK) == 0)
            {      
                printf("File already exists and has write permission.\n");
                // Write to this file.  
                long int frames_to_receive = 0;
                long int rcvd_bytes = 0;

                t_out.tv_sec = TIMEOUT;
                
                // Set socket options.
                if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&t_out, sizeof(struct timeval)) < 0)
                {
                    print_error("Failed to set socket timeout.\n");
                    exit(EXIT_FAILURE);
                             
                }

                // Receive no. of bytes.
                recvfrom(fd, &(frames_to_receive), sizeof(frames_to_receive), 0, (struct sockaddr *) &cln_addr, (socklen_t *) &cln_addrlen); 
                printf("Frames to receive: %ld\n", frames_to_receive);
                // Disable timeout.
                t_out.tv_sec = 0;
			    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&t_out, sizeof(struct timeval)); 
                
                if (frames_to_receive > 0)
                {   
                    // Send ACK for no. of frames to client.
                    sendto(fd, &(frames_to_receive), sizeof(frames_to_receive), 0, (struct sockaddr *) &cln_addr, cln_addrlen);

                    // Open file to write.
                    file_ptr = fopen(rcvd_filename, "wb");

                    // Receive all frames. Send ACK for each using stop-and-wait.
                    for (int i = 1; i <= frames_to_receive; i++)
                    {   
                        // Initialise frame struct with zeros.
                        memset(&frame, 0, sizeof(frame));

                        recvfrom(fd, &frame, sizeof(frame), 0, (struct sockaddr *)&cln_addr, (socklen_t *)&cln_addrlen); // Receive frame.
                        sendto(fd, &frame.id, sizeof(frame.id), 0, (struct sockaddr *)&cln_addr, cln_addrlen);           // Send ACK for frame.

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
                        
                        printf("Frame %ld received.\n", frame.id);
                    }

                    printf("File received: %ld bytes.\n", rcvd_bytes);
                    fclose(file_ptr);

                }
                else
                {
                    printf("File is empty.\n");
                    send_error(fd, (struct sockaddr *)&cln_addr, cln_addrlen);
                }

            }
            else
            {
                // Create a new file.
                long int frames_to_receive = 0;
                long int rcvd_bytes = 0;

                t_out.tv_sec = TIMEOUT;
                
                // Set socket options.
                if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&t_out, sizeof(struct timeval)) < 0)
                {
                    print_error("Failed to set socket timeout.\n");
                    exit(EXIT_FAILURE);
                }

                // Receive no. of bytes/MD5 to compare later.
                recvfrom(fd, &(frames_to_receive), sizeof(frames_to_receive), 0, (struct sockaddr *) &cln_addr, (socklen_t *) &cln_addrlen); 
                printf("Frames to receive: %ld\n", frames_to_receive);
                
                // Disable timeout.
                t_out.tv_sec = 0;
			    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&t_out, sizeof(struct timeval)); 

                if (frames_to_receive > 0)
                {   
                    // Send ACK for no. of frames to client.
                    sendto(fd, &(frames_to_receive), sizeof(frames_to_receive), 0, (struct sockaddr *) &cln_addr, cln_addrlen);

                    // Open file to write.
                    file_ptr = fopen(rcvd_filename, "wb");

                    // Receive all frames. Send ACK for each using stop-and-wait.
                    for (int i = 1; i <= frames_to_receive; i++)
                    {
                        // Initialise frame struct with zeros.
                        memset(&frame, 0, sizeof(frame));

                        recvfrom(fd, &frame, sizeof(frame), 0, (struct sockaddr *)&cln_addr, (socklen_t *)&cln_addrlen); // Receive frame.
                        sendto(fd, &frame.id, sizeof(frame.id), 0, (struct sockaddr *)&cln_addr, cln_addrlen);           // Send ACK for frame.

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
            
                        printf("Frame %ld received.\n", frame.id);
                    }

                    printf("File received: %ld bytes.\n", rcvd_bytes);
                    fclose(file_ptr);

                }
                else
                {
                    printf("File is empty.\n");
                    send_error(fd, (struct sockaddr *)&cln_addr, cln_addrlen);
                }
            }

        }
        /* **************************** Handle DEL **************************** */

        else if ((strcmp(rcvd_cmd, "delete") == 0) && (strcmp(rcvd_filename, "\0") != 0))
        {
            printf("Command received: %s for filename %s\n", rcvd_cmd,rcvd_filename);

            if (access(rcvd_filename, F_OK) == 0)
            {
                // printf("Deleting %s.\n", rcvd_filename);
                if (remove(rcvd_filename) < 0) //Error case.
                {
                    printf("Error deleting file\n.");
                    send_error(fd, (struct sockaddr *)&cln_addr, cln_addrlen);
                }
                else
                {
                    int success_ack = 1;
                    sendto(fd, &success_ack, sizeof(success_ack), 0, (struct sockaddr *)&cln_addr, cln_addrlen); // Send success ACK to client.
                }
            }
            else
            {
                // Send error
                printf("No permission to delete.\n");
                send_error(fd, (struct sockaddr *)&cln_addr, cln_addrlen);
            }
        }

        /* **************************** Handle LS **************************** */

        else if (strcmp(rcvd_cmd, "ls") == 0)
        {   
            int pos_ack = 1;
            int ack = 0;
            printf("Command received: %s for filename %s\n", rcvd_cmd,rcvd_filename);

            char file_list[1024]; // String to send to client.
            memset(file_list, 0, sizeof(file_list)); // Write zeros to the file_list.

            char file_name[] = "flist"; // Temp file to store list in.

            if (get_file_list(file_name) < 0)
            {
                // Send error
                send_error(fd, (struct sockaddr *)&cln_addr, cln_addrlen); 
            }
            else
            {
                //Send positive ack.
                sendto(fd, &pos_ack, sizeof(pos_ack), 0, (struct sockaddr *) &cln_addr, cln_addrlen);
            }
        
            // Now read from file and send to client in frames.
            file_ptr = fopen(file_name, "rb");

            // Populate file_list by reading from file.
            int file_size = fread(file_list, 1, (sizeof(file_list)/sizeof(char)), file_ptr);

            printf("Entries: %d ; Total size: %ld\n", file_size, strlen(file_list));

            t_out.tv_sec = TIMEOUT;
                
            // Set socket options.
            if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&t_out, sizeof(struct timeval)) < 0)
            {
                print_error("Failed to set socket timeout.\n");
                exit(EXIT_FAILURE);
            }

            // No framing here assuming that lists won't exceed 2048 bytes.

            sendto(fd, file_list, strlen(file_list), 0, (struct sockaddr *) &cln_addr, cln_addrlen);  //Send the file list
            // Receive ACK for list.
            recvfrom(fd, &ack, sizeof(ack), 0, (struct sockaddr *)&cln_addr, (socklen_t *)&cln_addrlen);
            if (ack == pos_ack)
            {
                printf("List sent succesfully.\n");
            }
            else
            {
                printf("Client did not receive list.\n");
            }
            

            // printf("List sent succesfully.\n");
            // printf("\nList of files:\n%s \n", file_list);
            fclose(file_ptr);
            remove(file_name); // Delete the file we created for the list after transfer is done.
            t_out.tv_sec = 0;
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&t_out, sizeof(struct timeval));  // Disable timeout.

        }

        
        /* **************************** Handle EXIT **************************** */

        else if (strcmp(rcvd_cmd, "exit") == 0)
        {
            printf("Command received: %s for filename %s\n", rcvd_cmd,rcvd_filename);

            close(fd); // Close the socket.
            printf("Closing the server.\n");
            exit(EXIT_SUCCESS);
        }

        /* **************************** Handle Invalid Cases **************************** */

        else
        {
            print_error("Unknown command.\n");
            send_error(fd, (struct sockaddr *)&cln_addr, cln_addrlen);
        }

        printf("Operation finished.\n");
        
    }

    // Exit process after closing the socket.
    close(fd);
    exit(EXIT_SUCCESS);
}