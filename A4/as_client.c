/*****************************************************************************/
/*                       CSC209-24s A4 Audio Stream                          */
/*       Copyright 2024 -- Demetres Kostas PhD (aka Darlene Heliokinde)      */
/*****************************************************************************/
#include "as_client.h"


static int connect_to_server(int port, const char *hostname) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("connect_to_server");
        return -1;
    }

    struct sockaddr_in addr;

    // Allow sockets across machines.
    addr.sin_family = AF_INET;
    // The port the server will be listening on.
    // htons() converts the port number to network byte order.
    // This is the same as the byte order of the big-endian architecture.
    addr.sin_port = htons(port);
    // Clear this field; sin_zero is used for padding for the struct.
    memset(&(addr.sin_zero), 0, 8);

    // Lookup host IP address.
    struct hostent *hp = gethostbyname(hostname);
    if (hp == NULL) {
        ERR_PRINT("Unknown host: %s\n", hostname);
        return -1;
    }

    addr.sin_addr = *((struct in_addr *) hp->h_addr);

    // Request connection to server.
    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("connect");
        return -1;
    }

    return sockfd;
}


/*
** Helper for: list_request
** This function reads from the socket until it finds a network newline.
** This is processed as a list response for a single library file,
** of the form:
**                   <index>:<filename>\r\n
**
** returns index on success, -1 on error
** filename is a heap allocated string pointing to the parsed filename
*/
static int get_next_filename(int sockfd, char **filename) {
    static int bytes_in_buffer = 0;
    static char buf[RESPONSE_BUFFER_SIZE];

    while((*filename = find_network_newline(buf, &bytes_in_buffer)) == NULL) {
        int num = read(sockfd, buf + bytes_in_buffer,
                       RESPONSE_BUFFER_SIZE - bytes_in_buffer);
        if (num < 0) {
            perror("list_request");
            #ifdef DEBUG
            printf("Error reading from socket\n");
            #endif
            return -1;
        }
        bytes_in_buffer += num;
        if (bytes_in_buffer == RESPONSE_BUFFER_SIZE) {
            ERR_PRINT("Response buffer filled without finding file\n");
            ERR_PRINT("Bleeding data, this shouldn't happen, but not giving up\n");
            memmove(buf, buf + BUFFER_BLEED_OFF, RESPONSE_BUFFER_SIZE - BUFFER_BLEED_OFF);
        }
    }

    char *parse_ptr = strtok(*filename, ":");
    int index = strtol(parse_ptr, NULL, 10);
    parse_ptr = strtok(NULL, ":");
    // moves the filename to the start of the string (overwriting the index)
    memmove(*filename, parse_ptr, strlen(parse_ptr) + 1);

    return index;
}

/*
** Sends a list request to the server and prints the list of files in the
** library. Also parses the list of files and stores it in the list parameter.
**
** The list of files is stored as a dynamic array of strings. Each string is
** a path to a file in the file library. The indexes of the array correspond
** to the file indexes that can be used to request each file from the server.
**
** You may free and malloc or realloc the library->files array as preferred.
**
** returns the length of the new library on success, -1 on error
*/

