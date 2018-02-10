#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//Reference : https://filippo.io/linux-syscall-table/ + http://man7.org/linux/man-pages/man2/ + https://www.ibiblio.org/gferg/ldp/GCC-Inline-Assembly-HOWTO.html


unsigned int sleep(unsigned int seconds)
{
	unsigned long syscallnumber = 56;
	
	uint64_t start=0;
	
	uint64_t time=0;
	
	while(1)
	{
		if((time - start)==seconds)
			break;

		__asm__(
			"movq %1, %%rax;\n"
			"int $0x80;\n"
			"movq %%rax, %0;\n"
			: "=m"(time)
			: "m" (syscallnumber)
			: "rax","rdi"
		);
		
		if(start==0)
			start=time;
	
	}
	

	return time - start;
	
}


int chdir(char* in_path){

	unsigned long syscallnumber = 80;
	int ret = -1;

	__asm__(
		"movq %1, %%rax;\n"
		"movq %2, %%rdi;\n"
		"int $0x80;\n"
		"movq %%rax, %0;\n"
		: "=m" (ret)
		: "m" (syscallnumber), "m" (in_path)
		: "rax","rdi"
	);

	return ret;
}

char* getcwd(char *buf, unsigned long size){		//TODO change int size to size_t

	unsigned long syscallnumber = 79;
	char* ret = (char* )malloc(size);

	__asm__(
		"movq %1, %%rax;\n"
		"movq %2, %%rdi;\n"
		"movq %3, %%rsi;\n"
		"int $0x80;\n"
		"movq %%rax, %0;\n"
		: "=m" (ret)
		: "m" (syscallnumber), "m" (buf), "m" (size)
		: "rax","rdi", "rsi"
	);

	buf=ret;
	return buf;
}


int open(const char *pathname, int flags){

	unsigned long syscallnumber = 2;
	int f;


	__asm__(
	"movq %1, %%rax;\n"
	"movq %2, %%rdi;\n"
	"movq %3, %%rsi;\n"
	"int $0x80;\n"
	"movq %%rax, %0;\n"
	: "=m" (f)
	: "m" (syscallnumber), "m" (pathname), "m" (flags)
	: "rax","rdi", "rsi"
	);

	return f;
}

ssize_t read(int fd, void *buf, size_t count){

	unsigned long syscallnumber = 0;
	int read_count;

	__asm__(
		"movq %1, %%rax;\n"
		"movq %2, %%rdi;\n"
		"movq %3, %%rsi;\n"
		"movq %4, %%rdx;\n"
		"int $0x80;\n"
		"movq %%rax, %0;\n"
		: "=m" (read_count)
		: "m" (syscallnumber), "m" (fd), "m" ((unsigned long)buf), "m" (count)
		: "rax","rdi", "rsi", "rdx"
	);

	//Return value doesnt work maybe need to deep copy use the parameter buff in app
	return read_count;
}

char fgetc(File* fp){
	// This misght be buggy.. Not tested
	int fd = fp->fd;
	char* buff = (char*) malloc(4096*sizeof(char));
	
	char ch;

	int read_count = read(fd, buff, 1);
	ch = buff[0];
	free(buff);
	// __asm__(
	// 	"movq %1, %%rax;\n"
	// 	"movq %2, %%rdi;\n"
	// 	"movq %3, %%rsi;\n"
	// 	"movq %4, %%rdx;\n"
	// 	"int $0x80;\n"
	// 	"movq %%rax, %0;\n"
	// 	: "=m" (read_count)
	// 	: "m" (syscallnumber), "m" (fd), "m" (ch), "m" (count)
	// 	: "rax","rdi", "rsi", "rdx"
	// );

	if (read_count != 0)
	{
		return ch;
	}

	return '\0';
}


File *fopen(const char *path,const char *mode)
{

	//FILE fp;

	File *file=(File*)malloc(sizeof(File));

	int flag;

	const char *filepath=path;

	const char *filemode=mode;

	if(strcmp(filemode,"r")==0)
	flag=O_RDONLY;
	else if(strcmp(filemode,"w")==0)
	flag=O_WRONLY | O_TRUNC;
	else if(strcmp(filemode,"r+")==0)
	flag=O_RDWR;
	else if(strcmp(filemode,"w+")==0)
	flag=O_RDWR | O_TRUNC;
	else if(strcmp(filemode,"a+")==0)
	flag=O_APPEND;
	else
	flag=O_RDONLY;

	//putchar(flag + 48);


	int f = open(filepath, flag);
	// putchar(f+48);

	//fp.fd=f;

	file->fd=f;

	if(f<0)
	return NULL;
	else
	return file;


}

int close( int fd)
{
	unsigned long syscallnumber = 3;
	int return_fd = 0;			

	__asm__(
	"movq %1, %%rax;\n"
	"movq %2, %%rdi;\n"
	"int $0x80;\n"
	"movq %%rax, %0;\n"
	: "=m" (return_fd)
	: "m" (syscallnumber), "m" (fd)
	: "rax","rdi"
	);

	return return_fd;

}

