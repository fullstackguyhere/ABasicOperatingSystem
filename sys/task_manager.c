#include<sys/virt_mem.h>
#include<sys/task_manager.h>
#include<sys/phy_mem_manager.h>
#include<sys/gdt.h>
#include<sys/idt.h>
#include<sys/paging.h>
#include<sys/tarfs.h>
#include<sys/kmalloc.h>
#include<sys/defs.h>
#include<sys/kstring.h>

int proc_start=0;
int proc_end=0;

PCB *active = NULL;

uint64_t proc_Q[PROC_SIZE + 1];

PCB all_pro[PROC_SIZE];

//PCB *all_pro;

uint64_t kernbase;

uint64_t upml4;

uint64_t ustacktop;

uint64_t u_heapboundary;

uint64_t global_pid=1;

extern int ctr;

extern uint64_t kern_VA;

extern uint8_t initial_stack[4096];

uint64_t proc_descriptor[PROC_SIZE];

uint64_t kstackbase;

uint8_t cow_page[4096];

uint8_t parent_stack[4096];



extern uint64_t RING_0_MODE;


void copy_parent_stack()
{
	uint8_t *parent_stackbase=(uint8_t *)active->u_stackbase;
	for (int i=0;i<(4096);i++)
	{
		parent_stack[i]=*(parent_stackbase - i);
	}
}

PCB *get_nextproc()
{
	//kprintf("proc start:%d\n",proc_start);
	
	PCB *temp;
	
	while(1)
	{
		//kprintf("proc_start %d proc_end %d\n",proc_start,proc_end);
		if(proc_start==proc_end)
		{
			kprintf("0 process in the queue\n");
			return NULL;
		}
		temp=all_pro + proc_Q[proc_start];
		proc_Q[proc_start]=-1;
		proc_start=(proc_start + 1)%(PROC_SIZE + 1);
		if(proc_descriptor[temp->sno]==1 && (temp->state==RUNNING || temp->state==NEW_PROC))
			break;
	}
	//kprintf("from get next new kstack:%p new:%p\n",(temp)->k_stack,(temp)->u_stack);
	
	return temp;
}

void init_stack(PCB *proc)
{
	int i;
	uint64_t top=(ustacktop -(4096 * 2 * proc->sno)) & 0xFFFFFFFFFFFFF000;

	
	if(proc->pid==1)
	{
		
		for(i=0;i<=4096;i++)
		{
			*((uint64_t *)(top - i))=0;
		}
		proc->u_stack=top;
		proc->u_stackbase=top;
	}
	else
	{	//kprintf("\nfrom init active->u_stackbase: %p %p\n",active->u_stack,*((uint64_t *)(active->u_stackbase-1)));

		//kprintf("phys stack: %p\n",phys_stack);
		//ctr=1;
		proc->u_stackbase=top;
		uint64_t parent_stackbase=active->u_stackbase;
		for(i=0;i<4096;i++)
		{
			if(parent_stackbase>=active->u_stack)
			{
				proc->u_stack=top;
			}
			*((uint8_t *)(top))=parent_stack[i];
			top--;
			parent_stackbase--;
		}
		//proc->u_stack;
		//kprintf("\nfrom init base: %p %p\n",proc->u_stack,*((uint64_t *)proc->u_stack));	
	}
	
	return;
}


uint64_t mappageTable(uint64_t proc_index, int new)
{
	
	uint64_t *v_pml4=(uint64_t *)(upml4-(4096 * proc_index));
	//kprintf("\nv_pml4:%p\n",v_pml4);
	
	//kprintf("mapping page table\n");
	
	uint64_t p_pml4=map_phyaddr((uint64_t)v_pml4);
	
	p_pml4=p_pml4 & 0xFFFFFFFFFFFFF000;
	//kprintf("\np_pml4:%p\n",p_pml4);
	uint64_t base=0xFFFF000000000000;

	base=(((base >> 48)<<9)|0x1FE)<<39;
	base=(((base >> 39)<<9)|0x1FE)<<30;
	base=(((base >> 30)<<9)|0x1FE)<<21;
	base=(((base >> 21)<<9)|0x1FE)<<12;

	uint64_t *p1=(uint64_t *)base;
	
	for(int i=0;i<510;i++)
	{
		v_pml4[i]=0x0;	
	}
	v_pml4[511]=p1[511];
	v_pml4[510]=(p_pml4 & 0xFFFFFFFFFFFFF000) | 7;
	
	
	
	//kprintf("\nvpml4 511: %p\n",v_pml4[511]);
	//kprintf("old 511: %p\n",(p1[511] & 0xFFFFFFFFFFFFF000) | 7);
	//while(1);
	return p_pml4;
}

