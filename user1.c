// =====================================
// Mini Project 1 Submission
// Group Details:
// Member 1 Name: hritwik upadhyay
// Member 1 Roll number: 23cs30023
//  member 2 Name : ritabrata sarkar
// member2 Roll no : 23cs30045
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
        perror("user1: k_socket failed");
        return -1;
    }
    if (k_bind(sockfd, my_ip, my_port, dest_ip, dest_port) < 0) {
        perror("user1: k_bind failed");
        return -1;
    }
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(dest_port);
    inet_pton(AF_INET, dest_ip, &dest_addr.sin_addr);
    FILE *fp = fopen("send_file.txt", "rb");
    if (fp == NULL) {
        perror("user1: Could not open send_file.txt");
        return -1;
    }
    printf("user1: Starting file transfer...\n");
    sleep(2); 
    char buffer[MSGSIZE];
    size_t bytes_read;
    int total_sent = 0;  
    while ((bytes_read = fread(buffer, 1, MSGSIZE, fp)) > 0) {
        int sent_bytes = -1;        
        while (sent_bytes == -1) {
            sent_bytes = k_sendto(sockfd, buffer, bytes_read, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
            if (sent_bytes == -1) {
                if (errno == ENOSPC || errno == ENOTCONN) {
                 // sleep briefly to wait for the flow control window to open
                    usleep(50000); 
                } else {
                    perror("user1: Fatal k_sendto error");
                    fclose(fp);
                    return -1;
                }
            }
        }
        total_sent += sent_bytes;
        printf("\ruser1: Sent %d bytes...", total_sent);
        fflush(stdout);
    }
    printf("\nuser1: File read complete. Sending EOF marker...\n");
    int eof_sent = -1;
    while (eof_sent == -1) {
        eof_sent = k_sendto(sockfd, EOF_MARKER, 1, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (eof_sent == -1) usleep(50000);
    }
    fclose(fp);
    printf("user1: Waiting for background threads to settle before closing...\n");
    sleep(15); 
    k_close(sockfd);
    printf("user1: Socket closed. Exiting.\n");
    return 0;
}