int list_request(int sockfd, Library *library) {

    // 1. Send the list request to the server
    char *list_request = REQUEST_LIST END_OF_MESSAGE_TOKEN;
    if (write(sockfd, list_request, strlen(list_request)) == -1) {
        ERR_PRINT("list_request: write");
        return -1;
    }

    //2. Read the response from the server into libary->files)
    char *filename = (char *)malloc(MAX_FILE_NAME);
    if (filename == NULL) {
        ERR_PRINT("list_request: malloc");
        return -1;
    }
    int index = get_next_filename(sockfd, &filename);
    if (index == -1) {
        ERR_PRINT("list_request: get_next_filename");
        return -1;
    }

    //3 Get the number of files
    library->num_files = index + 1;
    #ifdef DEBUG
    printf("Library size: %d\n", library->num_files);
    #endif
    //4. Allocate memory for the library files
    if(library->files == NULL){
        free(library->files);
    }    
    library->files = malloc(library->num_files * sizeof(char *));
    if (library->files == NULL) {
        ERR_PRINT("list_request: malloc");
        return -1;
    }
    if(library->files[library->num_files - 1] == NULL){
        free(library->files[library->num_files - 1]);
    }
    library->files[library->num_files - 1] = malloc(strlen(filename) * sizeof(char));
    if (library->files[library->num_files - 1] == NULL) {
        ERR_PRINT("list_request: malloc");
        return -1;
    }

    //5. Copy the first filename into the library files
    strncpy(library->files[library->num_files - 1], filename, strlen(filename));

    //6. Get the rest of the filenames
    int file_counter = 0;
    while(file_counter < library->num_files - 1) {
        index = get_next_filename(sockfd, &filename);
        if (index == -1) {
            ERR_PRINT("list_request: get_next_filename");
            return -1;
        }
        int pi = library->num_files - file_counter - 2; //pi = position index
        if(library->files[pi] == NULL){
            free(library->files[pi]);
        }
        library->files[pi] = malloc(strlen(filename) * sizeof(char));
        if (library->files[pi] == NULL) {
            ERR_PRINT("list_request: malloc");
            return -1;
        }
        strncpy(library->files[pi], filename, strlen(filename) + 1);
        file_counter++;
    }


    //7. Print out libary contents
    for(int i = 0; i < library->num_files; i++){
        fprintf(stdout, "%d: %s\n", i, library->files[i]);
    }

    free(filename);

    return library->num_files;
}

/*
** Get the permission of the library directory. If the library 
** directory does not exist, this function shall create it.
**
** library_dir: the path of the directory storing the audio files
** perpt:       an output parameter for storing the permission of the 
**              library directory.
**
** returns 0 on success, -1 on error
*/
static int get_library_dir_permission(const char *library_dir, mode_t * perpt) {
    #ifdef DEBUG
    printf("Lib dir: %s\n", library_dir);
    #endif

    // check if the library directory exists and make library directory if it does not.
    DIR *dir = opendir(library_dir);
    if(!dir){
        if(errno == ENOENT){
            if(mkdir(library_dir, 0700) == -1){
                ERR_PRINT("get_library_dir_permission: mkdir");
                return -1;
            }
            *perpt = 0777;
            //create_missing_directories(library_dir, library_dir);
            return 0;
        }
        
    }

    //create_missing_directories(library_dir, library_dir);

    struct stat statbuf;
    if(stat(library_dir, &statbuf) == -1){
        ERR_PRINT("get_library_dir_permission: stat");
        return -1;
    }
    *perpt = statbuf.st_mode;

    return 0;


}

/*
** Creates any directories needed within the library dir so that the file can be
** written to the correct destination. All directories will inherit the permissions
** of the library_dir.
**
** This function is recursive, and will create all directories needed to reach the
** file in destination.
**
** Destination shall be a path without a leading /
**
** library_dir can be an absolute or relative path, and can optionally end with a '/'
**
*/
static void create_missing_directories(const char *destination, const char *library_dir) {
    // get the permissions of the library dir
    mode_t permissions;
    if (get_library_dir_permission(library_dir, &permissions) == -1) {
        exit(1);
    }

    char *str_de_tokville = strdup(destination);
    if (str_de_tokville == NULL) {
        perror("create_missing_directories");
        return;
    }

    char *before_filename = strrchr(str_de_tokville, '/');
    if (!before_filename){
        goto free_tokville;
    }

    char *path = malloc(strlen(library_dir) + strlen(destination) + 2);
    if (path == NULL) {
        goto free_tokville;
    } *path = '\0';

    char *dir = strtok(str_de_tokville, "/");
    if (dir == NULL){
        goto free_path;
    }
    strcpy(path, library_dir);
    if (path[strlen(path) - 1] != '/') {
        strcat(path, "/");
    }
    strcat(path, dir);

    while (dir != NULL && dir != before_filename + 1) {
        #ifdef DEBUG
        printf("Creating directory %s\n", path);
        #endif
        if (mkdir(path, permissions) == -1) {
            if (errno != EEXIST) {
                perror("create_missing_directories");
                goto free_path;
            }
        }
        dir = strtok(NULL, "/");
        if (dir != NULL) {
            strcat(path, "/");
            strcat(path, dir);
        }
    }
free_path:
    free(path);
free_tokville:
    free(str_de_tokville);
}