void change_ptable(uint64_t addr)
{
	uint64_t pml4=addr;
	__asm__(
	"movq %0,%%rax;\n"
	"movq %%rax,%%cr3;\n"
	:
	:"g"(pml4)
	);
	
	//while(1);
	return;
}

int check_cow(uint64_t addr)
{
	
	
	uint64_t p1_index = (addr << 16) >> (9 + 9 + 9 + 12 + 16);
	uint64_t p2_index = (addr << (16 +9)) >> (9 + 9 + 12 + 16 + 9);
	uint64_t p3_index = (addr << (16 + 9 + 9)) >> (9 + 12 + 16 + 9 + 9);
	uint64_t p4_index = (addr << (16 + 9 + 9 + 9)) >> (12 + 16 + 9 + 9 + 9);
	
	
	uint64_t base=0xFFFF000000000000;

	base=(((base >> 48)<<9)|0x1FE)<<39;
    base=(((base >> 39)<<9)|p1_index)<<30;
    base=(((base >> 30)<<9)|p2_index)<<21;
    base=(((base >> 21)<<9)|p3_index)<<12;
	
	uint64_t *p4=(uint64_t *)base;
	
	//base=0xFFFF000000000000;
    //
	//base=(((base >> 48)<<9)|0x1FE)<<39;
    //base=(((base >> 39)<<9)|0x1FE)<<30;
    //base=(((base >> 30)<<9)|0x1FE)<<21;
    //base=(((base >> 21)<<9)|0x1FE)<<12;
	//
	//uint64_t *p1=(uint64_t *)base;
	
	// kprintf("%p\n",p1[p1_index]);
	if(p4[p4_index]!=0 && (p4[p4_index] & 0b111)==5)
	{
		
		return 1;
	}
	
	return 0;
}


uint64_t get_phys_addr(uint64_t addr)
{
	uint64_t p1_index = (addr << 16) >> (9 + 9 + 9 + 12 + 16);
	uint64_t p2_index = (addr << (16 +9)) >> (9 + 9 + 12 + 16 + 9);
	uint64_t p3_index = (addr << (16 + 9 + 9)) >> (9 + 12 + 16 + 9 + 9);
	uint64_t p4_index = (addr << (16 + 9 + 9 + 9)) >> (12 + 16 + 9 + 9 + 9);
	
	
	uint64_t base=0xFFFF000000000000;

	base=(((base >> 48)<<9)|0x1FE)<<39;
    base=(((base >> 39)<<9)|0x1FE)<<30;
    base=(((base >> 30)<<9)|0x1FE)<<21;
    base=(((base >> 21)<<9)|0x1FE)<<12;
	
	uint64_t *p1=(uint64_t *)base;
	
	uint64_t p2_pg=p1[p1_index];
	
	if(!p2_pg)
		return 0;
	
	base=0xFFFF000000000000;

	base=(((base >> 48)<<9)|0x1FE)<<39;
    base=(((base >> 39)<<9)|0x1FE)<<30;
    base=(((base >> 30)<<9)|0x1FE)<<21;
    base=(((base >> 21)<<9)|p1_index)<<12;
	
	uint64_t *p2=(uint64_t *)base;
	
	uint64_t p3_pg=p2[p2_index];
	
	if(!p3_pg)
		return 0;
	
	
	if(!p2_pg)
		return 0;
	
	base=0xFFFF000000000000;

	base=(((base >> 48)<<9)|0x1FE)<<39;
    base=(((base >> 39)<<9)|0x1FE)<<30;
    base=(((base >> 30)<<9)|p1_index)<<21;
    base=(((base >> 21)<<9)|p2_index)<<12;
	
	uint64_t *p3=(uint64_t *)base;
	
	uint64_t p4_pg=p3[p3_index];
	
	if(!p4_pg)
		return 0;
	
	base=0xFFFF000000000000;
	
	base=(((base >> 48)<<9)|0x1FE)<<39;
    base=(((base >> 39)<<9)|p1_index)<<30;
    base=(((base >> 30)<<9)|p2_index)<<21;
    base=(((base >> 21)<<9)|p3_index)<<12;
	
	
	uint64_t *p4=(uint64_t *)base;
	
	uint64_t padd=p4[p4_index];
	
	if(!padd)
		return 0;
	
	//kprintf("phys addr of %p is %p\n",addr,p4[p4_index]);
	
	return padd;
}

