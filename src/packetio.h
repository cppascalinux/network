#include<netinet/ether.h>
#include<pcap/pcap.h>

/* *
* @brief Process a frame upon receiving it .
*
* @param buf Pointer to the frame .
* @param len Length of the frame .
* @param id ID of the device ( returned by ‘ addDevice ‘) receiving
*
 current frame .
* @return 0 on success , -1 on error .
* @see add_device
*/
typedef int (*frame_receive_callback)(const void*,int,int);

extern frame_receive_callback global_callback;

/* *
* @bried wrap up function for the call back
*/
void callback_wrapper(u_char *user,const struct pcap_pkthdr *h,const u_char *bytes);

/* *
* @brief Encapsulate some data into an Ethernet II frame and send it*
* @param buf Pointer to the payload .
* @param len Length of the payload .
* @param eth_type EtherType field value of this frame .
* @param dest_mac MAC address of the destination .
* @param id ID of the device ( returned by ‘ addDevice ‘) to send on .
* @return 0 on success , -1 on error .
* @see add_device
*/
int send_frame(const void *buf,int len,int eth_type,const void *dest_mac,int id);


/* *
* @brief Register a callback function to be called each time*
 Ethernet II frame was received .
*
* @param callback the callback function .
* @return 0 on success , -1 on error .
* @see frame_receive_callback
*/
int set_frame_receive_callback(frame_receive_callback callback);

/* *
* @brief Receive frames in a loop, until cnt packets are processed
* @param cnt Number of packets to process, infinity for cnt<=0
* @return 0 on success, -1 on error
*/
int receive_frame_loop(int cnt);