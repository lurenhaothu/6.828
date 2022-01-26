#include "ns.h"
#include <kern/e1000.h>

extern union Nsipc nsipcbuf;

void
output(envid_t ns_envid)
{
	binaryname = "ns_output";
	int r;
	envid_t e;


	// LAB 6: Your code here:
	// 	- read a packet from the network server
	//	- send the packet to the device driver
	while(1){
		r = ipc_recv(&e, (void*)&nsipcbuf, 0);
		if(r < 0) panic("%e\n", r);
		if(e != ns_envid) continue;
		else if(r != NSREQ_OUTPUT) continue;
		r = sys_tsend((void*)nsipcbuf.pkt.jp_data, nsipcbuf.pkt.jp_len);
		while(r == -E1000_FULL){
			sys_yield();
			r = sys_tsend((void*)nsipcbuf.pkt.jp_data, nsipcbuf.pkt.jp_len);
		}
		//r = sys_page_unmap(thisenv->env_id, (void*)UTEMP);
		//if(r < 0) panic("%e\n", r);
	}
}
