// =====================================
// Mini Project 1 Submission
// Group Details:
// Member 1 Name: hritwik upadhyay
// Member 1 Roll number: 23cs30023
//  member 2 Name : ritabrata sarkar
// member2 Roll no : 23cs30045
// =====================================
#include "ksocket.h"

extern ktp_pcb_t *SM;

void *threadG(void *arg) {
    printf("[Thread G] Garbage Collector started.\n");
    while (1) {
        // wake up every T sec to check for dead processes
        sleep(T); 

        for (int i = 0; i < MAX_SOCKETS; i++) {
            sem_t *lock = get_socket_lock(i);
            if (!lock) continue;

            sem_wait(lock);
            if (SM[i].is_allocated) {
                // sending signal 0 checks if process exists without killing it
                if (kill(SM[i].owner_pid, 0) == -1 && errno == ESRCH) {
                    printf("[Thread G] Process %d died! Reclaiming socket %d.\n", SM[i].owner_pid, i);
                    // force teardown since the user app crashed without closing
                    SM[i].is_teardown_initiated = true;
                }
            }
            sem_post(lock);
            sem_close(lock);
        }
    }
    return NULL;
}

void *threadS(void *arg) {
    printf("[Thread S] Sender Thread started.\n");
    
    char packet[PACKETSIZE]; 

    while (1) {
        usleep((T * 1000000) / 2); 

        time_t current_time = time(NULL);
        
        for (int i = 0; i < MAX_SOCKETS; i++) {
            sem_t *lock = get_socket_lock(i);
            if (!lock) continue;

            sem_wait(lock);
            if (!SM[i].is_allocated || !SM[i].is_bound) {
                sem_post(lock);
                sem_close(lock);
                continue;
            }
            //GBN
            for (int j = 0; j < BUFFSIZE; j++) {
                if (SM[i].tx_buffer[j].is_occupied && SM[i].tx_buffer[j].tx_time != -1) {
                    if (current_time - SM[i].tx_buffer[j].tx_time > T) {
                        printf("[Thread S] Timeout on socket %d! Retransmitting.\n", i);
                        // reset time to -1 to force the loop below to send it again
                        SM[i].tx_buffer[j].tx_time = -1; 
                        //SM[i].total_tx++; // Use the one inside the socket structure;
                    }
                }
            }
            // find pending messages in the send window that need to be sent
            for (int j = 0; j < BUFFSIZE; j++) {
                if (SM[i].tx_buffer[j].is_occupied && SM[i].tx_buffer[j].tx_time == -1) {
                    //header
                    packet[0] = FLAG_DATA;                                  
                    packet[1] = (uint8_t)(SM[i].swnd.window_base + j);      
                    uint16_t net_rwnd = htons(SM[i].rwnd.current_size);     
                    memcpy(&packet[2], &net_rwnd, 2);                       
                    // copy  512 byte
                    memcpy(&packet[4], SM[i].tx_buffer[j].payload, MSGSIZE);

                    sendto(SM[i].udp_sockfd, packet, sizeof(packet), 0, 
                           (struct sockaddr *)&SM[i].remote_ip, sizeof(struct sockaddr_in));
                    printf("S: DATA %u sent through ksocket %d | Content: %.20s...\n", 
        (uint8_t)packet[1], i, &packet[4]);
                        //SM[i].unique_msg++; 
                        SM[i].total_tx++;
                    SM[i].tx_buffer[j].tx_time = time(NULL);
                }
            }
            // wait for all pending packets and acks to clear before closing
            if (SM[i].is_teardown_initiated) {
                bool all_clear = true;
                for (int j = 0; j < BUFFSIZE; j++) {
                    if (SM[i].tx_buffer[j].is_occupied) all_clear = false;
                }

                if (all_clear) {
                    printf("[Thread S] Tearing down socket %d.\n", i);
                    //                 printf("\n--- Final Results for p = %.2f ---\n", P);
                    // printf("Total Transmissions: %d\n", total_transmissions);
                    // printf("Unique Messages: %d\n", unique_messages);
                    // printf("Average: %.3f\n", (float)total_transmissions / unique_messages);
                    // printf("-------------------------------\n");
                    
                    // Reset counters for the next run
                    if (SM[i].unique_msg > 0) {
                        printf("\n--- Results for Socket %d (p = %.2f) ---\n", i, P);
                        printf("Total Transmissions: %d\n", SM[i].total_tx);
                        printf("Unique Messages: %d\n", SM[i].unique_msg);
                        printf("Average: %.3f\n", (float)SM[i].total_tx / SM[i].unique_msg);
                        printf("-------------------------------\n");
                        // Reset them specifically for this socket
                        SM[i].total_tx = 0;
                        SM[i].unique_msg = 0;
                    }
    
                    close(SM[i].udp_sockfd);
                    SM[i].is_allocated = false; 
                    SM[i].is_bound = false;
                    SM[i].bound_ip.sin_family = 0;
                }
            }

            sem_post(lock);
            sem_close(lock);
        }
    }
    return NULL;
}

