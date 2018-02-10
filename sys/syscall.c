#include<sys/kprintf.h>
#include <sys/phy_mem_manager.h>
#include <sys/virt_mem.h>
#include <sys/task_manager.h>
#include <sys/idt.h>
#include <sys/error.h>
#include<sys/kmalloc.h>
#include<sys/debug.h>
#include<sys/gdt.h>
#include <sys/terminal.h>
#include <sys/kmalloc.h>
#include <sys/vfs.h>
#include <sys/kstring.h>


uint64_t (*p[200])(gpr_t *reg);

extern PCB *active;
extern uint64_t proc_Q[PROC_SIZE + 1];
extern PCB all_pro[PROC_SIZE];

//extern PCB* all_pro;

extern uint64_t proc_descriptor[PROC_SIZE];
extern int proc_start;
extern int proc_end;
extern uint64_t global_pid;
extern struct terminal terminal_for_keyboard;
extern uint64_t RING_0_MODE;
extern uint64_t upml4;

extern int time_in_sec;

int exectr=0;

uint64_t syscall_time(gpr_t * reg)
{
	return time_in_sec;
}	

uint64_t dead(gpr_t *reg){

	//Get the address from the register to free
	kprintf("R %d, %d, %d, %d, %d, %d %d\n", reg->rax, reg->rdi,reg->rsi, reg->rdx, reg->r10, reg->r9, reg->r8);
	kprintf("Dead called\n");
	return 12653712;
}


uint64_t k_mmap(gpr_t *reg){

	// Get values from registers
	//TODO Use these variables
	uint64_t length = reg->rsi;
	
    #ifdef DEBUG_MALLOC
	void *addr = (void*)reg->rdi;
	int prot = reg->rdx;
	int flags = reg->r10;
    int fd = reg->r9; 
    int offset = reg->r8;
	kprintf("addr %d length %d prot %d, flags %p, fd %d, offset %d\n", addr, length, prot, flags, fd, offset);
	#endif
	//call mmap
	//Change this later make some freelist of vaddr to reuse vaddr
	uint64_t nextAvailHeapMem = active->heap_top;

	//Point to next page after length
	uint64_t endAddr = active->heap_top + length - 1;
	uint64_t nextPageAfterEndAddr = (((endAddr>>12)+1)<<12); 
	active->heap_top = nextPageAfterEndAddr;

	vma* anon_vma = alloc_vma(nextAvailHeapMem, endAddr);

	append_to_vma_list(active, anon_vma);
	// walkthrough_vma_list(active);

	// while(1);

	#ifdef DEBUG_MALLOC
	kprintf("anon_vma %p %p active->heap_top %p\n", anon_vma->vstart, anon_vma->vend, active->heap_top);
	kprintf("malloc %p\n", nextAvailHeapMem);
	#endif

	#ifdef DEBUG_MALLOC
	printtotalFreeBlocks();
	#endif

	return nextAvailHeapMem;
}


uint64_t k_munmap(gpr_t *reg){

	//Get the address from the register to free

	uint64_t addrToFree = (uint64_t)reg->rdi;
	uint64_t length = (uint64_t)reg->rsi;
	#ifdef DEBUG_MALLOC
	kprintf("Requested %p to free length %d\n", addrToFree, length);
	#endif

	// Remove entry from vma
	if(remove_from_vma_list(active, addrToFree, addrToFree+length) == 0)
	{
		ThrowSegmentationFault(addrToFree);
	}
	//TODO remove entry from vma
	//Remove entry from pagetable and clean page
	#ifdef DEBUG_MALLOC
	printtotalFreeBlocks();
	#endif

	return 0;
}

uint64_t temporary_printf(gpr_t *reg)
{
	//kprintf("execute syscall printf %p\n",p[0]);
	//while(1);
	uint64_t printval = reg->rbx;
	// __asm__(
	// "movq %%rbx,%0;\n"
	// :"=g"(printval)
	// );
	//kprintf("123::%p\n",printval);
	char *print=(char *)printval;
	//kprintf("123::%p\n",printval);
	//kprintf("user stack: %p re-entry point: %p rax:\n",reg->usersp,reg->rip);	
	kprintf("%s",print);

	return 0;
}



void clean_stack(uint64_t stack)
{
	for(int i=0;i<(4096 * 2);i++)
		*((uint8_t *)(stack - i))=0;
}

void clean_pg_table(PCB *proc)
{
	uint64_t * v_pml4=(uint64_t *)(upml4-(4096 * proc->sno));
	for(int i=0;i<512;i++)
		v_pml4[i]=0;
}