/*
** Helper for: get_file_request
*/
static int file_index_to_fd(uint32_t file_index, const Library * library){
    create_missing_directories(library->files[file_index], library->path);

    char *filepath = _join_path(library->path, library->files[file_index]);
    if (filepath == NULL) {
        return -1;
    }

    int fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    #ifdef DEBUG
    printf("Opened file %s\n", filepath);
    #endif
    free(filepath);
    if (fd < 0 ) {
        perror("file_index_to_fd");
        return -1;
    }

    return fd;
}


int get_file_request(int sockfd, uint32_t file_index, const Library * library){
    #ifdef DEBUG
    printf("Getting file %s\n", library->files[file_index]);
    #endif

    int file_dest_fd = file_index_to_fd(file_index, library);
    if (file_dest_fd == -1) {
        return -1;
    }

    int result = send_and_process_stream_request(sockfd, file_index, -1, file_dest_fd);
    if (result == -1) {
        return -1;
    }

    return 0;
}

/*
** Starts the audio player process and returns the file descriptor of
** the write end of a pipe connected to the audio player's stdin.
**
** The audio player process is started with the AUDIO_PLAYER command and the
** AUDIO_PLAYER_ARGS arguments. The file descriptor to write the audio stream to
** is returned in the audio_out_fd parameter.
**
** returns PID of AUDIO_PLAYER (returns in parent process on success), -1 on error
** child process does not return.
*/

int start_audio_player_process(int *audio_out_fd) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return -1;
    }
    
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return -1;
    }
    
    if (pid == 0) {
        // Child process
        close(pipefd[1]); // Close the write end of the pipe
        
        // Redirect stdin to read from the pipe
        if (dup2(pipefd[0], STDIN_FILENO) == -1) {
            perror("dup2");
            exit(EXIT_FAILURE);
        }
        
        // Close the unused file descriptor
        close(pipefd[0]);
        
        // Execute the audio player process
        // char *args[] = {AUDIO_PLAYER, NULL};
        // execvp(args[0], args);

        char *args[] = AUDIO_PLAYER_ARGS;
        execvp(AUDIO_PLAYER, args);
        
        // If execvp returns, it means an error occurred
        perror("execvp");
        exit(EXIT_FAILURE);
    } else {
        // Parent process
        close(pipefd[0]); // Close the read end of the pipe
        
        // Pass the write end of the pipe to the audio_out_fd parameter
        *audio_out_fd = pipefd[1];
        
        return pid;
    }
}


static void _wait_on_audio_player(int audio_player_pid) {
    int status;
    if (waitpid(audio_player_pid, &status, 0) == -1) {
        perror("_wait_on_audio_player");
        return;
    }
    if (WIFEXITED(status)) {
        fprintf(stderr, "Audio player exited with status %d\n", WEXITSTATUS(status));
    } else {
        printf("Audio player exited abnormally\n");
    }
}


int stream_request(int sockfd, uint32_t file_index) {
    int audio_out_fd;
    int audio_player_pid = start_audio_player_process(&audio_out_fd);

    int result = send_and_process_stream_request(sockfd, file_index, audio_out_fd, -1);
    if (result == -1) {
        ERR_PRINT("stream_request: send_and_process_stream_request failed\n");
        return -1;
    }

    _wait_on_audio_player(audio_player_pid);

    return 0;
}


int stream_and_get_request(int sockfd, uint32_t file_index, const Library * library) {
    int audio_out_fd;
    int audio_player_pid = start_audio_player_process(&audio_out_fd);

    #ifdef DEBUG
    printf("Getting file %s\n", library->files[file_index]);
    #endif

    int file_dest_fd = file_index_to_fd(file_index, library);
    if (file_dest_fd == -1) {
        ERR_PRINT("stream_and_get_request: file_index_to_fd failed\n");
        return -1;
    }

    int result = send_and_process_stream_request(sockfd, file_index,
                                                 audio_out_fd, file_dest_fd);
    if (result == -1) {
        ERR_PRINT("stream_and_get_request: send_and_process_stream_request failed\n");
        return -1;
    }

    _wait_on_audio_player(audio_player_pid);

    return 0;
}