void *threadR(void *arg) {
    printf("[Thread R] Receiver Thread started.\n");
    
    fd_set readfds;
    struct timeval timeout;
    char buffer[PACKETSIZE];

    while (1) {
        FD_ZERO(&readfds);
        int max_fd = -1;

        for (int i = 0; i < MAX_SOCKETS; i++) {
            sem_t *lock = get_socket_lock(i);
            if (!lock) continue;

            sem_wait(lock);
            if (SM[i].is_allocated) {
                //user called k_bind then we need to bind udp socket here
                if (!SM[i].is_bound && SM[i].bound_ip.sin_family == AF_INET) {
                    int fd = socket(AF_INET, SOCK_DGRAM, 0);
                    if (fd >= 0) {
                        if (bind(fd, (struct sockaddr *)&SM[i].bound_ip, sizeof(struct sockaddr_in)) == 0) {
                            SM[i].udp_sockfd = fd;
                            SM[i].is_bound = true; 
                            printf("[Thread R] Daemon successfully bound socket %d!\n", i);
                        } else {
                            close(fd);
                        }
                    }
                }
                // add active sockets
                if (SM[i].is_bound && SM[i].udp_sockfd >= 0) {
                    FD_SET(SM[i].udp_sockfd, &readfds);
                    if (SM[i].udp_sockfd > max_fd) {
                        max_fd = SM[i].udp_sockfd;
                    }
                }
            }
            sem_post(lock);
            sem_close(lock);
        }

        if (max_fd == -1) {
            usleep(500000); 
            continue;
        }

        timeout.tv_sec = T;
        timeout.tv_usec = 0;

        int activity = select(max_fd + 1, &readfds, NULL, NULL, &timeout);

        if (activity < 0) {
            perror("select error");
            continue;
        }
        // select timed out so check if we can send duplicate acks for freed space
        if (activity == 0) {
            for (int i = 0; i < MAX_SOCKETS; i++) {
                sem_t *lock = get_socket_lock(i);
                if (!lock) continue;
                sem_wait(lock);
                
                if (SM[i].is_allocated && SM[i].rx_buffer_full && SM[i].rwnd.current_size > 0) {
                    char ack_pkt[4];
                    ack_pkt[0] = FLAG_ACK;
                    ack_pkt[1] = SM[i].rwnd.window_base - 1; 
                    uint16_t my_rwnd = htons(SM[i].rwnd.current_size);
                    memcpy(&ack_pkt[2], &my_rwnd, 2);
                    
                    sendto(SM[i].udp_sockfd, ack_pkt, 4, 0, 
                           (struct sockaddr *)&SM[i].remote_ip, sizeof(struct sockaddr_in));
                           
                    SM[i].rx_buffer_full = false;
                }
                sem_post(lock);
                sem_close(lock);
            }
            continue; 
        }
        // process arriving packets
        for (int i = 0; i < MAX_SOCKETS; i++) {
            sem_t *lock = get_socket_lock(i);
            if (!lock) continue;

            sem_wait(lock);
            if (SM[i].is_allocated && SM[i].is_bound && FD_ISSET(SM[i].udp_sockfd, &readfds)) {
                
                struct sockaddr_in sender_addr;
                socklen_t addr_len = sizeof(sender_addr);
                // use dontwait to prevent blocking
                int recv_len = recvfrom(SM[i].udp_sockfd, buffer, PACKETSIZE, MSG_DONTWAIT, 
                                        (struct sockaddr *)&sender_addr, &addr_len);
                
                if (recv_len > 0) {
                    if (dropMessage(P) == 1) {
                        printf("[Thread R] Packet dropped deliberately (Simulated Loss).\n");
                    } else {
                        uint8_t flag = buffer[0];
                        uint8_t seq_num = buffer[1];
                        uint16_t remote_rwnd;
                        memcpy(&remote_rwnd, &buffer[2], 2);
                        remote_rwnd = ntohs(remote_rwnd);

                        if (flag == FLAG_DATA) {

                            printf("R: DATA %u received on ksocket %d | Content: %.20s...\n", 
            seq_num, i, &buffer[4]);
                            if (seq_num < SM[i].rwnd.window_base) {
                                char ack_pkt[4];
                                ack_pkt[0] = FLAG_ACK;
                                ack_pkt[1] = SM[i].rwnd.window_base - 1; 
                                uint16_t my_rwnd = htons(SM[i].rwnd.current_size);
                                memcpy(&ack_pkt[2], &my_rwnd, 2);
                                sendto(SM[i].udp_sockfd, ack_pkt, 4, 0, 
                                       (struct sockaddr *)&SM[i].remote_ip, sizeof(struct sockaddr_in));
                                
                                sem_post(lock);
                                sem_close(lock);
                                continue; 
                            }
                            // keep out of order messages but do not send ack
                            if (seq_num > SM[i].rwnd.window_base) {
                                sem_post(lock);
                                sem_close(lock);
                                continue;
                            }
                            // find an empty slot in recieve buffer
                            int empty_slot = -1;
                            for(int j=0; j<BUFFSIZE; j++) {
                                if(!SM[i].rx_buffer[j].is_occupied) { empty_slot = j; break; }
                            }
                            
                            if (empty_slot != -1) {
                                memcpy(SM[i].rx_buffer[empty_slot].payload, &buffer[4], MSGSIZE);
                                SM[i].rx_buffer[empty_slot].is_occupied = true;
                                SM[i].rwnd.current_size--; 
                                // slide the recieve window forward
                                SM[i].rwnd.window_base = seq_num + 1;
                                
                                char ack_pkt[4];
                                ack_pkt[0] = FLAG_ACK;
                                ack_pkt[1] = seq_num;
                                uint16_t my_rwnd = htons(SM[i].rwnd.current_size);
                                memcpy(&ack_pkt[2], &my_rwnd, 2);
                                
                                sendto(SM[i].udp_sockfd, ack_pkt, 4, 0, 
                                       (struct sockaddr *)&SM[i].remote_ip, sizeof(struct sockaddr_in));
                            } else {
                                SM[i].rx_buffer_full = true; 
                            }
                        } 
                        else if (flag == FLAG_ACK) {
                            if (seq_num >= SM[i].swnd.window_base) {
                                int acked_count = seq_num - SM[i].swnd.window_base + 1;
                                SM[i].swnd.window_base = seq_num + 1;
                                // shift remaining messages in the outbox to the left
                                for (int k = 0; k < acked_count; k++) {
                                    for (int j = 0; j < BUFFSIZE - 1; j++) {
                                        SM[i].tx_buffer[j] = SM[i].tx_buffer[j+1];
                                    }
                                    SM[i].tx_buffer[BUFFSIZE - 1].is_occupied = false;
                                    SM[i].tx_buffer[BUFFSIZE - 1].tx_time = -1;
                                }
                            }
                            SM[i].swnd.current_size = remote_rwnd; 
                        }
                    }
                }
            }
            sem_post(lock);
            sem_close(lock);
        }
    }
    return NULL;
}