void kill_proc()
{
	//kprintf("in kill\n");
	all_pro[active->sno].state=ZOMBIE;
	//proc_descriptor[active->sno]=0;
	all_pro[active->sno].cr3=0;

	//clean_stack(all_pro[active->sno].u_stackbase);

	//clean_stack(all_pro[active->sno].k_stackbase);

	vma *last_vma=(active->mmstruct).vma_list;
	vma* next_vma;
	while(last_vma != NULL)
	{
		//kprintf("removing vma %p %p\n",last_vma->vstart, last_vma->vend);
		
		next_vma = last_vma->nextvma;
		remove_from_vma_list(active,last_vma->vstart, last_vma->vend);
		last_vma = next_vma;
	}
	(active->mmstruct).vma_list = NULL;

	for (int i = 0; i < MAX_FD; ++i)
	{
		all_pro[active->sno].fd[i] = NULL;
	}

	all_pro[active->sno].name[0] = '\0';
	all_pro[active->sno].currentDir[0] = '\0';

	//kprintf("exit status: %d\n",reg->rdi);
	uint64_t ppid=active->ppid;
	for(int i=0;i<PROC_SIZE;i++)
	{
		if(all_pro[i].pid==ppid)
		{
			
			all_pro[i].sigchild_state=0;
			all_pro[i].signalling_child=active->pid;
			break;
		}
	}
	int i;
	for(i=0; i<PROC_SIZE; i++)
	{
		if(all_pro[i].pid==active->signalling_child)
			break;
	}
	all_pro[i].state=DEAD;
	//clean_stack(all_pro[i].k_stackbase);
	//clean_pg_table(all_pro + i);
	proc_descriptor[all_pro[i].sno]=0;
	active=NULL;
	return;
}

void get_phys_addr(uint64_t addr);

uint64_t syscall_switch(gpr_t *reg)
{
	//kprintf("\nswitch called\n");

	if(active!=NULL && active->waitstate!=1)
	{
		proc_Q[proc_end]=(uint64_t)active->sno;    
		active->k_stack=(uint64_t)reg;
		active->u_stack=reg->usersp;
		active->state=1;
		
		proc_end=(proc_end + 1)%(PROC_SIZE + 1);
	}
	if(active->waitstate==1)
	{
		uint64_t kstack;
		
		__asm__(
		"movq %%rsp,%0;\n"
		:"=g"(kstack)
		:
		);
		active->k_stack=(uint64_t)kstack;
	}
	PCB *p_to=get_nextproc();
	active=p_to;
	// set_tss_rsp((void *)active->k_stack);
	set_tss_rsp((void *)active->k_stackbase);
	//kprintf("switching to %d %d ",active->pid,active->waitstate);
	change_ptable(active->cr3);
				//while(1);
	
	if(active->state==0)
	{
		RING_0_MODE=0;	
		
		__asm__(
		"pushq $0x23;\n"
		"pushq %0;\n"
		"pushf;\n"
		"pushq $0x1B;\n"
		"pushq %1;\n"
		"iretq;\n"
		:
		:"g"(p_to->u_stack),"g"(p_to->entry_point)
		);
	}
	else
	{
		if(active->waitstate==WAITING_TO_LIVE)
		{
			//kprintf("I am waiting on!!!!!\n");
			__asm__(
			"movq %0,%%rsp;\n"
			:
			:"g"(active->k_stack)
			);

			//while(1);
			return 0;
		}
		if(active->waitstate==WAITING_TO_DIE)
		{
			//kprintf("\n waiting to die called\n");
			//while(1);
			if(active->signalling_child==active->waitingfor && active->sigchild_state==0)
			{	
				kill_proc();
				//if (exectr==2)
				//{
				//	exectr=3;
				//	//while(1);
				//}
			}
			else
			{
				proc_Q[proc_end]=(uint64_t)active->sno;
				proc_end=(proc_end + 1)%(PROC_SIZE + 1);
				active=NULL;
			}
			
			syscall_switch(reg);
			return 0;
		}
		RING_0_MODE=0;	
		
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
		"iretq;\n"
		:
		:"g"(active->k_stack)
		);
		return 0;
		
	}
	return 0;
}

