/* *
* @file device . h
* @brief Library supporting network device management .
*/

#include<pcap/pcap.h>
#include<netinet/ether.h>

/* *
* device data type
*/
struct device
{
	int id,fd;
	char *name;
	pcap_t *handle;
	struct ether_addr addr;
	struct device *next;
};

typedef struct device device_t;

// some global variables
extern device_t *dev_head;
extern char errbuf[PCAP_ERRBUF_SIZE];


/*
* Get all available network devices
* @return pointer to array of strings corresponding to names of devices
*/
char **get_available_device();

/*
* Free the list created by get_available_devices
* @param pointer to the array of strings
*/
void free_device_list(char **s);


/* *
* Add a device to the library for sending / receiving packets .
*
* @param device Name of network device to send / receive packet on .
* @return A non - negative _device - ID_ on success , -1 on error .
*/
int add_device(const char *name);

/* *
* Find a device added by ‘ add_device ‘.
*
* @param device Name of the network device .
* @return pointer to the device on success , NULL if no such device
* was found .
*/
device_t *find_device_name(const char *name);

/* *
* Find a device added by ‘ add_device ‘.
*
* @param device id of the network device .
* @return pointer to the device on success , NULL if no such device
* was found .
*/
device_t *find_device_id(int id);

/* *
* Find a device added by ‘ add_device ‘ by its file descriptor.
*
* @param device fd of the network device .
* @return pointer to the device on success , NULL if no such device
* was found .
*/
device_t *find_device_fd(int fd);


/* *
* free the memory allocated by dev
*
* @param pointer to the device
*/
void free_device(device_t *dev);