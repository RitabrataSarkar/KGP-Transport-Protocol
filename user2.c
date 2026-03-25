// =====================================
// Mini Project 1 Submission
// Group Details:
// Member 1 Name: hritwik upadhyay
// Member 1 Roll number: 23cs30023
//  Member 2 Name: ritabrata sarkar
//     member 2 Roll no :23cs30045
// =====================================

#include "ksocket.h"

#define EOF_MARKER "\x04"

int main(int argc, char *argv[]) {
    if (argc != 5) {
        printf("Usage: %s <my_ip> <my_port> <dest_ip> <dest_port>\n", argv[0]);
        return -1;
    }

    const char *my_ip = argv[1];
    int my_port = atoi(argv[2]);
    const char *dest_ip = argv[3];
    int dest_port = atoi(argv[4]);

    int sockfd = k_socket(AF_INET, SOCK_KTP, 0);
    if (sockfd < 0) {
        perror("user2: k_socket failed");
        return -1;
    }

    if (k_bind(sockfd, my_ip, my_port, dest_ip, dest_port) < 0) {
        perror("user2: k_bind failed");
        return -1;
    }

    FILE *fp = fopen("recv_file.txt", "wb");
    if (fp == NULL) {
        perror("user2: Could not create recv_file.txt");
        return -1;
    }

    printf("user2: Listening for incoming file...\n");

    char buffer[MSGSIZE];
    int total_recv = 0;

    while (1) {
        int recv_bytes = k_recvfrom(sockfd, buffer, MSGSIZE, 0, NULL, NULL);
        
        if (recv_bytes > 0) {
           if (buffer[0] == EOF_MARKER[0] && buffer[1] == '\0' && buffer[2] == '\0') {
                printf("\nuser2: EOF marker received. Transfer complete!\n");
                break;
            }
            
            fwrite(buffer, 1, recv_bytes, fp);
            total_recv += recv_bytes;
            printf("\ruser2: Received %d bytes...", total_recv);
            fflush(stdout);
            
        } else if (recv_bytes == -1) {
            if (errno == ENOMSG) {
               
                usleep(50000);
            } else {
                perror("user2: Fatal k_recvfrom error");
                break;
            }
        }
    }

    fclose(fp);

    printf("user2: Waiting before teardown...\n");
    sleep(15);
    k_close(sockfd);
    printf("user2: Socket closed. Exiting.\n");
    
    return 0;
}