#include "ns.h"
#include <kern/e1000.h>

extern union Nsipc nsipcbuf;

#define BUF_PAGE_SIZE 2

void
input(envid_t ns_envid)
{
	//cprintf("enter input()\n");
	binaryname = "ns_input";
	int r;
	struct jif_pkt *pk[BUF_PAGE_SIZE];
	void* base[BUF_PAGE_SIZE];
	for(int i = 0; i < BUF_PAGE_SIZE; i++){
		r = sys_page_alloc(thisenv->env_id, (void*)UTEMP + i * PGSIZE, PTE_U | PTE_P | PTE_W);
		if(r < 0) panic("%e\n",r);
		pk[i] = (struct jif_pkt*) (UTEMP + i * PGSIZE);
		base[i] = pk[i]->jp_data;
	}

	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server
	// Hint: When you IPC a page to the network server, it will be
	// reading from it for a while, so don't immediately receive
	// another packet in to the same physical page.
	int index = 0;
	while(1){
		//cprintf("one loop\n");
		r = sys_trcv(base[index]);
		while(r == -E1000_NON){
			sys_yield();
			r = sys_trcv(base[index]);
		}
		if(r > 0){//receive success
			pk[index]->jp_len = r;
			//cprintf("received one packet, size: %d\n", r);
			ipc_send(ns_envid, NSREQ_INPUT, (void*) UTEMP + index * PGSIZE, PTE_U | PTE_P | PTE_W);
			index = (index + 1) % BUF_PAGE_SIZE;
			//sys_yield();
		}
	}
}
