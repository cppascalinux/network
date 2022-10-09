/* *
* @file ip.h
* @brief Library supporting sending / receiving IP packets encapsulated
in an Ethernet II frame .
*/
#include<netinet/ip.h>
#include<netinet/ether.h>
#include<pthread.h>
#include"packetio.h"

// maximum hops to deliver a packet
#define MAX_HOPS 16
// default route entry timeout (sec)
#define ROUTE_TIMEOUT 3
// default interval (sec) for hosts to broadcast its routing table
#define ROUTE_INTERVAL 1
// Ethernet frame type: ip
#define ETH_TYPE_IP 8
// Ethernet frame type: routing distance vector
#define ETH_TYPE_ROUTE 0x1111

/**
 * @brief Entry of the routing table
 * @param dest: target ip address
 * @param mask: subnet mask
 * @param id: id of device to forward package to
 * @param hops: number of hops from the target
 * @param  timeout: remaining timeout for this entry
 * @param  next: next entry in the list
 */
struct route
{
	struct in_addr dest;
	struct in_addr mask;
	int id;
	struct ether_addr mac;
	int hops;
	int timeout;
	struct route *next;
};
typedef struct route route_t;

// mutex lock for global routing table
extern pthread_mutex_t route_lock;

// head pointer to global routing table
extern route_t *route_head;

/**
 * @brief Select a routing entry according to the longest prefix rule
 * @param dest Destination IP address
 * @param r Pointer to the routing entry to store the result
 * @return 0 on success, -1 on error
 */
int select_routing_entry(const struct in_addr dest,route_t *r);

/**
 * @brief Send an IP packet to specified host .
 * @param src Source IP address.
 * @param dest Destination IP address .
 * @param proto Value of ‘ protocol ‘ field in IP header .
 * @param buf pointer to IP payload
 * @param len Length of IP payload
 * @return 0 on success , -1 on error .
 */
int send_ip_packet(const struct in_addr src,const struct in_addr dest,int proto,const void *buf,int len);

/**
 * @brief Forward an ip packet using the routing table. The destination address is contained in the packet.
 * @param buf Pointer to the packet
 * @param len Length of the packet
 * @return 0 on success, -1 on error
 */
int forward_ip_packet(const void *buf,int len);

/**
 * @brief Process an IP packet upon receiving it .
 * @param buf Pointer to the packet .
 * @param len Length of the packet .
 * @return 0 on success , -1 on error .
 */
typedef int (*ip_packet_receive_callback)(const void *buf,int len);

/**
 * @brief Register a callback function to be called each time an IP
 * packet was received .
 * @param callback The callback function .
 * @return 0 on success , -1 on error .
 * @see ip_packet_receive_callback
 */
int set_ip_packet_receive_callback(ip_packet_receive_callback callback);

/**
 * @brief Manully add an item to routing table . Useful when talking
 * with real Linux machines .
 * @param dest The destination IP prefix .
 * @param mask The subnet mask of the destination IP prefix .
 * @param id Id of the device to send packet on.
 * @param mac Mac address of device of the next hop.
 * @param hops Hops from the destination
 * @param timeout Remaining timeout for this entry
 * @return 0 on success , -1 on error
 */
int update_routing_entry(const struct in_addr dest,const struct in_addr mask,
	int id,struct ether_addr mac,int hops,int timeout);

/**
 * @brief Update routing table with devices within the host.
 * @return 0 on success, -1 on error.
 */
int route_local_device();

/**
 * @brief Broadcast the routing table through all the devices on the host.
 * @return 0 on success, -1 on error.
 */
int broadcast_distance_vector();

/**
 * @brief Subtract 1 from the timeout of all entries in the table, and remove the entries with 0 timeout
 * @return 0 on success, -1 on error
 * @see struct route
 */
int tick_routing_table();

/**
 * @brief Update the routing table using the routing table message sent by its neighbours.
 * @param id The id of device that received the message
 * @param buf Pointer to the message
 * @param len Length of the message
 */
int update_distance_vector(int id,const unsigned char *buf,int len);

/**
 * @brief Preprocess the received packet. 
 * It first drops out all loopback packets. 
 * Then it checks whether the packet is the routing table from its neighbours. 
 * Then it checks whether the destination address is itself. 
 * If so, it calls the ip_packet_receive_callback. 
 * Else, it forwards the packet according to the routing table. 
 * @param id The id of device that received the packet
 * @param buf Pointer to the packet
 * @param len Length of the packet
 * @return 0 on success, -1 on error
 */
int packet_preprocessor(const void* buf,int len,int id);

/**
 * @brief Check whether a mac address belongs to the host.
 * @param mac Input mac address.
 * @return -1 if the mac belongs to the host, 0 otherwise.
 */
int check_mac(struct ether_addr mac);

/**
 * @brief Check whether an IP address belongs to the host.
 * @param ip Input IP address.
 * @return -1 if the IP address belongs to the host, 0 otherwise.
 */
int check_ip_addr(struct in_addr ip);

/**
 * @brief Thread in the background, to handle packet IO and forwarding.
 */
void *router_thread(void *vargp);

/**
 * @brief Thread to broadcast and tick te routing table every second
 */
void *timer_thread(void *vargp);

/**
 * @brief Print the routing table
 */
void print_routing_table();

/**
 * @brief print brief information of a received ip packet
 * @param buf Pointer to the packet
 * @param len Lengith of the packet
 * @return 0
 */
int print_packet(const void *buf,int len);