int fclose(File *fp)
{
	int file=fp->fd;
	return close(file);
}



int execvpe(const char *filename, char *const argv[], char *const envp[]){

	// shputs("execve:");shputs(filename);shputs(argv[0]);
	

	unsigned long syscallnumber = 59;
	int ret;
	

	__asm__(
		"movq %1, %%rax;\n"
		"movq %2, %%rdi;\n"
		"movq %3, %%rsi;\n"
		"movq %4, %%rdx;\n"
		"int $0x80;\n"
		"movq %%rax, %0;\n"
		: "=m" (ret)
		: "m" (syscallnumber), "m" (filename), "m" (argv), "m" (envp)
		: "rax","rdi", "rsi", "rdx"
	);

	return ret;
}

pid_t fork(void)
{
	
	unsigned long syscallnumber = 57;
	pid_t ret;


	__asm__(
		"movq %1, %%rax;\n"
		"int $0x80;\n"
		"movq %%rax, %0;\n"
		: "=m" (ret)
		: "m" (syscallnumber)
	);
	//printf("fork syscall complete %d\n",ret);
	return ret;
}

void yield(void)
{
	unsigned long syscallnumber = 58;
	//pid_t ret;

	__asm__(
		"movq %0, %%rax;\n"
		"int $0x80;\n"
		//"movq %%rax, %0;\n"
		: 
		: "m" (syscallnumber)
	);
	//printf("yield syscall complete %d\n");
	return ;
}

pid_t getpid(void){

	unsigned long syscallnumber = 39;
	pid_t ret;

	__asm__(
		"movq %1, %%rax;\n"
		"syscall;\n"
		"movq %%rax, %0;\n"
		: "=m" (ret)
		: "m" (syscallnumber)
		: "rax"
	);

	return ret;
}

pid_t wait(int *status){

	unsigned long syscallnumber = 61;
	
	int pid=-1;
	
	pid_t ret;

	__asm__(
		"movq %1, %%rax;\n"
		"movq %2, %%rdi;\n"
		"movq %3, %%rsi;\n"
		"int $0x80;\n"
		"movq %%rax, %0;\n"
		: "=m" (ret)
		: "m" (syscallnumber),"m"(pid),"m"(status)
		: "rax","rdi","rsi"
	);
	
	//printf("waited on %d\n",ret);

	return ret;	
}

int waitpid(int pid, int *status, int options){
	if (options > 0)
	{
		puts("Need to implement waitpid with WNOHANG\n");
	}	
	//printf("calling wait: %p\n",status);
	
	unsigned long syscallnumber = 61;
	pid_t ret;

	__asm__(
		"movq %1, %%rax;\n"
		"movq %2, %%rdi;\n"
		"movq %3, %%rsi;\n"
		"int $0x80;\n"
		"movq %%rax, %0;\n"
		: "=m" (ret)
		: "m" (syscallnumber), "m" (pid), "m" (status)
		: "rax", "rdi", "rsi"
	);

	return ret;	

}

int pipe(int* pipefd){
	unsigned long syscallnumber = 22;
	int ret;

	__asm__(
		"movq %1, %%rax;\n"
		"movq %2, %%rdi;\n"
		"syscall;\n"
		"movq %%rax, %0;\n"
		: "=m" (ret)
		: "m" (syscallnumber), "m" (pipefd)
		: "rax", "rdi"
	);

	return ret;		
}

int dup2(int oldfd, int newfd){

	puts("We dont support dup2");
	return -1;

	unsigned long syscallnumber = 33;
	int ret;

	__asm__(
		"movq %1, %%rax;\n"
		"movq %2, %%rdi;\n"
		"movq %3, %%rsi;\n"
		"syscall;\n"
		"movq %%rax, %0;\n"
		: "=m" (ret)
		: "m" (syscallnumber), "m" (oldfd), "m" (newfd)
		: "rax", "rdi", "rsi"
	);

	return ret;		
}


int execvp(const char *file, char* argv[])
{
	puts("Need to implement execvp");
	return -1;
        // puts("Hello");
    
    //Commented for build
    char *path;
    // path=getenv("PATH");


    // LOGG("execvp");LOGG( path);
    // puts(path);
    // int cmdind=0;
    char dir[1024];
    int k=0;
    int len=strlen(path);
//Go to each path defined in env and search the command
    for (int j = 0;j<=len; ++j)
    {
            if(path[j]!=':' && path[j]!='\0')
            {
                    dir[k++] = path[j];
                    continue;
            }
            else
            {
        //printf("a");
                    dir[k++]='/';
                    for(int l=0;file[l]!='\0';l++)
                    {
                            dir[k++]=file[l];
                    }
                    dir[k]='\0';
                    k=0;
                    //Run execve
                    // LOG("%s\n", dir);
                    argv[0] = dir;
                    //Commented for build
                    // execve(dir, argv, NULL);
                    //puts("Failed");
            }
    }
    return -1;
}

int execve(const char *filename, char *const argv[], char *const envp[])
{
	puts("Need to implement execvp\n");
	return -1;
}