void increment_childpg_ref(uint64_t vaddr_start,uint64_t vaddr_end)
{
	uint64_t marked=0;
	while (vaddr_start <= vaddr_end)
	{
		uint64_t paddr=(get_phys_addr(vaddr_start) & 0xFFFFFFFFFFFFF000);
		if(paddr != 0x0)
		{
			if(paddr != marked)
			{
				increment_reference_count(paddr);
				marked=paddr;
			}
		}
		vaddr_start++;
	}
	
}


void copy_vma(PCB *proc)
{
	vma *last_vma=(active->mmstruct).vma_list;
	while(last_vma != NULL)
	{
		//kprintf("copyiimg vma %p %p\n",last_vma->vstart, last_vma->vend);
		vma *new_vma=alloc_vma(last_vma->vstart, last_vma->vend);
		append_to_vma_list(proc,new_vma);
		increment_childpg_ref(last_vma->vstart, last_vma->vend);
		
		uint64_t start=last_vma->vstart;
		
		uint64_t end=last_vma->vend;
		
		uint64_t marked=0;
		
		while(start <= end)
		{
			uint64_t padd=get_phys_addr(start);
			if(padd==0 || padd==marked)
			{
				start++;
				continue;
				
			}
			change_ptable(proc->cr3);
			set_Cow(start,padd);
			
			change_ptable(active->cr3);
			marked=padd;
			
			start++;
			
		}
		last_vma = last_vma->nextvma;
	}
}


