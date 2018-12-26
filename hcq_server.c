#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "hcq.h"


#ifndef PORT
  #define PORT 59598
#endif
#define MAX_BACKLOG 5
#define MAX_CONNECTIONS 128
#define BUF_SIZE 1024


struct sockname {
    int sock_fd;
    char *username;
};

struct the_client {
	int phase; // Indicates which stage of data is coming next; 0~name, 1~role, 2~course, 3~command for student, 4~command for TA
	int role; // 0 for Student, 1 for TA
	char name[30]; // client name
};

// Use global variables so we can have exactly one TA list and one student list
Ta *ta_list = NULL;
Student *stu_list = NULL;
Course *courses; 
int num_courses = 3;
Course *courselist;

/* Accept a connection. Note that a new file descriptor is created for
 * communication with the client. The initial socket descriptor is used
 * to accept connections, but the new socket is used to communicate.
 * Return the new client's file descriptor or -1 on error.
 */
int accept_connection(int fd, struct sockname *usernames, struct the_client *clients) {
    int user_index = 0;
    while (user_index < MAX_CONNECTIONS && usernames[user_index].sock_fd != -1) {
        user_index++;
    }

    if (user_index == MAX_CONNECTIONS) {
        fprintf(stderr, "server: max concurrent connections\n");
        return -1;
    }

    int client_fd = accept(fd, NULL, NULL);
    if (client_fd < 0) {
        perror("server: accept");
        close(fd);
        exit(1);
    }

    usernames[user_index].sock_fd = client_fd;
    usernames[user_index].username = NULL;
    clients[user_index].phase = 0;

    return client_fd;
}

int find_network_newline(char *buf) {
  for (int i = 0; i < BUF_SIZE; i++) {
      if (buf[i] == '\r' || buf[i] == '\n') {
          return i;
      }
  }
  return -1;
}


/* Read a message from client_index and echo it back to them.
 * Return the fd if it has been closed or 0 otherwise.
 */
int read_from(int client_index, struct sockname *usernames, struct the_client *clients) {
    int fd = usernames[client_index].sock_fd;
    char buf[BUF_SIZE];
    for (int i = 0; i < BUF_SIZE; i++){
        buf[i] = '\0';
    }

    // Prepare output string
	char to_write[BUF_SIZE];
    to_write[0] = '\0';
    int my_phase = (clients[client_index]).phase;

    // Read input from client
    int num_read = read(fd, &buf, BUF_SIZE-1);
	if (num_read == 0){
		usernames[client_index].sock_fd = -1;
        return fd;
	}


    // Clean input
    int line_len = find_network_newline(buf);
    if (line_len != -1){
        buf[line_len] = '\0';
    }
    buf[num_read] = '\0';

    if (strlen(buf) == 0){
        return 0;
    }


    // Begin phase states
    // Phase 0: Entering Name
	if (my_phase == 0){

        // Save client's name
		strncpy(clients[client_index].name, buf, 30);
		(clients[client_index].name)[29] = '\0';

        // Set output to next question
		strncpy(to_write, "Are you a TA or a Student (Enter T or S)?\n", BUF_SIZE);
		(clients[client_index]).phase = 1;
	}

    // Phase 1: Student or TA
	else if (my_phase == 1){
        
        // If the client is a student
		if (strcmp(buf, "S") == 0){

            // Set the client's role as a student
			(clients[client_index]).role = 0;

            // Proceed to the next question
			(clients[client_index]).phase = 2;
			strncpy(to_write, "Valid courses: CSC108, CSC148, CSC209\nWhich course are you asking about?\n", BUF_SIZE);
		}

        // If the client is a TA
		else if(strcmp(buf, "T") == 0){

            // Set the client's role as TA
			(clients[client_index]).role = 1;
			add_ta(&ta_list, (clients[client_index]).name);

            // Proceed to the next question
			(clients[client_index]).phase = 4;
			strncpy(to_write, "Valid commands for TA:\n        stats\n        next\n        (or use Ctrl-C to leave)\n", BUF_SIZE);
		}

        // If they enter something invalid
		else{
            
            // Don't proceed to next question, but ask again
			strncpy(to_write, "Invalid role (enter T or S)\n", BUF_SIZE);
		}
	}
	
    // Phase 2: Entering Course Code
	else if (my_phase == 2){

        // If the course entered is invalid
		if (find_course(courselist, num_courses, buf) == NULL){

            // Don't proceed to next question and ask again
			strncpy(to_write, "That course is not valid. Please enter a valid course code.\n", BUF_SIZE);
		}
		
        // If the course entered is valid
		else{
    
            // add the student to the queue and proceed to next question
			add_student(&stu_list, (clients[client_index]).name, buf, courselist, num_courses);
			(clients[client_index]).phase = 3;
			strncpy(to_write, "You have been entered into the queue. You can use the command stats to see which TAs are currently serving students\n", BUF_SIZE);
		}
	}
	
    // Phase 3: Student commands
	else if (my_phase == 3){

        // If the student requests stats
		if (strcmp(buf, "stats") == 0){

            // Show them the stats about the TAs
			strncpy(to_write, print_currently_serving(ta_list), BUF_SIZE);
		}

        // If the student enters an invalid command
		else{
            
            // Tell them it is an invalid command
			strncpy(to_write, "That is not a valid command. Please try again.\n", BUF_SIZE);
		}
	}
	
    // Phase 4: Ta commands
	else if (my_phase == 4){

        // If the TA requests stats
		if (strcmp(buf, "stats") == 0){
			strncpy(to_write, print_full_queue(stu_list), BUF_SIZE);
		}

        // If the TA requests next
		else if(strcmp(buf, "next") == 0){

            // If the queue is empty
            if (strlen(print_full_queue(stu_list)) == 11){
                
                // Output no students waiting and finish with current student
                strncpy(to_write, "No students waiting.\n", BUF_SIZE);
                next_overall((clients[client_index]).name, &ta_list, &stu_list);
            }

            // If there is a student waiting
            else{

                // Take the next student
                next_overall((clients[client_index]).name, &ta_list, &stu_list);

                // Find the current student and prepare to disconnect with it
                Student* stu = find_ta(ta_list, (clients[client_index]).name) -> current_student;
                for (int i = 0; i < MAX_CONNECTIONS; i++) {
                    if (strcmp((clients[i]).name, stu -> name) == 0){

                        // Send the student a parting message
                        int fdr = usernames[i].sock_fd;
                        strncpy(to_write, "Your turn to see the TA.\nYou will be disconnected from the server now. Press CTRL-C to exit nc.\n", BUF_SIZE);
                        to_write[BUF_SIZE-1] = '\0';
                        write(fdr, to_write, strlen(to_write)); // If this fails it is ok. The client will be disconnected anyways
                        
                        // Set it's sock_fd to -1 so it will be disconnected in the main method
                        usernames[i].sock_fd = -1;
                        return 0;
                    }
                }
            }			
		}
		
        // If the command is invalid
		else{

            // Tell them it is invalid
			strncpy(to_write, "That is not a valid command. Please try again.\n", BUF_SIZE);
		}
	}

    // Write the desired output to the client
    to_write[BUF_SIZE-1] = '\0';
    if (write(fd, to_write, strlen(to_write)) != strlen(to_write)) {
        usernames[client_index].sock_fd = -1;
        return fd;
    }

    return 0;
}