uint64_t syscall_exit(gpr_t *reg)
{
	//kprintf("in exit\n");
	all_pro[active->sno].state=ZOMBIE;
	//proc_descriptor[active->sno]=0;
	all_pro[active->sno].cr3=0;

	//clean_stack(active->u_stackbase);

	// clean_stack(active->k_stackbase);

	for (int i = 0; i < MAX_FD; ++i)
	{
		all_pro[active->sno].fd[i] = NULL;
	}
	all_pro[active->sno].name[0] = '\0';
	all_pro[active->sno].currentDir[0] = '\0';

	vma *last_vma=(active->mmstruct).vma_list;
	vma* next_vma;
	while(last_vma != NULL)
	{
		//kprintf("removing vma %p %p\n",last_vma->vstart, last_vma->vend);

		next_vma = last_vma->nextvma;
		remove_from_vma_list(active,last_vma->vstart, last_vma->vend);
		last_vma = next_vma;
	}
	(active->mmstruct).vma_list = NULL;

	//kprintf("exit status: %d\n",reg->rdi);
	uint64_t ppid=active->ppid;
	for(int i=0;i<PROC_SIZE;i++)
	{
		if(all_pro[i].pid==ppid)
		{
			
			all_pro[i].sigchild_state=reg->rdi;
			all_pro[i].signalling_child=active->pid;
			break;
		}
	}
	
	active=NULL;
	
	//kprintf("calling switch\n");
	syscall_switch(reg);
	return 0;
}


void do_wait(gpr_t *reg)
{
	
	
	//kprintf("parent waiting:\n");
	proc_Q[proc_end]=(uint64_t)active->sno;
	
	active->u_stack=reg->usersp;
	active->state=1;
		
	proc_end=(proc_end + 1)%(PROC_SIZE + 1);
	
	syscall_switch(reg);
	//kprintf("aahhaa\n");
	
}

int check_live_child(int child_id)
{
	for(int i=0;i<PROC_SIZE;i++)
	{
		if(proc_descriptor[i]==1)
		{
			if(all_pro[i].ppid==active->pid &&(child_id==-1 || child_id==all_pro[i].pid))
				return 1;
		}
	}
	//kprintf("no child present\n");
	return 0;
}

uint64_t syscall_wait(gpr_t *reg)
{
	//kprintf("wait called %d\n",reg->rdi);
	if(check_live_child(reg->rdi)==0)
		return 0;
	active->waitstate=WAITING_TO_LIVE;
	active->waitingfor=reg->rdi;
	//kprintf("wait called\n");
	//int ctr=0;
	while(1)
	{
		do_wait(reg);
		if(active->sigchild_state==0 && (active->waitingfor==-1 || active->waitingfor==active->signalling_child))
		{
			active->waitstate=0;		
			break;
		}
		
		//ctr++;
		//if(ctr >= 4)
		//while(1);
	}
	int i;
	for(i=0; i<PROC_SIZE; i++)
	{
		if(all_pro[i].pid==active->signalling_child)
			break;
	}
	all_pro[i].state=DEAD;
	//clean_stack(all_pro[i].k_stackbase);
	proc_descriptor[all_pro[i].sno]=0;
	//clean_pg_table(all_pro + i);

	uint8_t *p=(uint8_t *)(all_pro + i);


	for(int j=0;j<sizeof(PCB);j++)
	{
		*(p + j)=0x00;
	}

	//kprintf("wait complete\n");
	active->k_stack=(uint64_t)reg;
	active->u_stack=reg->usersp;
	// set_tss_rsp((void *)active->k_stack);
	set_tss_rsp((void *)active->k_stackbase);
	reg->rax=active->signalling_child;
	
	*((uint64_t *)reg->rsi)=active->sigchild_state;
	active->sigchild_state=DEAD;

	//kprintf("rsi: %p\n",reg->rsi);
	//while(1);
	return active->signalling_child;
}



//----------------------------EXECVPE--------------------------------------//



int check_abs_path(char *file)
{
	int ret=0;
	for(int i=0;file[i]!='\0';i++)
	{
		if(file[i]=='/')
		{
			ret=1;
			break;
		}
	}
	return ret;
}

void split_path(char *path,char *Paths[])
{
	
	int ctr=0;
	Paths[ctr]=path;
	for(int i=0;path[i]!='\0';i++)
	{
		if(path[i]==':')
		{
			ctr++;
			Paths[ctr]=path + i;
		}
	}
	Paths[ctr + 1]=NULL;
	//return Paths;
	
}
void concat_path(char *str1,char *str2,char *abs_path)
{
	
	int ctr=0;
	for(int i=0;str1[i]!='\0';i++)
	{
		abs_path[ctr]=str1[i];
		ctr++;
	}
	if(abs_path[ctr]!='/')
	{
		abs_path[ctr]='/';
		ctr++;
	}
	for(int j=0;str2[j]!='\0';j++)
	{
		abs_path[ctr]=str2[j];
		ctr++;
	}
	abs_path[ctr]='\0';
	//return new;
}

uint64_t count_args(char **args)
{
	int ctr;
	for (ctr=0;args[ctr]!=NULL;ctr++);
	
	return ctr;
}

void strcpy(char *src,char *tgt,int len)
{
	int i;
	//kprintf("4444 args: %s\n",src);
	for(i=0;i<len;i++)
		tgt[i]=src[i];
	tgt[i]='\0';
}