void cow(uint64_t addr)
{
	uint8_t *cow_addr=(uint8_t *)(addr & 0xFFFFFFFFFFFFF000);
	
	//kprintf("cow addr %p\n",cow_addr);
	
	for(int i=0;i<4096;i++)
		cow_page[i]=cow_addr[i];
	
	uint64_t p1_index = (addr << 16) >> (9 + 9 + 9 + 12 + 16);
	uint64_t p2_index = (addr << (16 +9)) >> (9 + 9 + 12 + 16 + 9);
	uint64_t p3_index = (addr << (16 + 9 + 9)) >> (9 + 12 + 16 + 9 + 9);
	uint64_t p4_index = (addr << (16 + 9 + 9 + 9)) >> (12 + 16 + 9 + 9 + 9);
	
	
	
	uint64_t base=0xFFFF000000000000;

	base=(((base >> 48)<<9)|0x1FE)<<39;
    base=(((base >> 39)<<9)|p1_index)<<30;
    base=(((base >> 30)<<9)|p2_index)<<21;
    base=(((base >> 21)<<9)|p3_index)<<12;
	
	uint64_t *p4=(uint64_t *)base;
	
	base=0xFFFF000000000000;
	
	__volatile__ uint64_t old_page=p4[p4_index];
	
	//kprintf("decrementing for %p\n",old_page);
    
	if(old_page!=0x5 && old_page!=0x0)
	{
		
		decrement_reference_count(old_page);
	}
	
	uint64_t padd=get_page();
	
	//kprintf("new p4 %p\n",padd);
	
	//kprintf("ref count %d %d\n",get_reference_count(padd),get_reference_count(p4[p4_index]));

	//remove_from_vma_list(active, (uint64_t)addr, (uint64_t)cow_addr + 4096);
	
	p4[p4_index]=(padd & 0xFFFFFFFFFFFFF000) | 7;
	flushTLB();
	
	//kprintf("p4 %p\n",p4[p4_index]);
	
	for(int i=0;i<4096;i++)
		cow_addr[i]=cow_page[i];
	
	
	//vma* seg_vma = alloc_vma(addr, addr+4096);
		// kprintf("seg_vma [%d - %d]\n", seg_vma->vstart, seg_vma->vend);
	//append_to_vma_list(active, seg_vma);
	
	return;
	
}
/*
void kernel_switch_to()
{
	__asm__(
	"pushq %%rax;\n"
	"pushq %%rbx;\n"
	"pushq %%rcx;\n"
	"pushq %%rdx;\n"
	"pushq %%rsi;\n"
	"pushq %%rdi;\n"
	"pushq %%rbp;\n"
	"pushq %%r8;\n"
	"pushq %%r9;\n"
	"pushq %%r10;\n"
	"pushq %%r11;\n"
	"pushq %%r12;\n"
	"pushq %%r13;\n"
	"pushq %%r14;\n"
	"pushq %%r15;\n"
	:
	:
	);
	(proc_Q + proc_end)->pid=active->pid;
	
	(proc_Q + proc_end)->k_stack=active->k_stack;
	
	uint64_t user_stack;
	__asm__(
	"movq %%rsp,%0;\n"
	:"=g"(user_stack)
	:
	);
	(proc_Q + proc_end)->u_stack=user_stack;
	
	(proc_Q + proc_end)->entry_point=active->entry_point;
	//proc_end->next=(PCB *)kmalloc(sizeof(PCB));
	(proc_Q + proc_end)->state=1;
	proc_end=proc_end + 1;
	PCB *p_to=get_nextproc();
	active=p_to;
	if(p_to->state==0)
	{
		
		__asm__(
		"movq %0,%%rsp;\n"
		"pushq %1;\n"
		"retq;\n"
		:
		:"g"(p_to->u_stack),"g"(p_to->entry_point)
		);
	}
	else
	{
		__asm__(
		"movq %0,%%rsp;\n"
		"popq %%r15;\n"
		"popq %%r14;\n"
		"popq %%r13;\n"
		"popq %%r12;\n"
		"popq %%r11;\n"
		"popq %%r10;\n"
		"popq %%r9;\n"
		"popq %%r8;\n"
		"popq %%rbp;\n"
		"popq %%rdi;\n"
		"popq %%rsi;\n"
		"popq %%rdx;\n"
		"popq %%rcx;\n"
		"popq %%rbx;\n"
		"popq %%rax;\n"
		//"retq;\n"
		:
		:"g"(p_to->u_stack)
		);
		return;
		
	}
}
*/

void init_kstack2()
{
	//kprintf("\nhere\n");
	kstackbase=(uint64_t)((kern_VA + 4096) & 0xFFFFFFFFFFFFF000);
	kern_VA = kern_VA + (4096 *2* (PROC_SIZE + 1));
	//kprintf("\nhere\n");
	//while(1);
}

void create_kstack2(PCB *proc)
{
	int i;
	//kstackdescriptor[proc->pid]=0;
	uint64_t top=kstackbase + (4096 * 2 * proc->sno);
	//kprintf("\nfrom init top: %p\n",top);
	for(i=0;i<=4096;i++)
	{
		*((uint8_t *)(top + i))=0;
		top++;
	}
	
	//kprintf("\nfrom init top: %p\n",top);
	proc->k_stack=top;
	proc->k_stackbase=top;
}

void init_kstack()
{
              //kprintf("\nhere\n");
              kstackbase=(uint64_t)(0xFFFFFFFFFFFFF000 - (4096 *3* (PROC_SIZE + 1)));
              //kern_VA = kstackbase + (4096 *2* (PROC_SIZE + 1));
              //kprintf("\nhere\n");
              //while(1);
}
 
void create_kstack(PCB *proc)
{
              int i;
              //kstackdescriptor[proc->pid]=0;
              uint64_t top=kstackbase + (4096 * 2 * (proc->sno));
              //kprintf("\nfrom init top: %p\n",top);
              for(i=0;i<4096 * 2;i++)
              {
                           *((uint8_t *)(top + i))=0;
                           top++;
              }
             
              // kprintf("\nfrom init top: %p\n",top);
              proc->k_stack=top;
              proc->k_stackbase=top;
}

