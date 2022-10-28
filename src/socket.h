/**
 * @file socket.h
 * @brief POSIX - compatible socket library supporting TCP protocol on IPv4.
 */
#include<sys/types.h>
#include<sys/socket.h>
#include<netdb.h>
#include<semaphore.h>

#define CWR 0x80U
#define ECE 0x40U
#define URG 0x20U
#define ACK 0x10U
#define PSH 0x8U
#define RST 0x4U
#define SYN 0x2U
#define FIN 0x1U
#define SOCKET_BUF_SIZE 65535U
#define FD_MASK 0x10000000
#define MTU 1460U
#define RESEND_SEC 1U
#define CLOSE_TIMEOUT 60U
#define TOTAL_PORTS 65536
#define MIN_PORT 1024

// states for sockets
#define CLOSED 0U
#define LISTEN 0x1U
#define SYN_SENT 0x2U
#define SYN_RCVD 0x4U
#define ESTABLISHED 0x8U
#define FIN_WAIT_1 0x10U
#define FIN_WAIT_2 0x20U
#define CLOSING 0x40U
#define TIMED_WAIT 0x80U
#define CLOSE_WAIT 0x100U
#define LAST_ACK 0x200U
#define ALL_STATES 0xFFFFU

/**
 * @brief struct for a TCP packet.
 * 
 */
struct tcp_packet
{
	size_t len;
	struct sockaddr_in src,dest;
	uint32_t seq,ack;
	uint8_t header_len;
	uint8_t flag;
	uint16_t win;
	uint8_t *options;
	uint8_t *data;
};
typedef struct tcp_packet tcp_packet;

/**
 * @brief Struct for a socket
 * 
 */
struct socket_s
{
	struct socket_s *next;
	int fd;
	uint16_t state;
	uint16_t mtu;
	struct sockaddr_in src,dest;

	size_t max_conn,num_conn;
	struct socket_s **conn;
	sem_t conn_sem;

	pthread_mutex_t lock;

	uint32_t send_seq,send_ack;
	sem_t write_sem;

	uint8_t recv_buf[SOCKET_BUF_SIZE],recv_valid[SOCKET_BUF_SIZE];
	uint32_t recv_ack,recv_seq;
	sem_t read_sem;
};
typedef struct socket_s socket_s;



/**
 * @brief Init the socket threads and other stuff
 * 
 */
void init_socket();

/**
 * @brief Send a tcp packet.
 * 
 * @param p The TCP packet to send.
 * @return 0 on success, -1 on error.
 */
int send_tcp_packet(const tcp_packet p);

/**
 * @brief Free the contents allocated in a TCP packet.
 * 
 * @param p The TCP packet.
 */
void free_packet(tcp_packet p);

/**
 * @brief Get the pointer to the socket by its fd.
 * 
 * @param fd The file descriptor of the socket.
 * @return Pointer to the socket, 0 if none is found.
 */
socket_s *get_socket_fd(int fd);

/**
 * @brief Check if two socket address are the same
 * 
 * @param a one socket address
 * @param b another socket address
 * @return 1 if a==b, 0 otherwise
 */
int equal_sockaddr(struct sockaddr_in a,struct sockaddr_in b);

/**
 * @brief Get the pointer to a socket, given the source and destination address, and the allowed states
 * 
 * @param src Source socket address of the socket
 * @param dest Destination socket address of the socket
 * @param states Allowed states for the socket
 * @return Pointer to the socket, 0 if none is found
 */
socket_s *get_socket_addr(struct sockaddr_in src,struct sockaddr_in dest,uint16_t states);

/**
 * @brief Fill a tcp_packet struct with the given packet buffer
 * 
 * @param buf Pointer to the buffer
 * @param len Length of the packet
 * @return A tcp_packet struct of the TCP packet
 */
tcp_packet get_tcp_packet(uint8_t *buf,size_t len);


/**
 * @brief Handle the received SYN packet
 * 
 * @param p The TCP packet
 * @return 0 on success, -1 on error
 */
int syn_handler(tcp_packet p);

/**
 * @brief Handle the SYN/ACK packet
 * 
 * @param p The TCP packet
 * @return 0 on success, -1 on error
 */
int syn_ack_handler(tcp_packet p);

/**
 * @brief Handles a normal ACK packet
 * 
 * @param p The TCP packet
 * @return 0 on success, -1 on error
 */
int ack_handler(tcp_packet p);

/**
 * @brief Handles a FIN/ACK packet
 * 
 * @param p The TCP packet
 * @return 0 on success, -1 on error
 */
int fin_handler(tcp_packet p);

/**
 * @brief Handles all the TCP packets
 * 
 * @param buf pointer to the buffer of the packet
 * @param len Length of the packet
 * @return 0 on success, -1 on error
 */