void copy_args(char **src,char **tgt,int ctr)
{
	int i;
	//kprintf("2222 args: %s\n",src[0]);
	for(i=0;i<ctr && src[i]!=NULL;i++)
	{
		//kprintf("3333 args: %s\n",src[i]);
		strcpy(src[i],tgt[i],strlen(src[i]));
		
		//kprintf("copy args:%s %s\n",src[i],tgt[i]);
		//tgt[i]=src[i];
	}
	tgt[i]=NULL;
	//kprintf("5555 args: %s\n",tgt[0]);
}



void cpy_filename2args(char *name,char **args,int args_ctr)
{
	int i;
	for(i=args_ctr;i>0;i--)
		args[i]=args[i-1];
	args[i]=name;
}



uint64_t syscall_exec(gpr_t *reg)
{
	//exectr++;
	
	
	char *file_name=(char *)reg->rdi;
	char **args=(char **)reg->rsi;
	char **path=(char **)reg->rdx;

	//kprintf("test exec args %p\n",(uint64_t)args[0]);
	
	uint64_t namelen=strlen(file_name);
	
	uint64_t args_ctr=count_args(args);
	
	uint64_t path_ctr=count_args(path);
	
	//====copy the file name, arguements and paths before the page tables are changed
	
	char file[strlen(file_name)];
	strcpy(file_name,file,namelen);
	
	//increment the args count by 1 to store the file name
	args_ctr++;
	
	char cpy_args[args_ctr][1024];	
	for(int i=0;i<(args_ctr - 1);i++)
		strcpy(args[i],cpy_args[i],strlen(args[i]));
	//kprintf("test exec args 2 %p\n",(uint64_t)args[0]);
	//copy_args(args,(char **)cpy_args,args_ctr);
	
	char cpy_path[path_ctr][1024];
	//copy_args(path,cpy_path,path_ctr);
	
	for(int i=0;i<(path_ctr);i++)
		strcpy(path[i],cpy_path[i],strlen(path[i]));
	
	//------------------------------------------------------------------------
	
	//=========create a new process for execvpe binary======================
	/* new process is created so that it doesnt interfere with the current processes address space*/
	int i;
	for(i=0;i<PROC_SIZE;i++)
	{
		if(proc_descriptor[i]==0)
			break;
	}
	copy_parent_stack();
	
	PCB *child;
	
	create_new_process(i,NEW);
	
	child=&all_pro[i];
	// kprintf("new execvpe sno: %d\n",child->sno);
	//if(exectr==2)
		//while(1);
	child->state=1;
	child->ppid=active->pid;
	change_ptable(child->cr3);//page table changed to new process PML4
	copy_cur_dir(child);
	create_kstack(child);
	//---------------------------------------------------------------------------
	//store current process stack pointer. Incase execvpe fails, the stack will be restored and program will return safely
	
	uint64_t parent_stack;
	
	__asm__(
	"movq %%rsp,%0;\n"
	:"=g"(parent_stack)
	:
	);
	
	//----------------------------
	copy_kstack(child); //stack changed to new process stack
	init_stack(child);	
	//kprintf("file name:%s",file);
	
	
	
	int file_found=0;
	
	//=======================File search begins=============================
	
	/*If absolute path provided, then search for the file in that path only, 
	else search for the file in the paths provided in envps parameter*/
	
	char *Paths[200];
	char abs_path[200];
	
	if(check_abs_path(file))
	{
		char *file_path=file;
		if(file[0]=='/')
			file_path++;
		file_found=scan_tarfs(child,file_path);
		if(file_found)
			strcpy(file_path,file_name,strlen(file_path));
	}
	else
	{
		i=0;
		while(cpy_path!=NULL && cpy_path[i]!=NULL)
		{
			char *p=cpy_path[i];
			split_path(p,Paths);
			for(int j=0;Paths[j]!=NULL;j++)
			{
				concat_path(Paths[j],file,abs_path);
				char *file_path=abs_path;
				if(file_path[0]=='/')
					file_path++;
				file_found=scan_tarfs(child,file_path);
				if(file_found)
				{
					strcpy(file_path,file_name,strlen(file_path));
					break;
				}
			}
			if(file_found)
				break;
			i++;
		}
		if(file_found==0)
		{
			concat_path(child->currentDir,file,abs_path);
			char *file_path=abs_path;
			if(file_path[0]=='/')
				file_path++;
			file_found=scan_tarfs(child,file_path);
			if(file_found)
			{
				strcpy(file_path,file_name,strlen(file_path));
			}
		}
	}
	
	//---------------FILE search ends----------------------------------
	
	if(file_found==0)
	{
		//if file not found then restore page table and stack and return
		change_ptable(active->cr3);
		__asm__(
		"movq %0,%%rsp;\n"
		:
		:"g"(parent_stack)
		);
		kprintf("exec failed: file not found\n");
		return 0;
	}
	//kprintf("test exec args 2 %p\n",(uint64_t)args[0]);
	/*Copy the arguements into the new processes address space*/
	//copy_args((char **)cpy_args,args,args_ctr);
	uint64_t argsdiff=active->u_stackbase - (uint64_t)args;

	char **newargs=(char **)(child->u_stackbase - argsdiff);

	for(int i=0;i<(args_ctr-1);i++)
		newargs[i]=args[i];

	for(i=0;i<(args_ctr - 1);i++)
		strcpy(cpy_args[i],newargs[i],strlen(cpy_args[i]));
		
	cpy_filename2args(file_name,newargs,args_ctr); //copies filename into args
	
	strcpy(file_name,child->name,strlen(file_name));
	//copy_args(cpy_path,path,path_ctr);	
	/*---------------Create VMA entries for the arguements------*/
	
	//vma *new_vma=alloc_vma((uint64_t)args, (uint64_t)args + args_ctr);
	//append_to_vma_list(child,new_vma);
	//increment_childpg_ref((uint64_t)args, (uint64_t)args + args_ctr);

	//kprintf("test args: %s\n",newargs[0]);

	//kprintf("test args 2 %p %p\n",(uint64_t)newargs,(uint64_t)newargs[1]);


	
	for(i=0;i<args_ctr;i++)
	{
		vma *new_vma=alloc_vma((uint64_t)newargs[i], (uint64_t)newargs[i] + strlen(newargs[i]));
		append_to_vma_list(child,new_vma);
		increment_childpg_ref((uint64_t)newargs[i],(uint64_t)newargs[i] + strlen(newargs[i]));
	}
	
	//--------------------------------------------------------------//
	
	
	
	//kprintf("old kstack: %p new kstack: %p\n",parent_stack,child->k_stack);
	
	
	//kprintf("current stack: %p\n",current_stack);
	//kprintf("new stack base: %p\n",child->k_stackbase);
	//kprintf("old stack base: %p\n",active->k_stackbase);
	
	
	uint64_t stackdiff=active->k_stackbase-(uint64_t)reg;
	//gpr_t *new_rg=(gpr_t *)(active->k_stackbase - stackdiff);
	gpr_t *child_rg=(gpr_t *)(child->k_stackbase - stackdiff);
	
	//kprintf("active rip addr: %p child rip addr: %p\n",&new_rg->rip,&child_rg->rip);
	
	//kprintf("active rip: %p child rip: %p\n",new_rg->rip,child_rg->rip);
	
	active->waitstate=WAITING_TO_DIE;
	//active->state=ZOMBIE;
	active->waitingfor=child->pid;
	proc_Q[proc_end]=(uint64_t)active->sno;
	proc_end=(proc_end + 1)%(PROC_SIZE + 1);
	
	active=child;
	
	child_rg->rip=active->entry_point;
	child_rg->rdi=(uint64_t)newargs;
	child_rg->usersp=active->u_stack;
	
	i=0;
	
	for(i=0;args[i]!=NULL && newargs!=NULL;i++);
	
	child_rg->rsi=i;
	
	active->k_stack=(uint64_t)(child_rg);
	
	set_tss_rsp((void *)active->k_stackbase);
	// set_tss_rsp((void *)active->k_stack);
	
	RING_0_MODE=0;	
		
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
	"iretq;\n"
	:
	:"g"(active->k_stack)
	);
	
	
	//kprintf("file found entry point %p\n",new_rg->rip);
	
	return 0;
}

