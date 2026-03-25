// =====================================
// Mini Project 1 Submission
// Group Details:
// Member 1 Name: hritwik upadhyay
// Member 1 Roll number: 23cs30023
//  member 2 Name : ritabrata sarkar
// member2 Roll no : 23cs30045
// =====================================

#ifndef KSOCKET_H
#define KSOCKET_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdint.h>      
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>    
#include <fcntl.h>       
#include <sys/stat.h>    
#include <semaphore.h>   


#define SOCK_KTP 256            
#define MSGSIZE 512            
#define PACKETSIZE 516          // 4-Byte header + 512-byte payload
#define WINDOWSIZE 10           //  max sliding window size
#define BUFFSIZE 10             //  10 message max in the buffer
#define T 5                     // timeout in sec for thread s retransmision
#define P 0.0                // Prob of dropping  message 
#define MAX_SOCKETS 10          // max no of active ktp connection allowed

// custom ktp error codes 

#define ENOSPACE   ENOSPC       // No space left on device
#define ENOTBOUND  ENOTCONN     // Transport endpoint is not connected
#define ENOMESSAGE ENOMSG       // No message of desired type

// IPC Keys and Names
#define SHM_KEY 0x5051     
#define SEM_PREFIX "/ktp_lock_" // prefix for POSIX locks 


// Using bits instead of 4-byte strings like "DATA" saves bandwidth and is much faster for the CPU to process using bitwise AND operations
#define FLAG_DATA 0x01  // 0000 0001
#define FLAG_ACK  0x02  // 0000 0010
#define FLAG_FIN  0x04  // 0000 0100
#define FLAG_FAK  0x08  // 0000 1000 (FIN-ACK)



typedef struct {
    char payload[MSGSIZE];  //   512 bytes of file data
    bool is_occupied;       // True if  valid data , False if empty
    time_t tx_time;         // timestamp of when Thread S sent it (-1 if not sent yet)
} ktp_buffer_slot_t;

// unified window struct to track sliding window protocol state
typedef struct {
    int window_base;                  // oldest unacknowledged seq no
    uint16_t current_size;            // How many slots are currently open
    uint8_t expected_seq[WINDOWSIZE]; 
    bool slot_status[WINDOWSIZE];     // Tracks which specific seq no have been ACKed
} ktp_window_t;

typedef struct {
    bool is_allocated;           // Is this slot currently being used by  user application
    pid_t owner_pid;             // Process id of the user app (used by Garbage Collector)
    int udp_sockfd;              // actual UDP socket pushing data to the network
    struct sockaddr_in bound_ip; // My local Ip and port
    struct sockaddr_in remote_ip;// The destination Ip and port i am talking to
    int total_tx;   
    int unique_msg;
    // array of Structs  - The Inbox and Outbox
    ktp_buffer_slot_t tx_buffer[BUFFSIZE]; // The Outbox (User writes here, Thread S sends it)
    ktp_buffer_slot_t rx_buffer[BUFFSIZE]; // The Inbox (Thread R writes here, User reads it)
    ktp_window_t swnd;           // Sender Window state
    ktp_window_t rwnd;           // Receiver Window state
    bool rx_buffer_full;         // flag to tell Thread R to stop accepting new packets
    bool is_teardown_initiated;  // flag set by k_close() to tell Thread S to send a FIN packet
    bool is_bound;               // flag set by k_bind() to tell Thread R to start listening
} ktp_pcb_t;

// only functions  user1.c and user2.c programs are allowed to call.
int k_socket(int domain, int type, int protocol);
int k_bind(int sockfd, const char *src_ip, int src_port, const char *dest_ip, int dest_port);
int k_sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen);
int k_recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen);
int k_close(int sockfd);
int dropMessage(float p); // simulates network packet loss

ktp_pcb_t* attach_shared_memory();
sem_t* get_socket_lock(int sockfd);

#endif