/*
** Sends a stream request for the particular file_index to the server and sends the audio
** stream to the audio_out_fd and file_dest_fd file descriptors
** -- provided that they are not < 0.
**
** The select system call should be used to simultaneously wait for data to be available
** to read from the server connection/socket, as well as for when audio_out_fd and file_dest_fd
** (if applicable) are ready to be written to. Differing numbers of bytes may be written to
** at each time (do no use write_precisely for this purpose -- you will nor receive full marks)
** audio_out_fd and file_dest_fd, and this must be handled.
**
** One of audio_out_fd or file_dest_fd can be -1, but not both. File descriptors >= 0
** should be closed before the function returns.
**
** This function will leverage a dynamic circular buffer with two output streams
** and one input stream. The input stream is the server connection/socket, and the output
** streams are audio_out_fd and file_dest_fd. The buffer should be dynamically sized using
** realloc. See the assignment handout for more information, and notice how realloc is used
** to manage the library.files in this client and the server.
**
** Phrased differently, this uses a FIFO with two independent out streams and one in stream,
** but because it is as long as needed, we call it circular, and just point to three different
** parts of it.
**
** returns 0 on success, -1 on error
*/

int send_and_process_stream_request(int sockfd, uint32_t file_index,
                                    int audio_out_fd, int file_dest_fd) {
    // 1. Send the stream request to the server
    char *stream_request = REQUEST_STREAM END_OF_MESSAGE_TOKEN;
    if(write_precisely(sockfd, stream_request, strlen(stream_request)) == -1){
        ERR_PRINT("send_and_process_stream_request: write_precisely");
        return -1;
    }
    
    // 2. Send the file index to the server
    uint32_t file_index_nbo = htonl(file_index);
    if(write_precisely(sockfd, &file_index_nbo, sizeof(uint32_t)) == -1){
        ERR_PRINT("send_and_process_stream_request: write_precisely");
        return -1;
    }

    // 3. Get the file size
    uint8_t file_size[4];
    if(read(sockfd, &file_size, sizeof(uint32_t)) == -1){
        ERR_PRINT("send_and_process_stream_request: read");
        return -1;
    }
    
    #ifdef DEBUG
    printf("File size: %d\n", ntohl(*(uint32_t *)file_size));
    #endif

    // 4. Read the file from the server and write it to the audio_out_fd and file_dest_fd using select system call to wait for data to be available to read from the server connection/socket, as well as for when audio_out_fd and file_dest_fd (if applicable) are ready to be written to.

    int continue_reading = 1;
    int continue_writing_stdout = 1;
    if(audio_out_fd < 0){
        continue_writing_stdout = 0;
    }
    int continue_writing_file = 1;
    if(file_dest_fd < 0){
        continue_writing_file = 0;
    }
    int bytes_read = 0;
    int bytes_written = 0;
    int bytes_to_read = ntohl(*(uint32_t *)file_size);

    char buffer[1024];
    fd_set read_fds;
    fd_set write_fds;

    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);

    while(continue_reading){
        FD_SET(sockfd, &read_fds);
        if(continue_writing_stdout){
            FD_SET(audio_out_fd, &write_fds);
        }
        if(continue_writing_file){
            FD_SET(file_dest_fd, &write_fds);
        }
        //FD_SET(audio_out_fd, &write_fds);
        //FD_SET(file_dest_fd, &write_fds);

        if(select(sockfd + 1, &read_fds, &write_fds, NULL, NULL) == -1){
            ERR_PRINT("send_and_process_stream_request: select");
            return -1;
        }

        //Read from the server
        if(FD_ISSET(sockfd, &read_fds)){
            int num = read(sockfd, buffer, MIN(bytes_to_read, 1024));
            if(num == -1){
                ERR_PRINT("send_and_process_stream_request: read");
                return -1;
            }
            else if(num == 0){
                continue_reading = 0;
            }
            else{
                bytes_read += num;
                bytes_to_read -= num;
                if(audio_out_fd >= 0){
                    if(write(audio_out_fd, buffer, num) == -1){
                        ERR_PRINT("send_and_process_stream_request: write");
                        return -1;
                    }
                }
                if(file_dest_fd >= 0){
                    if(write(file_dest_fd, buffer, num) == -1){
                        ERR_PRINT("send_and_process_stream_request: write");
                        return -1;
                    }
                }
            }
        }

        //Write to the audio_out_fd
        if(continue_writing_stdout && audio_out_fd && FD_ISSET(audio_out_fd, &write_fds)){
            int num = read(audio_out_fd, buffer, MIN(bytes_to_read, 1024));
            if(num == -1){
                ERR_PRINT("send_and_process_stream_request: read");
                return -1;
            }
            else if(num == 0){
                continue_writing_stdout = 0;
                //continue_reading = 0;
            }
            else{
                bytes_written += num;
                bytes_to_read -= num;
                if(audio_out_fd >= 0){
                    if(write(audio_out_fd, buffer, num) == -1){
                        ERR_PRINT("send_and_process_stream_request: write");
                        return -1;
                    }
                }
                if(file_dest_fd >= 0){
                    if(write(file_dest_fd, buffer, num) == -1){
                        ERR_PRINT("send_and_process_stream_request: write");
                        return -1;
                    }
                }
            }
        }

        //Write to the file_dest_fd
        if(continue_writing_file && file_dest_fd && FD_ISSET(file_dest_fd, &write_fds)){
            int num = read(file_dest_fd, buffer, MIN(bytes_to_read, 1024));
            if(num == -1){
                ERR_PRINT("send_and_process_stream_request: read");
                return -1;
            }
            else if(num == 0){
                continue_writing_file = 0;
                //continue_reading = 0;
            }
            else{
                bytes_written += num;
                bytes_to_read -= num;
                if(audio_out_fd >= 0){
                    if(write(audio_out_fd, buffer, num) == -1){
                        ERR_PRINT("send_and_process_stream_request: write");
                        return -1;
                    }
                }
                if(file_dest_fd >= 0){
                    if(write(file_dest_fd, buffer, num) == -1){
                        ERR_PRINT("send_and_process_stream_request: write");
                        return -1;
                    }
                }
            }
        }

        //In case we have read all the bytes
        if(bytes_to_read == 0){
            continue_reading = 0;
        }
    }

    // 5. Close the file descriptors
    if(audio_out_fd >= 0){
        close(audio_out_fd);
    }
    if(file_dest_fd >= 0){
        close(file_dest_fd);
    }
    // close(audio_out_fd);
    // close(file_dest_fd);

    return 0;