int main(void) {
	config_course_list(&courselist, "");

    // Prepare an array to store clients' info
    struct sockname usernames[MAX_CONNECTIONS];
	struct the_client clients[MAX_CONNECTIONS];
    for (int i= 0; i < MAX_CONNECTIONS; i++) {
        usernames[i].sock_fd = -1;
        usernames[i].username = NULL;
		clients[i].phase = 0;
    }

    // Create the socket FD.
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("server: socket");
        exit(1);
    }
	
	// Bit that we are supposed to add
	int on = 1;
    int status = setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR,
                            (const char *) &on, sizeof(on));
    if(status == -1) {
        perror("setsockopt -- REUSEADDR");
    }

    // Set information about the port (and IP) we want to be connected to.
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    server.sin_addr.s_addr = INADDR_ANY;

    // This should always be zero. On some systems, it won't error if you
    // forget, but on others, you'll get mysterious errors. So zero it.
    memset(&server.sin_zero, 0, 8);

    // Bind the selected port to the socket.
    if (bind(sock_fd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("server: bind");
        close(sock_fd);
        exit(1);
    }

    // Announce willingness to accept connections on this socket.
    if (listen(sock_fd, MAX_BACKLOG) < 0) {
        perror("server: listen");
        close(sock_fd);
        exit(1);
    }
	

    // The client accept - message accept loop. First, we prepare to listen to multiple
    // file descriptors by initializing a set of file descriptors.
    int max_fd = sock_fd;
    fd_set all_fds;
    FD_ZERO(&all_fds);
    FD_SET(sock_fd, &all_fds);
	

    while (1) {
        // select updates the fd_set it receives, so we always use a copy and retain the original.
        fd_set listen_fds = all_fds;
        int nready = select(max_fd + 1, &listen_fds, NULL, NULL, NULL);
        if (nready == -1) {
            perror("server: select");
            exit(1);
        }

        // Is it the original socket? Create a new connection ...
        if (FD_ISSET(sock_fd, &listen_fds)) {
            int client_fd = accept_connection(sock_fd, usernames, clients);
            if (client_fd > max_fd) {
                max_fd = client_fd;
            }
            FD_SET(client_fd, &all_fds);
			char* message = "Welcome to the Help Centre, what is your name?\n";
			if (write(client_fd, message, strlen(message)) != strlen(message)) {
				perror("Error writing to client");
			}	
        }

        // Next, check the clients.
        // NOTE: We could do some tricks with nready to terminate this loop early.
        for (int index = 0; index < MAX_CONNECTIONS; index++) {
            if (usernames[index].sock_fd > -1 && FD_ISSET(usernames[index].sock_fd, &listen_fds)) {
                // Note: never reduces max_fd	
                int client_closed = read_from(index, usernames, clients);
                if (client_closed > 0) {             
                    
                    // Check weather the client leaving is a Student or Ta, then act accordingly
                    if (clients[index].role == 0){
                        give_up_waiting(&stu_list, clients[index].name);
                    }

                    else if(clients[index].role == 1){
                        remove_ta(&ta_list, clients[index].name);
                    }

                    usernames[index].sock_fd = -1;
                }
					
                FD_CLR(client_closed, &all_fds);
            }
        }
    }

    // Should never get here.
    return 1;
}