//------------------------------------------------EXECVPE END--------------------------------------//

uint64_t syscall_fork(gpr_t *reg)
{
	copy_parent_stack();
	
	//kprintf("parent entry point:%p\n",reg->rip);
	
	if((proc_end + 1) % (PROC_SIZE + 1)==proc_start)
	{
		kprintf("child can not be queued\n");
		while(1);
	}
	PCB *child;
	int i;
	for(i=0;i<PROC_SIZE;i++)
	{
		if(proc_descriptor[i]==0)
			break;
	}
	create_new_process(i,CHILD);
	child=&all_pro[i];
	child->state=1;
	child->ppid=active->pid;
	child->entry_point=active->entry_point;
	child->heap_top =active->heap_top;
	copy_cur_dir(child);
	copy_name(child,active->name);
	(child->mmstruct).vma_list=NULL;
	copy_vma(child);
	//Copy all fds from parent to child
	for (int i = 0; i < MAX_FD; ++i)
	{
		if (active->fd[i] != NULL)
		{
			child->fd[i] = (uint64_t*)kmalloc(sizeof(file_object));
			((file_object*)child->fd[i])->currentoffset = ((file_object*)active->fd[i])->currentoffset;
			((file_object*)child->fd[i])->node = ((file_object*)active->fd[i])->node;
		}
		else
		{
			child->fd[i] = NULL;
		}
	}
	
	
	
	//while(1);
	change_ptable(child->cr3);
	
	create_kstack(child);
	copy_kstack(child);
	
	
	gpr_t *parent_rg,*child_rg;
	parent_rg=(gpr_t *)reg;
	
	active->k_stack=(uint64_t)reg;
	active->u_stack=(uint64_t)(parent_rg->usersp);
	//kprintf("Inside forks 1234\n");
	
	init_stack(child);
	reg->rax=child->pid;
	active->sigchild_state=child->state;
	active->state=1;
	proc_Q[proc_end]=(uint64_t)active->sno;
	proc_end=(proc_end + 1)%(PROC_SIZE + 1);
	//kprintf("parent kstack:%p ustack:%p\n",(proc_Q + proc_start)->k_stack,(proc_Q + proc_start)->u_stack);
	//kprintf("parent sp val:%p sp:%p\n",*(uint64_t *)parent_rg->usersp,parent_rg->usersp);
	
	
	uint64_t stackdiff=active->k_stackbase-(uint64_t)reg;
	child_rg=(gpr_t *)(child->k_stackbase - stackdiff);
	
	//kprintf("parent sp val:%p %\n",*(uint64_t *)child_reg->usersp,child_reg->usersp);
	
	
	child_rg->usersp=child->u_stack;

	//("child kstack:%p ustack:%p\n",child->k_stack,child->u_stack);
	//kprintf("parent kstack:%p ustack:%p\n",active->k_stack,active->u_stack);

	
	child_rg->rax=0;
	//kprintf("child cr3: %p parent cr3: %p\n",child->cr3,active->cr3);
	active=child;
	active->k_stack=(uint64_t)(child_rg);
	
	
	// set_tss_rsp((void *)active->k_stack);
	set_tss_rsp((void *)active->k_stackbase);
	
	RING_0_MODE=0;	
		
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
	"iretq;\n"
	:
	:"g"(active->k_stack)
	);	
	
	return child->pid;
}


