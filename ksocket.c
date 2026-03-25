// =====================================
// Mini Project 1 Submission
// Group Details:
// Member 1 Name: hritwik upadhyay
// Member 1 Roll number: 23cs30023
//  member 2 Name : ritabrata sarkar
// member2 Roll no : 23cs30045
// =====================================
#include "ksocket.h"

// Global pointer to our shared memory segment

ktp_pcb_t *SM = NULL;

ktp_pcb_t* attach_shared_memory() {
    if (SM != NULL) return SM; // If we already attached to it earlier, just return it.
    
    int shmid = shmget(SHM_KEY, sizeof(ktp_pcb_t) * MAX_SOCKETS, 0666);
    if (shmid < 0) {
        perror("k_socket error: initk daemon might not be running. shmget failed");
        return NULL;
    }

    SM = (ktp_pcb_t *)shmat(shmid, NULL, 0);
    if (SM == (void *)-1) {
        perror("k_socket error: shmat failed");
        return NULL;
    }
    return SM;
}

sem_t* get_socket_lock(int sockfd) {
    char sem_name[32];
    snprintf(sem_name, sizeof(sem_name), "%s%d", SEM_PREFIX, sockfd);
    
    sem_t *lock = sem_open(sem_name, 0);
    if (lock == SEM_FAILED) {
        perror("Semaphore open failed");
        return NULL;
    }
    return lock;
}

int dropMessage(float p) {
    
    float r = (float)rand() / (float)RAND_MAX;
    if (r < p) {
        return 1; // Drop it 
    }
    return 0; // Keep it
}

//  SETUP FUNCTIONS

int k_socket(int domain, int type, int protocol) {
  
    if (type != SOCK_KTP || domain != AF_INET) {
        return -1;
    }

    if (attach_shared_memory() == NULL) {
        return -1;
    }

    //  search the Shared memory array for an empty ktp socket slot
    int free_idx = -1;
    for (int i = 0; i < MAX_SOCKETS; i++) {
        //  lock the slot before checking it so two apps don't grab it at the same time
        sem_t *lock = get_socket_lock(i);
        if (lock) {
            sem_wait(lock); // LOCK 
            if (!SM[i].is_allocated) {
                //  found an empty slot, claim it 
                SM[i].is_allocated = true;
                SM[i].owner_pid = getpid(); // record our PID for thread g
                free_idx = i;
                
                // Unlock and break 
                sem_post(lock); 
                sem_close(lock);
                break;
            }
            sem_post(lock); // UNLOCK 
            sem_close(lock);
        }
    }

    //  If all 10 slots are full set error to ENOSPACE
    if (free_idx == -1) {
        errno = ENOSPACE;
        return -1;
    }

    sem_t *lock = get_socket_lock(free_idx);
    sem_wait(lock); // LOCK
    
    SM[free_idx].udp_sockfd = -1;
    SM[free_idx].is_teardown_initiated = false;
    SM[free_idx].rx_buffer_full = false;
    SM[free_idx].is_bound = false;
    memset(&SM[free_idx].bound_ip, 0, sizeof(struct sockaddr_in));
    
    SM[free_idx].swnd.window_base = 1;
    SM[free_idx].swnd.current_size = WINDOWSIZE;
    SM[free_idx].rwnd.window_base = 1;
    SM[free_idx].rwnd.current_size = WINDOWSIZE;

    for(int j = 0; j < BUFFSIZE; j++) {
        SM[free_idx].tx_buffer[j].is_occupied = false;
        SM[free_idx].tx_buffer[j].tx_time = -1;
        SM[free_idx].rx_buffer[j].is_occupied = false;
        
        SM[free_idx].swnd.slot_status[j] = false;
        SM[free_idx].rwnd.slot_status[j] = false;
    }
    
    sem_post(lock); // UNLOCK
    sem_close(lock);

    return free_idx; 
}