return - 1;
}


static void _print_shell_help(){
    printf("Commands:\n");
    printf("  list: List the files in the library\n");
    printf("  get <file_index>: Get a file from the library\n");
    printf("  stream <file_index>: Stream a file from the library (without saving it)\n");
    printf("  stream+ <file_index>: Stream a file from the library\n");
    printf("                        and save it to the local library\n");
    printf("  help: Display this help message\n");
    printf("  quit: Quit the client\n");
}


/*
** Shell to handle the client options
** ----------------------------------
** This function is a mini shell to handle the client options. It prompts the
** user for a command and then calls the appropriate function to handle the
** command. The user can enter the following commands:
** - "list" to list the files in the library
** - "get <file_index>" to get a file from the library
** - "stream <file_index>" to stream a file from the library (without saving it)
** - "stream+ <file_index>" to stream a file from the library and save it to the local library
** - "help" to display the help message
** - "quit" to quit the client
*/
static int client_shell(int sockfd, const char *library_directory) {
    char buffer[REQUEST_BUFFER_SIZE];
    char *command;
    int file_index;

    Library library = {"client", library_directory, NULL, 0};

    while (1) {
        if (library.files == 0) {
            printf("Server library is empty or not retrieved yet\n");
        }

        printf("Enter a command: ");
        if (fgets(buffer, REQUEST_BUFFER_SIZE, stdin) == NULL) {
            perror("client_shell");
            goto error;
        }

        command = strtok(buffer, " \n");
        if (command == NULL) {
            continue;
        }

        // List Request -- list the files in the library
        if (strcmp(command, CMD_LIST) == 0) {
            if (list_request(sockfd, &library) == -1) {
                goto error;
            }


        // Get Request -- get a file from the library
        } else if (strcmp(command, CMD_GET) == 0) {
            char *file_index_str = strtok(NULL, " \n");
            if (file_index_str == NULL) {
                printf("Usage: get <file_index>\n");
                continue;
            }
            file_index = strtol(file_index_str, NULL, 10);
            if (file_index < 0 || file_index >= library.num_files) {
                printf("Invalid file index\n");
                continue;
            }

            if (get_file_request(sockfd, file_index, &library) == -1) {
                goto error;
            }

        // Stream Request -- stream a file from the library (without saving it)
        } else if (strcmp(command, CMD_STREAM) == 0) {
            char *file_index_str = strtok(NULL, " \n");
            if (file_index_str == NULL) {
                printf("Usage: stream <file_index>\n");
                continue;
            }
            file_index = strtol(file_index_str, NULL, 10);
            if (file_index < 0 || file_index >= library.num_files) {
                printf("Invalid file index\n");
                continue;
            }

            if (stream_request(sockfd, file_index) == -1) {
                goto error;
            }

        // Stream and Get Request -- stream a file from the library and save it to the local library
        } else if (strcmp(command, CMD_STREAM_AND_GET) == 0) {
            char *file_index_str = strtok(NULL, " \n");
            if (file_index_str == NULL) {
                printf("Usage: stream+ <file_index>\n");
                continue;
            }
            file_index = strtol(file_index_str, NULL, 10);
            if (file_index < 0 || file_index >= library.num_files) {
                printf("Invalid file index\n");
                continue;
            }

            if (stream_and_get_request(sockfd, file_index, &library) == -1) {
                goto error;
            }

        } else if (strcmp(command, CMD_HELP) == 0) {
            _print_shell_help();

        } else if (strcmp(command, CMD_QUIT) == 0) {
            printf("Quitting shell\n");
            break;

        } else {
            printf("Invalid command\n");
        }
    }

    _free_library(&library);
    return 0;
error:
    _free_library(&library);
    return -1;
}