int main() {
    printf("Starting KTP Kernel Daemon...\n");
    //create SM
    int shmid = shmget(SHM_KEY, sizeof(ktp_pcb_t) * MAX_SOCKETS, IPC_CREAT | 0666);
    if (shmid < 0) {
        perror("shmget failed");
        exit(1);
    }
    //attachit to addrees space
    SM = (ktp_pcb_t *)shmat(shmid, NULL, 0);
    if (SM == (void *)-1) {
        perror("shmat failed");
        exit(1);
    }

    for (int i = 0; i < MAX_SOCKETS; i++) {
        SM[i].is_allocated = false;
    }
    //all sem to Unlock
    for (int i = 0; i < MAX_SOCKETS; i++) {
        char sem_name[32];
        snprintf(sem_name, sizeof(sem_name), "%s%d", SEM_PREFIX, i);
        
        sem_t *lock = sem_open(sem_name, O_CREAT, 0666, 1);
        if (lock == SEM_FAILED) {
            perror("Semaphore creation failed");
            exit(1);
        }
        sem_close(lock);
    }

    pthread_t r_tid, s_tid, g_tid;
    pthread_create(&r_tid, NULL, threadR, NULL);
    pthread_create(&s_tid, NULL, threadS, NULL);
    pthread_create(&g_tid, NULL, threadG, NULL);

    pthread_join(r_tid, NULL);
    pthread_join(s_tid, NULL);
    pthread_join(g_tid, NULL);

    return 0;
}