int k_bind(int sockfd, const char *src_ip, int src_port, const char *dest_ip, int dest_port) {
   
    if (sockfd < 0 || sockfd >= MAX_SOCKETS || SM == NULL) {
     
        return -1;
    }

    sem_t *lock = get_socket_lock(sockfd);
    sem_wait(lock); 

    struct sockaddr_in source;
    memset(&source, 0, sizeof(source));
    source.sin_family = AF_INET;
    source.sin_port = htons(src_port);            
    inet_pton(AF_INET, src_ip, &source.sin_addr);  

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(dest_port);
    inet_pton(AF_INET, dest_ip, &dest.sin_addr);

    SM[sockfd].bound_ip = source;
    SM[sockfd].remote_ip = dest;

    sem_post(lock);
    sem_close(lock);

    return 0; 
}
//  DATA TRANSFER FUNCTIONS
int k_sendto(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen) {
    if (sockfd < 0 || sockfd >= MAX_SOCKETS || SM == NULL || !SM[sockfd].is_allocated) {
        return -1;
    }

    sem_t *lock = get_socket_lock(sockfd);
    sem_wait(lock); 

    struct sockaddr_in *dest_in = (struct sockaddr_in *)dest_addr;
    if (dest_in->sin_addr.s_addr != SM[sockfd].remote_ip.sin_addr.s_addr ||
        dest_in->sin_port != SM[sockfd].remote_ip.sin_port) {
        errno = ENOTBOUND; 
        sem_post(lock);
        sem_close(lock);
        return -1;
    }

    // look for an empty slot 
    int free_slot = -1;
    for (int i = 0; i < BUFFSIZE; i++) {
        if (!SM[sockfd].tx_buffer[i].is_occupied) {
            free_slot = i;
            break;
        }
    }

    if (free_slot == -1) {
        errno = ENOSPACE;
        sem_post(lock);
        sem_close(lock);
        return -1;
    }

   
    size_t copy_len = (len > MSGSIZE) ? MSGSIZE : len;
    memset(SM[sockfd].tx_buffer[free_slot].payload, 0, MSGSIZE);
    memcpy(SM[sockfd].tx_buffer[free_slot].payload, buf, copy_len); 
    
    //  mark it as occupied. We not send it to the network here
    // We leave tx_time as -1. Thread s runs in the background, sees this flag, 
    // and handles the actual udp network transmission
    SM[sockfd].tx_buffer[free_slot].is_occupied = true;
    SM[sockfd].tx_buffer[free_slot].tx_time = -1; 
    SM[sockfd].unique_msg++;
    sem_post(lock); 
    sem_close(lock);

    return copy_len;
}

int k_recvfrom(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen) {
    if (sockfd < 0 || sockfd >= MAX_SOCKETS || SM == NULL || !SM[sockfd].is_allocated) {
        return -1;
    }

    sem_t *lock = get_socket_lock(sockfd);
    sem_wait(lock); 

    int ready_slot = -1;
    for (int i = 0; i < BUFFSIZE; i++) {
        if (SM[sockfd].rx_buffer[i].is_occupied) {
            ready_slot = i;
            break;
        }
    }

    if (ready_slot == -1) {
        errno = ENOMESSAGE; 
        sem_post(lock);
        sem_close(lock);
        return -1;
    }

    size_t copy_len = (len > MSGSIZE) ? MSGSIZE : len;
    memcpy(buf, SM[sockfd].rx_buffer[ready_slot].payload, copy_len);

    
    // Instead of just freeing the single slot. we shift all remaining messages forward by 1
    // this array shifting mechanism guarantees strict FIFO  ordering 
    // and prevents sequence numbers from misaligning when packets wrap around the buffer
    for (int j = ready_slot; j < BUFFSIZE - 1; j++) {
        SM[sockfd].rx_buffer[j] = SM[sockfd].rx_buffer[j+1];
    }
   
    SM[sockfd].rx_buffer[BUFFSIZE - 1].is_occupied = false;
 

    if (SM[sockfd].rwnd.current_size < WINDOWSIZE) {
        SM[sockfd].rwnd.current_size++;
    }
    
    SM[sockfd].rx_buffer_full = false;

    if (src_addr != NULL && addrlen != NULL) {
        *addrlen = sizeof(struct sockaddr_in);
        memcpy(src_addr, &SM[sockfd].remote_ip, *addrlen);
    }

    sem_post(lock); 
    sem_close(lock);

    return copy_len;
}

int k_close(int sockfd) {
    if (sockfd < 0 || sockfd >= MAX_SOCKETS || SM == NULL || !SM[sockfd].is_allocated) {
       
        return -1;
    }

    sem_t *lock = get_socket_lock(sockfd);
    sem_wait(lock); 

    // We not immediately wipe the memory or close the UDP socket.
    // that would instantly sever the connection and drop packets in transit
    // instead, we just flip this flag. Thread s will see it, finish sending 
    // any pending data, send a FIN packet, and handle the graceful teardown
    SM[sockfd].is_teardown_initiated = true;

    sem_post(lock); 
    sem_close(lock);

    return 0;
}