void copy_kstack(PCB *proc)
{
	uint64_t kernel_stack=0;
	
	__asm__(
	"movq %%rsp,%0;\n"
	:"=g"(kernel_stack)
	:
	);
	
	uint64_t k_base;
	
	if(proc->pid==1)
		k_base=(uint64_t)&initial_stack[4096];
	else
		k_base=active->k_stackbase;
	//kprintf("kernel_stack:%p initial_stack:%p\n",kernel_stack,k_base);
	while (k_base>=kernel_stack)
	{
		*((uint8_t *)(proc->k_stack))=*((uint8_t *)k_base);
		k_base--;
		proc->k_stack--;
	}
	proc->k_stack++;
	//kprintf("new k stack:%p\n",proc->k_stack);
	//while(1);
	__asm__(
	 "movq %0,%%rsp;\n"
	 :
	 :"g"(proc->k_stack)
	 );
	//while(1);
}

void create_new_process(int proc_index,int new){
	(all_pro + proc_index)->sno=proc_index;
	((all_pro + proc_index)->mmstruct).vma_list = NULL;
	(all_pro + proc_index)->cr3=mappageTable(proc_index,new);
	if((all_pro + proc_index)->cr3==0)
	{
		kprintf("\npage table creation failed\n");
		return;
	}
	
	(all_pro + proc_index)->pid=global_pid;
	global_pid++;
	(all_pro + proc_index)->next = (all_pro + proc_index + 1);
	(all_pro + proc_index)->state=0;	
	all_pro[proc_index].waitstate=0;
	all_pro[proc_index].waitingfor=0;
	all_pro[proc_index].signalling_child=0;
	all_pro[proc_index].sigchild_state=5;
	(all_pro + proc_index)->currentDir=kmalloc(100*sizeof(char));	
	(all_pro + proc_index)->currentDir[0] = '/';
	(all_pro + proc_index)->currentDir[1] = '\0';
	(all_pro + proc_index)->name=kmalloc(100*sizeof(char));
	for (int i = 0; i < MAX_FD; ++i)
	{
		(all_pro + proc_index)->fd[i] = NULL;
	}
	proc_descriptor[proc_index]=1;
}

void copy_cur_dir(PCB *proc)
{
	int i;
	for(i=0;active->currentDir[i]!='\0';i++)
	{
		proc->currentDir[i]=active->currentDir[i];
	}
	proc->currentDir[i]='\0';
}

void copy_name(PCB *proc,char *str)
{
	int i;
	//
	//kprintf("length %d\n",strlen(shell));
	//
	for(i=0;i<strlen(str);i++)
		(proc)->name[i]=str[i];
	(proc)->name[i]='\0';
}

void init_proc()
{

	
	for(int i=0;i<100;i++)
	{
		proc_descriptor[i]=0;
	}
	for(int i=0;i<(PROC_SIZE + 1);i++)
	{
		proc_Q[i]=-1;
	}
	init_kstack();
	int proc_index=0;
	create_new_process(proc_index,NEW);
	
	change_ptable((all_pro + proc_index)->cr3);
	init_stack(all_pro + proc_index);
	create_kstack(all_pro + proc_index);
	copy_kstack(all_pro + proc_index);
	
	char shell[]="bin/sbush";
	
	scan_tarfs(all_pro + proc_index,shell);
	//while(1);
	copy_name(all_pro + proc_index,shell);
	proc_Q[proc_end]=(uint64_t)proc_index;
	proc_end=proc_end + 1;
	active=(all_pro + proc_index);
	proc_start=proc_start + 1;
	active->state=1;
	kprintf("initial child state %d\n",active->sigchild_state);
	//kprintf("user stack: %p\n",active->u_stack);
	
	set_tss_rsp((void *)active->k_stack);
	
	RING_0_MODE=0;
     __asm__(
     "pushq $0x23;\n"
     "pushq %0;\n"
     "pushf;\n"
     "pushq $0x2B;\n"
     "pushq %1;\n"
	 "iretq;\n"
	 :
	 :"g"(active->u_stack),"g"(active->entry_point)
	 );
	 
     
	/*
	__asm__(
	"movq %0,%%rsp;\n"
	"pushq %1;\n"
	"retq;\n"
	:
	:"g"(active->u_stack),"g"(active->entry_point)
	);
	
	*/
	return;
}