int tcp_packet_handler(const void *buf,int len);

/**
 * @brief Thread to send a packet, and resend when no ACK has been received.
 * 
 * @param vargp Pointer to the socket, and the packet to send.
 * @return 0 on success, -1 on error
 */
void *packet_sender(void *vargp);

/**
 * @brief Thread to destory a socket, and free the allocated memory after a timeout.
 * 
 * @param vargp porinter to the socket to destroy.
 * @return 0 on success, -1 on error
 */
void *socket_destructor(void *vargp);

int __real_socket(int domain,int type,int protocol);
int __real_bind(int sockfd,const struct sockaddr *addr,socklen_t addrlen);
int __real_listen(int sockfd,int backlog);
int __real_connect(int sockfd,const struct sockaddr *addr,socklen_t addrlen);
int __real_accept(int sockfd,struct sockaddr *addr,socklen_t *addrlen);
ssize_t __real_read(int fd,void *buf,size_t count);
ssize_t __real_write(int fd,void *buf,size_t count);
int __real_close(int fd);
int __real_getaddrinfo(const char *node,const char *service,const struct addrinfo *hints,struct addrinfo **res);

/**
 * @brief Create a file descriptor for a socket
 * 
 * @param domain Support AF_INET only.
 * @param type Support SOCK_STREAM only.
 * @param protocol Support IPPROTO_TCP or 0 only.
 * @return A file descriptor on success, -1 on error
 */
int __wrap_socket(int domain,int type,int protocol);

/**
 * @brief Override this function, otherwise it will return an error
 * 
 * @param sockfd 
 * @param level 
 * @param optname 
 * @param optval 
 * @param optlen 
 * @return int 
 */
int __wrap_setsockopt(int sockfd,int level,int optname,const void *optval,socklen_t optlen);

/**
 * @brief Bind a socket to a given address
 * 
 * @param sockfd File descriptor of the socket
 * @param addr The address to bind to
 * @param addrlen Length of *address
 * @return 0 on success, -1 on error
 */
int __wrap_bind(int sockfd,const struct sockaddr *addr,socklen_t addrlen);

/**
 * @brief Set a socket to passive mode and accept incoming connections
 * 
 * @param socket File descriptor of the socket
 * @param backlog Maximum number of buffered connections
 * @return 0 on success, -1 on error
 */
int __wrap_listen(int sockfd,int backlog);

/**
 * @brief Connect to a given address
 * 
 * @param sockfd File descriptor of the socket
 * @param addr The address to connect to
 * @param addrlen Length of *address
 * @return 0 on success, -1 on error
 */
int __wrap_connect(int sockfd,const struct sockaddr *addr,socklen_t addrlen);

/**
 * @brief Accept a incoming connection for a listening socket
 * 
 * @param sockfd File descriptor of the socket
 * @param addr The peer address
 * @param addrlen Length of *address
 * @return A file descriptor on success, -1 on error
 */
int __wrap_accept(int sockfd,struct sockaddr *addr,socklen_t *addrlen);

/**
 * @brief Read bytes from fd into buf. Block until we read count bytes.
 * Fallback to real read for system fds.
 * @param fd File descriptor to read from.
 * @param buf Buffer to write into.
 * @param count Number of bytes to read.
 * @return Number of bytes read on success; -1 on error.
 */
ssize_t __wrap_read(int fd,void *buf,size_t count);

/**
 * @brief Write bytes from buf into fd. Block until we write count bytes.
 * Fallback to real write for system fds.
 * 
 * @param fd File descriptor to write into.
 * @param buf Buffer to read from.
 * @param count Number of bytes tp write.
 * @return Number of bytes written on success; -1 on error.
 */
ssize_t __wrap_write(int fd,void *buf,size_t count);

/**
 * @brief Close a normal (not LISTEN) socket.
 * 
 * @param s Pointer to the socket.
 * @return 0 on success, -1 on error.
 */
int close_normal(socket_s *s);

/**
 * @brief Close a file descriptor. Fallback to real close for system fds.
 * 
 * @param fd File descriptor to close.
 * @return 0 on success, -1 on error.
 */
int __wrap_close(int fd);

/**
 * @brief Get the address information
 * 
 * @param node IP address
 * @param service Port number
 * @param hints Hints
 * @param res Pointer to store the result
 * @return 0 on success, -1 on error
 */
int __wrap_getaddrinfo(const char *node,const char *service,const struct addrinfo *hints,struct addrinfo **res);

/**
 * @brief Free the memory allocated by getaddrinfo
 * 
 * @param res Pointer to the linked list
 * @return 0 on success, -1 on error
 */
int __wrap_freeaddrinfo(struct addrinfo *res);