uint64_t k_open(gpr_t *reg){
	char* filePath = (char*)reg->rdi;
	// uint64_t flags = reg->rsi;   	//TODO use this

	// kprintf("Request to open %s\n", filePath);
	inode* query = GetInode(filePath);

	if (query == NULL || (query != NULL && query->type == DIR))
	{
		kprintf("\nNo Such File!\n");
		return -1;
	}

	for (int i = 2; i < MAX_FD; ++i)
	{
		if (active->fd[i] == NULL)
		{
			//Allocate this fd 
			active->fd[i] = (uint64_t*)kmalloc(sizeof(file_object));
			((file_object*)active->fd[i])->currentoffset = query->start;
			((file_object*)active->fd[i])->node = query;
			// active->fd[i] = (uint64_t*)fObj;
			// kprintf("Returning to good world %p!\n", active->fd[i]);
			return i;
		}
	}
	kprintf("\nWe dont support opening more than 20 files!\n");
	return -1;
}

uint64_t k_close(gpr_t *reg){

	uint64_t fd_index = reg->rdi;
	// kprintf("Request to Free %d\n", fd_index);

	if (fd_index > 1)
	{
		// I know its memory leak but can help
		// kprintf("Freeed %d\n", fd_index);
		active->fd[(int)fd_index] = NULL;
	}	
	return 1;
}

uint64_t k_read(gpr_t *reg){
	// kprintf("Read request arrives\n");

	uint64_t fd_index = reg->rdi;
	char* buf = (char*)reg->rsi;
	uint64_t count = reg->rdx;
	uint64_t read_count = 0;

	// kprintf("fd_index %d, count %d\n", fd_index, count);
	if (fd_index == 0)
	{
		uint64_t buff = reg->rsi;
		return terminal_for_keyboard.read(&terminal_for_keyboard, (uint64_t)buff);
	}
	else if (fd_index > 1)
	{
		// kprintf("Going to fetch file object, fd_index %d\n", fd_index);
		file_object* file = (file_object*)active->fd[(int)fd_index];

		// kprintf("Going to read %d\n", file->node->end-file->node->start);
		char* ch = (char*)file->currentoffset;
		uint64_t start = file->currentoffset; uint64_t end = file->currentoffset + count;

		for (uint64_t i = start; i < end; ++i)
		{
			if (i >= file->node->end)
			{
				break;
			}
			// kprintf("%c", *ch);
			*buf++ = *ch++;
			read_count++;
			file->currentoffset++;
		}
		*buf = '\0';
		return read_count;
	}
	else
	{
		kprintf("We dont support file pointers other than stdin stdout!!\n");
	}
	return 0;	
}

