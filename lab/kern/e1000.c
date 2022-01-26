#include <kern/e1000.h>
#include <kern/pmap.h>
#include <kern/pci.h>
#include <kern/pmap.h>
#include <inc/string.h>

// LAB 6: Your driver code here

volatile int32_t *e1000_reg;
struct tx_desc e1000_tdl[NTDSCPT]; //transmit descriptor list
struct pk_buffer e1000_pkbuf[NTDSCPT];
struct rx_desc e1000_rdl[NRDSCPT]; //receive descriptor list
struct pk_buffer e1000_pkbuf_rcv[NRDSCPT]; //receive packet buffer

void e1000cw(uint32_t index, uint32_t val){
    e1000_reg[index] = val;
    e1000_reg[index];
    if(e1000_reg[index] != val) cprintf("write index %x failed\n", index);
}

// Attach function for e1000 network card
int pci_e1000_attach(struct pci_func *pcif)
{
	//cprintf("enter e1000 attach func\n");
    pci_func_enable(pcif);

    e1000_reg = mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);

    //size is 0x20000 byte, or 0x20 pages

    //cprintf("e1000 device status register: %08x, size: %d\n", e1000_reg[2], pcif->reg_size[0]);
    //cprintf("e100_tdl is %08x, %08x\n", e1000_tdl, PADDR(e1000_tdl) + 8);

    e1000_tx_init();
    e1000_rx_init();

    //test_tsend();

    //cprintf("leave e1000 attach func\n");
	return 1;
}

// Trans a packet to e1000, packet size limit: PK_MAXSIZE
// return 
// 0 if sucess, 
// -E1000_FULL if the packet is full, 
// -E1000_INVAL if size invalid parameters
int e1000_transpk(void* base, int size){
    //cprintf("enter e1000 transmit!, index: %d, status is: %x\n", 0, e1000_tdl[0].status);
    if(!base || size > PK_MAXSIZE) return -E1000_INVAL;
    if((e1000_tdl[E1000_TTAIL].cmd & E1000_TXD_CMD_RS) &&
        !(e1000_tdl[E1000_TTAIL].status & E1000_TXD_STAT_DD)){
        return -E1000_FULL;
    }
    memmove(e1000_pkbuf[E1000_TTAIL].pk, base, size);
    e1000_tdl[E1000_TTAIL].length = size;
    e1000_tdl[E1000_TTAIL].status &= (~E1000_TXD_STAT_DD);
    e1000_tdl[E1000_TTAIL].cmd |= (E1000_TXD_CMD_EOP >> 24); //only one packet, so mark as the end packet
    e1000_tdl[E1000_TTAIL].cmd |= (E1000_TXD_CMD_RS >> 24);
    E1000_TTAIL = (E1000_TTAIL + 1) % NTDSCPT;
    return 0;
}

void test_tsend(){
    char message[] = "Hello world!";
	int r = e1000_transpk(message ,13);
	if(r == 0) cprintf("sent a message!\n");
	else cprintf("sending failed\n");
}

void e1000_tx_init(){
    for(int i = 0; i < NTDSCPT; i++){
        e1000_tdl[i].addr = (uint32_t) PADDR(&e1000_pkbuf[i]);
        e1000_tdl[i].cmd |= (E1000_TXD_CMD_RS >> 24);
        //e1000_tdl[i].status |= E1000_TXD_STAT_DD;
    }
    cprintf("tdl 2 cmd is: %x", e1000_tdl[2].cmd);
    e1000cw(E1000_TDBAL >> 2, (int32_t)PADDR(e1000_tdl));
    e1000cw(E1000_TDLEN >> 2, NTDSCPT * sizeof(struct tx_desc));
    e1000cw(E1000_TDH >> 2, 0);
    e1000cw(E1000_TDT >> 2, 0);
    int32_t tctl = 0;
    tctl |= E1000_TCTL_EN;
    tctl |= E1000_TCTL_PSP;
    tctl |= (0x10 << 4);
    tctl |= (0x40 << 12);
    e1000cw(E1000_TCTL >> 2, tctl);
    int32_t tipg = 0;
    tipg |= 10; //TIPG
    tipg |= (4 << 10); //IPGR1
    tipg |= (6 << 20); //IPGR2
    e1000cw(E1000_TIPG >> 2, tipg);
}

void e1000_rx_init(){
    for(int i = 0; i < NRDSCPT; i++){
        e1000_rdl[i].addr = (uint32_t) PADDR(&e1000_pkbuf_rcv[i]);
        e1000_rdl[i].status = 0;
    }
    e1000cw(E1000_RA >> 2, 0x12005452);
    e1000cw((E1000_RA + 4) >> 2, 0x5634 | (1 << 31));
    //e1000cw(E1000_MTA >> 2, 0);
    for(int i = E1000_MTA; i < E1000_RA; i += 4){
        e1000cw(i >> 2, 0);
    }
    e1000cw(E1000_RDBAL >> 2, (int32_t) PADDR(e1000_rdl));
    e1000cw(E1000_RDLEN >> 2, NRDSCPT * sizeof(struct rx_desc));
    e1000cw(E1000_RDH >> 2, 0);
    e1000cw(E1000_RDT >> 2, NRDSCPT - 1);
    int32_t rctl = 0;
    rctl |= E1000_RCTL_EN;
    rctl |= E1000_RCTL_BAM;
    rctl |= E1000_RCTL_SZ_2048;
    rctl |= E1000_RCTL_SECRC;
    e1000cw(E1000_RCTL >> 2, rctl);
}

// Receive a packet from e1000
// return 
// size if sucess, 
// -E1000_NON if no packet is received 
// -E1000_INVAL if invalid parameters
int e1000_rcvpk(void* base){
    //cprintf("enter e1000_rcv\n");
    if(!base) return -E1000_INVAL;
    int index = (E1000_RTAIL + 1) % NRDSCPT;
    //cprintf("index is %d, base is %08x, %c, hey!\n", index, base, *(int*)base);
    if(e1000_rdl[index].status & E1000_RXD_STAT_DD){ //ready to receive
        //cprintf("now the head is: %d\n", E1000_RHEAD);
        memmove(base, e1000_pkbuf_rcv[index].pk, e1000_rdl[index].length);
        e1000_tdl[index].status &= (~E1000_RXD_STAT_DD);
        E1000_RTAIL = index;
        return e1000_rdl[index].length;
    }else{//full
        return -E1000_NON;
    }
    return 0;
}