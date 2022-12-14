/* *
* @file device . h
* @brief Library supporting network device management .
*/

#include<pcap/pcap.h>
#include<netinet/ether.h>
#include<pthread.h>

// default buffer timeout, in miliseconds
#define BUF_TIMEOUT 10

/**
 * @brief device data type
 * @param id: device id
 * @param fd: device file descriptor
 * @param name: device name
 * @param handle: libpcap handle
 * @param addr: hardware address
 * @param ip_addr: ipv4 address
 * @param lock: mutex lock for read/write of the device
 * @param next: pointer to he next device entry
 */
struct device
{
	int id;
	int fd;
	char *name;
	pcap_t *handle;
	struct ether_addr addr;
	struct in_addr ip_addr;
	pthread_mutex_t lock;
	struct device *next;
};

typedef struct device device_t;

// head pointer to list of devices
extern device_t *dev_head;

// mutex lock for list of devices
extern pthread_mutex_t dev_lock;

// error buffer for libpcap
extern char errbuf[PCAP_ERRBUF_SIZE];


/**
 * @brief Get all available network devices
 * @return pointer to array of strings corresponding to names of devices
 */
char **get_available_device();

/**
 * @brief Free the list created by get_available_devices
 * @param pointer to the array of strings
 */
void free_device_list(char **s);


/**
 * @brief Add a device to the library for sending / receiving packets .
 * @param device Name of network device to send / receive packet on .
 * @return A non - negative _device - ID_ on success , -1 on error .
 */
int add_device(const char *name);

/**
 * @brief Add all devices, except lo, into the device list.
 * 
 * @return Number of devices added.
 */
int add_all_devices();

/**
 * @brief Find a device added by ‘ add_device ‘.
 * @param device Name of the network device .
 * @return pointer to the device on success , NULL if no such device
 * was found .
 */
device_t *find_device_name(const char *name);

/**
 * @brief Find a device added by ‘ add_device ‘.
 * @param device id of the network device .
 * @return pointer to the device on success , NULL if no such device
 * was found .
 */
device_t *find_device_id(int id);

/**
 * @brief Find a device added by ‘ add_device ‘ by its file descriptor.
 * @param device fd of the network device .
 * @return pointer to the device on success , NULL if no such device
 * was found .
 */
device_t *find_device_fd(int fd);


/**
 * @brief Free the memory allocated by dev
 * @param pointer to the device
 */
void free_device(device_t *dev);

/**
 * @brief Print the device table
 */
void print_device_table();