uint64_t k_write(gpr_t *reg){
	// kprintf("Read request arrives\n");
	uint64_t fd = reg->rdi;
	if (fd == 1)
	{
		uint64_t buff = reg->rsi;
		uint64_t count = reg->rdx;
		return terminal_for_keyboard.write(&terminal_for_keyboard, (uint64_t)buff, count);
	}

	kprintf("We dont support file pointers other than stdin stdout!!\n");
	return 0;
}

uint64_t k_writecolor(gpr_t *reg){
	// kprintf("Read request arrives\n");
	uint64_t fd = reg->rdi;
	if (fd == 1)
	{
		uint64_t buff = reg->rsi;
		// uint64_t count = reg->rdx;
		// uint64_t color = reg->rbx;
		flushcolor((char*)buff);
		// char* str = (char*)(uint64_t*)buff;
		// kprintf("%s",str);
		return 1;
	}

	kprintf("We dont support file pointers other than stdin stdout!!\n");
	return 0;
}

uint64_t k_opendir(gpr_t *reg){
	char* path = (char*) reg->rdi;
	// kprintf("Path recieved by opendir %s", path);
	
	// dir* dirobj = (dir*)kmalloc(sizeof(dir));		//Figure out how to free it
	dir* dirobj = getnewdir();
	inode* query = GetInode(path);
	if (query == NULL)
	{
		return (uint64_t)0;
	}
	if (query->type == FILE)
	{
		return (uint64_t)0;
	}
	int i = 0;
	for (; path[i] != '\0'; ++i)
	{
		dirobj->path[i] = path[i];
	}
	dirobj->path[i] = '\0';
	
	dirobj->currInode = 1;
	return (uint64_t)dirobj;
}

uint64_t k_closedir(gpr_t *reg){
	// I know its memory leak but cant help
	dir* dirobj = (dir*)reg->rdi;
	dirobj->path[0] = '\0';
	dirobj->currInode = -1;
	dirobj->nextdir = NULL;
	dirobj = NULL;
	recycledirstruct(dirobj);
	return 1;
}

uint64_t k_readdir(gpr_t *reg){
	// kprintf("Inside readdir\n");
	dir* dirobj = (dir*)reg->rdi;
	if (dirobj == NULL)
	{
		return 0;
	}

	inode* query = GetInode(dirobj->path);
	if (query == NULL || dirobj->currInode == -1 || dirobj->currInode >= query->familyCount)
	{
		return 0;
	}
	// kprintf("path %s\n", dirobj->path);
	int i = 0;
	for (; query->family[(int)dirobj->currInode]->inodeName[i] != '\0'; ++i)
	{
		dirobj->currDirent.d_name[i] = query->family[dirobj->currInode]->inodeName[i];
		// kprintf("%s", query->family[dirobj->currInode]->inodeName);
	}
	dirobj->currDirent.d_name[i] = '\0';

	dirobj->currInode++;
	return (uint64_t)&dirobj->currDirent;
}

uint64_t k_chdir(gpr_t *reg){
	char* path = (char*)reg->rdi;

		// kprintf("Change dir request %s\n", path);


	if(path == NULL){
		return 0;
	}

	char* newPath = NULL;
	if (path[0] == '/')
	{
		inode* query = GetInode(path);
		if (NULL == query)
		{
			// kprintf("Change dir request inode NULL%s\n", path);
			return 0;
		}
		if (FILE == query->type)
		{
			//Reset newPath
			return 0;
		}
		newPath = path;
	}
	else if(path[0] != '.')
	{
		newPath = active->currentDir;
		int lastPos = 0;
		for (; newPath[lastPos] != '\0' && lastPos < 512; ++lastPos);
		int lastPos_bk = lastPos;
		for (int i = 0; path[i] != '\0'; ++i)
		{
			newPath[lastPos++] = path[i]; 
		}
		newPath[lastPos++] = '/';
		newPath[lastPos] = '\0';

		inode* query = GetInode(newPath);
		if (NULL == query)
		{
			//Reset newPath
			newPath[lastPos_bk] = '\0';
			return 0;
		}
		if (FILE == query->type)
		{
			//Reset newPath
			newPath[lastPos_bk] = '\0';
			return 0;
		}
	}
	else if (path[0] == '.' && path[1] == '.')
	{
		//Do something babes use head
		newPath = active->currentDir;
		int lastPos = 0;
		for (; newPath[lastPos] != '\0' && lastPos < 512; ++lastPos);
		int goback = lastPos-2;
		for ( ; goback >= 0	; --goback)
		{
			if (newPath[goback] == '/')
			{
				newPath[++goback] = '\0';
				break;
			}
		}
	}
	else
	{
		//Dont do anything 
		return 0;
	}

	// kprintf("Going to change directory %s", newPath);


	int i = 0;
	for (; newPath[i] != '\0'; ++i)
	{
		if (i >= 510)	
		{
			kprintf("We dont support path to be more than 512 characters!\n");
			break;
		}

		active->currentDir[i] = newPath[i];
	}
	active->currentDir[i] = '\0';
	return 1;
}

