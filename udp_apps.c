
#include "uip/uip.h"
#include "udp_apps.h"

void udp_callbacks(void) __banked
{
	dhcp_callback(uip_udp_conn->lport); 	// let the application decide if this is for it or not
}