static void print_usage() {
    printf("Usage: as_client [-h] [-a NETWORK_ADDRESS] [-p PORT] [-l LIBRARY_DIRECTORY]\n");
    printf("  -h: Print this help message\n");
    printf("  -a NETWORK_ADDRESS: Connect to server at NETWORK_ADDRESS (default 'localhost')\n");
    printf("  -p  Port to listen on (default: " XSTR(DEFAULT_PORT) ")\n");
    printf("  -l LIBRARY_DIRECTORY: Use LIBRARY_DIRECTORY as the library directory (default 'as-library')\n");
}


int main(int argc, char * const *argv) {
    int opt;
    int port = DEFAULT_PORT;
    const char *hostname = "localhost";
    const char *library_directory = "saved";

    while ((opt = getopt(argc, argv, "ha:p:l:")) != -1) {
        switch (opt) {
            case 'h':
                print_usage();
                return 0;
            case 'a':
                hostname = optarg;
                break;
            case 'p':
                port = strtol(optarg, NULL, 10);
                if (port < 0 || port > 65535) {
                    ERR_PRINT("Invalid port number %d\n", port);
                    return 1;
                }
                break;
            case 'l':
                library_directory = optarg;
                break;
            default:
                print_usage();
                return 1;
        }
    }

    printf("Connecting to server at %s:%d, using library in %s\n",
           hostname, port, library_directory);

    int sockfd = connect_to_server(port, hostname);
    if (sockfd == -1) {
        return -1;
    }

    int result = client_shell(sockfd, library_directory);
    if (result == -1) {
        close(sockfd);
        return -1;
    }

    close(sockfd);
    return 0;
}