uint64_t k_getcwd(gpr_t *reg){
	
	char* buff = (char*)reg->rdi;
	uint64_t size = reg->rsi;

	int i = 0;
	for (; active->currentDir[i] != '\0'; ++i)
	{
		if (i >= size-1)	
		{
			break;
		}

		buff[i] = active->currentDir[i];
	}
	buff[i] = '\0';
	return (uint64_t)buff;
}

uint64_t syscall_get_proccount(gpr_t *reg)
{
	int ctr=0;
	for(int i=0;i<PROC_SIZE;i++)
	{
		if(proc_descriptor[i]==1)
			ctr++;
	}
	return ctr;
}

uint64_t syscall_get_proclist(gpr_t *reg)
{
	uint64_t addr=reg->rdi;
	struct proc_lst *plist=(struct proc_lst *)addr;
	int ctr=0;
	for(int i=0;i<PROC_SIZE;i++)
	{
		if(proc_descriptor[i]==1)
		{
			plist[ctr].pid=all_pro[i].pid;
			plist[ctr].ppid=all_pro[i].ppid;
			plist[ctr].state=all_pro[i].state;
			plist[ctr].waitstate=all_pro[i].waitstate;
			strcpy(all_pro[i].name,plist[ctr].name,strlen(all_pro[i].name));
			ctr++;
		}
	}
	return 0;
}


uint64_t syscall_kill(gpr_t *reg)
{
	uint64_t pid=reg->rdi;
	
	int sno;
	for(sno=0;sno <PROC_SIZE;sno++)
	{
		if(all_pro[sno].pid==pid)
			break;
	}

	for (int i = 0; i < MAX_FD; ++i)
	{
		all_pro[active->sno].fd[i] = NULL;
	}
	all_pro[active->sno].name = NULL;
	all_pro[active->sno].currentDir = NULL;

	all_pro[sno].state=DEAD;
	proc_descriptor[sno]=0;
	all_pro[sno].cr3=0;
	vma *last_vma=((all_pro + sno)->mmstruct).vma_list;
	while(last_vma != NULL)
	{
		remove_from_vma_list((all_pro + sno),last_vma->vstart, last_vma->vend);
		last_vma = last_vma->nextvma;
	
	}
	//kprintf("exit status: %d\n",reg->rdi);
	uint64_t ppid=(all_pro + sno)->ppid;
	for(int i=0;i<100;i++)
	{
		if(all_pro[i].pid==ppid)
		{
			
			all_pro[i].sigchild_state=0;
			all_pro[i].signalling_child=(all_pro + sno)->pid;
			break;
		}
	}
	
	/*If the process being killed is active, remove it from schedule queue*/
	
	for(int i=0;i<(PROC_SIZE + 1);i++)
	{
		if(i==sno)
		{
			while(i!=proc_start)
			{
				proc_Q[i]=proc_Q[i-1];
				i--;
				if(i==0 && i!=proc_start)
					i=PROC_SIZE;
			}
			proc_start=(proc_start + 1)%(PROC_SIZE + 1);
			break;
		}
	}
	
	return 0;
}

void syscall_init()
{
	//kprintf("iiiiiioooo\n");
	p[0] = k_read;
	p[1] = k_write;
	p[2] = k_open;
	p[3] = k_close;
	p[9] = k_mmap;
	p[11] = k_munmap;
	p[53] = syscall_kill;
	p[54] = syscall_get_proccount;
	p[55] = syscall_get_proclist;
	p[56] = syscall_time;
	p[57] = syscall_fork;
	p[58] = syscall_switch;
	p[59] = syscall_exec;
	p[60] = syscall_exit;
	p[61] = syscall_wait;
	p[62] = k_opendir;
	p[63] = k_readdir;
	p[64] = k_closedir;
	p[79] = k_getcwd;
	p[80] = k_chdir;
	p[98] = k_writecolor;
	p[99] = temporary_printf;
}